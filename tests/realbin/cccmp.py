#!/usr/bin/env python3
"""cccmp.py - mutos_cc, the port of the real cc, against the original.

The original MUTOS 1700 cc (mutos/mutos_bin/cc.golden.base64.txt) runs under
mutosemu.py, which fakes fork/exece/wait and records every exece with its
path and arguments (cctrace.py); mutos_cc (src/mutos_cc/ccport/) runs on
the host and forks real processes. To see what the port runs, each one
gets a MUTOS root of its own with ccstub.c compiled and copied to every
pass's place (lib/cpp, lib/c0, lib/c1, lib/c2, bin/as, bin/ld): the stub
logs its path and arguments and marks the place in cc's stdout and
stderr. Both get the same command line in copies of the same directory,
and then must agree on:

  - the passes run, in order: their MUTOS paths (the port's root and the
    current directory taken off the host paths its stub was run from)
    and their arguments (the port passes /tmp/ctm... and /lib/crt0.o as
    host paths under its root - taken off again);
  - what cc and its passes wrote to stdout and to stderr before the
    first pass, between two passes, after the last;
  - the exit status;
  - the files left in the directory and in the root's tmp/ (names,
    bytes, modes).

cc's temporary names, /tmp/ctm<k> and six characters made of the process
number (100 for the original under the emulator, the host's for the
port), count as one name: /tmp/ctm<k>XXXXXX. A pass whose file does not
exist is not run - the original's trace lists the attempt, the port
cannot; both print "Can't find" for it.

The passes, three ways:
  - traced (cases, files): no pass runs. A pass "exits 0", or as a case
    says: an exit status or a signal (the original's on_exec answers it,
    the port's stub ends so);
  - run (run, files --run): the emulated original passes run in both -
    for the port the stub hands each to this script (--pass), which runs
    the original under the emulator, the MUTOS paths given back; so a
    difference is the driver's;
  - the ported passes (goldens, programs): the port runs mutos_cpp,
    mutos_c0, mutos_c1, mutos_as, mutos_ld - the whole cross toolchain -
    mutos_c2 (since 2026-10-25; until then the original c2 under the
    emulator).

A refusal of the port (exit status 2 and a last line on stderr ending in
"refused") counts apart; what it wrote before must be what the original
wrote up to there.

Usage:
    cccmp.py cases [-j N] [--port PATH] [--show N] [--keep DIR]
        a matrix of command lines, traced: every option alone and in
        combinations, -o in its forms, sources, objects, libraries,
        several files, the limits (DIUC options, files, ptemp[]), the
        overruns (-K, -B, a long name), passes failing and killed
    cccmp.py run [-j N] [--port PATH] [--show N] [--keep DIR] [--all]
        the construct corpus's programs, every pass run (the emulated
        originals in both), with a cycle of option lines (-c, -S, -O,
        -o, a.out, -P, -E, two sources); --all: every line for every
        program
    cccmp.py goldens [-j N] [--port PATH]
        the port with the ported passes against the real-hardware goldens:
        "cc -S x.c" (the .s, c1's messages, the exit status), "cc -P x.c"
        (the .i), "cc -O -S x.c" for the nine 11_kernel sources (the
        kernel_opt sources; c2 the port, mutos_c2)
    cccmp.py programs [-j N] [--port PATH] [--show N]
        the construct corpus's programs compiled and linked by the port
        with the ported passes and by the original chain: the a.out and
        the messages the same
    cccmp.py files [--run] [ARG...]
        one command line, in a copy of the current directory
    cccmp.py --pass SELF ARG...     (the stub's runner; internal)

As a module: compare(args, srcdir, ...) -> (kind, detail), kind one of
'same', 'refused', 'orig-fails' (the original does not finish, the port
refused), 'DIFFERENT', 'BAD-REFUSAL'.

Requires the Python module "unicorn" (pip install unicorn) and a C
compiler ($CC, default cc) for ccstub.c.
"""
import base64
import concurrent.futures
import io
import os
import re
import resource
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_cc')
STUB_SRC = os.path.join(HERE, 'ccstub.c')
CRT0 = os.path.join(ROOT, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt')
LIBC = os.path.join(ROOT, 'tests', 'mutos1700_libc', 'libc.a.base64.txt')
LIBC_SYMDEF_TIME = 602282477      # 1989-01-31 20:41:17 UTC
CC_DIR = os.path.join(ROOT, 'tests', 'mutos_cc')
KERNEL_OPT = os.path.join(ROOT, 'tests', 'mutos_as', 'kernel_opt')

# the passes: MUTOS path, original (mutos/mutos_bin/), port (under src/)
PASSES = (('lib/cpp', 'cpp', 'mutos_cpp/mutos_cpp'),
          ('lib/c0', 'c0', 'mutos_cc/mutos_c0'),
          ('lib/c1', 'c1', 'mutos_cc/mutos_c1'),
          ('lib/c2', 'c2', 'mutos_cc/mutos_c2'),
          ('bin/as', 'as', 'mutos_as/mutos_as'),
          ('bin/ld', 'ld', 'mutos_ld/mutos_ld'))


def orig_of(path):
    """The original (mutos/mutos_bin/) a pass path stands for, by the end of its
    name: lib/c0, lib/c0f, usr/src/cmd/c/oc0 are c0."""
    name = os.path.basename(path)
    for suffix, tool in (('cpp', 'cpp'), ('c0', 'c0'), ('c0f', 'c0'),
                         ('c1', 'c1'), ('c1f', 'c1'), ('c2', 'c2'),
                         ('asf', 'as'), ('as', 'as'), ('ld', 'ld')):
        if name.endswith(suffix):
            return tool
    return None


# the ported tools, by the original they stand for
PORT_OF = {orig: os.path.join(ROOT, 'src', port)
           for _, orig, port in PASSES if port}
MARKER = b'\x01CCSTUB\x01\n'
TEMP = re.compile(rb'ctm(\d)[0-9a-z]{6}')
TEMP_S = re.compile(r'ctm(\d)[0-9a-z]{6}')
CC_MAX_INSNS = 50_000_000         # cc itself (and its faked children)
PASS_MAX_INSNS = 2_000_000_000
OUT_LIMIT = 1 << 20
PORT_CPU = 20                     # seconds, traced (no pass runs)
PORT_CPU_RUN = 900                # per process, passes run (an emulated c2
                                  # on 09_amx takes over a minute)


def canon(b):
    return TEMP.sub(rb'ctm\1XXXXXX', b)


# ---- the stub, the roots ------------------------------------------------------

_stub = None


def build_stub(where):
    """Compile ccstub.c into directory where; its path."""
    global _stub
    out = os.path.join(where, 'ccstub')
    cc = os.environ.get('CC', 'cc')
    subprocess.run([cc, '-std=c11', '-O', '-o', out, STUB_SRC], check=True)
    _stub = out
    return out


def _base_root(path):
    for sub in ('lib', 'bin', 'usr/lib', 'usr/include', 'tmp'):
        os.makedirs(os.path.join(path, sub), exist_ok=True)
    for name, src in (('lib/crt0.o', CRT0), ('lib/libc.a', LIBC)):
        with open(os.path.join(path, name), 'wb') as f:
            f.write(base64.b64decode(open(src, 'rb').read()))
    t = LIBC_SYMDEF_TIME
    os.utime(os.path.join(path, 'lib', 'libc.a'), (t, t))
    return path


def oracle_root(path, extra=()):
    """The original's root: its passes from mutos/mutos_bin/ at their paths (and
    at extra paths, by their names: usr/src/cmd/c/c0 is c0)."""
    _base_root(path)
    for mpath in [p for p, _, _ in PASSES] + list(extra):
        tool = orig_of(mpath) or 'cc'
        dst = os.path.join(path, mpath)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as f:
            f.write(mutosemu.load_image(tool))
        os.chmod(dst, 0o755)
    return path


def port_root(path, stub, extra=()):
    """The port's root: the stub copied to every pass's place (and to
    extra paths)."""
    _base_root(path)
    for mpath, _, port in PASSES:
        shutil.copy(stub, os.path.join(path, mpath))
    for mpath in extra:
        dst = os.path.join(path, mpath)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(stub, dst)
    return path


def snapshot(d):
    """{name: (bytes, mode)} of a directory's files, temporary names made
    one name; subdirectories by name."""
    out = {}
    if not os.path.isdir(d):
        return out
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        key = TEMP_S.sub(r'ctm\1XXXXXX', name)
        st = os.lstat(p)
        if stat.S_ISDIR(st.st_mode):
            out[key + '/'] = (b'', st.st_mode & 0o777)
        else:
            with open(p, 'rb') as f:
                out[key] = (canon(f.read()), st.st_mode & 0o777)
    return out


# ---- the original -------------------------------------------------------------

class _Limit(Exception):
    pass


class _Tap:
    """stdout or stderr of the emulated cc (and its passes): events."""

    def __init__(self, events, fd):
        self.events, self.fd, self.n = events, fd, 0

    def write(self, b):
        self.events.append((self.fd, bytes(b)))
        self.n += len(b)
        if self.n > OUT_LIMIT:
            raise _Limit()
        return len(b)

    def flush(self):
        pass


def _after_o(argv):
    for i in range(1, len(argv) - 1):
        if argv[i] == '-o':
            return argv[i + 1]
    return None


def outputs(emu, argv):
    """A traced pass that "exits 0" leaves the files the real one writes,
    each holding its argv[0] and a linefeed - as ccstub.c's outputs()
    does for the port: cpp its second argument (not "-"), c0 its second
    and third, c1 its third, c2 its second, as and ld the argument after
    "-o" (ld without one: a.out)."""
    n = argv[0] if argv else ''
    names = []
    if n == 'cpp':
        if len(argv) > 2 and argv[2] != '-':
            names = [argv[2]]
    elif n in ('c0', 'c0f'):
        if len(argv) > 3:
            names = [argv[2], argv[3]]
    elif n in ('c1', 'c1f'):
        if len(argv) > 3:
            names = [argv[3]]
    elif n == 'c2':
        if len(argv) > 2:
            names = [argv[2]]
    elif n in ('as', 'asf'):
        names = [_after_o(argv)]
    elif n == 'ld':
        o = _after_o(argv)
        names = [o if o is not None else 'a.out']
    for name in names:
        if not name:
            continue
        try:
            # byte for byte, as the port's stub gets the name (latin1)
            with open(emu._path(name).encode('latin1'), 'wb') as f:
                f.write(n.encode('latin1') + b'\n')
        except OSError:
            pass


def run_orig(args, cwd, root, run=False, status=None):
    """The original cc: (exit status or None, segments, execs, why).
    status: {MUTOS path: wait status word} for passes that end so."""
    events = []
    status = status or {}

    def on_exec(emu, path, argv, envp, pid):
        host = emu._path(path)
        exists = os.path.isfile(host)
        events.append(('exec', path, list(argv), exists))
        if not exists:
            return None
        if path in status and status[path] != 0:
            return status[path]
        if not run:
            outputs(emu, argv)
            return 0
        with open(host, 'rb') as f:
            image = f.read()
        if len(image) < 16 or \
                struct.unpack_from('<H', image)[0] not in (0o407, 0o411):
            return -8                                  # ENOEXEC
        child = mutosemu.Emu(image, argv, cwd=emu.cwd, root=emu.root,
                             fds=emu.fds, pid=pid, on_exec=on_exec,
                             umask=emu.umask, envp=envp)
        st = child.run(PASS_MAX_INSNS)
        if child.killed:
            return child.killed
        return (st & 0xFF) << 8

    emu = mutosemu.Emu(mutosemu.load_image('cc'), ['cc'] + list(args),
                       cwd=cwd, root=root, stdin=io.BytesIO(b''),
                       stdout=_Tap(events, 1), stderr=_Tap(events, 2),
                       pid=100, on_exec=on_exec)
    why = ''
    st = None
    try:
        st = emu.run(CC_MAX_INSNS)
    except _Limit:
        why = 'output past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
    segs = [[b'', b'']]
    execs = []
    for e in events:
        if e[0] == 'exec':
            if e[3]:
                execs.append((e[1], [TEMP_S.sub(r'ctm\1XXXXXX', a)
                                     for a in e[2]]))
                segs.append([b'', b''])
        else:
            segs[-1][e[0] - 1] += e[1]
    segs = [[canon(o), canon(r)] for o, r in segs]
    return st, segs, execs, why


# ---- the port -------------------------------------------------------------------

def _limits(cpu=PORT_CPU):
    def set_limits():
        resource.setrlimit(resource.RLIMIT_CPU, (cpu, cpu))
        resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))
    return set_limits


