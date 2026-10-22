#!/usr/bin/env python3
"""check_root.py - the MUTOS root (mutos_root/) and its lookup.

docs/TOOLCHAIN_PLAN.md, decisions 2 and 3: the build fills mutos_root/
(scripts/mutos_root.sh), and every tool looks up the absolute MUTOS
paths under $MUTOS_ROOT, by default that directory
(src/h/mutos_root.h). This checks both:

  1. the root: lib/crt0.o and lib/libc.a byte for byte the references
     in tests/ (decoded from their base64 text), libc.a's modification
     time its __.SYMDEF time (602282477, 1989-01-31 20:41:17 UTC), the
     links of the passes and tools to the right files under src/ (a tool
     not built yet is listed, not an error), usr/lib/ and usr/include/;
  2. mutos_root.h: rootpath_test.c, compiled with the
     -DMUTOS_ROOT_DEFAULT the Makefiles pass, with MUTOS_ROOT unset,
     empty, set with and without trailing slashes, and "/"; relative
     paths unchanged; a buffer too small;
  3. the time: mutos_ld links a compiled golden (08_float/01_floatbas,
     which pulls in much of libc.a) against the root's crt0.o and libc.a
     without "out of date", and with a copy of libc.a with a new time
     with it - the check can fail;
  4. with --realbin (needs the Python module "unicorn"): the same against
     the original ld, emulated (tests/realbin/mutosemu.py), through the
     root's lib/ and usr/ - status 0 and no message; with a newer
     libc.a its "out of date" warning.

Usage: check_root.py [--cc CC] [--realbin] ROOT
Exit status 1 on any failure.
"""
import base64
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(REPO, 'src')
SYMDEF_TIME = 602282477
LINKS = (('lib/cpp', 'mutos_cpp/mutos_cpp'), ('lib/c0', 'mutos_cc/mutos_c0'),
         ('lib/c1', 'mutos_cc/mutos_c1'), ('lib/c2', 'mutos_cc/mutos_c2'),
         ('bin/as', 'mutos_as/mutos_as'), ('bin/ld', 'mutos_ld/mutos_ld'),
         ('bin/cc', 'mutos_cc/mutos_cc'))
GOLDEN_S = os.path.join(REPO, 'tests', 'mutos_cc', '08_float',
                        '01_floatbas.s.golden')

FAILS = []


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        FAILS.append(what)


def decoded(rel):
    with open(os.path.join(REPO, rel), 'rb') as f:
        return base64.b64decode(f.read())


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def check_files(root):
    print('== the root: %s ==' % root)
    for d in ('lib', 'bin', 'usr/lib', 'usr/include'):
        check(os.path.isdir(os.path.join(root, d)), d + '/ is a directory')
    crt0 = os.path.join(root, 'lib', 'crt0.o')
    libc = os.path.join(root, 'lib', 'libc.a')
    check(os.path.isfile(crt0) and read(crt0) ==
          decoded('tests/mutos1700_crt0/crt0.o.base64.txt'),
          'lib/crt0.o is tests/mutos1700_crt0/crt0.o.base64.txt decoded')
    check(os.path.isfile(libc) and read(libc) ==
          decoded('tests/mutos1700_libc/libc.a.base64.txt'),
          'lib/libc.a is tests/mutos1700_libc/libc.a.base64.txt decoded')
    if os.path.isfile(libc):
        t = int(os.stat(libc).st_mtime)
        check(t == SYMDEF_TIME, "lib/libc.a's time is its __.SYMDEF time "
              '(%d, found %d)' % (SYMDEF_TIME, t))
    missing = []
    for mpath, tool in LINKS:
        p = os.path.join(root, mpath)
        want = os.path.realpath(os.path.join(SRC, tool))
        ok = os.path.islink(p) and os.path.realpath(p) == want
        check(ok, '%s -> src/%s' % (mpath, tool))
        if ok and not os.path.exists(p):
            missing.append(mpath)
    if missing:
        print('  (not built yet, the links dangle: %s)' % ', '.join(missing))


