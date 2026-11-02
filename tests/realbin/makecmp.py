#!/usr/bin/env python3
"""makecmp.py - mutos_make, the port of the real make, against the original.

The original MUTOS 1700 make (mutos/mutos_bin/make.golden.base64.txt) runs
under mutosemu.py; mutos_make (src/mutos_make/) runs on the host. A case is
a directory of files and a sequence of steps - a make command line, or a
change of a file - run in two copies of the directory. After every step
their standard output, standard error, exit status and both trees - every
file's name, bytes and mode - must be the same.

The commands the makefiles run are the host's, for both: /bin/sh, and the
programs in the PATH, which has a MUTOS root's bin/ first - there are
small shell scripts there in place of cc, as, ld, yacc, lex and f77 (they
say what they were asked, and make the file they were asked for), so that
the built-in rules can run. The emulated original's exece() runs the host's
program (on_exec()), its output going to the same files as make's.

The roots, the PATH and the environment are one for both runs; the path of
the directory a run is in, which differs, counts as one: "<dir>". The
files a step has written carry the time it ran at, which would differ
between the two runs: any file newer than an hour before the run gets a
fixed time after each step (arcmp.settle_times()); and the "TIME(name)=" lines
of -d, which print the clock for a target a step has made, count a time within
an hour of the run as one: "<now>".

A refusal of the port (exit status 125 and a last message ending in
"refused") counts apart: what it wrote to standard output and standard
error before must be lines of what the original wrote, in order, and the
sequence ends there. The classes: same, refused, orig-fails (the original
does not finish - it runs past the instruction budget, or makes the
emulator fail - and the port refused), endless (neither finishes; what
they wrote is the same as far as both got), limits (the output past 1 MB).

Usage:
    makecmp.py cases [-j N] [--port PATH] [--show N] [--keep DIR]
        the hand-written cases
    makecmp.py corpus [-j N] [--port PATH] [--show N] [--keep DIR]
        the Makefile.mutos of tests/mutos_cc, with the fake tools
    makecmp.py run [ARG...]
        one command line, in a copy of the current directory

As a module: Case, compare_seq(), the pieces makefuzz.py uses.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import errno
import io
import os
import re
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402
import arcmp          # noqa: E402
from arcmp import F, place, snapshot, _same_trees, diff_at, settle_times  # noqa: E402
from arcmp import mkar, NOW  # noqa: E402,F401

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_make', 'mutos_make')
MAX_INSNS = 300_000_000
OUT_LIMIT = 1 << 20
PORT_CPU = 20
BASE_PATH = '/usr/bin:/bin'
CHILD_TIMEOUT = 30

# ---- the programs of the root's bin/ -------------------------------------------

FAKE_TOOL = r'''#!/bin/sh
# a fake of cc, as, ld, f77, yacc, lex, cpp and c0: says what it was asked and makes
# the file it was asked for
name=`basename $0`
echo "$name $*"
case $name in
yacc)
	case "$*" in
	*-r*) f=y.tab.r;;
	*-e*) f=y.tab.e;;
	*) f=y.tab.c;;
	esac
	echo yacc > $f
	exit 0;;
lex)
	echo lex > lex.yy.c
	exit 0;;
esac
case $name in
c0)
	shift
	for f in "$@"; do echo c0 made > "$f"; done
	exit 0;;
cpp)
	exit 0;;
esac
out=; c=; s=; prev=; src=
for a in "$@"; do
	if [ "$prev" = -o ]; then out=$a; fi
	case $a in
	-c) c=1;;
	-S) s=1;;
	*.[cfres]) src=$a;;
	esac
	prev=$a
done
if [ -z "$out" ]; then
	if [ -n "$c" ]; then out=`echo $src | sed 's/\.[a-z]*$/.o/'`
	elif [ -n "$s" ]; then out=`echo $src | sed 's/\.[a-z]*$/.s/'`
	else out=a.out; fi
fi
echo "$name made" > "$out"
'''


def make_root(root):
    os.makedirs(os.path.join(root, 'bin'))
    os.makedirs(os.path.join(root, 'tmp'))
    for t in ('cc', 'as', 'ld', 'f77', 'yacc', 'lex', 'cpp', 'c0'):
        p = os.path.join(root, 'bin', t)
        with open(p, 'w') as f:
            f.write(FAKE_TOOL)
        os.chmod(p, 0o755)


def environment(root):
    """The environment of the port's run (it puts the root's bin/ first in
    PATH itself) and of the original's (which gets that PATH as it is)."""
    base = {'PATH': BASE_PATH, 'MUTOS_ROOT': root, 'TZ': 'UTC', 'LC_ALL': 'C',
            'HOME': '/nonexistent'}
    orig = dict(base, PATH=root + '/bin:' + BASE_PATH)
    return base, orig


# ---- the emulated original --------------------------------------------------------

class _Limit(Exception):
    pass


class Sink:
    """A standard output or error: a file make and its commands write to."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, 'wb', buffering=0)

    def write(self, b):
        n = self.f.write(b)
        if os.fstat(self.f.fileno()).st_size > OUT_LIMIT:
            raise _Limit()
        return n

    def fileno(self):
        return self.f.fileno()

    def flush(self):
        pass

    def close(self):
        self.f.close()

    def data(self):
        with open(self.path, 'rb') as f:
            return f.read(OUT_LIMIT + 1)


def host_exec(emu, path, argv, envp, pid):
    """The original's exece(): the host's program runs. None: no such file;
    -errno: it cannot be run; else the child's wait() status word."""
    host = emu._path(path)
    if not os.path.isfile(host):
        return None if not os.path.exists(host) else -errno.EACCES
    if not os.access(host, os.X_OK):
        return -errno.EACCES
    env = dict(e.split('=', 1) for e in envp if '=' in e)
    try:
        r = subprocess.run([a.encode('latin1') for a in argv], executable=host,
                           cwd=emu.cwd, env=env, stdin=subprocess.DEVNULL,
                           stdout=emu.fds[1].fileno(), stderr=emu.fds[2].fileno(),
                           timeout=CHILD_TIMEOUT)
    except subprocess.TimeoutExpired:
        return signal.SIGKILL
    except OSError as e:
        return -(e.errno or errno.ENOEXEC)
    rc = r.returncode
    return (rc & 0xFF) << 8 if rc >= 0 else -rc


def run_orig(args, cwd, root, tmp, stdin=b'', max_insns=MAX_INSNS):
    """(status or None, stdout, stderr, why it did not finish)."""
    so, se = Sink(os.path.join(tmp, 'o.out')), Sink(os.path.join(tmp, 'o.err'))
    _, env = environment(root)
    envp = ['%s=%s' % kv for kv in env.items()]
    emu = mutosemu.Emu(mutosemu.load_image('make'), ['make'] + list(args), cwd=cwd,
                       stdin=io.BytesIO(stdin), stdout=so, stderr=se,
                       on_exec=host_exec, envp=envp)
    emu.read_dirs = True        # srchdir() reads the directory as MUTOS gives it
    why, st = '', None
    try:
        st = emu.run(max_insns)
    except _Limit:
        why = 'endless: output past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why:
            why = 'endless: ' + why
    out, err = so.data(), se.data()
    so.close()
    se.close()
    return st, out, err, why


# ---- the port ---------------------------------------------------------------------

