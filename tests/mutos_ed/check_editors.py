#!/usr/bin/env python3
"""check_editors.py - mutos_ed and mutos_em on the MUTOS kernel's makefile.

The MUTOS 1700 kernel is configured and built with a makefile that edits
itself with ed and post-processes an assembler source with em
(kernel/conf.Makefile, the kernel's conf/Makefile, and kernel/fakeawk, its
mdep/fakeawk). This runs it, with the ports only (mutos_make, and the tools
of the MUTOS root's bin/ that its recipes call: ed, em, cc - no emulator,
plain Python 3; the comparison with the originals under the emulator is
tests/realbin/edcmp.py and edfuzz.py), in a scratch copy of a kernel tree -
conf/, mdep/ with mch.c (tests/mutos_cpp/c/) and fakeawk, h/ with the
kernel's headers (tests/mutos_cpp/h/), and a small source in io/, sys/ and
cfg/ for the recipes' globs:

  1. "make 7150", "make 1834", "make 7100": the configuration block of the
     machine named is commented in, those of the others out, and the
     "mutos:" line of the machine is the one in force - as an independent
     reading of the eddep script gives it -; Makefile.bak is the makefile
     before, arith.a is touched, eddep removed; "make 7100" gives the
     original makefile back byte for byte;
  2. every option target (AMX noAMX .. V30IDE noV30IDE): the option is
     added to (or removed from) DRIVER, CONFIG or FLAGS (-D<option>) as the
     script's substitutions give it, the sources that test the option are
     touched; all of them, X then noX, leave DRIVER, CONFIG and FLAGS empty
     and the rest of the makefile as it was;
  3. "make depend": the dependencies behind "# DO NOT DELETE THIS LINE" are
     the #include lines of every source, a "touch $@" behind each, then the
     three comment lines; a second "make depend" gives the same file;
  4. "make ../mdep/mch_M7100.o": cc -P with the configuration's -D flags
     (those the kernel's goldens were made with), em with fakeawk, cc -c:
     the object's text, data and relocation are kernel_opt/mch.o.golden's
     (real hardware) - the relocation's symbols by name -, its symbols the
     golden's without the local L symbols (cc runs as with -C, the golden
     was assembled without it); mch.i and
     mch.s are removed; the two steps of em alone give mch.i.golden's
     post-processed form, kernel_opt/mch.s, byte for byte;
  5. the shell commands, which the emulator cannot check: ed's "!cmd", em's
     "!cmd" with "%" and "!!", and em's "|cmd".

Usage: check_editors.py [ROOT]     (default: $MUTOS_ROOT, else <tree>/mutos_root)
Exit status 1 on any failure.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
KERNEL = os.path.join(HERE, 'kernel')
MAKE = os.path.join(REPO, 'src', 'mutos_make', 'mutos_make')

FAILED = []
CHECKS = 0


def check(ok, what):
    global CHECKS
    CHECKS += 1
    if not ok:
        FAILED.append(what)
        print('FAILED: ' + what)
    return ok


def rd(p):
    with open(p, 'rb') as f:
        return f.read()


def wr(p, b):
    with open(p, 'wb') as f:
        f.write(b)


OPTIONS = {'LP': 'DRIVER', 'AMX': 'DRIVER', 'ASK': 'DRIVER', 'BUS': 'DRIVER',
           'IFSP': 'DRIVER', 'IFSS': 'DRIVER', 'V24': 'DRIVER', 'RAM': 'DRIVER',
           'DABA': 'DRIVER', 'V30IDE': 'DRIVER', 'MSDOS': 'CONFIG',
           'KDEBUG': 'CONFIG', 'LOAD86': 'CONFIG', 'INSTRM': 'FLAGS',
           'DISKMON': 'FLAGS', 'SYSACCT': 'FLAGS', 'SYSLOCK': 'FLAGS'}


def tree(tmp):
    """A kernel tree to build in: tmp/conf is the directory of the makefile."""
    k = os.path.join(tmp, 'kernel')
    for d in ('conf', 'mdep', 'h', 'io', 'sys', 'cfg'):
        os.makedirs(os.path.join(k, d))
    shutil.copy(os.path.join(KERNEL, 'conf.Makefile'), os.path.join(k, 'conf', 'Makefile'))
    shutil.copy(os.path.join(KERNEL, 'fakeawk'), os.path.join(k, 'mdep', 'fakeawk'))
    shutil.copy(os.path.join(REPO, 'tests', 'mutos_cpp', 'c', 'mch.c'), os.path.join(k, 'mdep'))
    hd = os.path.join(REPO, 'tests', 'mutos_cpp', 'h')
    for n in sorted(os.listdir(hd)):
        if os.path.isfile(os.path.join(hd, n)):
            shutil.copy(os.path.join(hd, n), os.path.join(k, 'h'))
    tests = ''.join('#ifdef %s\n#endif\n' % o for o in sorted(OPTIONS))
    wr(os.path.join(k, 'io', 'opts.c'), ('#include "../h/param.h"\n' + tests).encode())
    wr(os.path.join(k, 'sys', 'sx.c'), b'#include "../h/param.h"\n#include "../h/user.h"\nint x;\n')
    wr(os.path.join(k, 'cfg', 'cy.c'), b'int y;\n')
    wr(os.path.join(k, 'conf', 'c.c'), b'#include "../h/param.h"\nint c;\n')
    return os.path.join(k, 'conf')


def make(conf, root, *targets):
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    r = subprocess.run([MAKE] + list(targets), cwd=conf, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode, r.stdout, r.stderr


# ---- an independent reading of the scripts ------------------------------------------

MACHINES = ('7100', '7150', '1834')


def configure(lines, m):
    """The eddep of "make <m>": the four lines behind each "### NNNN only:"
    commented, those behind "### <m> only:" uncommented, the "mutos:" lines
    marked "### NNNN only" commented, the one of <m> in force."""
    out = list(lines)
    for i, l in enumerate(lines):
        if re.match(r'^### [17][18][035][04] only:$', l):
            for j in range(i + 1, i + 5):
                if out[j] and out[j][0] != '#':
                    out[j] = '# ' + out[j]
    for i, l in enumerate(out):
        if re.search(r'### [17][18][035][04] only$', l) and l.startswith('  '):
            out[i] = '# ' + l[2:]
    for i, l in enumerate(lines):
        if l == '### %s only:' % m:
            for j in range(i + 1, i + 5):
                if out[j].startswith('# '):
                    out[j] = out[j][2:]
    for i, l in enumerate(out):
        if re.search(r'### %s only$' % m, l) and l.startswith('# '):
            out[i] = '  ' + l[2:]
    return out


def option(lines, target):
    """The eddep of an option target: the option out of its line, and in
    again at its end for the positive target."""
    neg = target.startswith('no')
    o = target[2:] if neg else target
    s = OPTIONS[o]
    word = '-D' + o if s == 'FLAGS' else o
    out = list(lines)
    last = None
    for i, l in enumerate(out):
        if l.startswith(s + ' ='):
            out[i] = re.sub(re.escape(word) + ' *', '', l)
            last = i
    if not neg:
        # "s/ *$/ word/" on the current line - the last one the g matched
        out[last] = re.sub(r' *$', ' ' + word, out[last], count=1)
    return out


def depend(lines, conf):
    """The dependencies "make depend" puts behind the marker."""
    deps = []
    for pat in ('*.c', '../sys/*.c', '../mdep/*.c', '../io/*.c', '../cfg/*.c'):
        d, g = os.path.split(pat)
        dd = os.path.normpath(os.path.join(conf, d))
        names = sorted(n for n in os.listdir(dd) if n.endswith('.c'))
        for n in names:
            src = os.path.join(d, n) if d else n
            for l in rd(os.path.join(dd, n)).decode('latin1').split('\n'):
                if re.match(r'^#[ \t]*include', l):
                    m = re.match(r'^[^"]*"([^"]*)".*', l)
                    deps.append('%s: %s' % (src, m.group(1)) if m else '%s:%s' % (src, l))
            deps.append('\ttouch $@')
    i = lines.index('# DO NOT DELETE THIS LINE -- make depend uses it')
    return lines[:i + 2] + deps + ['# DEPENDENCIES MUST END AT END OF FILE',
                                   '# IF YOU PUT STUFF HERE IT WILL GO AWAY',
                                   '# see make depend above']


def text(p):
    return rd(p).decode('latin1').split('\n')


def check_configure(conf, root):
    orig = text(os.path.join(conf, 'Makefile'))
    cur = orig
    for m in ('7150', '1834', '7100'):
        st, out, err = make(conf, root, m)
        check(st == 0, 'make %s: exit %d %r' % (m, st, err[-300:]))
        new = text(os.path.join(conf, 'Makefile'))
        want = configure(cur, m)
        check(new == want, 'make %s: the makefile (%s)' % (m, [
            (i + 1, a, b) for i, (a, b) in enumerate(zip(new, want)) if a != b][:3]))
        check(text(os.path.join(conf, 'Makefile.bak')) == cur, 'make %s: Makefile.bak' % m)
        check(not os.path.exists(os.path.join(conf, 'eddep')), 'make %s: eddep removed' % m)
        check(os.path.exists(os.path.join(conf, 'arith.a')), 'make %s: arith.a touched' % m)
        typ = [l for l in new if l.startswith('TYP = ')]
        check(typ == ['TYP = M' + m], 'make %s: TYP %r' % (m, typ))
        cur = new
    check(rd(os.path.join(conf, 'Makefile')) == rd(os.path.join(KERNEL, 'conf.Makefile')),
          'make 7150, 1834, 7100: not the original makefile again')


def check_options(conf, root):
    opts = os.path.join(conf, '..', 'io', 'opts.c')
    targets = []
    for o in OPTIONS:
        targets += [o, 'no' + o]
    for t in targets:
        cur = text(os.path.join(conf, 'Makefile'))
        before = os.stat(opts).st_mtime_ns
        time.sleep(0.01)
        st, out, err = make(conf, root, t)
        check(st == 0, 'make %s: exit %d %r' % (t, st, err[-300:]))
        new = text(os.path.join(conf, 'Makefile'))
        want = option(cur, t)
        check(new == want, 'make %s: the makefile %r' % (t, [
            (i + 1, a, b) for i, (a, b) in enumerate(zip(new, want)) if a != b][:3]))
        check(text(os.path.join(conf, 'Makefile.bak')) == cur, 'make %s: Makefile.bak' % t)
        check(os.stat(opts).st_mtime_ns > before, 'make %s: io/opts.c not touched' % t)
        check(not os.path.exists(os.path.join(conf, 'eddep')), 'make %s: eddep removed' % t)
    # every option out: DRIVER, CONFIG and FLAGS empty, the rest unchanged
    orig = text(os.path.join(KERNEL, 'conf.Makefile'))
    new = text(os.path.join(conf, 'Makefile'))
    want = [re.sub(r'^(DRIVER|CONFIG|FLAGS) =.*', r'\1 = ', l) for l in orig]
    check(new == want, 'the option targets X, noX: %r' % (
        [(a, b) for a, b in zip(new, want) if a != b][:3],))
    wr(os.path.join(conf, 'Makefile'), rd(os.path.join(KERNEL, 'conf.Makefile')))


def check_depend(conf, root):
    cur = text(os.path.join(conf, 'Makefile'))
    st, out, err = make(conf, root, 'depend')
    check(st == 0, 'make depend: exit %d %r' % (st, err[-300:]))
    new = text(os.path.join(conf, 'Makefile'))
    want = depend(cur, conf)
    # the makefile ends with the echo's newline: a last empty "line"
    check(new[:-1] == want and new[-1] == '', 'make depend: the makefile %r' % (
        [(i + 1, a, b) for i, (a, b) in enumerate(zip(new, want)) if a != b][:3]
        or (len(new), len(want)),))
    for n in ('eddep', 'makedep'):
        check(not os.path.exists(os.path.join(conf, n)), 'make depend: %s removed' % n)
    first = rd(os.path.join(conf, 'Makefile'))
    st, out, err = make(conf, root, 'depend')
    check(st == 0 and rd(os.path.join(conf, 'Makefile')) == first,
          'make depend twice: another makefile')
    wr(os.path.join(conf, 'Makefile'), rd(os.path.join(KERNEL, 'conf.Makefile')))


def aout(b):
    """header words, text+data, relocation, the symbols (12 bytes each)."""
    h = struct.unpack_from('<8H', b, 0)
    t, d, syms = h[1], h[2], h[4]
    body = b[16:16 + t + d]
    reloc = b'' if h[7] & 1 else b[16 + t + d:16 + 2 * (t + d)]
    off = 16 + (t + d) * (1 if h[7] & 1 else 2)
    st = [b[off + i:off + i + 12] for i in range(0, syms, 12)]
    return h, body, reloc, st


def check_mch(conf, root):
    golden = rd(os.path.join(REPO, 'tests', 'mutos_as', 'kernel_opt', 'mch.o.golden'))
    st, out, err = make(conf, root, '../mdep/mch_M7100.o')
    check(st == 0, 'make ../mdep/mch_M7100.o: exit %d %r' % (st, err[-400:]))
    mdep = os.path.join(conf, '..', 'mdep')
    obj = os.path.join(mdep, 'mch_M7100.o')
    if not check(os.path.exists(obj), 'make ../mdep/mch_M7100.o: no object'):
        return
    for n in ('mch.i', 'mch.s', 'mch.o'):
        check(not os.path.exists(os.path.join(mdep, n)), 'mch rule: %s left' % n)
    gh, gbody, greloc, gsyms = aout(golden)
    h, body, reloc, syms = aout(rd(obj))
    check(h[:4] == gh[:4] and h[5:] == gh[5:], 'mch_M7100.o: header %r, golden %r' % (h, gh))
    check(body == gbody, 'mch_M7100.o: text and data not the golden\'s')

    def named(rel, st):
        # an external relocation names its symbol by number (bits 4-14):
        # by name, as the symbol tables differ
        out = []
        for i in range(0, len(rel), 2):
            r = struct.unpack_from('<H', rel, i)[0]
            if r & 0x0e == 0x08:
                k = (r >> 4) & 0x7ff
                out.append((r & 0x800f, st[k][:8] if k < len(st) else k))
            else:
                out.append(r)
        return out
    check(named(reloc, syms) == named(greloc, gsyms), 'mch_M7100.o: relocation not the golden\'s')

    def local_l(s):
        typ = struct.unpack_from('<H', s, 8)[0]
        return s[:1] == b'L' and (typ & 0x20) == 0 and (typ & 0x1f) != 0
    want = [s for s in gsyms if not local_l(s)]
    check(syms == want, 'mch_M7100.o: %d symbols, the golden\'s without the local L ones %d'
          % (len(syms), len(want)))
    # the steps of the recipe, one by one
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    env['PATH'] = os.path.join(root, 'bin') + ':' + env.get('PATH', '/bin:/usr/bin')
    r = subprocess.run(['cc', '-P', '-DM7100', '-DASK', '-DIFSS', '-DV24', '-DV30IDE',
                        '../mdep/mch.c'], cwd=mdep, env=env, capture_output=True)
    check(r.returncode == 0 and rd(os.path.join(mdep, 'mch.i')) ==
          rd(os.path.join(REPO, 'tests', 'mutos_cpp', 'c', 'mch.i.golden')),
          'cc -P mch.c: not mch.i.golden')
    with open(os.path.join(mdep, 'fakeawk'), 'rb') as fi:
        r = subprocess.run(['em', 'mch.i'], cwd=mdep, env=env, stdin=fi, capture_output=True)
    check(r.returncode == 0 and r.stdout.startswith(b'Editor\n39873\n>'),
          'em mch.i <fakeawk: exit %d, %r' % (r.returncode, r.stdout[:40]))
    check(rd(os.path.join(mdep, 'mch.i')) ==
          rd(os.path.join(REPO, 'tests', 'mutos_as', 'kernel_opt', 'mch.s')),
          'em mch.i <fakeawk: not kernel_opt/mch.s')
    check(os.listdir(os.path.join(root, 'tmp')) == [] or
          not [n for n in os.listdir(os.path.join(root, 'tmp')) if n.startswith('e')],
          'em: a temporary file left in the root\'s tmp/')


def run(tool, args, script, cwd, root):
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    p = os.path.join(REPO, 'src', 'mutos_' + tool, 'mutos_' + tool)
    with tempfile.TemporaryFile() as fi:
        fi.write(script)
        fi.seek(0)
        r = subprocess.run([p] + args, cwd=cwd, env=env, stdin=fi, capture_output=True)
    return r.returncode, r.stdout, r.stderr


def check_shell(tmp, root):
    d = os.path.join(tmp, 'shell')
    os.makedirs(d)
    wr(os.path.join(d, 't'), b'one\ntwo\nthree\n')
    st, out, err = run('ed', ['-', 't'], b'!echo hi\n1p\n!echo there; echo x\nq\n', d, root)
    check(st == 0 and out == b'hi\n!\none\nthere\nx\n!\n', 'ed !: %r' % out)
    st, out, err = run('em', ['t'], b'!echo a %\n!! b\n!echo c\n2p\nq\n', d, root)
    check(st == 0 and out == b'Editor\n14\n>!echo a t\na t\n!\n>!echo a t b\na t b\n!\n'
          b'>c\n!\n>two\n>', 'em !, %%, !!: %r' % out)
    st, out, err = run('em', ['-s', 't'], b'1,2|cat\n3|tr a-z A-Z\n.=\n', d, root)
    check(st == 0 and out == b'one\ntwo\nTHREE\n3\n', 'em |: %r' % out)
    st, out, err = run('em', ['-s', 't'], b'!echo $PATH\nq\n', d, root)
    check(st == 0 and out.split(b':')[0] == os.path.join(root, 'bin').encode(),
          'em !: the root\'s bin/ not first in the PATH: %r' % out[:80])


def main(argv):
    root = argv[0] if argv else os.environ.get('MUTOS_ROOT') or os.path.join(REPO, 'mutos_root')
    root = os.path.abspath(root)
    for t in ('ed', 'em', 'cc', 'make'):
        if not os.path.exists(os.path.join(root, 'bin', t)):
            print('FAILED: %s/bin/%s missing (run make first)' % (root, t))
            return 1
    with tempfile.TemporaryDirectory(prefix='check_editors') as tmp:
        conf = tree(tmp)
        check_configure(conf, root)
        check_options(conf, root)
        check_depend(conf, root)
        check_mch(conf, root)
        check_shell(tmp, root)
    print('check_editors: %d checks, %d failed' % (CHECKS, len(FAILED)))
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
