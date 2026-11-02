#!/usr/bin/env python3
"""check_tools.py - mutos_size, mutos_nm, mutos_strip, mutos_tsort and
mutos_lorder on the real crt0.o and libc.a.

The reference is the real crt0.o and libc.a (tests/mutos1700_crt0/,
tests/mutos1700_libc/: the archive, its 167 objects and its __.SYMDEF) and
a program that the ported cc makes. This checks, with the ports only (the
tools of the MUTOS root's bin/, no emulator, plain Python 3 - the comparison
with the originals under the emulator is tests/realbin/utilcmp.py):

  1. size: for crt0.o and every object of libc.a the line
     "text+data+bss = sum" is what the object's header says; with several
     files the names come first;
  2. nm: for crt0.o and every object, nm -p gives the symbol table in its
     order, as an independent reading of the a.out header and of the 12-byte
     entries gives it (value, type letter, name); the default order is a
     sort by name, -n one by value, -g the external symbols, -u the
     undefined ones; nm of libc.a names every member that is an object;
  3. libc.a's __.SYMDEF is the table "nm -g" gives - the same names, once
     each, every one defined in the member whose header the entry names;
  4. a program made by cc: nm lists main and the undefined library routines,
     size reads its header, strip leaves the header with no symbols and the
     relocation-stripped bit, the file as long as header, text and data, nm
     says "no name list", a second strip says "already stripped", size is
     the same;
  5. tsort: a chain, a diamond, a cycle (the message, and every item printed
     once), odd data, a missing file;
  6. lorder libc.a | tsort prints every object of libc.a once; lorder of a
     single object names it twice; the pairs lorder gives are what an
     independent reading of nm -g gives (a uses a symbol that b defines).

Usage: check_tools.py [ROOT]      (default: $MUTOS_ROOT, else <tree>/mutos_root)
Exit status 1 on any failure.
"""
import base64
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
LIBDIR = os.path.join(REPO, 'tests', 'mutos1700_libc')
CRT0 = os.path.join(REPO, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt')

FAILED = []
CHECKS = 0


def check(ok, what):
    global CHECKS
    CHECKS += 1
    if not ok:
        FAILED.append(what)
        print('FAILED: ' + what)
    return ok


def b64(path):
    with open(path, 'rb') as fh:
        return base64.b64decode(fh.read())


def archive(arch):
    """[(name, header offset, data)] of an archive."""
    if arch[:2] != b'\x65\xff':
        raise ValueError('no archive magic')
    out, p = [], 2
    while p + 26 <= len(arch):
        name = arch[p:p + 14].split(b'\0')[0].decode('latin1')
        hi, lo = struct.unpack_from('<HH', arch, p + 22)
        size = (hi << 16) | lo
        out.append((name, p, arch[p + 26:p + 26 + size]))
        p += 26 + size + (size & 1)
    return out


def header(o):
    """(magic, text, data, bss, syms, entry, unused, flag) of an a.out file."""
    return struct.unpack_from('<8H', o, 0)


def symbols(o):
    """[(name, type word, value)] of an a.out file, in the table's order."""
    magic, text, data, bss, syms, entry, unused, flag = header(o)
    p = 16 + (text + data) * (1 if flag & 1 else 2)      # the relocation is as long as text and data
    out = []
    for i in range(syms // 12):
        name = o[p:p + 8].split(b'\0')[0]
        t, v = struct.unpack_from('<HH', o, p + 8)
        out.append((name.decode('latin1'), t, v))
        p += 12
    return out


def aout_of(name, t, v):
    """A tiny object with the one symbol (name, t, v)."""
    return struct.pack('<8H', 0o407, 0, 0, 0, 12, 0, 0, 1) + (
        name.encode('latin1')[:8].ljust(8, b'\0') + struct.pack('<HH', t, v))


def letter(t, v):
    """nm's type letter, from the a.out.h's N_TYPE and N_EXT."""
    c = {0: 'c' if v else 'u', 1: 'a', 2: 't', 3: 'd', 4: 'b', 0x1f: 'f', 0x14: 'r'}.get(
        t & 0x1f, 'a')
    return c.upper() if t & 0x20 else c


def expect_lines(o, opts=''):
    """The lines nm -p<opts> prints for an object (opts: 'g' and 'u' only)."""
    out = []
    for name, t, v in symbols(o):
        c = letter(t, v)
        if 'g' in opts and not t & 0x20:
            continue
        if 'u' in opts:
            if c.lower() != 'u':
                continue
            out.append(name[:8])
            continue
        out.append(('      ' if c in 'uU' else '%04x' % v) + ' %s ' % c + name[:8])
    return out


def run(root, tool, args, cwd, stdin=b'', env=None):
    e = dict(os.environ, MUTOS_ROOT=root)
    e.update(env or {})
    p = subprocess.run([os.path.join(root, 'bin', tool)] + args, cwd=cwd, input=stdin,
                       capture_output=True, env=e, timeout=120)
    return p.returncode, p.stdout.decode('latin1'), p.stderr.decode('latin1')


def main(argv):
    root = os.path.abspath(argv[0] if argv else os.environ.get(
        'MUTOS_ROOT', os.path.join(REPO, 'mutos_root')))
    for t in ('size', 'strip', 'nm', 'tsort', 'lorder', 'cc'):
        if not os.path.exists(os.path.join(root, 'bin', t)):
            print('no %s in %s/bin - run make first' % (t, root))
            return 1
    cor = {'crt0.o': b64(CRT0)}
    libc = b64(os.path.join(LIBDIR, 'libc.a.base64.txt'))
    members = archive(libc)
    objs = [(n, off, d) for n, off, d in members if d[:2] == b'\x07\x01']
    for n, _, d in objs:
        cor[n] = d
    with tempfile.TemporaryDirectory(prefix='binutils') as tmp:
        for n, d in cor.items():
            with open(os.path.join(tmp, n), 'wb') as fh:
                fh.write(d)
        with open(os.path.join(tmp, 'libc.a'), 'wb') as fh:
            fh.write(libc)

        print('== size: the header\'s sizes ==')
        bad = 0
        for n, d in cor.items():
            _, text, data, bss, *_ = header(d)
            s = text + data + bss
            st, out, err = run(root, 'size', [n], tmp)
            want = '%d+%d+%d = %db = 0%ob\n' % (text, data, bss, s, s)
            if (st, out, err) != (0, want, ''):
                bad += 1
                print('  %s: %r, want %r' % (n, out, want))
        check(not bad, 'size gives the header\'s sizes for %d objects (%d wrong)' % (len(cor), bad))
        st, out, err = run(root, 'size', ['crt0.o', 'nothere', 'libc.a'], tmp)
        check(out.startswith('crt0.o: ') and 'size: nothere not found\n' in out
              and 'size: libc.a not an object file\n' in out,
              'size with several files names them, and reports what is none')

        print('== nm: the symbol tables ==')
        bad = 0
        for n, d in cor.items():
            for opts, flags in (('', ['-p']), ('g', ['-pg']), ('u', ['-pu'])):
                st, out, err = run(root, 'nm', flags + [n], tmp)
                want = expect_lines(d, opts)
                if err or st != 0 or out.splitlines() != want:
                    bad += 1
                    print('  nm %s %s differs' % (' '.join(flags), n))
            # the default order: by name; -n: by value, then name; -r reversed
            st, out, err = run(root, 'nm', [n], tmp)
            names = [l.rsplit(' ', 1)[1] for l in out.splitlines()]
            if names != sorted(names):
                bad += 1
                print('  nm %s: not in the order of the names' % n)
            st, out2, err = run(root, 'nm', ['-r', n], tmp)
            if [l.rsplit(' ', 1)[1] for l in out2.splitlines()] != sorted(names, reverse=True):
                bad += 1
                print('  nm -r %s: not reversed' % n)
            st, out3, err = run(root, 'nm', ['-n', n], tmp)
            key = {}
            for name, t, v in symbols(d):
                key[expect_lines(aout_of(name, t, v))[0]] = (v, name.encode('latin1'))
            ent = [key[l] for l in out3.splitlines()]
            if ent != sorted(ent):
                bad += 1
                print('  nm -n %s: not in the order of the values' % n)
            if sorted(out.splitlines()) != sorted(expect_lines(d)) or \
                    sorted(out3.splitlines()) != sorted(expect_lines(d)):
                bad += 1
                print('  nm %s: the sorted lists are not the table' % n)
        check(not bad, 'nm gives the table, -g, -u and the three orders for %d objects (%d wrong)'
              % (len(cor), bad))
        st, out, err = run(root, 'nm', ['-p', 'libc.a'], tmp)
        want = ''.join('\n%s:\n' % n + ''.join(l + '\n' for l in expect_lines(d))
                       for n, _, d in objs)
        check(st == 0 and out == want and err == '',
              'nm -p libc.a: every member that is an object, named, with its table')
        st, out, err = run(root, 'nm', ['-po', 'crt0.o'], tmp)
        check(out.splitlines() == ['crt0.o:' + l for l in expect_lines(cor['crt0.o'])],
              'nm -po prefixes the file name')
        st, out, err = run(root, 'nm', ['-z', 'crt0.o'], tmp)
        check(st == 1 and err == 'nm: invalid argument -z\n' and out == '',
              'nm -z: invalid argument, status 1')

        print('== libc.a\'s __.SYMDEF is the table nm -g gives ==')
        symdef = [d for n, _, d in members if n == '__.SYMDEF']
        check(len(symdef) == 1, 'libc.a has its __.SYMDEF')
        tab = {}
        sd = symdef[0]
        for i in range(0, len(sd) - 11, 12):
            name = sd[i:i + 8].split(b'\0')[0].decode('latin1')
            hi, lo = struct.unpack_from('<HH', sd, i + 8)
            tab.setdefault(name, []).append((hi << 16) | lo)
        want = {}
        for n, off, d in objs:
            for name, t, v in symbols(d):
                if t & 0x20 and (t & 0x1f) != 0:       # ranlib: external, not undefined
                    want.setdefault(name[:8], []).append(off)
        # the table has an entry for every defined external symbol of a member, once
        missing = [k for k in want if k not in tab]
        extra = [k for k in tab if k not in want]
        if missing or extra:
            print('  missing: %s; extra: %s' % (missing, extra))
        check(not missing and not extra,
              '__.SYMDEF names the externals nm -g shows as defined (%d names; %d missing, %d extra)'
              % (len(want), len(missing), len(extra)))
        ok = all(sorted(tab.get(k, [])) == sorted(v) for k, v in want.items())
        check(ok, '__.SYMDEF names the member that defines each symbol')

        print('== a program made by cc: nm, size, strip ==')
        with open(os.path.join(tmp, 'hello.c'), 'w') as fh:
            fh.write('#include <stdio.h>\n\nmain()\n{\n\tprintf("hello, world\\n");\n\texit(0);\n}\n')
        st, out, err = run(root, 'cc', ['-o', 'hello', 'hello.c'], tmp)
        if not check(st == 0 and os.path.exists(os.path.join(tmp, 'hello')),
                     'cc -o hello hello.c works (%s)' % err.strip()[:100]):
            return report()
        prog = open(os.path.join(tmp, 'hello'), 'rb').read()
        _, text, data, bss, syms, _, _, flag = header(prog)
        check(syms > 0, 'the program has symbols')
        st, out, err = run(root, 'nm', ['hello'], tmp)
        lines = out.splitlines()
        check(any(l.endswith(' T _main') for l in lines) and any(l.endswith(' T _printf')
                                                                   for l in lines)
              and [l.rsplit(' ', 1)[1] for l in lines] == sorted(l.rsplit(' ', 1)[1] for l in lines),
              'nm hello: _main and _printf are defined, sorted by name')
        sz = run(root, 'size', ['hello'], tmp)
        st, out, err = run(root, 'strip', ['hello'], tmp)
        check((st, out, err) == (0, '', ''), 'strip hello: quiet, status 0')
        stripped = open(os.path.join(tmp, 'hello'), 'rb').read()
        _, t2, d2, b2, s2, e2, u2, f2 = header(stripped)
        check(s2 == 0 and f2 & 1 and (t2, d2, b2, e2) == (text, data, bss, header(prog)[5])
              and len(stripped) == 16 + t2 + d2 and stripped[16:] == prog[16:16 + text + data],
              'strip hello: no symbols, the flag set, header, text and data as they were')
        check(run(root, 'size', ['hello'], tmp) == sz, 'size hello is the same after strip')
        st, out, err = run(root, 'nm', ['hello'], tmp)
        check(st == 0 and out == '' and err == 'nm: hello-- no name list\n',
              'nm hello after strip: no name list')
        st, out, err = run(root, 'strip', ['hello', 'nothere', 'crt0.o'], tmp)
        check(st == 1 and out.splitlines()[0] == 'hello already stripped'
              and 'cannot open nothere' in out, 'strip says "already stripped" and "cannot open"')
        check(not os.listdir(os.path.join(root, 'tmp')), 'strip leaves nothing in the root\'s tmp/')

        print('== tsort ==')

        def ts(text, args=()):
            return run(root, 'tsort', list(args), tmp, text.encode())
        check(ts('a b\nb c\nc d\n') == (0, 'a\nb\nc\nd\n', ''), 'tsort: a chain')
        st, out, err = ts('a b\na c\nb d\nc d\n')
        o = out.split()
        check(st == 0 and sorted(o) == list('abcd') and o.index('a') < o.index('b') < o.index('d')
              and o.index('a') < o.index('c') < o.index('d') and err == '', 'tsort: a diamond')
        st, out, err = ts('a b\nb c\nc a\nc d\n')
        check(sorted(out.split()) == list('abcd') and err.startswith(
            'tsort: cycle in reverse order\n'), 'tsort: a cycle is reported, every item once')
        check(ts('a b\nc\n')[0] == 1 and ts('a b\nc\n')[2] == 'tsort: odd data\n',
              'tsort: odd data, status 1')
        check(ts('', ['nothere'])[:3] == (1, '', 'tsort: cannot open nothere\n'),
              'tsort: cannot open')
        open(os.path.join(tmp, 'g.txt'), 'w').write('x y\ny z\n')
        check(ts('', ['g.txt']) == (0, 'x\ny\nz\n', ''), 'tsort: a file named')

        print('== lorder ==')
        st, out, err = run(root, 'lorder', ['printf.o'], tmp)
        check((st, out, err) == (0, 'printf.o printf.o\n' * 2, ''),
              'lorder of one object: the pair with itself, twice')
        st, out, err = run(root, 'lorder', ['crt0.o'], tmp)
        check((st, out, err) == (0, '', 'nm: crt0.o-- no name list\n' * 2),
              'lorder of crt0.o, which has no symbols: nothing, "no name list" twice')
        st, lo, err = run(root, 'lorder', ['libc.a'], tmp)
        check(st == 0 and lo.count('\n') > len(objs), 'lorder libc.a: %d pairs' % lo.count('\n'))
        # an independent reading: a defines a symbol that b uses (nm -g's T and D defined,
        # every other symbol a reference, as lorder's script has it)
        defs, refs = {}, {}         # symbol -> the objects that define / use it
        for n, _, d in objs:
            for name, t, v in symbols(d):
                if not t & 0x20:
                    continue
                c = letter(t, v)
                (defs if c in 'TD' else refs).setdefault(name[:8], set()).add(n)
        pairs = set()
        for s, rs in refs.items():
            for r in rs:
                for dm in defs.get(s, ()):
                    pairs.add((r, dm))
        got = {tuple(l.split()) for l in lo.splitlines()}
        selfs = {(n, n) for n, _, _ in objs}
        check(got - selfs == pairs - selfs and selfs <= got,
              'lorder libc.a gives the pairs "user definer" of nm -g, and each object with itself '
              '(%d pairs)' % len(got))
        st, sorted_, err = run(root, 'tsort', [], tmp, lo.encode())
        names = sorted_.split()
        check(sorted(names) == sorted(n for n, _, _ in objs) and len(names) == len(set(names)),
              'lorder libc.a | tsort prints each of the %d objects once' % len(objs))
        pos = {n: i for i, n in enumerate(names)}
        cyc = err.count('\n') - 1 if err else 0
        ordered = sum(1 for a, b in pairs if a != b and pos[a] < pos[b])
        total = sum(1 for a, b in pairs if a != b)
        check(ordered * 2 > total,
              'tsort puts the user before the definer for most pairs (%d of %d; %d lines of cycles)'
              % (ordered, total, cyc))
    return report()


def report():
    print('%d checks, %d failed' % (CHECKS, len(FAILED)))
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