def _limits():
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def run_port(args, cwd, root, tmp, stdin=b'', port=PORT):
    """(status, stdout, stderr); status None for a run a limit stopped."""
    base, _ = environment(root)
    args = [os.fsdecode(a.encode('latin1')) for a in args]
    po, pe = os.path.join(tmp, 'p.out'), os.path.join(tmp, 'p.err')
    with open(po, 'wb') as fo, open(pe, 'wb') as fe, \
            tempfile.TemporaryFile() as fi:
        fi.write(stdin)
        fi.seek(0)
        try:
            r = subprocess.run([port] + list(args), cwd=cwd, stdin=fi, stdout=fo,
                               stderr=fe, env=base, preexec_fn=_limits,
                               timeout=PORT_CPU * 4)
            st = r.returncode
        except subprocess.TimeoutExpired:
            st = None
    with open(po, 'rb') as f:
        out = f.read(OUT_LIMIT + 1)
    with open(pe, 'rb') as f:
        err = f.read(OUT_LIMIT + 1)
    if st is not None and st < 0:
        st = None
    return st, out, err


# ---- the judge --------------------------------------------------------------------

def _lines(b):
    """The lines of b without the end of its last (none for no bytes)."""
    return b.rstrip(b'\n').split(b'\n') if b else []


def _subsequence(a, b):
    """The lines of a are lines of b, in order."""
    it = iter(b)
    return all(x in it for x in a)


_TIMELINE = re.compile(rb'(TIME\([^)\n]*\)=)(\d+)')


def judge(ost, oout, oerr, why, pst, pout, perr, do, dp):
    """(kind, detail) of one step: 'same', 'refused', 'orig-fails', 'endless',
    'limits', 'DIFFERENT', 'BAD-REFUSAL'. The trees are compared by the
    caller's snapshot of both directories (do, dp)."""
    def norm(b, d):
        # -d prints TIME(name)=<now> for a target the step made; the two runs
        # are some milliseconds apart, so the second may tick between them
        now = time.time()
        b = _TIMELINE.sub(lambda m: m.group(1) + (
            b'<now>' if abs(int(m.group(2)) - now) < 3600 else m.group(2)), b)
        return b.replace(d.encode('latin1', 'replace'), b'<dir>')
    oout, oerr = norm(oout, do), norm(oerr, do)
    pout, perr = norm(pout, dp), norm(perr, dp)
    plines = perr.rstrip(b'\n').split(b'\n')
    refused = (pst == 125 and plines[-1].startswith(b'Make: ')
               and plines[-1].endswith(b'refused'))
    if len(oout) > OUT_LIMIT or len(pout) > OUT_LIMIT or \
            len(oerr) > OUT_LIMIT or len(perr) > OUT_LIMIT or \
            why.startswith('endless: output past'):
        return 'limits', 'output past %d bytes' % OUT_LIMIT
    if refused:
        body = perr.rstrip(b'\n')
        body = body[:body.rfind(b'\n') + 1] if b'\n' in body else b''
        ok = (_subsequence(_lines(pout), _lines(oout))
              and _subsequence(_lines(body), _lines(oerr)))
        if ost is None:
            return ('orig-fails' if ok else 'BAD-REFUSAL'), \
                '%s: %s' % (why, plines[-1].decode('latin1'))
        return ('refused' if ok else 'BAD-REFUSAL'), plines[-1].decode('latin1')
    if ost is None and pst is None:
        n, m = min(len(oout), len(pout)), min(len(oerr), len(perr))
        if pout[:n] == oout[:n] and perr[:m] == oerr[:m]:
            return 'endless', why
        return 'DIFFERENT', 'both endless (%s), the messages differ: %s' % (
            why, diff_at(pout, oout))
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original ' \
            '%s' % ('exit %d' % ost if ost is not None else why)
    if ost is None:
        return 'DIFFERENT', 'the original does not finish (%s); the port exit %s' \
            % (why, pst)
    parts = []
    if pst != ost:
        parts.append('exit %s, original %d' % (pst, ost))
    if pout != oout:
        parts.append('stdout %r, original %r (%s)' % (
            pout[:300], oout[:300], diff_at(pout, oout)))
    if perr != oerr:
        parts.append('stderr %r, original %r (%s)' % (
            perr[:300], oerr[:300], diff_at(perr, oerr)))
    _same_trees(snapshot(dp), snapshot(do), parts)
    if parts:
        return 'DIFFERENT', '; '.join(parts)
    return 'same', ''


# ---- sequences --------------------------------------------------------------------

def compare_seq(steps, files, port=PORT, keep=None, name=''):
    """Run the steps in two copies of a directory made from files; return
    [(step number, kind, detail)] up to the first difference or refusal.
    A step: a list of make's arguments, ('stdin', args, bytes), ('write',
    name, F|bytes), ('rm', name), ('mkdir', name), ('touch', name, mtime),
    ('trunc', name, size) - those of a file that is not there change
    nothing."""
    results = []
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    with tempfile.TemporaryDirectory(prefix='makecmp') as tmp:
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        root = os.path.join(tmp, 'root')
        make_root(root)
        for d in (do, dp):
            os.makedirs(d)
            place(d, files)
        for i, st in enumerate(steps):
            stdin = b''
            if isinstance(st, tuple) and st[0] == 'stdin':
                stdin, st = st[2], st[1]
            if isinstance(st, tuple):
                for d in (do, dp):
                    p = os.path.join(d, st[1])
                    if st[0] == 'write':
                        place(d, {st[1]: st[2]})
                    elif st[0] == 'mkdir':
                        os.makedirs(p, exist_ok=True)
                    elif not os.path.exists(p):
                        pass            # nothing there to change
                    elif st[0] == 'rm':
                        os.unlink(p)
                    elif st[0] == 'touch':
                        os.utime(p, (st[2], st[2]))
                    elif st[0] == 'trunc':
                        os.truncate(p, st[2])
                    else:
                        raise ValueError(st)
                continue
            ost, oout, oerr, why = run_orig(st, do, root, tmp, stdin)
            pst, pout, perr = run_port(st, dp, root, tmp, stdin, port)
            kind, detail = judge(ost, oout, oerr, why, pst, pout, perr, do, dp)
            for d in (do, dp):
                settle_times(d)
            results.append((i, kind, detail))
            if kind != 'same':
                if keep and kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    os.makedirs(keep, exist_ok=True)
                    k = tempfile.mkdtemp(dir=keep, prefix='case')
                    shutil.copytree(do, os.path.join(k, 'orig'), symlinks=True)
                    shutil.copytree(dp, os.path.join(k, 'port'), symlinks=True)
                    with open(os.path.join(k, 'args'), 'w',
                              errors='backslashreplace') as f:
                        f.write('%s\nstep %d: %s\n%s: %s\n' % (
                            name, i, ' '.join(st), kind, detail))
                break
    return results


# ---- the hand-written cases -------------------------------------------------------

OLD = 600000000