def _unhex(f):
    return '' if f == '-' else bytes.fromhex(f).decode('latin1')


def run_port(args, cwd, root, port=PORT, status=None, run=None):
    """The port: (exit status, segments, execs, raw stderr). root must be
    a real path (the stub sees resolved paths); status as for run_orig;
    run: None - no pass runs; 'orig' - every pass runs as the emulated
    original; 'ported' - as the ported tool (every pass has one since
    the port of c2)."""
    status = status or {}
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    log = os.path.join(os.path.dirname(root), 'stub.log')
    env['CCSTUB_LOG'] = log
    env['CCSTUB_STATUS'] = ';'.join(
        '%s=%d' % (os.path.join(root, p.lstrip('/')) if p.startswith('/')
                   else os.path.join(cwd, p), w)
        for p, w in status.items())
    if run:
        env['CCSTUB_RUN'] = _runner(os.path.dirname(root))
        env['CCRUN'] = run
    else:
        env.pop('CCSTUB_RUN', None)
    if os.path.exists(log):
        os.unlink(log)
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        # the arguments byte for byte, as the emulator gives them (latin1)
        r = subprocess.run([os.fsencode(port)]
                           + [a.encode('latin1') for a in args], cwd=cwd,
                           stdout=fo, stderr=fe, env=env,
                           preexec_fn=_limits(PORT_CPU_RUN if run
                                              else PORT_CPU))
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read()[:OUT_LIMIT + 1], fe.read()[:OUT_LIMIT + 1]
    execs = []
    if os.path.exists(log):
        with open(log) as f:
            for line in f:
                fields = [_unhex(x) for x in line.split()]
                path, argv = fields[0], fields[1:]
                if path.startswith(root + '/'):
                    path = '/' + path[len(root) + 1:]
                elif path.startswith(cwd + '/'):
                    path = path[len(cwd) + 1:]
                argv = [('/' + a[len(root) + 1:]) if a.startswith(root + '/')
                        else a for a in argv]
                execs.append((path, [TEMP_S.sub(r'ctm\1XXXXXX', a)
                                     for a in argv]))
    outs, errs = out.split(MARKER), err.split(MARKER)
    if len(outs) != len(execs) + 1 or len(errs) != len(execs) + 1:
        # a pass's output held the marker, or a stub did not write it
        outs = (outs + [b''] * len(execs))[:len(execs) + 1]
        errs = (errs + [b''] * len(execs))[:len(execs) + 1]
    root_b = root.encode() + b'/'
    segs = [[canon(o.replace(root_b, b'/')), canon(e.replace(root_b, b'/'))]
            for o, e in zip(outs, errs)]
    st = r.returncode
    if st < 0:
        st = 128 - st
    return st, segs, execs, err


