#!/usr/bin/env python3
"""cctrace.py - the original MUTOS 1700 cc under the emulator, traced.

The original driver (mutos_bin/cc, 0407) forks and execs its passes.
mutosemu.py fakes fork, exece and wait (see its docstring), so cc runs
there; this tool records what it does - every exece with its path and
argv, in order with what cc and its passes write to stdout and stderr,
and cc's exit status. It is the oracle for the port of cc
(docs/TOOLCHAIN_PLAN.md, step 3): the port's trace over a matrix of
options is to be the original's.

Two ways to run cc:

  * traced (the default): no pass runs. An exece of a file that exists
    "exits 0", one of a file that does not fails with ENOENT, as on
    MUTOS - cc then prints "Can't find ..." itself;
  * --run: every pass runs, as an emulated MUTOS a.out (the originals of
    an oracle root, below) - the whole chain cpp, c0, c1, c2, as, ld,
    with their messages and exit statuses; their output files are left
    in the working directory. A file that is not a MUTOS a.out fails
    with ENOEXEC, as on MUTOS.

The MUTOS root: cc's absolute paths (/lib/c0, /lib/crt0.o, /tmp/ctm...)
are looked up under a root directory (mutosemu.py's --root). By default
cctrace.py makes a fresh oracle root for the run and removes it after:
lib/crt0.o and lib/libc.a from tests/ (libc.a's time set to its
__.SYMDEF's, as decision 3 of the plan has it for mutos_root/), the
originals of mutos_bin/ as lib/cpp, lib/c0, lib/c1, lib/c2, bin/as,
bin/ld, bin/cc, empty usr/lib and usr/include, and tmp/. --make-root DIR
writes one to keep; --root DIR uses a given one - mutos_root/, say, to
see the paths cc asks for there.

The trace lists each exece as "exec PATH ARGV...", output lines as "1|
text" (stdout) and "2| text" (stderr), then "exit N". cc's temporary
files, /tmp/ctm<k> and mktemp's six digits of the process number, are
written /tmp/ctm<k>XXXXXX unless --raw. The process numbers are fixed
(--pid, default 100; the children count up from it), so a raw trace is
repeatable too.

Usage:
    cctrace.py [--root DIR] [--cwd DIR] [--pid N] [--run] [--raw]
               [--] CC-ARG...
    cctrace.py --make-root DIR
    cctrace.py goldens [--keep DIR] [--show N]

"goldens" checks the emulated cc, running the emulated original passes,
against the real-hardware goldens it was used to make: "cc -S x.c" for
every tests/mutos_cc source with a .s golden (the corpus was compiled
that way on the hardware, from inside each directory - with c1's
messages, and exit status 1, where c1_errors.txt or a round log, the
hardware's own transcript of those runs, has them; 0 and no message
everywhere else),
"cc -P x.c" for every .i golden ("/lib/cpp -P" made those, and cc -P
runs cpp with -P), and "cc -O -S x.c" for the nine 11_kernel sources,
whose optimized output is tests/mutos_as/kernel_opt/ - c2 in the chain.

Requires the Python module "unicorn" (pip install unicorn).
"""
import base64
import io
import os
import re
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
ENOEXEC = 8

# The originals' MUTOS paths (the ones cc has compiled in) and their
# names in mutos_bin/
ORIGINALS = (('lib/cpp', 'cpp'), ('lib/c0', 'c0'), ('lib/c1', 'c1'),
             ('lib/c2', 'c2'), ('bin/as', 'as'), ('bin/ld', 'ld'),
             ('bin/cc', 'cc'))
