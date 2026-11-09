#!/usr/bin/env python3
"""linkcheck.py - the corrected copy of libc.a against the original, on
whole programs the original compiler made.

Step F3b of docs/TOOLCHAIN_PLAN.md: the copy (src/libc_fixed/libc.a)
must keep the interface of the original archive - what the original
compiler made links with it, and runs as with the original but where a
corrected bug changes a value. Every assemblable .s golden of
tests/mutos_cc (the real compiler's output, as tests/realbin/ldcmp.py
takes them) is assembled by mutos_as, linked by mutos_ld as cc links
(-X crt0.o x.o, then the archive) once with the original libc.a and
once with the copy, and both images run under tests/realbin/mutosemu.py
(no arguments, an empty standard input, an instruction limit): the exit
status, both output streams - or the way the run stopped - must be the
same. A program that links none of the corrected members gets the same
image; one that does is run.

Then the triggers (triggers/*.c): one small program per correction,
compiled and linked by the port's cc in the original mode (lib/libc.a)
and with -F (the corrected mode: lib/fixed/libc.a, ld's rule LIBC-F) in
the MUTOS root, both run under the emulator; each program says what it
must give - "expect: original X, corrected Y", X and Y an exit status or
SIGFPE (the program killed by it) - and, on a line "cc: ...", options of
its own for cc.

Usage:
    linkcheck.py [--copy ARCHIVE] [--show N]

Exit status 0 if every program runs as it must, 1 otherwise. Requires
the Python module "unicorn" and a built tree ("make").
"""

import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'tests', 'realbin'))

try:
    import mutosemu  # noqa: E402
    import ldcmp     # noqa: E402  (golden_sources: the goldens ldcmp.py links)
except ImportError:
    sys.exit('linkcheck.py: needs the Python module "unicorn" (pip install unicorn)')

AS = os.path.join(ROOT, 'src', 'mutos_as', 'mutos_as')
CC = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_cc')
MUTOS_ROOT = os.path.join(ROOT, 'mutos_root')
TRIGGERS = os.path.join(HERE, 'triggers')
SIGFPE = 8
LD = os.path.join(ROOT, 'src', 'mutos_ld', 'mutos_ld')
ORIG = os.path.join(ROOT, 'mutos_root', 'lib', 'libc.a')
CRT0 = os.path.join(ROOT, 'mutos_root', 'lib', 'crt0.o')
COPY = os.path.join(ROOT, 'src', 'libc_fixed', 'libc.a')
FIXED = os.path.join(ROOT, 'src', 'libc_fixed', 'fixed')
MAX_INSNS = 20000000


def link(work, archive, out):
    """mutos_ld -X crt0.o x.o ARCHIVE into OUT; (image, the members of
    the corrected ones it took) or None."""
    r = subprocess.run([LD, '-X', '-o', out, CRT0, 'x.o', archive], cwd=work,
                       capture_output=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(work, out)):
        return None, r.stderr.decode('latin-1').strip()
    with open(os.path.join(work, out), 'rb') as f:
        return f.read(), None


def run(image, work):
    o, e = io.BytesIO(), io.BytesIO()
    try:
        emu = mutosemu.Emu(image, ['a.out'], cwd=work, stdin=io.BytesIO(b''),
                           stdout=o, stderr=e, eof_limit=1000)
        st = emu.run(MAX_INSNS)
        return ('exit', st, o.getvalue(), e.getvalue())
    except RuntimeError as x:
        return ('stopped', str(x).split(' at ')[0], o.getvalue(), e.getvalue())


def status(text):
    return 128 + SIGFPE if text == 'SIGFPE' else int(text)