_runners = {}


def _runner(where):
    """A shell script that runs this file's --pass for the stub."""
    p = os.path.join(where, 'ccrunner')
    if not os.path.exists(p):
        with open(p, 'w') as f:
            f.write('#!/bin/sh\nexec "%s" -I "%s" --pass "$@"\n'
                    % (sys.executable, os.path.abspath(__file__)))
        os.chmod(p, 0o755)
    return p


def pass_main(argv):
    """--pass SELF ARG...: run the pass SELF stands for - with CCRUN=ported
    the ported tool, if there is one, with the arguments as they are;
    else the original under the emulator, the MUTOS paths of its
    arguments given back."""
    self, args = argv[0], argv[1:]
    root = os.environ['MUTOS_ROOT']
    tool = orig_of(self)
    if tool is None:
        sys.stderr.write('cccmp --pass: no original for %s\n' % self)
        return 125
    if os.environ.get('CCRUN') == 'ported' and tool in PORT_OF:
        os.execv(PORT_OF[tool], args)
    margs = [('/' + a[len(root) + 1:]) if a.startswith(root + '/') else a
             for a in args]
    sys.stdout.flush()
    emu = mutosemu.Emu(mutosemu.load_image(tool), margs, cwd=os.getcwd(),
                       root=root, stdin=sys.stdin.buffer,
                       stdout=sys.stdout.buffer, stderr=sys.stderr.buffer,
                       pid=(os.getpid() & 0x7fff) or 1)
    try:
        st = emu.run(PASS_MAX_INSNS)
    except RuntimeError as e:
        sys.stdout.flush()
        sys.stderr.write('cccmp --pass: %s\n' % e)
        return 125
    sys.stdout.flush()
    sys.stderr.flush()
    if emu.killed:
        signal.signal(emu.killed, signal.SIG_DFL)
        os.kill(os.getpid(), emu.killed)
    return st


