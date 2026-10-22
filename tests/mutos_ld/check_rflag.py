#!/usr/bin/env python3
"""check_rflag.py - mutos_ld's extension: -r and -d.

The original MUTOS 1700 ld refuses -r and -d ("ld: -r and -d flags not
supported", exit status 4); mutos_ld, the port of it, keeps them as a
documented extension (docs/TOOLCHAIN_PLAN.md, decision 5): v7's -r (keep
the relocation bits, leave the commons undefined) and -d (define the
commons even with -r), with MUTOS's object conventions - data and BSS
values offsets in their own segment, the byte-shifted relocation words
(bit 15) kept. There is no original to compare them with, so this checks
them by what they are for: a program linked through a relocatable object
must do what the same program linked at once does.

For every program of tests/mutos_cc's construct corpus (the categories
00 to 10, each a main() whose exit status is its result), assembled by
mutos_as and linked with the real crt0.o and libc.a:

  direct   ld -X crt0.o x.o -lc
  -r x     ld -r -o r.o x.o;                 ld -X crt0.o r.o -lc
  -r all   ld -r -o r.o crt0.o x.o -lc;      ld -X r.o
  -r -d    ld -r -d -o r.o crt0.o x.o -lc;   ld r.o
  -r -d -i ld -r -d -o r.o crt0.o x.o -lc;   ld -i r.o

each run under the 8086 emulator (tests/realbin/mutosemu.py): the exit
status and the output must be the direct link's. As v7's, -d also
defines _etext, _edata and _end in the relocatable object; a program
that uses one of them (through libc.a's sbrk()) is then refused by the
second link - "_end: ld:Multiply defined", exit status 1 -, which is
checked as such. Also checked: a -r
output's header (magic 0407, relocation present), and that the original
ld, emulated, refuses -r and -d as described.

Usage: check_rflag.py [--port PATH]
Requires the Python module "unicorn" (pip install unicorn); run by
"make check-realbin".
"""
import base64
import glob
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tests', 'realbin'))
import mutosemu       # noqa: E402

PORT = os.path.join(REPO, 'src', 'mutos_ld', 'mutos_ld')
AS = os.path.join(REPO, 'src', 'mutos_as', 'mutos_as')
CORPUS = os.path.join(REPO, 'tests', 'mutos_cc')
SYMDEF_TIME = 602282477

VARIANTS = [
    ('-r x', [['-r', '-o', 'r.o', 'x.o'], ['-X', '-o', 'p', 'crt0.o', 'r.o', '-lc']]),
    ('-r all', [['-r', '-o', 'r.o', 'crt0.o', 'x.o', '-lc'], ['-X', '-o', 'p', 'r.o']]),
    ('-r -d', [['-r', '-d', '-o', 'r.o', 'crt0.o', 'x.o', '-lc'], ['-o', 'p', 'r.o']]),
    ('-r -d -i', [['-r', '-d', '-o', 'r.o', 'crt0.o', 'x.o', '-lc'],
                  ['-i', '-o', 'p', 'r.o']]),
]

FAILS = []


def check(ok, what):
    if not ok:
        print('  FAIL %s' % what)
        FAILS.append(what)
    return ok


def make_root(path):
    for sub in ('lib', 'usr/lib', 'tmp'):
        os.makedirs(os.path.join(path, sub), exist_ok=True)
    for name, rel in (('lib/crt0.o', 'tests/mutos1700_crt0/crt0.o.base64.txt'),
                      ('lib/libc.a', 'tests/mutos1700_libc/libc.a.base64.txt')):
        with open(os.path.join(REPO, rel), 'rb') as f, \
                open(os.path.join(path, name), 'wb') as g:
            g.write(base64.b64decode(f.read()))
    os.utime(os.path.join(path, 'lib', 'libc.a'), (SYMDEF_TIME, SYMDEF_TIME))
    return path


def ld(port, args, cwd, root):
    env = dict(os.environ, MUTOS_ROOT=root)
    return subprocess.run([port] + args, cwd=cwd, env=env, capture_output=True)


def run(prog):
    """(exit status, stdout) of a MUTOS program under the emulator."""
    with open(prog, 'rb') as f:
        image = f.read()
    import io
    out = io.BytesIO()
    emu = mutosemu.Emu(image, ['p'], cwd=os.path.dirname(prog),
                       stdin=io.BytesIO(b''), stdout=out, stderr=io.BytesIO())
    try:
        st = emu.run(20_000_000)
    except RuntimeError as e:
        return 'stopped: %s' % e, out.getvalue()
    return st, out.getvalue()