def cases():
    """[(name, files, steps)]"""
    out = []

    def case(name, steps, mk=None, files=None, **more):
        f = {}
        if mk is not None:
            f['makefile'] = F(mk.encode('latin1') if isinstance(mk, str) else mk,
                              mtime=OLD)
        for k, v in (files or {}).items():
            f[k] = v if isinstance(v, F) else F(v, mtime=OLD)
        for k, v in more.items():
            f[k] = v if isinstance(v, F) else F(v, mtime=OLD)
        out.append((name, f, steps))

    # ---- the description file -------------------------------------------------
    case('no makefile, no argument', [[], ['-n'], ['-p']])
    case('no makefile, a target', [['x'], ['-n', 'x'], ['-k', 'x']])
    case('no makefile, a file as target', [['a.c'], ['-n', 'a.c']], files={'a.c': b'int x;\n'})
    case('no makefile, a rule as target', [['a.o'], ['-n', 'a.o']], files={'a.c': b'int x;\n'})
    case('Makefile is read', [[], ['-n']], files={'Makefile': b'all:\n\techo from Makefile\n'})
    case('makefile before Makefile', [[]], files={
        'makefile': b'all:\n\techo from makefile\n', 'Makefile': b'all:\n\techo from Makefile\n'})
    case('-f', [['-f', 'other'], ['-f', 'nothere'], ['-f'], ['-fother'], ['-n', '-f', 'other']],
         files={'other': b'all:\n\techo from other\n'})
    case('-f twice', [['-f', 'a.mk', '-f', 'b.mk'], ['-f', 'a.mk', '-f', 'b.mk', 'y']],
         files={'a.mk': b'x:\n\techo x\n', 'b.mk': b'y:\n\techo y\n'})
    case('-f -', [('stdin', ['-f', '-'], b'all:\n\techo from stdin\n'),
                  ('stdin', ['-f', '-', 'z'], b'z:\n\techo z\n')])
    case('-f combined flags', [['-nf', 'other'], ['-sf', 'other'], ['-fn', 'other']],
         files={'other': b'all:\n\techo from other\n'})
    case('unknown flag', [['-z'], ['-nz'], ['-'], ['-n', '-']], 'all:\n\techo hi\n')
    case('all the flags', [['-n'], ['-s'], ['-i'], ['-S'], ['-k'], ['-r'], ['-t'], ['-q'],
                           ['-d'], ['-p'], ['-nsiSkrtqdp']], 'all:\n\techo hi\n')
    case('-r no built-in rules', [['-r', 'a.o'], ['-r', '-p'], ['-p']],
         'x:\n\techo x\n', files={'a.c': b'int x;\n'})
    case('-p', [['-p'], ['-p', '-n'], ['-pn', 'b']],
         'CFLAGS = -O\nall: a b\n\techo all\na: b\n\techo a\nb:\n\techo b\n.c.o:\n\tcc -c $<\n')
    case('-d', [['-d'], ['-d', 'b'], ['-dn']],
         'CFLAGS = -O\nall: a b\n\techo all\na: b\n\techo a\nb:\n\techo b\n')
    case('target arguments', [['a', 'b'], ['b', 'a', 'b'], ['nothere'], ['-k', 'nothere', 'a']],
         'all:\n\techo all\na:\n\techo a\nb:\n\techo b\n')
    case('first target is main', [[]], '.hidden:\n\techo hidden\nmain:\n\techo main\nother:\n\techo other\n')
    case('dot with slash is main', [[]], './x:\n\techo slash\n')
    case('only dot targets', [[], ['.a']], '.a:\n\techo dot\n')
    case('empty makefile', [[], ['x'], ['-p']], '')
    case('blank makefile', [[]], '\n\n   \n\t\n\n')

    # ---- rules, dependencies, up to date ----------------------------------------
    case('up to date', [[], [], ['t']], 't: s\n\techo made t\n',
         files={'s': F(b's', mtime=OLD), 't': F(b't', mtime=OLD + 10)})
    case('out of date', [[], []], 't: s\n\techo made t\n\ttouch t\n',
         files={'s': F(b's', mtime=OLD + 10), 't': F(b't', mtime=OLD)})
    case('target missing', [[], []], 't: s\n\techo made t\n\ttouch t\n',
         files={'s': F(b's', mtime=OLD)})
    case('no commands', [[], ['t'], ['-n']], 't: s\n', files={'s': F(b's', mtime=OLD)})
    case('no deps, no file', [[], [], ['-n']], 't:\n\techo made t\n')
    case('no deps, file exists', [[]], 't:\n\techo made t\n', files={'t': F(b't', mtime=OLD)})
    case('chain', [[], [], ['-n']], 'a: b\n\techo a\n\ttouch a\nb: c\n\techo b\n\ttouch b\n'
         'c:\n\techo c\n\ttouch c\n')
    case('diamond', [[], ['-n']], 'a: b c\n\techo a\nb: d\n\techo b\nc: d\n\techo c\nd:\n\techo d\n')
    case('cycle', [[], ['-n']], 'a: b\n\techo a\nb: a\n\techo b\n')
    case('self dependency', [[]], 'a: a\n\techo a\n')
    case('missing dependency', [[], ['-k'], ['-n'], ['-ki']], 'a: nothere\n\techo a\n')
    case('missing dependency with file', [[]], 'a: nothere\n\techo a\n', files={'a': F(b'', mtime=OLD)})
    case('dependency exists, no rule', [[]], 'a: s\n\techo a\n', files={'s': F(b'', mtime=OLD)})
    case('same dep twice', [[]], 'a: b b\n\techo a\nb:\n\techo b\n')
    case('many deps', [[], ['-n']], 'a:' + ''.join(' d%d' % i for i in range(60)) + '\n\techo a\n' +
         ''.join('d%d:\n\techo d%d\n' % (i, i) for i in range(60)))
    case('deps on more lines', [[]], 'a: b\na: c\n\techo a\nb:\n\techo b\nc:\n\techo c\n')
    case('two command lines', [[]], 'a: b\n\techo one\na: c\n\techo two\nb:\n\techo b\nc:\n\techo c\n')
    case('multiple targets', [[], ['y'], ['-n', 'z']], 'x y z: s\n\techo made\n',
         files={'s': F(b's', mtime=OLD)})
    case('inconsistent rules', [[], ['-n']], 'a: b\n\techo a\na:: c\n\techo a2\nb:\n\techo b\nc:\n\techo c\n')
    case('double colon', [[], ['-n']], 'a:: b\n\techo a1\na:: c\n\techo a2\nb:\n\techo b\nc:\n\techo c\n')
    case('double colon no deps', [[], [], ['a']], 'a::\n\techo a1\na::\n\techo a2\n')
    case('double colon with files', [[], []], 'a:: s\n\techo a1\n\ttouch a\na:: t\n\techo a2\n',
         files={'s': F(b'', mtime=OLD + 20), 't': F(b'', mtime=OLD), 'a': F(b'', mtime=OLD + 10)})
    case('double colon with $?', [[]], 'a:: s t\n\techo $?\na:: u\n\techo $?\n',
         files={'s': F(b'', mtime=OLD + 20), 't': F(b'', mtime=OLD), 'u': F(b'', mtime=OLD),
                'a': F(b'', mtime=OLD + 10)})
    case('multiple rules lines', [[], ['-n']], 'a: b\n\techo one\na: c\n\techo two\nb:\nc:\n')
    case('multiple rules lines dot', [[], ['-n']], '.x: b\n\techo one\n.x: c\n\techo two\nb:\nc:\n')
    case('.DEFAULT', [['nothere'], ['-n', 'other'], [], ['q']], '.DEFAULT:\n\techo default for $<\n'
         'all: nothere\n\techo all\n')
    case('.DEFAULT empty', [['nothere']], '.DEFAULT:\nall: nothere\n')
    case('.SILENT', [[]], '.SILENT:\nall:\n\techo quiet\n')
    case('.IGNORE', [[]], '.IGNORE:\nall:\n\tfalse\n\techo goes on\n')
    case('.PRECIOUS', [[]], '.PRECIOUS: a\nall:\n\techo ok\n')
    case('-k keeps going', [['-k'], ['-k', 'a']], 'a: b c\n\techo a\nb:\n\tfalse\nc:\n\techo c\n')
    case('stop at error', [[], ['-S'], ['-i']], 'a: b c\n\techo a\nb:\n\tfalse\nc:\n\techo c\n')
    case('-n is not run', [['-n'], ['-n', 'a'], ['-nq']], 'a: b\n\techo a\nb:\n\ttouch b\n')
    case('-q', [['-q'], ['-q', 'b'], ['b'], ['-q', 'b']], 'a: b\n\techo a\n\ttouch a\nb:\n\ttouch b\n',
         files={'b': F(b'', mtime=OLD)})
    case('-q up to date', [['-q'], ['-q', 't']], 't: s\n\techo t\n',
         files={'s': F(b'', mtime=OLD), 't': F(b'', mtime=OLD + 5)})
    case('-t', [['-t'], ['-t', 'a'], [], ['-t', '-n'], ['-ts']],
         'a: b\n\techo a\nb:\n\techo b\n', files={'b': F(b'x', mtime=OLD)})
    case('-t existing and empty', [['-t'], ['-t', 'e'], ['-tn']], 'f e:\n\techo\n',
         files={'e': F(b'', mtime=OLD), 'f': F(b'data', mtime=OLD)})
    case('-t missing', [['-t', 'a'], ['-ts', 'a']], 'a b:\n\techo\n')

    # ---- macros ----------------------------------------------------------------
    case('macros', [[], ['X=cmd'], ['X=cmd', 'Y=other'], ['X'], ['X=a b']],
         'X = hello\nY=world\nall:\n\techo $(X) $(Y) ${X} $X $Y\n')
    case('macro forms', [[]],
         'A=a\nAB=ab\nall:\n\techo $A $(A) ${A} $(AB) $AB $( A ) ${ A} $() $(  ) $$ \'$$$$\'A $(A $\n\t')
    case('macro unclosed', [[]], 'A=a\nall:\n\techo $(A\n')
    case('macro in macro', [[]], 'A=$(B)\nB=b\nall:\n\techo $(A)\n')
    case('macro recursion', [[], ['-n']], 'A=$(A)\nall:\n\techo $(A)\n')
    case('macro recursion 2', [[]], 'A=$(B)\nB=$(A)\nall:\n\techo $(A)\n')
    case('macro depth 99', [[]], ''.join('M%d=$(M%d)\n' % (i, i + 1) for i in range(98)) +
         'M98=end\nall:\n\techo $(M0)\n')
    case('macro depth 100', [[]], ''.join('M%d=$(M%d)\n' % (i, i + 1) for i in range(100)) +
         'M100=end\nall:\n\techo $(M0)\n')
    case('undefined macro', [[]], 'all:\n\techo [$(NOPE)] [$NOPE] [${NOPE}]\n')
    case('empty macro', [[]], 'E=\nall:\n\techo [$(E)]\n')
    case('macro spaces', [[]], 'A   =    spaced   \nB=\t\ttabbed\nall:\n\techo [$(A)] [$(B)]\n')
    case('macro with colon', [[]], 'A = a:b\nall:\n\techo $(A)\n')
    case('macro value has equals', [[]], 'A = a=b\nall:\n\techo $(A)\n')
    case('macro with leading space', [[]], ' A=1\nall:\n\techo $(A)\n')
    case('macro leading dot', [[]], '.A=1\nall:\n\techo $(.A)\n')
    case('macro redefined', [[]], 'A=1\nall:\n\techo $(A)\nA=2\n')
    case('macro changed after use', [[]], 'A=1\nB=$(A)\nA=2\nall:\n\techo $(A) $(B)\n')
    case('macro changed after use in command', [[]], 'all:\n\techo $(A)\nA=2\n')
    case('command line macro wins', [['A=3'], ['A=3', 'B=4']], 'A=1\nB=2\nall:\n\techo $(A) $(B)\n')
    case('command line macro first', [['A=3', 'all'], ['all', 'A=3'], ['-n', 'A=3']],
         'A=1\nall:\n\techo $(A)\n')
    case('command line macro with space', [['A= x y'], ['A =x'], ['A = x']], 'all:\n\techo [$(A)]\n')
    case('command line empty macro', [['A='], ['A=', '-n']], 'A=1\nall:\n\techo [$(A)]\n')
    case('command line colon is a target', [['a:b']], 'all:\n\techo\n')
    case('$@ $< $* $?', [[], ['-n']], 'a.o: a.c\n\techo $@ $< $* $?\n.c.o:\n\techo impl $@ $< $*\n',
         files={'a.c': F(b'int;', mtime=OLD)})
    case('$@ explicit', [[]], 'a b: s\n\techo $@ [$(@)] [${@}] $?\n', files={'s': F(b'', mtime=OLD)})
    case('$@ not yet defined', [[]], 'all:\n\techo [$@] [$?] [$<] [$*]\n')
    case('$? lists newer', [[]], 't: a b c\n\techo $?\n',
         files={'a': F(b'', mtime=OLD + 5), 'b': F(b'', mtime=OLD), 'c': F(b'', mtime=OLD + 20),
                't': F(b'', mtime=OLD + 10)})
    case('$? empty', [[]], 't: a\n\techo [$?]\n', files={'a': F(b'', mtime=OLD),
                                                        't': F(b'', mtime=OLD + 10)})
    case('$? no deps', [[]], 'all:\n\techo [$?]\n')
    case('$? no deps after $@', [[]], 'one:\n\techo $@\nall: one\n\techo [$?] $@\n')
    case('$? no deps double colon', [[]], 'one::\n\techo $@\none::\n\techo [$?] $@\n')
    case('$? too long', [[]], 't: ' + ' '.join('%s%d' % ('x' * 140, i) for i in range(12)) + '\n\techo $?\n',
         files={'%s%d' % ('x' * 140, i): F(b'', mtime=OLD) for i in range(12)})
    case('$< in suffix rule', [[], ['b.o']], '.SUFFIXES: .o .c\n.c.o:\n\techo $< $@ $*\n',
         files={'a.c': F(b'', mtime=OLD), 'b.c': F(b'', mtime=OLD)})
    case('$ at the end of a line', [[]], 'all:\n\techo cost 5$\n')
    case('$$ in command', [[]], 'all:\n\techo $$HOME \'$$$$\'\n')
    case('macro in dependency line', [[]], 'D=b c\nall: $(D)\n\techo all\nb:\n\techo b\nc:\n\techo c\n')
    case('macro in target', [[]], 'T=x\n$(T): s\n\techo $@\n', files={'s': F(b'', mtime=OLD)})
    case('macro in dep line after semicolon', [[]], 'A=a\nall: ; echo $(A)\n')
    case('macro command on dependency line', [[]], 'all: b ; echo one $(A)\nA=x\nb:\n\techo b\n')
    case('macro changing value', [[]], 'A=1\nall:\n\techo $(A)\nA=2\nB=3\nall2: all\n\techo $(A) $(B)\n')

    # ---- lines -------------------------------------------------------------------
    case('comments', [[]], '# comment\nall: # target comment\n\techo one # not a comment for the shell?\n'
         '# another\n')
    case('comment in command', [[]], 'all:\n\techo a#b\n\t# shell comment\n\techo c\n')
    case('comment after semicolon', [[]], 'all: ; echo x # y\n')
    case('comment hides macro', [[]], 'A=1 # one\nall:\n\techo [$(A)]\n')
    case('continuation', [[]], 'all: \\\n  b \\\n\tc\n\techo \\\n\tdone\nb:\nc:\n')
    case('continuation macro', [[]], 'A=one \\\n   two \\\n\n\tthree\nall:\n\techo [$(A)]\n')
    case('continuation at eof', [[]], 'all:\n\techo a \\')
    case('continuation then eof', [[]], 'all:\n\techo a \\\n')
    case('backslash in comment', [[]], 'all: # c \\\n\techo hidden\n\techo shown\n')
    case('no final newline', [[]], 'all:\n\techo no newline')
    case('no final newline rule', [[]], 'all: b\nb:\n\techo b\nc:')
    case('tab-only lines', [[]], 'all:\n\t\n\techo a\n\t \n\techo b\n')
    case('blank lines in rule', [[]], 'all:\n\n\techo a\n\n\n\techo b\n')
    case('space start is a rule', [[]], ' all:\n\techo a\n')
    case('spaces before tab', [[]], 'all:\n  \techo a\n')
    case('indented command with spaces', [[]], 'all:\n    echo a\n')
    case('semicolon command', [[]], 'all: ; echo one\n\techo two\nb: c ;echo b\nc:\n')
    case('semicolon only', [[]], 'all:;\n')
    case('semicolon spaces', [[]], 'all: ;   \techo hi\n')
    case('CR LF', [[]], 'all:\r\n\techo crlf\r\n')
    case('long line 1499', [[]], 'all:\n\techo ' + 'x' * (1499 - 6) + '\n')
    case('long line 1500', [[]], 'all:\n\techo ' + 'x' * (1500 - 6) + '\n')
    case('long line 1498 macro', [[]], 'A=' + 'y' * 1496 + '\nall:\n\techo $(A)\n')
    case('long target name', [[]], 'x' * 120 + ':\n\techo long\n')
    case('target name 99', [[]], 'x' * 99 + ': ' + 'y' * 99 + '\n' + 'y' * 99 + ':\n\techo y\n')
    case('target name 100', [[]], 'x' * 100 + ': ' + 'y' * 100 + '\n' + 'y' * 100 + ':\n\techo y\n')
    case('NUL in line', [[]], b'all:\n\techo a\x00b\n\techo c\n')
    case('NUL in dependency line', [[]], b'all: b\x00 c\n\techo a\nb:\n\techo b\n')
    case('high bytes in comment', [[]], b'all: # \xe4\xf6\xfc\n\techo ok\n')
    case('high bytes in command', [[]], b'all:\n\techo "\xe4\xf6\xfc"\n')
    case('high bytes in command bare', [[]], b'all:\n\techo \xe4\xf6\xfc\n')
    case('high bytes in target', [[]], b'\xe4: \n\techo hi\n')
    case('high bytes in macro', [[]], b'A=\xe4\xf6\xfc\nall:\n\techo "$(A)"\n')
    case('0xff byte', [[]], b'all:\n\techo "\xff"\n')

    # ---- syntax errors --------------------------------------------------------------
    case('no separator', [[]], 'all\n\techo hi\n')
    case('no separator two names', [[]], 'a b\n')
    case('colon first', [[]], ': a\n')
    case('double colon first', [[]], ':: a\n')
    case('greater than', [[]], 'a > b:\n')
    case('bad character amp', [[]], 'a: b & c\n')
    case('bad character pipe', [[]], 'a: b | c\n')
    case('bad char at line 3', [[]], 'a:\n\techo\nb: c | d\n')
    case('two colons spaced', [[]], 'a: : b\n')
    case('three colons', [[]], 'a::: b\n')
    case('colon in name', [[]], 'a:b: c\n')
    case('command first', [[]], '\techo hi\nall:\n\techo x\n')
    case('command before any target', [[]], '\techo hi\n')
    case('only a name', [[]], 'name\n')
    case('semicolon first', [[]], '; echo\n')
    case('equals in dependency line', [[]], 'a: b=c\n')
    case('syntax after macros', [[]], 'A=1\nB=2\nC\n')
    case('300 targets', [[]], ' '.join('t%d' % i for i in range(300)) + ':\n\techo x\n')
    case('301 targets', [[]], ' '.join('t%d' % i for i in range(301)) + ':\n\techo x\n')
    case('302 targets', [[]], ' '.join('t%d' % i for i in range(302)) + ':\n\techo x\n')
    case('299 targets and run', [['t0'], ['-n', 't298']], ' '.join('t%d' % i for i in range(299)) +
         ': s\n\techo $@\n', files={'s': F(b'', mtime=OLD)})
    case('hash table', [[]], ''.join('n%d:\n' % i for i in range(520)))
    case('hash table 500', [[]], ''.join('n%d:\n' % i for i in range(500)))

    # ---- commands ------------------------------------------------------------------
    case('command prefixes', [[], ['-n'], ['-s'], ['-i']],
         'all:\n\t@echo silent\n\t-false\n\t-@false\n\t@-false\n\techo noisy\n')
    case('prefix only', [[], ['-n']], 'all:\n\t@\n\t-\n\t@-\n\t- \n\techo done\n')
    case('prefix then space', [[], ['-n']], 'all:\n\t@ echo a\n\t- echo b\n')
    case('exit codes', [[], ['-i'], ['-k']], 'all:\n\tsh -c "exit 3"\n\techo not reached\n')
    case('exit code 200', [[]], 'all:\n\tsh -c "exit 200"\n')
    case('exit code 255', [[]], 'all:\n\tsh -c "exit 255"\n')
    case('exit code 128', [[], ['-i']], 'all:\n\tsh -c "exit 128"\n')
    case('exit code 256 wraps', [[]], 'all:\n\tsh -c "exit 256"\n')
    case('killed by signal', [[], ['-i']], 'all:\n\tsh -c "kill -9 \\$\\$"\n')
    case('terminated by signal 15', [[]], 'all:\n\tsh -c "kill -15 \\$\\$"\n')
    case('signal 11 core', [[]], 'all:\n\tsh -c "kill -11 \\$\\$"\n')
    case('false in ignored', [['-i']], 'all:\n\tfalse\n\techo after\n')
    case('command not found', [[], ['-i'], ['-k']], 'all:\n\tnosuchprogram arg\n\techo after\n')
    case('command not found in shell', [[]], 'all:\n\tnosuchprogram arg;echo after\n')
    case('command is a directory', [[]], 'all:\n\tsub\n', files={'sub/x': b''})
    case('command path', [[]], 'all:\n\t/bin/echo direct\n\t./script a b\n',
         files={'script': F(b'#!/bin/sh\necho script "$@"\n', mtime=OLD, mode=0o755)})
    case('command not executable', [[]], 'all:\n\t./script\n',
         files={'script': F(b'#!/bin/sh\necho x\n', mtime=OLD, mode=0o644)})
    case('script without shebang', [[]], 'all:\n\t./script\n',
         files={'script': F(b'echo no shebang\n', mtime=OLD, mode=0o755)})
    case('direct command blanks', [[]], 'all:\n\techo   a    b \t c  \n')
    case('blank command', [[], ['-n'], ['-i']], 'all:\n\t- \n\techo after\n')
    case('blank command after macro', [[], ['-i']], 'E=\nall:\n\t$(E)\n\techo after\n')
    case('shell metas', [[]], 'all:\n\techo a > out.txt\n\tcat out.txt\n\techo b | cat\n\techo "c d"\n')
    case('shell wildcard', [[]], 'all:\n\techo *.txt\n', files={'a.txt': b'', 'b.txt': b''})
    case('shell -e', [[], ['-i']], 'all:\n\tfalse; echo after\n')
    case('shell -e ignored', [['-i']], 'all:\n\tfalse; echo after\n')
    case('dash prefix shell -e', [[]], 'all:\n\t-false; echo after\n')
    case('many words', [[]], 'all:\n\techo ' + ' '.join('w%d' % i for i in range(198)) + '\n')
    case('199 words', [[]], 'all:\n\techo ' + ' '.join('w%d' % i for i in range(198)) + '\n')
    case('200 words', [[]], 'all:\n\techo ' + ' '.join('w%d' % i for i in range(199)) + '\n')
    case('201 words', [[]], 'all:\n\techo ' + ' '.join('w%d' % i for i in range(200)) + '\n')
    case('command 2500', [[]], 'all:\n\techo ' + 'x' * 2493 + '\n')
    case('command 2499 expanded', [[]], 'A=' + 'x' * 1400 + '\nall:\n\techo $(A)$(A)\n')
    case('command overflow expanded', [[]], 'A=' + 'x' * 1400 + '\nall:\n\techo $(A)$(A)$(A)\n')
    case('output order', [[]], 'all:\n\techo one\n\t@echo two\n\techo three 1>&2\n\techo four\n')
    case('stdout then child', [['-n'], [], ['-s'], ['-sn']], 'all:\n\t@echo a\n\techo b\n')
    case('output buffered', [[]], 'all: b\n\techo a\nb:\n\t@echo b\n.SILENT:\n')
    case('output 600 lines', [['-n']], 'all:\n' + ''.join('\techo line %d xxxxxxxxxxxxxxxxxxxxxxx\n' % i
                                                          for i in range(60)))
    case('up to date messages', [[], ['all'], ['a', 'all'], ['-n']], 'all: a\n\techo all\na:\n\t@\n',
         files={'a': F(b'', mtime=OLD), 'all': F(b'', mtime=OLD + 10)})
    case('not remade because of errors', [[], ['-k'], ['-i']], 'all: a\n\techo all\na:\n\tfalse\n')

    # ---- suffix rules ---------------------------------------------------------------
    case('built-in c.o', [['a.o'], ['-n', 'a.o'], ['a.o']], '', files={'a.c': b'int a;\n'})
    case('built-in c.out', [['a.out'], ['a'], ['-n', 'a.out']], '', files={'a.c': b'int a;\n'})
    case('built-in s.o', [['a.o']], '', files={'a.s': b'.text\n'})
    case('built-in s.o and c.o', [['a.o']], '', files={'a.s': b'.text\n', 'a.c': b'int a;\n'})
    case('built-in y.o', [['a.o'], ['-n', 'a.o']], '', files={'a.y': b'%%\n'})
    case('built-in y.c', [['a.c'], ['a.o']], '', files={'a.y': b'%%\n'})
    case('built-in l.o', [['a.o'], ['a.c']], '', files={'a.l': b'%%\n'})
    case('built-in f.o', [['a.o'], ['a.out']], '', files={'a.f': b'      end\n'})
    case('built-in e.o r.o', [['a.o'], ['b.o'], ['a.out']], '', files={'a.e': b'x', 'b.r': b'x'})
    case('built-in yr ye', [['a.o'], ['b.o'], ['a.r'], ['b.e']], '', files={'a.yr': b'x', 'b.ye': b'x'})
    case('built-in o.out', [['a.out']], '', files={'a.o': b'obj'})
    case('built-in y.out l.out', [['a.out'], ['b.out']], '', files={'a.y': b'x', 'b.l': b'x'})
    case('built-in macros', [['-p'], ['-pn']], 'x:\n\techo\n')
    case('built-in macros in rules', [['a.o'], ['CC=mycc', 'a.o'], ['CFLAGS=-O', 'a.o'],
                                      ['AS=as2', 'b.o']], '',
         files={'a.c': b'int a;\n', 'b.s': b'x'})
    case('built-in redefine suffixes', [['a.o'], ['a.x']], '.SUFFIXES:\n.SUFFIXES: .x .o\n.x.o:\n\techo xo $<\n',
         files={'a.c': b'', 'a.x': b''})
    case('suffixes cleared', [['a.o']], '.SUFFIXES:\n', files={'a.c': b''})
    case('suffixes added', [['a.o'], ['b.p']], '.SUFFIXES: .p\n.c.p:\n\techo cp $<\n',
         files={'a.c': b'', 'b.c': b''})
    case('suffixes no list', [['a.o']], '', files={'a.c': b''})
    case('-r suffix rules', [['-r', 'a.o'], ['-r', '-p']], '.SUFFIXES: .c .o\n.c.o:\n\techo co $<\n',
         files={'a.c': b''})
    case('suffix with dir', [['sub/a.o']], '', files={'sub/a.c': b'int;'})
    case('suffix prefix 100', [[]], 'z' * 98 + '.o:\n\techo x\n', files={'z' * 98 + '.c': b''})
    case('suffix prefix 101', [['%s.o' % ('z' * 101)]], '', files={'%s.c' % ('z' * 101): b''})
    case('suffix chain', [['a.o']], '.SUFFIXES: .o .y .c\n.y.c:\n\techo yc $<\n\ttouch $@\n.c.o:\n\techo co $<\n',
         files={'a.y': b''})
    case('suffix explicit wins', [['a.o']], 'a.o: a.c\n\techo explicit\n.c.o:\n\techo implicit\n',
         files={'a.c': b''})
    case('suffix explicit deps implicit command', [['a.o']], 'a.o: a.c b.h\n.c.o:\n\techo implicit $<\n',
         files={'a.c': b'', 'b.h': b''})
    case('suffix source newer', [[], []], 'a.o:\n.c.o:\n\techo co $<\n\ttouch $@\n',
         files={'a.c': F(b'', mtime=OLD + 10), 'a.o': F(b'', mtime=OLD)})
    case('suffix source older', [[]], 'a.o:\n.c.o:\n\techo co $<\n',
         files={'a.c': F(b'', mtime=OLD), 'a.o': F(b'', mtime=OLD + 10)})
    case('suffix $* of a returned doname', [[]], 'a.o: b.o\n\techo [$*]\nb.o:\n\techo b\n.c.o:\n\techo co $* $<\n',
         files={'a.c': b'', 'b.c': b''})
    case('suffix $* dangling', [['b.o', 'x']], '.c.o:\n\techo co $*\nx:\n\techo [$*]\n',
         files={'b.c': b''})
    case('suffix $* alive', [['a.o']], 'a.o: b.o\n\techo [$*] [$<]\nb.o:\n\techo b\n.c.o:\n\techo co $* $<\n',
         files={'a.c': b'', 'b.c': b''})
    case('suffix no command in rule', [['a.o']], '.c.o:\n', files={'a.c': b''})
    case('suffix rule only dep', [['a.o']], '.SUFFIXES: .c .o\n.c.o: x\n\techo co $<\nx:\n\techo x\n',
         files={'a.c': b''})

    # ---- wildcards -----------------------------------------------------------------
    star = {'a.c': b'', 'b.c': b'', 'c.h': b'', 'dir/x.c': b'', 'dir/y.c': b'', 'dir/z.h': b''}
    case('* wildcard', [[], ['-n']], 'all: *.c\n\techo $?\n', files=star)
    case('* wildcard order', [[]], 'all: *.c *.h\n\techo $?\n', files=star)
    case('? wildcard', [[]], 'all: ?.c\n\techo $?\n', files=star)
    case('[] wildcard', [[]], 'all: [ab].c\n\techo $?\n', files=star)
    case('[a-b] wildcard', [[]], 'all: [a-b].c [c-d].h\n\techo $?\n', files=star)
    case('[] unterminated', [[]], 'all: [ab\n\techo x\n', files=star)
    case('[] empty', [[]], 'all: []\n\techo x\n', files=star)
    case('wildcard no match', [[], ['-k']], 'all: *.zzz\n\techo $?\n', files=star)
    case('wildcard no match is a name', [[]], 'all: *.zzz\n\techo $?\n')
    case('wildcard in dir', [[]], 'all: dir/*.c\n\techo $?\n', files=star)
    case('wildcard in dir [] ', [[]], 'all: dir/[xy].c dir/?.h\n\techo $?\n', files=star)
    case('wildcard dir missing', [[]], 'all: nodir/*.c\n\techo $?\n', files=star)
    case('wildcard dir is a file', [[]], 'all: a.c/*\n\techo $?\n', files=star)
    case('wildcard star dots', [[]], 'all: *\n\techo $?\n', files={'a': b'', 'b': b''})
    case('wildcard .*', [[]], 'all: .*\n\techo $?\n', files={'.hid': b'', 'a': b''})
    case('wildcard dot dot', [[]], 'all: ../*\n\techo x\n', files={'a': b''})
    case('wildcard dir dot', [[]], 'all: ./*.c\n\techo $?\n', files=star)
    case('wildcard dir slash', [[]], 'all: /*\n\techo x\n')
    case('wildcard twice', [[]], 'a: *.c\n\techo a $?\nb: *.c\n\techo b $?\n', files=star)
    case('wildcard same dir twice', [[]], 'a: *.c dir/*.c *.h dir/*.h\n\techo $?\n', files=star)
    case('wildcard long names', [[]], 'all: *\n\techo $?\n',
         files={'exactly14chars': b'', 'fifteen_chars15': b'', 'a_much_longer_name.c': b'', 'ok': b''})
    case('wildcard in .PRECIOUS', [[]], '.PRECIOUS: *.c\nall:\n\techo x\n', files=star)
    case('wildcard in .SUFFIXES', [[]], '.SUFFIXES: *.c\nall:\n\techo x\n', files=star)
    case('wildcard -d', [['-d'], ['-dn']], 'all: *.c\n\techo $?\n', files=star)
    case('wildcard -p', [['-pn'], ['-p']], 'all: *.c\n\techo $?\n', files=star)
    case('wildcard name 100', [[]], 'all: dir/*\n\techo x\n', files={'dir/' + 'q' * 14: b''})
    case('wildcard dir name long', [[]], 'all: ' + 'd' * 99 + '/*\n\techo x\n')
    case('many files', [[]], 'all: *.o\n\techo $?\n', files={'f%03d.o' % i: b'' for i in range(100)})
    case('wildcard * pattern', [[]], 'all: *a*b*c\n\techo $?\n',
         files={'aXbYc': b'', 'abc': b'', 'cba': b'', 'xabcx': b''})
    case('wildcard [!', [[]], 'all: [!a]*\n\techo $?\n', files={'a': b'', 'b': b'', '!c': b''})
    case('wildcard [a-', [[]], 'all: [a-\n\techo $?\n', files={'a': b''})
    case('wildcard range reversed', [[]], 'all: [z-a]\n\techo $?\n', files={'a': b'', 'm': b'', 'z': b''})
    case('wildcard range dash', [[]], 'all: [a-]\n\techo $?\n', files={'a': b'', '-': b'', 'b': b''})
    case('wildcard range chain', [[]], 'all: [a-cx-z]\n\techo $?\n',
         files={'a': b'', 'b': b'', 'c': b'', 'd': b'', 'x': b'', 'y': b'', 'z': b''})
    case('wildcard after dir cut', [['-d']], 'all: dir/*.c\n\techo $?\n', files=star)

    # ---- archives -----------------------------------------------------------------
    arc = mkar([('x.o', b'XXXX', 600000100), ('y.o', b'YY', 600000200),
                ('exactly14chars', b'1', 600000300)])
    case('archive member', [[], ['-n']], 'lib.a(x.o): x.o\n\techo update $@\nx.o:\n\techo x\n',
         files={'lib.a': F(arc, mtime=OLD)})
    case('archive member newer', [[]], 'all: lib.a(x.o)\n\techo all $?\nlib.a(x.o): x.o\n\techo update\n',
         files={'lib.a': F(arc, mtime=OLD), 'x.o': F(b'', mtime=600000500)})
    case('archive member older', [[]], 'all: lib.a(x.o)\n\techo all $?\nlib.a(x.o): x.o\n\techo update\n',
         files={'lib.a': F(arc, mtime=OLD), 'x.o': F(b'', mtime=600000050)})
    case('archive member missing', [[]], 'all: lib.a(z.o)\n\techo all $?\n',
         files={'lib.a': F(arc, mtime=OLD)})
    case('archive missing', [[]], 'all: lib.a(z.o)\n\techo all $?\n')
    case('archive not an archive', [[]], 'all: lib.a(z.o)\n\techo all $?\n', files={'lib.a': b'not an archive'})
    case('archive empty file', [[]], 'all: lib.a(z.o)\n\techo all $?\n', files={'lib.a': b''})
    case('archive one byte', [[]], 'all: lib.a(z.o)\n\techo all $?\n', files={'lib.a': b'\x65'})
    case('archive one other byte', [[]], 'all: lib.a(z.o)\n\techo all $?\n', files={'lib.a': b'\x66'})
    case('archive magic only', [[]], 'all: lib.a(z.o)\n\techo all $?\n',
         files={'lib.a': mkar([])})
    case('archive 14 char member', [[]], 'all: lib.a(exactly14chars)\n\techo all $?\n',
         files={'lib.a': F(arc, mtime=OLD)})
    case('archive long member name', [[]], 'all: lib.a(exactly14charsXYZ)\n\techo all $?\n',
         files={'lib.a': F(arc, mtime=OLD)})
    case('archive empty member name', [[]], 'all: lib.a()\n\techo all $?\n',
         files={'lib.a': F(arc, mtime=OLD)})
    case('archive paren only', [[]], 'all: (x)\n\techo all $?\n', files={'lib.a': F(arc, mtime=OLD)})
    case('archive unclosed', [[]], 'all: lib.a(x.o\n\techo all $?\n', files={'lib.a': F(arc, mtime=OLD)})
    case('archive in dir', [[]], 'all: sub/lib.a(y.o)\n\techo all $?\n',
         files={'sub/lib.a': F(arc, mtime=OLD)})
    case('archive odd sizes', [[]], 'all: l.a(a) l.a(b) l.a(c)\n\techo all $?\n',
         files={'l.a': mkar([('a', b'1'), ('b', b'123'), ('c', b'12345', 600000900)])})
    case('archive truncated header', [[]], 'all: l.a(zz)\n\techo all $?\n',
         files={'l.a': mkar([('a', b'1'), ('b', b'123')])[:-40]})
    case('archive truncated data', [[]], 'all: l.a(zz)\n\techo all $?\n',
         files={'l.a': mkar([('a', b'1' * 100), ('b', b'123')])[:60]})
    case('archive negative size', [[]], 'all: l.a(zz)\n\techo all $?\n',
         files={'l.a': arcmp.struct.pack('<H', arcmp.ARMAG) + arcmp.hdr('n', -2) + b'tail' * 20})
    case('archive huge size', [[]], 'all: l.a(zz)\n\techo all $?\n',
         files={'l.a': arcmp.struct.pack('<H', arcmp.ARMAG) + arcmp.hdr('n', 0x7ffffff0) + b'tail' * 20})
    case('archive size wraps', [[]], 'all: l.a(zz)\n\techo all $?\n',
         files={'l.a': arcmp.struct.pack('<H', arcmp.ARMAG) + arcmp.hdr('n', 0xffffffe7) + b'tail' * 20})
    case('archive member time', [[]], 'all: l.a(a) l.a(b)\n\techo all $?\n',
         files={'l.a': mkar([('a', b'1', 0), ('b', b'123', 4294967295)])})
    case('archive name without NUL', [[]], 'all: l.a(abcdefghijklmn)\n\techo $?\n',
         files={'l.a': mkar([(b'abcdefghijklmn', b'full')])})

    def obj(syms, text=b'\x90' * 4, data=b'', flag=1, magic=0o407):
        sy = b''.join(arcmp.struct.pack('<8sBBH', s.encode(), t, 0, v) for s, t, v in syms)
        hdr = arcmp.struct.pack('<8H', magic, len(text), len(data), 0, len(sy), 0, 0, flag)
        return hdr + text + data + (b'' if flag else b'\0' * (len(text) + len(data))) + sy
    o1 = obj([('_main', 0x22, 0), ('_loc', 0x02, 4), ('_ext', 0x20, 0), ('_abs', 0x21, 5)])
    o2 = obj([('_foo', 0x22, 1), ('_bar', 0x23, 2)], data=b'dd', flag=0)
    oarc = mkar([('o1.o', o1, 600000100), ('o2.o', o2, 600000200)])
    case('archive entry point', [[]], 'all: l.a((_main)) l.a((_foo)) l.a((_bar)) l.a((_loc)) l.a((_ext))'
         ' l.a((_abs)) l.a((_none))\n\techo $?\n', files={'l.a': F(oarc, mtime=OLD)})
    case('archive entry point -d', [['-d']], 'all: l.a((_main))\n\techo $?\n', files={'l.a': F(oarc, mtime=OLD)})
    case('archive entry point long', [[]], 'all: l.a((_mainxxxxxx)) l.a((_ma))\n\techo $?\n',
         files={'l.a': F(oarc, mtime=OLD)})
    case('archive entry not objects', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': F(arc, mtime=OLD)})
    case('archive entry bad magic names', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': mkar([(b'abcdefghijklmn', b'x' * 40)])})
    case('archive entry bad magic short name', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': mkar([('abc', b'x' * 40)])})
    case('archive entry empty member', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': mkar([('abc', b''), ('o2.o', o2)])})
    case('archive entry other magics', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': mkar([('p.o', obj([('_foo', 0x22, 1)], magic=0o410)),
                             ('q.o', obj([('_bar', 0x22, 1)], magic=0o411)),
                             ('r.o', obj([('_baz', 0x22, 1)], magic=0o405))])})
    case('archive entry symbols huge', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': mkar([('p.o', arcmp.struct.pack('<8H', 0o407, 4, 0, 0, 0xfff0, 0, 0, 1) + b'x' * 20),
                             ('o2.o', o2)])})
    case('archive entry sizes 16 bit', [[]], 'all: l.a((_foo))\n\techo $?\n',
         files={'l.a': mkar([('p.o', arcmp.struct.pack('<8H', 0o407, 0xfff0, 0x0020, 0, 24, 0, 0, 1)
                             + b'x' * 100), ('o2.o', o2)])})
    case('archive $@ and touch', [[], ['-t']], 'l.a(x.o): x.o\n\techo $@\nx.o:\n\techo x\n\ttouch x.o\n',
         files={'l.a': F(arc, mtime=OLD)})
    case('archive with -t', [['-t']], 'all: l.a(zz)\n\techo x\n', files={'l.a': F(arc, mtime=OLD)})

    # ---- file system and names ---------------------------------------------------------
    case('name 14 chars', [[]], 'exactly14chars: s\n\techo made\n', files={'s': b''})
    case('name 15 chars', [[]], 'fifteen_chars15: s\n\techo made\n', files={'s': b''})
    case('dep name 15 chars', [[]], 'all: fifteen_chars15\n\techo x\n', files={'fifteen_chars15': b''})
    case('path with long dir', [[]], 'all: a_directory_with_a_long_name/x\n\techo x\n',
         files={'a_directory_with_a_long_name/x': b''})
    case('slash in target', [[]], 'sub/t: sub/s\n\techo made $@\n', files={'sub/s': b''})
    case('absolute target', [[]], 'all: /nonexistent/x\n\techo x\n')
    case('target is a directory', [[], ['sub']], 'sub: s\n\techo dir\n', files={'sub/x': b'', 's': b''})
    case('empty name arg', [['']], 'all:\n\techo x\n')
    case('arg with equals no colon', [['a=b=c'], ['=x'], ['a =b']], 'all:\n\techo x\n')

    return out