# ---- the comparison ---------------------------------------------------------------

def diff_at(a, b):
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
             min(len(a), len(b)))
    return 'differs at byte %d (%d vs %d bytes)' % (n, len(a), len(b))


def compare(args, srcdir, port=PORT, status=None, run=None, extra=(),
            keep=None):
    """The port against the original: the same arguments in copies of
    srcdir, each with a root of its own. status: {MUTOS path: wait word};
    run: None (traced), 'orig' (the emulated passes in both) or 'ported'
    (the port with the ported passes, the original chain); extra: more
    pass paths in both roots (usr/src/cmd/c/c0, lib/c0f, ...)."""
    with tempfile.TemporaryDirectory(prefix='cccmp') as tmp:
        tmp = os.path.realpath(tmp)
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        shutil.copytree(srcdir, do, symlinks=True)
        shutil.copytree(srcdir, dp, symlinks=True)
        ro = oracle_root(os.path.join(tmp, 'oroot'), extra)
        rp = port_root(os.path.join(tmp, 'proot'), _stub, extra)
        ost, osegs, oexecs, why = run_orig(args, do, ro, run=bool(run),
                                           status=status)
        pst, psegs, pexecs, perr = run_port(args, dp, rp, port, status=status,
                                            run=run)
        kind, detail = _judge(ost, osegs, oexecs, why, pst, psegs, pexecs,
                              perr, (do, ro), (dp, rp))
        if keep and kind in ('DIFFERENT', 'BAD-REFUSAL'):
            os.makedirs(keep, exist_ok=True)
            k = tempfile.mkdtemp(dir=keep, prefix='case')
            for n in ('orig', 'port', 'oroot', 'proot'):
                shutil.copytree(os.path.join(tmp, n), os.path.join(k, n),
                                symlinks=True)
            with open(os.path.join(k, 'args'), 'w') as f:
                f.write('%r\nstatus %r run %r\n%s: %s\n'
                        % (args, status, run, kind, detail))
        return kind, detail


def _flat(segs):
    return b''.join(o for o, _ in segs), b''.join(e for _, e in segs)


def _judge(ost, osegs, oexecs, why, pst, psegs, pexecs, perr, od, pd):
    lines = perr.decode('latin1').rstrip('\n').split('\n')
    refused = (pst == 2 and lines[-1].startswith('cc: ')
               and lines[-1].endswith('refused'))
    if refused:
        pout, perr2 = _flat(psegs)
        oout, oerr = _flat(osegs)
        perr2 = perr2[:perr2.rfind(b'cc: ')] if b'cc: ' in perr2 else perr2
        ok = (oout.startswith(pout) and oerr.startswith(perr2)
              and oexecs[:len(pexecs)] == pexecs)
        if ost is None:
            return ('orig-fails' if ok else 'BAD-REFUSAL'), \
                'the original does not finish (%s); the port: %s' % (
                    why, lines[-1])
        return ('refused' if ok else 'BAD-REFUSAL'), lines[-1]
    if ost is None:
        return 'DIFFERENT', 'the original does not finish (%s); the port ' \
            'exit %s' % (why, pst)
    parts = []
    if pexecs != oexecs:
        for i in range(max(len(pexecs), len(oexecs))):
            p = pexecs[i] if i < len(pexecs) else None
            o = oexecs[i] if i < len(oexecs) else None
            if p != o:
                parts.append('pass %d: %r, original %r' % (i + 1, p, o))
                break
    elif psegs != osegs:
        for i, (p, o) in enumerate(zip(psegs, osegs)):
            for k, name in ((0, 'stdout'), (1, 'stderr')):
                if p[k] != o[k]:
                    parts.append('%s after pass %d: %r, original %r'
                                 % (name, i, p[k][:200], o[k][:200]))
    if pst != ost:
        parts.append('exit %s, original %d' % (pst, ost))
    for (a, b, where) in ((od[0], pd[0], ''), (os.path.join(od[1], 'tmp'),
                                                os.path.join(pd[1], 'tmp'),
                                                'tmp/')):
        so, sp = snapshot(a), snapshot(b)
        if so != sp:
            for name in sorted(set(so) | set(sp)):
                if name not in so:
                    parts.append('%s%s: only the port\'s' % (where, name))
                elif name not in sp:
                    parts.append('%s%s: only the original\'s' % (where, name))
                elif so[name] != sp[name]:
                    (oc, om), (pc, pm) = so[name], sp[name]
                    if oc != pc:
                        parts.append('%s%s: %s' % (where, name, diff_at(pc, oc)))
                    if om != pm:
                        parts.append('%s%s: mode %o, original %o'
                                     % (where, name, pm, om))
    if parts:
        return 'DIFFERENT', '; '.join(parts)
    return 'same', ''