def symtype(path, name):
    """The type of a symbol in an a.out's symbol table, None if absent."""
    with open(path, 'rb') as f:
        b = f.read()
    magic, t, d, bss, syms, entry, pad, flag = struct.unpack_from('<8H', b)
    off = 16 + (t + d) * (1 if flag else 2)
    for i in range(off, off + syms, 12):
        if b[i:i + 8].rstrip(b'\0') == name.encode():
            return b[i + 8]
    return None


def programs():
    out = []
    for c in sorted(glob.glob(os.path.join(CORPUS, '[01][0-9]_*', '*.s.golden'))):
        if os.path.basename(os.path.dirname(c)) == '11_kernel':
            continue
        out.append(c)
    return out


def main(argv):
    port = PORT
    if argv[:1] == ['--port']:
        port = os.path.abspath(argv[1])
    with tempfile.TemporaryDirectory(prefix='rflag') as tmp:
        root = make_root(os.path.join(tmp, 'root'))
        crt0 = os.path.join(root, 'lib', 'crt0.o')
        print('== the original refuses -r and -d ==')
        for opt in ('-r', '-d'):
            st, out, err = mutosemu.run_tool('ld', [opt, crt0], cwd=tmp, root=root)
            check(st == 4 and out == b'ld: -r and -d flags not supported\n',
                  'the original on %s: status %s, %r' % (opt, st, out))
        print('  ok   (exit status 4, "ld: -r and -d flags not supported")'
              if not FAILS else '')
        print('== programs linked through a relocatable object ==')
        n = same = 0
        vsd = []
        for src in programs():
            name = os.path.relpath(src, CORPUS)[:-len('.s.golden')]
            work = os.path.join(tmp, name.replace('/', '_'))
            os.makedirs(work)
            r = subprocess.run([AS, '-o', os.path.join(work, 'x.o'), src],
                               capture_output=True)
            if not check(r.returncode == 0, '%s: mutos_as' % name):
                continue
            with open(crt0, 'rb') as f, open(os.path.join(work, 'crt0.o'), 'wb') as g:
                g.write(f.read())
            r = ld(port, ['-X', '-o', 'p', 'crt0.o', 'x.o', '-lc'], work, root)
            if not check(r.returncode == 0, '%s: the direct link: %r' % (
                    name, r.stdout + r.stderr)):
                continue
            want = run(os.path.join(work, 'p'))
            os.rename(os.path.join(work, 'p'), os.path.join(work, 'direct'))
            for label, steps in VARIANTS:
                n += 1
                r1 = ld(port, steps[0], work, root)
                ok = check(r1.returncode in (0, 1) and r1.stderr == b'',
                           '%s [%s]: ld -r: status %d, %r' % (
                               name, label, r1.returncode, r1.stdout + r1.stderr))
                if ok:
                    with open(os.path.join(work, 'r.o'), 'rb') as f:
                        h = struct.unpack('<8H', f.read(16))
                    ok = check(h[0] == 0o407 and h[7] == 0,
                               '%s [%s]: the -r header %s' % (name, label, h))
                if ok and '-d' in steps[0]:
                    defined = sorted(n for n in ('_etext', '_edata', '_end')
                                     if symtype(os.path.join(work, 'r.o'), n)
                                     not in (None, 0x20))
                    if defined:
                        # v7's -d: the second link defines them again
                        r2 = ld(port, steps[1], work, root)
                        want_msg = b''.join(b'%s: ld:Multiply defined\n' % d.encode()
                                            if i == 0 else
                                            b'%s: Multiply defined\n' % d.encode()
                                            for i, d in enumerate(defined))
                        same += check(r2.returncode == 1 and r2.stdout == want_msg,
                                      '%s [%s]: v7\'s -d: %r' % (name, label, r2.stdout))
                        vsd.append('%s [%s]' % (name, label))
                        continue
                if ok:
                    r2 = ld(port, steps[1], work, root)
                    ok = check(r2.returncode == 0,
                               '%s [%s]: the second link: status %d, %r' % (
                                   name, label, r2.returncode, r2.stdout + r2.stderr))
                if ok:
                    got = run(os.path.join(work, 'p'))
                    ok = check(got == want, '%s [%s]: %r, the direct link %r' % (
                        name, label, got, want))
                same += ok
        print('  %d of %d linked through -r as the direct link runs (%d of them '
              'v7\'s -d with _end defined: %s)' % (same, n, len(vsd), ', '.join(vsd)))
    print('check_rflag: %s' % ('%d failure(s)' % len(FAILS) if FAILS
                               else 'all checks passed'))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