def corpus_cases():
    """The Makefile.mutos of tests/mutos_cc through both makes, with the
    fake tools: the sources alone, the made files newer, the made files
    older. [(name, files, steps)]"""
    out = []
    macros = ['CPP=cpp', 'C0=c0']
    for mk in sorted(os.listdir(os.path.join(ROOT, 'tests', 'mutos_cc'))):
        d = os.path.join(ROOT, 'tests', 'mutos_cc', mk)
        if not os.path.exists(os.path.join(d, 'Makefile.mutos')):
            continue
        src, gen = {}, {}
        for fn in sorted(os.listdir(d)):
            with open(os.path.join(d, fn), 'rb') as fh:
                data = fh.read()
            if fn.endswith(('.c', '.h')) or fn == 'Makefile.mutos':
                src[fn] = data
            elif fn.endswith(('.s', '.i', '.1', '.2')):
                gen[fn] = data
        f = ['-f', 'Makefile.mutos'] + macros
        steps = [f + ['-n'], f + ['-i'], f + ['-i'], f + ['-q'], f + ['-pn'],
                 f + ['-t'], f + ['clean'], f + ['-n', 'clean']]
        for variant, delta in (('sources only', None), ('made newer', 100),
                               ('made older', -100)):
            files = {k: F(v, mtime=OLD) for k, v in src.items()}
            if delta is not None:
                files.update({k: F(v, mtime=OLD + delta) for k, v in gen.items()})
            out.append(('%s, %s' % (mk, variant), files, steps))
    return out