# ---- the cases ----------------------------------------------------------------------

PROG = b'main()\n{\n\treturn 0;\n}\n'


def _obj():
    """A small real object (an a.out with no undefined symbol)."""
    return bytes([0o407 & 0xff, 0o407 >> 8, 2, 0] + [0] * 12) + b'\xc3\x90'


def case_files():
    return {'a.c': PROG, 'b.c': PROG, 'e.c': b'main() { x = ; }\n',
            'x.s': b'.text\n', 'y.o': _obj(), 'z.o': _obj(),
            'lib.a': b'\x65\xff', 'sub/d.c': PROG, 'noext': b'',
            'h.h': b'#define X 1\n'}


def cases():
    """(name, args, status, extra pass paths)."""
    c = []

    def case(name, args, status=None, extra=()):
        c.append((name, list(args), status or {}, tuple(extra)))

    case('no arguments', [])
    for opts in ([], ['-c'], ['-S'], ['-P'], ['-E'], ['-O'], ['-O', '-S'],
                 ['-O', '-c'], ['-p'], ['-f'], ['-2'], ['-20'], ['-2x'],
                 ['-a'], ['-a', '-O'], ['-a', '-c'], ['-a', '-S'],
                 ['-K100'], ['-K1', '-K2'], ['-K'], ['-p', '-O'],
                 ['-f', '-c'], ['-f', '-O'], ['-P', '-S'], ['-S', '-P'],
                 ['-E', '-S'], ['-P', '-O'], ['-c', '-S'], ['-a', '-f'],
                 ['-DX', '-DY=1', '-I.', '-UZ', '-C'], ['-DX', '-P'],
                 ['-E', '-DX'], ['-i', '-s', '-x'], ['-lm'], ['-'],
                 ['-X'], ['-v'], ['-d'], ['-n'], ['-u'], ['-e'],
                 ['-p', '-c'], ['-p', '-S']):
        case('%s a.c' % ' '.join(opts), opts + ['a.c'])
    for args in (['-o', 'prog', 'a.c'], ['-o', 'a.c'], ['-o', 'a.c', 'y.o'],
                 ['-o', 'y.o'], ['-o', 'y.o', 'a.c'], ['-o', 'sub/p.o', 'y.o'],
                 ['-o', 'sub/d.c'], ['-o', '-c', 'a.c'], ['-o'],
                 ['-o', '-lm'], ['-o', 'x.s'], ['-o', 'p', '-o', 'q', 'a.c'],
                 ['a.c', '-o', 'prog'], ['-o', 'a.c', 'b.c'],
                 ['-o', 'a.c', '-o', 'b.c'], ['-o', 'noext', 'a.c'],
                 ['-o', '-S', 'a.c'], ['-o', '-P', 'a.c'], ['-o', 'p.c'],
                 ['-o', 'p.o', 'z.o', 'y.o'], ['-o', '-lm', 'y.o'],
                 ['-c', '-o', 'a.c'], ['-o', 'a.c', '-c']):
        case(' '.join(args), args)
    for args in (['a.c', 'b.c'], ['a.c', 'b.c', 'y.o', '-lm'], ['a.c', 'a.c'],
                 ['y.o', 'y.o'], ['y.o', 'z.o'], ['x.s'], ['-S', 'x.s'],
                 ['-P', 'x.s'], ['-c', 'x.s'], ['a.c', 'x.s'], ['y.o'],
                 ['lib.a'], ['-lm'], ['y.o', '-lc'], ['sub/d.c'],
                 ['-c', 'sub/d.c'], ['-E', 'a.c', 'b.c', 'y.o'],
                 ['-E', 'y.o', '-lm', 'noext'], ['-P', 'a.c', 'b.c'],
                 ['-S', 'a.c', 'b.c'], ['nonexist.c'], ['nonexist.o'],
                 ['noext'], ['a.c', 'y.o', 'a.o'], ['-c', 'a.c', 'b.c', 'x.s'],
                 ['.c'], ['a.'], ['ab'], ['x.cc'], ['abcdefghijklmn.c'],
                 ['abcdefghijklmno.c'], ['sub/abcdefghijkl.c'], ['a.c', 'h.h']):
        case(' '.join(args), args)
    for args in (['-t', 'a.c'], ['-t0', 'a.c'], ['-tp1', 'a.c'],
                 ['-t2', '-O', 'a.c'], ['-t', '-O', 'a.c'], ['-B', 'a.c'],
                 ['-Bnew/', 'a.c'], ['-t', '-t', 'a.c'], ['-B', '-B', 'a.c'],
                 ['-B', '-t1', 'a.c'], ['-t1', '-Bx', 'a.c'], ['-txyz', 'a.c'],
                 ['-f', '-t', 'a.c'], ['-t', 'y.o']):
        case(' '.join(args), args)
    case('-t: the passes there', ['-t', '-O', 'a.c'],
         extra=('usr/src/cmd/c/c0', 'usr/src/cmd/c/c1', 'usr/src/cmd/c/c2',
                'usr/src/cmd/c/cpp'))
    case('-B: the passes there', ['-B', 'a.c'],
         extra=('usr/src/cmd/c/oc0', 'usr/src/cmd/c/oc1',
                'usr/src/cmd/c/ocpp'))
    case('-f: the passes there', ['-f', 'a.c'],
         extra=('lib/c0f', 'lib/c1f', 'bin/asf'))
    case('-f -O: the passes there', ['-f', '-O', 'a.c'],
         extra=('lib/c0f', 'lib/c1f', 'bin/asf'))
    # the limits and the overruns
    case('99 DIUC options', ['-DX'] * 99 + ['a.c'])
    case('100 DIUC options', ['-DX'] * 100 + ['a.c'])
    case('101 DIUC options', ['-DX%d' % i for i in range(101)] + ['a.c'])
    case('99 DIUC, then -P', ['-DX'] * 99 + ['-P', 'a.c'])
    case('99 DIUC, then 2 -P', ['-DX'] * 99 + ['-P', '-P', 'a.c'])
    case('100 -P', ['-P'] * 100 + ['a.c'])
    case('101 -P', ['-P'] * 101 + ['a.c'])
    case('102 -P', ['-P'] * 102 + ['a.c'])
    case('99 sources', ['a.c'] * 99)
    case('100 sources', ['a.c'] * 100)
    case('98 objects', ['o%d.o' % i for i in range(98)])
    case('99 objects', ['o%d.o' % i for i in range(99)])
    case('48 objects (av[] full)', ['o%d.o' % i for i in range(45)] + ['-lm'])
    case('60 objects (past av[])', ['o%d.o' % i for i in range(60)])
    case('60 objects and a source', ['a.c'] + ['o%d.o' % i for i in range(60)])
    case('-o, 60 objects', ['-o', 'p'] + ['o%d.o' % i for i in range(60)])
    case('46 DIUC options and a source (cpp\'s av[])',
         ['-DX%d' % i for i in range(46)] + ['a.c'])
    case('60 DIUC options and a source (past av[])',
         ['-DX%d' % i for i in range(60)] + ['a.c'])
    case('60 DIUC options, two sources',
         ['-DX%d' % i for i in range(60)] + ['a.c', 'b.c'])
    case('-K long', ['-K' + '9' * 8, 'a.c'])
    case('-K into pass0', ['-K' + 'x' * 12, 'a.c'])
    case('-K into pass1', ['-K' + 'x' * 30, 'a.c'])
    case('-K over the strings', ['-K' + 'y' * 150, 'a.c'])
    case('-K -K', ['-Kab', '-Kcd', '-Kef', '-K12345', 'a.c'])
    case('-B long', ['-B/very/long/prefix/', 'a.c'])
    case('-B into pass1', ['-B' + 'b' * 30, 'a.c'])
    case('-B long -t1', ['-B' + 'b' * 25, '-t1', 'a.c'])
    case('long name', ['d' * 200 + '.c'])
    case('many long names', ['d%03d' % i + 'e' * 60 + '.c' for i in range(20)])
    case('long names (not copied: no suffix)', ['f%02d' % i + 'g' * 120
                                                 + '.o' for i in range(12)])
    # cc copies a source's whole name (setsuf()) into ts[] and takes a new
    # block from malloc() once tsp has passed tsa + 1000 - checked after
    # the copy, so a name can run past ts[] into av[] (CC-01). The names
    # below fill ts[] to tsa + 1000 exactly (nine of 100 bytes, one of 30,
    # one of 70), so that the original takes no block there, and the last
    # one runs 15 bytes into av[]: its "b.o" there is cut to "" when av[5]
    # is set for as, and ld gets "". The copies main() makes later (the
    # temporary names, one .o name per source) stay inside their blocks.
    fill = ['p' * 95 + '/a.c'] * 9 + ['p' * 25 + '/a.c']
    case('ts[] full at tsa + 1000, a name into av[]',
         fill + ['p' * 65 + '/a.c', 'q' * 60 + '/b.c'])
    case('ts[] at tsa + 999, a name into av[]',
         fill + ['p' * 64 + '/a.c', 'q' * 60 + '/b.c'])
    case('ts[] at tsa + 1001: a block from malloc()',
         fill + ['p' * 66 + '/a.c', 'q' * 60 + '/b.c'])
    case('ts[] full, a name into clist[] (refused)',
         fill + ['p' * 65 + '/a.c', 'q' * 150 + '/b.c'])
    case('malloc() blocks, one overrun (refused)',
         ['-c'] + ['p' * 95 + '/a%d.c' % i for i in range(40)])
    case('malloc() blocks, short names', ['-c'] + ['p' * 30 + '/a%d.c' % i
                                                   for i in range(120)][:99])
    case('-o resolved late', ['-o', 'q.c', '-lm', 'r.o'])
    case('-o never resolved, a library', ['-o', 'q.c', '-lm'])
    # passes that fail
    for p, w, opts in (('/lib/cpp', 1 << 8, []), ('/lib/c0', 1 << 8, []),
                       ('/lib/c1', 1 << 8, []), ('/lib/c2', 1 << 8, ['-O']),
                       ('/bin/as', 1 << 8, []), ('/bin/as', 2 << 8, []),
                       ('/bin/as', 255 << 8, []), ('/bin/ld', 1 << 8, []),
                       ('/bin/ld', 4 << 8, []), ('/lib/c0', 11, []),
                       ('/lib/c1', 2, []), ('/lib/cpp', 15, []),
                       ('/bin/ld', 9, []), ('/lib/c2', 2, ['-O']),
                       ('/lib/c0', 1 << 8, ['-S']), ('/lib/c1', 1 << 8, ['-S']),
                       ('/lib/cpp', 1 << 8, ['-P']), ('/bin/as', 1 << 8, ['-c']),
                       ('/lib/c1', 3 << 8, ['-O', '-S'])):
        case('%s %s, %s a.c b.c' % (p, w, ' '.join(opts)), opts + ['a.c', 'b.c'],
             status={p: w})
    case('ld fails, one source', ['a.c'], status={'/bin/ld': 1 << 8})
    case('as fails, one source', ['-o', 'p', 'a.c'], status={'/bin/as': 2 << 8})
    return c