def check_header(root, cc):
    print('== src/h/mutos_root.h ==')
    tmp = tempfile.mkdtemp(prefix='rootpath')
    try:
        exe = os.path.join(tmp, 'rootpath_test')
        cmd = cc.split() + ['-std=c11', '-Wall', '-Wextra', '-Wpedantic',
                            '-Werror', '-I', os.path.join(SRC, 'h'),
                            '-DMUTOS_ROOT_DEFAULT="%s"' % root, '-o', exe,
                            os.path.join(HERE, 'rootpath_test.c')]
        r = subprocess.run(cmd, capture_output=True, text=True)
        check(r.returncode == 0, 'rootpath_test.c compiles without a '
              'warning (%s)' % (r.stderr.strip() or 'clean'))
        if r.returncode != 0:
            return
        env0 = {k: v for k, v in os.environ.items() if k != 'MUTOS_ROOT'}

        def run(args, root_env=None):
            env = dict(env0)
            if root_env is not None:
                env['MUTOS_ROOT'] = root_env
            out = subprocess.run([exe] + args, env=env, capture_output=True,
                                 text=True, check=True).stdout
            return out.split('\n')[:-1]

        paths = ['/lib/crt0.o', '/usr/include/stdio.h', 'x.o', './x.o', '/']
        got = run(paths)
        check(got == [root, root + '/lib/crt0.o',
                      root + '/usr/include/stdio.h', 'x.o', './x.o',
                      root + '/'],
              'MUTOS_ROOT unset: the compiled-in default; relative paths '
              'unchanged (%s)' % got)
        check(run(paths[:1], '') == [root, root + '/lib/crt0.o'],
              'MUTOS_ROOT empty: the compiled-in default')
        check(run(paths[:2], '/opt/m') ==
              ['/opt/m', '/opt/m/lib/crt0.o', '/opt/m/usr/include/stdio.h'],
              'MUTOS_ROOT=/opt/m')
        check(run(paths[:1], '/opt/m//') == ['/opt/m//', '/opt/m/lib/crt0.o'],
              "MUTOS_ROOT's trailing slashes dropped")
        check(run(paths[:1], '/') == ['/', '/lib/crt0.o'],
              'MUTOS_ROOT=/: the paths themselves')
        n = len('/opt/m/lib/crt0.o') + 1
        check(run(['-n', str(n)] + paths[:1], '/opt/m')[1:] ==
              ['/opt/m/lib/crt0.o'], 'a buffer of exactly the size fits')
        check(run(['-n', str(n - 1)] + paths[:1], '/opt/m')[1:] ==
              ['(does not fit)'], 'one byte less: NULL and an empty buffer')
        check(run(['-n', '0'] + paths[:1], '/opt/m')[1:] ==
              ['(does not fit)'], 'no buffer: NULL')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def assemble(tmp):
    """01_floatbas.o from its .s golden, by mutos_as."""
    src = os.path.join(tmp, 'f.s')
    shutil.copy(GOLDEN_S, src)
    obj = os.path.join(tmp, 'f.o')
    r = subprocess.run([os.path.join(SRC, 'mutos_as', 'mutos_as'), '-o', obj,
                        src], capture_output=True, text=True)
    check(r.returncode == 0, 'mutos_as assembles 08_float/01_floatbas.s.golden')
    return obj if r.returncode == 0 else None


def newer_libc(root, tmp):
    """A copy of the root's libc.a with a time after its __.SYMDEF's."""
    p = os.path.join(tmp, 'newlibc.a')
    shutil.copy(os.path.join(root, 'lib', 'libc.a'), p)
    os.utime(p, (SYMDEF_TIME + 1, SYMDEF_TIME + 1))
    return p


def check_mutos_ld(root):
    print("== mutos_ld and libc.a's time ==")
    ld = os.path.join(SRC, 'mutos_ld', 'mutos_ld')
    tmp = tempfile.mkdtemp(prefix='rootld')
    try:
        obj = assemble(tmp)
        if not obj:
            return
        for libc, warn, what in (
                (os.path.join(root, 'lib', 'libc.a'), False,
                 "the root's libc.a: no 'out of date'"),
                (newer_libc(root, tmp), True,
                 "a newer copy of it: 'out of date' (the check can fail)")):
            r = subprocess.run([ld, '-X', os.path.join(root, 'lib', 'crt0.o'),
                                '-o', os.path.join(tmp, 'a.out'), obj, libc],
                               capture_output=True, text=True)
            out = r.stdout + r.stderr
            check(r.returncode == 0 and ('out of date' in out) == warn,
                  '%s (status %d%s)' % (what, r.returncode,
                                        ', ' + out.strip() if out else ''))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def check_original_ld(root):
    print("== the original ld (emulated) through the root ==")
    sys.path.insert(0, os.path.join(REPO, 'tests', 'realbin'))
    import mutosemu
    tmp = tempfile.mkdtemp(prefix='rootold')
    try:
        obj = assemble(tmp)
        if not obj:
            return
        for warn, what in (
                (False, "the root's lib/: status 0, no message"),
                (True, "a newer copy of libc.a: the original's 'out of "
                       "date' warning (the check can fail)")):
            # the original writes /tmp/lda<pid>, so each run has a root of
            # its own: tmp/, and the root's usr/ and lib/ (or a lib/ with
            # the newer libc.a)
            eroot = tempfile.mkdtemp(dir=tmp)
            os.makedirs(os.path.join(eroot, 'tmp'))
            os.symlink(os.path.join(root, 'usr'), os.path.join(eroot, 'usr'))
            lib = os.path.join(eroot, 'lib')
            if not warn:
                os.symlink(os.path.join(root, 'lib'), lib)
            else:
                os.makedirs(lib)
                shutil.copy(os.path.join(root, 'lib', 'crt0.o'), lib)
                shutil.copy2(newer_libc(root, tmp), os.path.join(lib, 'libc.a'))
            work = tempfile.mkdtemp(dir=tmp)
            shutil.copy(obj, os.path.join(work, 'f.o'))
            st, out, err = mutosemu.run_tool(
                'ld', ['-X', '/lib/crt0.o', '-o', 'a.out', 'f.o', '-lc'],
                cwd=work, root=eroot)
            msg = (out + err).decode('latin1')
            ok = (st == 0 and os.path.isfile(os.path.join(work, 'a.out'))
                  and ('out of date' in msg) == warn and (warn or not msg))
            check(ok, '%s (status %d%s)' % (what, st, ', ' + msg.strip()
                                             if msg else ''))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    cc = os.environ.get('CC', 'cc')
    realbin = False
    while argv and argv[0].startswith('--'):
        opt = argv.pop(0)
        if opt == '--cc':
            cc = argv.pop(0)
        elif opt == '--realbin':
            realbin = True
        else:
            sys.exit('check_root.py: unknown option %s' % opt)
    if len(argv) != 1:
        sys.exit(__doc__.split('Usage:')[1].split('\n')[0])
    root = os.path.abspath(argv[0])
    check_files(root)
    check_header(root, cc)
    check_mutos_ld(root)
    if realbin:
        check_original_ld(root)
    print('mutos_root: %s' % ('%d failure(s)' % len(FAILS) if FAILS
                              else 'all checks passed'))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