def _case_one(job):
    (name, files, steps), port, keep = job
    return name, [(str(i), k, d) for i, k, d in
                  compare_seq(steps, files, port, keep, name)]


def _run_one(job):
    args, port, keep = job
    cwd = os.getcwd()
    files = {}
    for dp, dns, fns in os.walk(cwd):
        for fn in fns:
            p = os.path.join(dp, fn)
            with open(p, 'rb') as fh:
                files[os.path.relpath(p, cwd)] = F(fh.read(), int(os.stat(p).st_mtime),
                                                   os.stat(p).st_mode & 0o7777)
    return ' '.join(args), [(str(i), k, d) for i, k, d in
                            compare_seq([args], files, port, keep, ' '.join(args))]


def main(argv):
    jobs, port, show, keep = os.cpu_count() or 1, PORT, 0, None
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-j', '--port', '--show', '--keep') and (not args or args[0] in ('cases', 'corpus')):
            if a == '-j':
                jobs = int(argv[i + 1])
            elif a == '--port':
                port = os.path.abspath(argv[i + 1])
            elif a == '--show':
                show = int(argv[i + 1])
            else:
                keep = os.path.abspath(argv[i + 1])
            i += 2
        else:
            args.append(a)
            i += 1
    if not args or args[0] not in ('cases', 'corpus', 'run'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    if args[0] in ('cases', 'corpus'):
        work = [(c, port, keep) for c in (cases() if args[0] == 'cases'
                                          else corpus_cases())]
        fn = _case_one
    else:
        work, fn = [(args[1:], port, keep)], _run_one
    counts, bad, n = {}, [], 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for name, results in ex.map(fn, work):
            for step, kind, detail in results:
                n += 1
                counts[kind] = counts.get(kind, 0) + 1
                label = '%s [step %s]' % (name, step)
                detail = detail.encode('utf-8', 'backslashreplace').decode()
                if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    bad.append('%s: %s: %s' % (label, kind, detail))
                elif (kind != 'same' and show) or args[0] == 'run':
                    print('%s: %s%s' % (label, kind, ': ' + detail if detail else ''))
    for b in bad[:show or 40]:
        print(b)
    print('make port against the original (%s): %d runs, %s'
          % (args[0], n, ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