def _write_files(d, files):
    for fn, data in files.items():
        p = os.path.join(d, fn)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)


def _case_one(job):
    (name, args, status, extra), port, keep, stub = job
    global _stub
    _stub = stub
    with tempfile.TemporaryDirectory(prefix='cccase') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        _write_files(src, case_files())
        return name, [('',) + compare(args, src, port, status=status,
                                      extra=extra, keep=keep)]


# ---- the corpus, run ------------------------------------------------------------------

def corpus():
    """(directory, base name) of the construct corpus's sources."""
    out = []
    for d in sorted(os.listdir(CC_DIR)):
        full = os.path.join(CC_DIR, d)
        if not os.path.isdir(full) or not d[:2].isdigit() or d == '11_kernel':
            continue
        for f in sorted(os.listdir(full)):
            if f.endswith('.c'):
                out.append((full, f[:-2]))
    return out


RUN_LINES = (['-c', '{x}.c'], ['-S', '{x}.c'], ['-O', '-c', '{x}.c'],
             ['-o', '{x}', '{x}.c'], ['{x}.c'], ['-P', '{x}.c'],
             ['-E', '{x}.c'], ['-O', '-S', '{x}.c'], ['-c', '{x}.c', '{y}.c'])


def _run_one(job):
    (d, base, other, lines), port, keep, stub, mode = job
    global _stub
    _stub = stub
    out = []
    with tempfile.TemporaryDirectory(prefix='ccrun') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        for f in os.listdir(d):
            if f.endswith(('.c', '.h')):
                shutil.copy(os.path.join(d, f), src)
        for line in lines:
            args = [a.format(x=base, y=other) for a in line]
            kind, detail = compare(args, src, port, run=mode, keep=keep)
            out.append((' '.join(args), kind, detail))
    return '%s/%s' % (os.path.basename(d), base), out