def triggers(work):
    """The trigger programs: [(name, problem)] of those that do not run as
    they must, and how many ran."""
    bad, n = [], 0
    env = dict(os.environ, MUTOS_ROOT=MUTOS_ROOT)
    env.pop('MUTOS_MODE', None)
    env.pop('MUTOS_RULELOG', None)
    for name in sorted(os.listdir(TRIGGERS)):
        if not name.endswith('.c'):
            continue
        with open(os.path.join(TRIGGERS, name), encoding='utf-8') as f:
            text = f.read()
        m = re.search(r'expect: original (\w+), corrected (\w+)', text)
        if m is None:
            bad.append((name, 'no "expect: original X, corrected Y"'))
            continue
        opts = re.search(r'^ \* cc: (.*)$', text, re.M)
        opts = opts.group(1).split() if opts else []
        shutil.copy(os.path.join(TRIGGERS, name), os.path.join(work, name))
        n += 1
        for mode, flag, want in (('original', [], m.group(1)), ('corrected', ['-F'], m.group(2))):
            r = subprocess.run([CC] + flag + opts + ['-o', 'a.out', name], cwd=work,
                               env=env, capture_output=True)
            if r.returncode != 0:
                bad.append((name, '%s: cc %s' % (mode, r.stderr.decode('latin-1').strip())))
                continue
            with open(os.path.join(work, 'a.out'), 'rb') as f:
                got = run(f.read(), work)
            if got[0] != 'exit' or got[1] != status(want):
                bad.append((name, '%s: %s, not %s' % (
                    mode, 'SIGFPE' if got[:2] == ('exit', 128 + SIGFPE) else got[:2], want)))
    return bad, n


def main(argv):
    copy, show = COPY, 5
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--copy':
            copy = os.path.abspath(args.pop(0))
        elif a == '--show':
            show = int(args.pop(0))
        else:
            print(__doc__)
            return 2
    for tool in (AS, LD, CC):
        if not os.access(tool, os.X_OK):
            sys.exit('linkcheck.py: %s is not built - run "make" first' % tool)
    for f in (ORIG, CRT0, copy):
        if not os.path.isfile(f):
            sys.exit('linkcheck.py: %s is missing - run "make" first' % f)
    goldens = ldcmp.golden_sources()
    n = same_image = ran = 0
    differ, unlinked = [], []
    with tempfile.TemporaryDirectory(prefix='linkcheck.') as work:
        shutil.copy2(ORIG, os.path.join(work, 'orig.a'))
        shutil.copy2(copy, os.path.join(work, 'copy.a'))
        for g in goldens:
            rel = os.path.relpath(g, os.path.join(ROOT, 'tests', 'mutos_cc'))
            r = subprocess.run([AS, '-o', os.path.join(work, 'x.o'), g], capture_output=True)
            if r.returncode != 0:
                continue
            a, why_a = link(work, 'orig.a', 'a.orig')
            b, why_b = link(work, 'copy.a', 'a.copy')
            if a is None or b is None:
                if (a is None) != (b is None) or why_a != why_b:
                    differ.append((rel, 'link: %s / %s' % (why_a, why_b)))
                else:
                    unlinked.append(rel)
                continue
            n += 1
            if a == b:
                same_image += 1
                continue
            ran += 1
            ra, rb = run(a, work), run(b, work)
            if ra != rb:
                differ.append((rel, 'original %r, copy %r' % (ra[:2] + (ra[2][:60], ra[3][:60]),
                                                              rb[:2] + (rb[2][:60], rb[3][:60]))))
        trig_bad, ntrig = triggers(work)
    print('linkcheck: %d golden programs linked with the original libc.a and with %s '
          '(%d not linkable alone: no main, undefined symbols): %d the same image, '
          '%d run under the emulator' % (n, os.path.relpath(copy, ROOT), len(unlinked),
                                          same_image, ran))
    for rel, what in differ[:show]:
        print('  DIFFERENT %s: %s' % (rel, what))
    if not differ:
        print('linkcheck: every golden program the same - exit status, output, or the '
              'way it stopped')
    print('linkcheck: %d trigger programs (%s), cc and cc -F: %s' % (
        ntrig, os.path.relpath(TRIGGERS, ROOT),
        'each gives what it must' if not trig_bad else '%d wrong' % len(trig_bad)))
    for name, what in trig_bad[:show]:
        print('  WRONG %s: %s' % (name, what))
    return 1 if differ or trig_bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