CRT0 = os.path.join(REPO, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt')
LIBC = os.path.join(REPO, 'tests', 'mutos1700_libc', 'libc.a.base64.txt')
# libc.a's __.SYMDEF time, 1989-01-31 20:41:17 UTC: ld uses the table
# only if the archive is not newer than it
LIBC_SYMDEF_TIME = 602282477

TEMP_NAME = re.compile(r'(/tmp/ctm\d)[0-9a-z]{6}')


def oracle_root(path):
    """Fill directory path with a MUTOS root for the originals."""
    for sub in ('lib', 'bin', 'usr/lib', 'usr/include', 'tmp'):
        os.makedirs(os.path.join(path, sub), exist_ok=True)
    for name, src in (('lib/crt0.o', CRT0), ('lib/libc.a', LIBC)):
        with open(src, 'rb') as f:
            data = base64.b64decode(f.read())
        with open(os.path.join(path, name), 'wb') as f:
            f.write(data)
    t = LIBC_SYMDEF_TIME
    os.utime(os.path.join(path, 'lib', 'libc.a'), (t, t))
    for name, tool in ORIGINALS:
        dst = os.path.join(path, name)
        with open(dst, 'wb') as f:
            f.write(mutosemu.load_image(tool))
        os.chmod(dst, 0o755)
    return path


class _Tap:
    """A stdout or stderr that logs each write as an event."""

    def __init__(self, events, fd):
        self.events, self.fd = events, fd

    def write(self, b):
        self.events.append((self.fd, bytes(b)))
        return len(b)

    def flush(self):
        pass

    def getvalue(self):
        return b''.join(d for fd, d in self.events if fd == self.fd)


def _run_exec(events, run):
    """An on_exec for mutosemu.Emu: log the exece, then - with run - run
    the program if it is a MUTOS a.out, else answer as default_on_exec."""
    def on_exec(emu, path, argv, envp, pid):
        events.append(('exec', path, list(argv)))
        host = emu._path(path)
        if not os.path.isfile(host):
            return None
        if not run:
            return 0
        with open(host, 'rb') as f:
            image = f.read()
        if len(image) < 16 or \
                struct.unpack_from('<H', image)[0] not in (0o407, 0o411):
            return -ENOEXEC
        child = mutosemu.Emu(image, argv, cwd=emu.cwd, root=emu.root,
                             fds=emu.fds, pid=pid, on_exec=on_exec,
                             umask=emu.umask, envp=envp)
        st = child.run()
        if child.killed:
            return child.killed
        return (st & 0xFF) << 8
    return on_exec


class Trace:
    """What cc did: events (('exec', path, argv), (1, bytes), (2,
    bytes)) in order, and its exit status."""

    def __init__(self, events, status):
        self.events, self.status = events, status

    @property
    def execs(self):
        return [(e[1], e[2]) for e in self.events if e[0] == 'exec']

    def output(self, fd):
        return b''.join(e[1] for e in self.events if e[0] == fd)

    def text(self, raw=False):
        lines = []
        pending = {1: b'', 2: b''}

        def flush(fd):
            # one "fd| " line per line written; a last line without its
            # newline (the other stream or a pass came between) as it is
            if pending[fd]:
                text = pending[fd].decode('latin1')
                parts = text.split('\n')
                if text.endswith('\n'):
                    parts.pop()
                for ln in parts:
                    lines.append('%d| %s' % (fd, ln))
                pending[fd] = b''

        for e in self.events:
            if e[0] == 'exec':
                flush(1)
                flush(2)
                lines.append('exec %s %s' % (e[1], ' '.join(e[2])))
            else:
                other = 2 if e[0] == 1 else 1
                flush(other)
                pending[e[0]] += e[1]
                if pending[e[0]].endswith(b'\n'):
                    flush(e[0])
        flush(1)
        flush(2)
        lines.append('exit %d' % self.status)
        out = '\n'.join(lines) + '\n'
        return out if raw else TEMP_NAME.sub(r'\1XXXXXX', out)


def run_cc(args, root=None, cwd='.', pid=100, run=False, stdin=b''):
    """Run the original cc with args under the emulator: a Trace. With
    no root, a fresh oracle root is made for the run and removed."""
    tmp = None
    if root is None:
        tmp = tempfile.mkdtemp(prefix='ccroot')
        root = oracle_root(tmp)
    try:
        events = []
        emu = mutosemu.Emu(mutosemu.load_image('cc'), ['cc'] + list(args),
                           cwd=cwd, root=root, stdin=io.BytesIO(stdin),
                           stdout=_Tap(events, 1), stderr=_Tap(events, 2),
                           pid=pid, on_exec=_run_exec(events, run))
        status = emu.run()
        return Trace(events, status)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


# -- goldens -------------------------------------------------------------
CC_DIR = os.path.join(REPO, 'tests', 'mutos_cc')
KERNEL_OPT = os.path.join(REPO, 'tests', 'mutos_as', 'kernel_opt')


def _sources():
    """(directory, base name) of every tests/mutos_cc source."""
    out = []
    for d in sorted(os.listdir(CC_DIR)):
        full = os.path.join(CC_DIR, d)
        if not os.path.isdir(full) or d == 'fuzz':
            continue
        for f in sorted(os.listdir(full)):
            if f.endswith('.c'):
                out.append((full, f[:-2]))
    return out


def goldens(argv):
    import check_real   # c1_errors.txt and the round logs' messages
    expected = check_real.expected_messages()
    keep = None
    show = 3
    while argv:
        if argv[0] == '--keep':
            keep = argv[1]
            argv = argv[2:]
        elif argv[0] == '--show':
            show = int(argv[1])
            argv = argv[2:]
        else:
            sys.exit('cctrace.py goldens: unknown argument %s' % argv[0])
    rootdir = tempfile.mkdtemp(prefix='ccroot')
    root = oracle_root(rootdir)
    work = keep or tempfile.mkdtemp(prefix='ccgold')
    os.makedirs(work, exist_ok=True)
    counts = {}
    bad = []

    def check(kind, label, ok, why=''):
        counts.setdefault(kind, [0, 0])[0 if ok else 1] += 1
        if not ok:
            bad.append('%s %s: %s' % (kind, label, why))

    try:
        for d, base in _sources():
            label = '%s/%s' % (os.path.basename(d), base)
            # a scratch copy of the directory's sources and headers
            cwd = os.path.join(work, os.path.basename(d))
            if not os.path.isdir(cwd):
                os.makedirs(cwd)
                for f in os.listdir(d):
                    if f.endswith(('.c', '.h')):
                        shutil.copy(os.path.join(d, f), cwd)
            gold_s = os.path.join(d, base + '.s.golden')
            if os.path.exists(gold_s):
                t = run_cc(['-S', base + '.c'], root=root, cwd=cwd, run=True)
                with open(gold_s, 'rb') as f:
                    want = f.read()
                try:
                    with open(os.path.join(cwd, base + '.s'), 'rb') as f:
                        got = f.read()
                except OSError:
                    got = None
                msgs = [ln for ln in (t.output(1) + t.output(2))
                        .decode('latin1').split('\n') if ln]
                want_msgs = expected.get(label, ([], ''))[0]
                why = ''
                if got != want:
                    why = 'no .s' if got is None else '.s differs'
                elif msgs != want_msgs:
                    why = 'messages %r, not %r' % (msgs[:2], want_msgs[:2])
                elif t.status != (1 if want_msgs else 0):
                    why = 'exit status %d' % t.status
                check('cc -S', label, not why, why)
                if why and show:
                    show -= 1
                    sys.stdout.write(t.text())
            gold_i = os.path.join(d, base + '.i.golden')
            if os.path.exists(gold_i):
                t = run_cc(['-P', base + '.c'], root=root, cwd=cwd, run=True)
                with open(gold_i, 'rb') as f:
                    want = f.read()
                try:
                    with open(os.path.join(cwd, base + '.i'), 'rb') as f:
                        got = f.read()
                except OSError:
                    got = None
                why = ''
                if got != want:
                    why = 'no .i' if got is None else '.i differs'
                elif t.status != 0:
                    why = 'exit status %d' % t.status
                elif len(t.execs) != 1:
                    why = '%d passes, not cpp alone' % len(t.execs)
                check('cc -P', label, not why, why)
            if os.path.basename(d) == '11_kernel':
                opt = os.path.join(KERNEL_OPT, base.split('_', 1)[1] + '.s')
                odir = os.path.join(work, 'kernel_O')
                if not os.path.isdir(odir):
                    shutil.copytree(cwd, odir)
                t = run_cc(['-O', '-S', base + '.c'], root=root, cwd=odir,
                           run=True)
                with open(opt, 'rb') as f:
                    want = f.read()
                try:
                    with open(os.path.join(odir, base + '.s'), 'rb') as f:
                        got = f.read()
                except OSError:
                    got = None
                why = ''
                if got != want:
                    why = 'no .s' if got is None else '.s differs from ' + opt
                elif t.status != 0:
                    why = 'exit status %d' % t.status
                elif [p for p, a in t.execs][-1:] != ['/lib/c2']:
                    why = 'c2 not the last pass'
                check('cc -O -S', label, not why, why)
    finally:
        shutil.rmtree(rootdir, ignore_errors=True)
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
    for kind in ('cc -S', 'cc -P', 'cc -O -S'):
        ok, fail = counts.get(kind, (0, 0))
        print('the original cc %-9s %d/%d as the goldens' % (kind + ':', ok,
                                                             ok + fail))
    for b in bad:
        print('  ' + b)
    return 1 if bad else 0


def main(argv):
    if argv and argv[0] == 'goldens':
        return goldens(argv[1:])
    root = None
    cwd = '.'
    pid = 100
    run = False
    raw = False
    while argv and argv[0].startswith('--'):
        opt = argv.pop(0)
        if opt == '--':
            break
        if opt == '--root':
            root = argv.pop(0)
        elif opt == '--make-root':
            oracle_root(argv.pop(0))
            return 0
        elif opt == '--cwd':
            cwd = argv.pop(0)
        elif opt == '--pid':
            pid = int(argv.pop(0))
        elif opt == '--run':
            run = True
        elif opt == '--raw':
            raw = True
        else:
            sys.exit('cctrace.py: unknown option %s' % opt)
    t = run_cc(argv, root=root, cwd=cwd, pid=pid, run=run)
    sys.stdout.write(t.text(raw))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