# ---- the goldens, with the ported passes ------------------------------------------------

def _golden_one(job):
    (d, base, expected, invalid), port, stub = job
    global _stub
    _stub = stub
    results = []
    with tempfile.TemporaryDirectory(prefix='ccgold') as tmp:
        tmp = os.path.realpath(tmp)
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        for f in os.listdir(d):
            if f.endswith(('.c', '.h')):
                shutil.copy(os.path.join(d, f), src)
        root = port_root(os.path.join(tmp, 'root'), stub)
        label = '%s/%s' % (os.path.basename(d), base)
        gold_s = os.path.join(d, base + '.s.golden')
        if os.path.exists(gold_s):
            st, segs, execs, err = run_port(['-S', base + '.c'], src, root,
                                            port, run='ported')
            want = open(gold_s, 'rb').read()
            got = _read(os.path.join(src, base + '.s'))
            msgs = [ln for ln in b''.join(o + e for o, e in segs)
                    .decode('latin1').split('\n') if ln]
            want_msgs = expected[0]
            why = ''
            if invalid is not None:
                # the real compiler's invalid output (invalid_goldens.txt):
                # mutos_c1 refuses it after the golden's first lines
                head = b''.join(want.splitlines(True)[:invalid])
                if not (got == head and st == 1 and msgs
                        and msgs[-1].endswith('refused')):
                    why = 'not refused after %d lines, as invalid_goldens.txt ' \
                        'says' % invalid
                results.append(('cc -S', label, why))
                gold_s = None
            elif got != want:
                why = 'no .s' if got is None else '.s ' + diff_at(got, want)
            elif msgs != want_msgs:
                why = 'messages %r, not %r' % (msgs[:2], want_msgs[:2])
            elif st != (1 if want_msgs else 0):
                why = 'exit status %d' % st
            elif [p for p, _ in execs] != ['/lib/cpp', '/lib/c0', '/lib/c1']:
                why = 'the passes %r' % [p for p, _ in execs]
            if gold_s is not None:
                results.append(('cc -S', label, why))
        gold_i = os.path.join(d, base + '.i.golden')
        if os.path.exists(gold_i):
            st, segs, execs, err = run_port(['-P', base + '.c'], src, root,
                                            port, run='ported')
            want = open(gold_i, 'rb').read()
            got = _read(os.path.join(src, base + '.i'))
            why = ''
            if got != want:
                why = 'no .i' if got is None else '.i ' + diff_at(got, want)
            elif st != 0:
                why = 'exit status %d' % st
            elif [p for p, _ in execs] != ['/lib/cpp']:
                why = 'the passes %r' % [p for p, _ in execs]
            results.append(('cc -P', label, why))
        if os.path.basename(d) == '11_kernel':
            opt = os.path.join(KERNEL_OPT, base.split('_', 1)[1] + '.s')
            os.unlink(os.path.join(src, base + '.s')) if os.path.exists(
                os.path.join(src, base + '.s')) else None
            st, segs, execs, err = run_port(['-O', '-S', base + '.c'], src,
                                            root, port, run='ported')
            want = open(opt, 'rb').read()
            got = _read(os.path.join(src, base + '.s'))
            why = ''
            if got != want:
                why = 'no .s' if got is None else '.s ' + diff_at(got, want)
            elif st != 0:
                why = 'exit status %d' % st
            elif [p for p, _ in execs][-1:] != ['/lib/c2']:
                why = 'c2 not the last pass'
            results.append(('cc -O -S', label, why))
    return results


def _read(p):
    try:
        with open(p, 'rb') as f:
            return f.read()
    except OSError:
        return None


def golden_jobs():
    import check_real
    import c1trees
    expected = check_real.expected_messages()
    invalid = c1trees.invalid_goldens()
    out = []
    for d in sorted(os.listdir(CC_DIR)):
        full = os.path.join(CC_DIR, d)
        if not os.path.isdir(full) or d == 'fuzz':
            continue
        for f in sorted(os.listdir(full)):
            if f.endswith('.c'):
                base = f[:-2]
                label = '%s/%s' % (d, base)
                out.append((full, base, expected.get(label, ([], '')),
                            invalid.get(os.path.join(full, base))))
    return out


# ---- main ---------------------------------------------------------------------------------

def main(argv):
    if argv[:1] == ['--pass']:
        return pass_main(argv[1:])
    jobs, port, show, keep, every, runflag = os.cpu_count() or 1, PORT, 0, \
        None, False, False
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if args[:1] == ['files']:
            # files [--run] [--] ARG...: the rest is cc's command line
            if a == '--run' and len(args) == 1:
                runflag = True
            elif a == '--' and len(args) == 1:
                args.extend(argv[i + 1:])
                break
            else:
                args.extend(argv[i:])
                break
            i += 1
            continue
        if a == '-j':
            jobs = int(argv[i + 1]); i += 2
        elif a == '--port':
            port = os.path.abspath(argv[i + 1]); i += 2
        elif a == '--show':
            show = int(argv[i + 1]); i += 2
        elif a == '--keep':
            keep = os.path.abspath(argv[i + 1]); i += 2
        elif a == '--all':
            every = True; i += 1
        elif a == '--run':
            runflag = True; i += 1
        else:
            args.append(a); i += 1
    if not args or args[0] not in ('cases', 'run', 'goldens', 'programs',
                                   'files'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    tmp = tempfile.mkdtemp(prefix='ccstub')
    try:
        stub = build_stub(tmp)
        if args[0] == 'goldens':
            counts = {}
            bad = []
            with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
                for results in ex.map(_golden_one,
                                      [(g, port, stub) for g in golden_jobs()]):
                    for kind, label, why in results:
                        c = counts.setdefault(kind, [0, 0])
                        c[0 if not why else 1] += 1
                        if why:
                            bad.append('%s %s: %s' % (kind, label, why))
            for kind in ('cc -S', 'cc -P', 'cc -O -S'):
                ok, fail = counts.get(kind, (0, 0))
                print('mutos_cc with the ported passes, %-9s %d/%d as the '
                      'goldens' % (kind + ':', ok, ok + fail))
            for b in bad:
                print('  ' + b)
            return 1 if bad else 0
        if args[0] == 'cases':
            work = [(c, port, keep, stub) for c in cases()]
            fn = _case_one
        elif args[0] in ('run', 'programs'):
            progs = corpus()
            work = []
            mode = 'orig' if args[0] == 'run' else 'ported'
            for k, (d, base) in enumerate(progs):
                same_dir = [b for dd, b in progs if dd == d and b != base]
                other = same_dir[0] if same_dir else base
                if args[0] == 'programs':
                    lines = (['{x}.c'], ['-O', '-o', '{x}', '{x}.c'])
                elif every:
                    lines = RUN_LINES
                else:
                    lines = (RUN_LINES[k % len(RUN_LINES)],
                             RUN_LINES[(k + 4) % len(RUN_LINES)])
                work.append(((d, base, other, lines), port, keep, stub, mode))
            fn = _run_one
        else:
            work = [(('files', args[1:], {}, ()), port, keep, stub)]
            fn = _files_one if runflag else _case_files_cwd
        counts = {}
        bad = []
        n = 0
        with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
            for name, results in ex.map(fn, work):
                for mode, kind, detail in results:
                    n += 1
                    counts[kind] = counts.get(kind, 0) + 1
                    label = name + (' [%s]' % mode if mode else '')
                    if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                        bad.append('%s: %s: %s' % (label, kind, detail))
                    elif (kind != 'same' and show) or args[0] == 'files':
                        print('%s: %s%s' % (label, kind,
                                            ': ' + detail if detail else ''))
        for b in bad[:show or 20]:
            print(b)
        print('cc port against the original (%s): %d runs, %s'
              % (args[0], n, ', '.join('%d %s' % (c, k)
                                       for k, c in sorted(counts.items()))))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _case_files_cwd(job):
    (name, args, status, extra), port, keep, stub = job
    global _stub
    _stub = stub
    return ' '.join(args), [('',) + compare(args, os.getcwd(), port,
                                            keep=keep)]


def _files_one(job):
    (name, args, status, extra), port, keep, stub = job
    global _stub
    _stub = stub
    return ' '.join(args), [('',) + compare(args, os.getcwd(), port,
                                            run='orig', keep=keep)]


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
