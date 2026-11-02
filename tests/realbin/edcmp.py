#!/usr/bin/env python3
"""edcmp.py - mutos_ed and mutos_em (step 9), the ports of the real ed and
em, against the originals.

The original MUTOS 1700 editors (mutos/mutos_bin/ed.golden.base64.txt,
em.golden.base64.txt) run under mutosemu.py; the ports (src/mutos_ed/,
src/mutos_em/) run on the host. A case is a directory of files, a command
line and the script on the standard input - a file, so that the editors'
seek to its end after an error works as on MUTOS -, run in two copies of the
directory, each with a MUTOS root of its own (tmp/ for the buffer's
temporary file /tmp/eXXXXX). The standard output, the standard error (em's
'h' writes there), the exit status and both trees - every file's name, bytes
and mode; the root's names with the process number in the temporary file's
name taken out - must be the same.

A refusal of the port (exit status 125 and a last message ending in
"refused") counts apart: what it wrote to the standard output must be a
prefix of what the original wrote. The original that runs out of
instructions ("endless") with the port refusing is a case where the
original loops for ever (an em 'z' that reads its answers from a standard
error at its end, an empty \\(\\) repeated by \\1*).

What the emulator cannot answer is not in the cases: fork(), pipe() and
dup() ('!' of both, em's '|'), the terminal (ioctl() is always "not a
tty": em's prompt, log and the -x key need one), signals.

Usage:
    edcmp.py cases [TOOL ...] [-j N] [--show N] [--keep DIR]
        the hand-written cases of ed and em (or the tools named)
    edcmp.py run TOOL [ARG...] < script
        one command line, in a copy of the current directory

As a module: Case, compare(), run_orig(), run_port() - edfuzz.py is built
on them.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import io
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

MAX_INSNS = 200_000_000
OUT_LIMIT = 1 << 20
PORT_CPU = 20
TOOLS = ('ed', 'em')
PORTS = {t: os.path.join(ROOT, 'src', 'mutos_' + t, 'mutos_' + t) for t in TOOLS}


class Case:
    """name, tool, command-line arguments, the script (bytes), the files
    (name -> bytes) of the directory; pipe: the script comes through a pipe,
    which the seek after an error cannot skip."""

    def __init__(self, name, tool, args, script, files=None, pipe=False):
        self.name, self.tool, self.args = name, tool, list(args)
        self.script, self.files, self.pipe = script, dict(files or {}), pipe


class _Pipe(io.BytesIO):
    """A standard input that cannot be sought (the emulator says ESPIPE)."""

    def seek(self, *a):
        if a == (0,) or a == (0, 0):
            return super().seek(*a)
        raise io.UnsupportedOperation('a pipe')


class _Capped(io.BytesIO):
    """An output that stops the run past OUT_LIMIT bytes."""

    def write(self, b):
        if self.tell() + len(b) > OUT_LIMIT:
            raise _Limit()
        return super().write(b)


class _Limit(Exception):
    pass


def run_orig(tool, args, cwd, root, stdin=b'', max_insns=MAX_INSNS, pipe=False):
    """(status or None, stdout, stderr, why it did not finish)."""
    out, err = _Capped(), _Capped()
    emu = mutosemu.Emu(mutosemu.load_image(tool), [tool] + list(args), cwd=cwd,
                       stdin=(_Pipe if pipe else io.BytesIO)(stdin), stdout=out,
                       stderr=err, root=root,
                       eof_limit=20000)
    why, st = '', None
    try:
        st = emu.run(max_insns)
    except _Limit:
        why = 'endless: output past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why or 'for ever' in why:
            why = 'endless: ' + why
    return st, out.getvalue(), err.getvalue(), why


def _limits():
    import resource
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def run_port(tool, args, cwd, root, stdin=b'', pipe=False):
    """(status or None, stdout, stderr)."""
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    argv = [PORTS[tool]] + [os.fsdecode(a.encode('latin1')) for a in args]
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe, \
            tempfile.TemporaryFile() as fi:
        fi.write(stdin)
        fi.seek(0)
        if pipe:
            r = subprocess.run(argv, cwd=cwd, input=stdin, stdout=fo, stderr=fe,
                               env=env, preexec_fn=_limits)
        else:
            r = subprocess.run(argv, cwd=cwd, stdin=fi, stdout=fo, stderr=fe,
                               env=env, preexec_fn=_limits)
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read()[:OUT_LIMIT + 1], fe.read()
    st = r.returncode
    return (None if st < 0 else st), out, err


def snapshot(d):
    """name -> (mode, bytes) of every file under d (directories: mode, None)."""
    out = {}
    for base, dirs, files in os.walk(d):
        for n in dirs + files:
            p = os.path.join(base, n)
            rel = os.path.relpath(p, d)
            st = os.lstat(p)
            if stat.S_ISDIR(st.st_mode):
                out[rel] = (stat.S_IMODE(st.st_mode), None)
            else:
                with open(p, 'rb') as f:
                    out[rel] = (stat.S_IMODE(st.st_mode), f.read())
    return out


def _root_view(r):
    """The root's files, the temporary file's process number taken out."""
    out = {}
    for k, v in snapshot(r).items():
        out[re.sub(r'^tmp/e[0-9a-z]\d{4}$', 'tmp/eNNNNN', k)] = v
    return sorted(out.items())


def _refused(st, err):
    lines = err.rstrip(b'\n').split(b'\n')
    return st == 125 and lines and lines[-1].endswith(b'refused')


def judge(ost, oout, oerr, why, pst, pout, perr, do, dp, ro, rp):
    """(kind, detail): same, refused, refused-endless, DIFFERENT, BAD-REFUSAL."""
    if _refused(pst, perr):
        what = perr.rstrip(b'\n').split(b'\n')[-1].decode('latin1')
        if why.startswith('endless'):
            return 'refused-endless', what
        if not oout.startswith(pout):
            return 'BAD-REFUSAL', '%s: output %r is not a prefix of %r' % (
                what, pout[-200:], oout[:len(pout) + 50][-250:])
        return 'refused', what
    if why:
        return 'DIFFERENT', 'the original did not finish (%s), the port: exit %s' % (why, pst)
    if ost != pst:
        return 'DIFFERENT', 'exit %s, original %s' % (pst, ost)
    if oout != pout:
        n = next((i for i in range(min(len(oout), len(pout))) if oout[i] != pout[i]),
                 min(len(oout), len(pout)))
        return 'DIFFERENT', 'stdout at %d: %r, original %r' % (
            n, pout[max(0, n - 60):n + 60], oout[max(0, n - 60):n + 60])
    if oerr != perr:
        return 'DIFFERENT', 'stderr %r, original %r' % (perr[:300], oerr[:300])
    so, sp = snapshot(do), snapshot(dp)
    if so != sp:
        bad = sorted(k for k in set(so) | set(sp) if so.get(k) != sp.get(k))
        return 'DIFFERENT', 'tree: %s' % ', '.join(bad[:8])
    if _root_view(ro) != _root_view(rp):
        return 'DIFFERENT', 'root: %r, original %r' % (
            [k for k, _ in _root_view(rp)], [k for k, _ in _root_view(ro)])
    return 'same', ''


def _prepare(tmp, files):
    do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
    ro, rp = os.path.join(tmp, 'orig-root'), os.path.join(tmp, 'port-root')
    for d in (do, dp):
        os.makedirs(d)
        for n, b in files.items():
            p = os.path.join(d, n)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(b)
    for r in (ro, rp):
        os.makedirs(os.path.join(r, 'tmp'))
    return do, dp, ro, rp


def compare(c, keep=None, max_insns=MAX_INSNS):
    """(kind, detail) of a Case."""
    with tempfile.TemporaryDirectory(prefix='edcmp') as tmp:
        do, dp, ro, rp = _prepare(tmp, c.files)
        ost, oout, oerr, why = run_orig(c.tool, c.args, do, ro, c.script, max_insns, c.pipe)
        pst, pout, perr = run_port(c.tool, c.args, dp, rp, c.script, c.pipe)
        kind, detail = judge(ost, oout, oerr, why, pst, pout, perr, do, dp, ro, rp)
        if keep and kind in ('DIFFERENT', 'BAD-REFUSAL'):
            os.makedirs(keep, exist_ok=True)
            k = tempfile.mkdtemp(dir=keep, prefix='case')
            shutil.copytree(do, os.path.join(k, 'orig'))
            shutil.copytree(dp, os.path.join(k, 'port'))
            with open(os.path.join(k, 'script'), 'wb') as f:
                f.write(c.script)
            with open(os.path.join(k, 'args'), 'w', errors='backslashreplace') as f:
                f.write('%s %s\n%s: %s\n' % (c.tool, ' '.join(c.args), kind, detail))
        return kind, detail


# ---- the cases ---------------------------------------------------------------------

def _kernel():
    """The kernel's conf/Makefile (tests/mutos_ed/kernel/) and the eddep
    scripts its targets write."""
    with open(os.path.join(ROOT, 'tests', 'mutos_ed', 'kernel', 'conf.Makefile'), 'rb') as f:
        mk = f.read()
    sc = {}
    for t in ('7100', '7150', '1834'):
        sc[t] = (b'g/^### [17][18][035][04] only:$/+,+4s/^[^#]/# &/\n'
                 b'g/### [17][18][035][04] only$/s/^  /# /\n'
                 b'g/^### ' + t.encode() + b' only:$/+,+4s/^# //\n'
                 b'g/### ' + t.encode() + b' only$/s/^# /  /\nw\n')
    for o, s in (('AMX', 'DRIVER'), ('V24', 'DRIVER'), ('IFSS', 'DRIVER'),
                 ('MSDOS', 'CONFIG'), ('LOAD86', 'CONFIG')):
        sc[o] = b'g/^%s =/s/%s *//g\ns/ *$/ %s/\nw\n' % (s.encode(), o.encode(), o.encode())
        sc['no' + o] = b'g/^%s =/s/%s *//g\nw\n' % (s.encode(), o.encode())
    for o in ('INSTRM', 'SYSLOCK'):
        sc[o] = b'g/^FLAGS =/s/-D%s *//g\ns/ *$/ -D%s/\nw\n' % (o.encode(), o.encode())
        sc['no' + o] = b'g/^FLAGS =/s/-D%s *//g\nw\n' % o.encode()
    sc['depend'] = b'/^# DO NOT DELETE THIS LINE/+2,$d\n$r makedep\nw\n'
    return mk, sc


def _mch():
    with open(os.path.join(ROOT, 'tests', 'mutos_cpp', 'c', 'mch.i.golden'), 'rb') as f:
        mchi = f.read()
    with open(os.path.join(ROOT, 'tests', 'mutos_ed', 'kernel', 'fakeawk'), 'rb') as f:
        fake = f.read()
    return mchi, fake


TEXT = (b'one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\n'
        b'\tTabbed line\nline with spaces   \nabcabcabc\naaa bbb ccc\n'
        b'x\n\nlast line\n')


def common(tool):
    """Cases that are the same commands for ed and em."""
    out = []

    def add(name, script, args=('t',), files=None):
        f = {'t': TEXT} if files is None else files
        out.append(Case('%s/%s' % (tool, name), tool, args, script, f))

    add('print', b'1,$p\n$=\n=\n.=\n3\n\n\n-\n--p\n+2p\n/abc/\n?t?\n//\n??\n1;+2p\nq\n')
    add('list', b'1,$l\n11l\n11p\n11n\nq\n')
    add('delete', b'2,4d\n.=\n$d\n1,$p\nw\nq\n')
    add('append', b'a\nnew 1\nnew 2\n.\n0a\nat 0\n.\n$a\nat end\n.\n1,$p\nw out\nq\n')
    add('insert', b'3i\nins\n.\n1i\nfirst\n.\n0i\n.\n1,$p\nQ\n')
    add('change', b'2,3c\nchanged\n.\n1,$p\n$c\nlast\n.\nw\nq\n')
    add('subst', b's/one/ONE/\n1,$s/e/E/g\n1,$p\ns/x/y/\ng/a/s//A/gp\n'
        b's/\\(a\\)\\(b\\)/\\2\\1/\n13p\n13s/abc/[&]/gp\n13s/\\[/\\\n/p\n1,$p\nw\nq\n')
    add('subst-fail', b'1,$s/nomatch/x/\n1,$p\n')
    add('global', b'g/e/p\nv/e/p\ng/^t/d\n1,$p\ng/o/s/o/0/g\n1,$p\ng/x/a\\\nadded\n1,$p\nQ\n')
    add('gdelete', b'g/./d\n=\na\nfresh\n.\nw\nq\n')
    add('move', b'1,3m$\n1,$p\n$-2,$m0\n1,$p\n2m1\n2m2\n1,3m2\n4,5m3\n1,$p\nw\nq\n')
    add('transfer', b'1,3t$\n1t0\n1,$p\n5,6t5\n1,$p\nw\nq\n')
    add('join', b'1,3j\n1,$p\nj\n1,$p\n5j\n.p\n$j\n0j\nw\nq\n')
    add('mark', b'3ka\n5kb\n\'a,\'bp\n\'ad\n\'ap\n\'b=\n\'zp\nq\n')
    add('undo', b's/o/O/\nu\np\n2s/t/T/\n3u\n2u\np\nQ\n')
    add('write', b'w\nw t2\n2,3w t3\n2,3W t3\nr t3\n1,$p\nf\nf other\nf\nw\nq\n')
    add('read', b'0r t\n$r t\n=\nr nofile\n=\nQ\n')
    add('edit', b'e\nE t\n1,$p\ne t\nf\nQ\n')
    add('edit-changed', b'1d\ne t\ne t\nq\n')
    add('quit-changed', b'1d\nq\nq\n')
    add('eof-changed', b'1d\n')
    add('address-wrap', b'1+65536p\n65537p\n2-65536p\n1+32768p\n.+65535p\n=\n')
    add('address-big', b'99p\n1,99p\n$+1p\n0p\n-1p\n')
    add('address-errors', b'1,2,3p\n3,2p\n.,.d\n1,1,p\n,p\n;p\n')
    add('error-seek', b'1p\nxyz\n2p\n')
    add('pattern-classes', b'g/[a-c]/p\ng/[^a-z]/p\ng/[]x]/p\ng/[a-]/p\n'
        b'g/^[0-9]*$/p\ng/c*/p\ng/.*e$/p\ng/^$/p\n/\\./p\nq\n')
    add('pattern-backref', b'g/\\(a\\)\\1/p\ng/\\(abc\\)\\1*/p\ns/\\(a\\)\\(b\\)\\(c\\)\\(a\\)\\(b\\)/[\\5\\4\\3\\2\\1]/\n'
        b'13p\ng/\\(a/p\ng/a\\)/p\ng/\\1/p\n')
    add('pattern-errors', b'/\\(\\(\\(\\(\\(\\(a\\)\\)\\)\\)\\)\\)/p\n/[abc/p\n/a**/p\n/*a/p\n')
    add('pattern-empty-group', b'/\\(\\)\\1*x/p\n', files={'t': b'abc\nxyz\n'})
    add('pattern-long', b'/' + b'a' * 140 + b'/p\n')
    add('long-line', b'a\n' + b'x' * 600 + b'\n.\n=\n', args=())
    add('long-file-line', b'1,$p\n', files={'t': b'y' * 700 + b'\n' + b'short\n'})
    add('no-newline-at-end', b'1,$p\n$=\n', files={'t': b'a\nb\nlast without newline'})
    add('nul-bytes', b'1,$p\n', files={'t': b'a\0b\nc\0\0d\n'})
    add('eight-bit', b'1,$p\n1,$l\n$=\n', files={'t': b'plain\n\xe4\xf6\xfc\n\x80x\n'})
    add('control-chars', b'1,$l\n', files={'t': b'a\x01b\x7fc\x08d\tz\n' + b'w' * 100 + b'\n'})
    add('empty-file', b'1,$p\n=\na\nx\n.\nw\nq\n', files={'t': b''})
    add('no-file', b'1,$p\n=\nr t\n=\nw\nq\n', args=())
    add('missing-file', b'=\nw\nq\n', args=('nofile',))
    add('dash', b'1,$p\nw\nq\n', args=('-', 't'))
    add('dash-q', b'1,$p\nq\n', args=('-q', 't'))
    add('bad-option', b'1p\nq\n', args=('-z', 't'))
    add('filename-long', b'w ' + b'n' * 70 + b'\nf\nq\n')
    add('filename-overflow', b'w ' + b'n' * 1200 + b'\nq\n')
    add('filename-space', b'w a b\nw\nf\nq\n')
    add('encrypt', b'x\n1p\ne t\nq\n')
    add('many-lines', b'1,$t$\n1,$t$\n1,$t$\n1,$t$\n1,$t$\n1,$t$\n1,$t$\n1,$t$\n$=\n'
        b'g/one/d\n$=\nw\nq\n')
    big = b''.join(b'line %05d %s\n' % (i, b'x' * (i % 50)) for i in range(4000))
    add('big-file', b'$=\n2000,2003p\ng/^line 01/d\n$=\n1,$s/x/y/g\nw\nq\n', files={'t': big})
    huge = b''.join(b'%06d %s\n' % (i, b'z' * 60) for i in range(2200))
    add('temp-overflow', b'1,$t$\n$=\n', files={'t': huge})
    return out


def piped(cs):
    """The cases again, the script through a pipe."""
    return [Case(c.name + '|pipe', c.tool, c.args, c.script, c.files, True) for c in cs]


def ed_cases():
    out = common('ed')
    out += piped(out)
    mk, sc = _kernel()
    for t, s in sorted(sc.items()):
        files = {'Makefile': mk}
        if t == 'depend':
            files['makedep'] = b'a.o: a.h\n\ttouch $@\nb.o: b.h\n\ttouch $@\n'
        out.append(Case('ed/kernel-' + t, 'ed', ['-', 'Makefile'], s, files))
    # the kernel's Makefile switched three times, without '-'
    out.append(Case('ed/kernel-chain', 'ed', ['Makefile'], sc['7150'][:-2] + sc['1834'][:-2]
                    + sc['AMX'] + b'q\n', {'Makefile': mk}))
    return out


def em_cases():
    out = common('em')
    out += piped(out)

    def add(name, script, args=('t',), files=None):
        f = {'t': TEXT} if files is None else files
        out.append(Case('em/%s' % name, 'em', args, script, f))

    mchi, fake = _mch()
    out.append(Case('em/kernel-mch', 'em', ['mch.i'], fake, {'mch.i': mchi}))
    add('silent', b'1,$p\nw\nq\n', args=('-s', 't'))
    add('eflag', b'1,$p\n2d\nf\nw\ne x\nr t\n$=\nq\n', args=('-e', 't'))
    add('eflag-nofile', b'1p\n', args=('-e',))
    add('lflag', b'1,$p\nq\n', args=('-l', 't'))
    add('recover-none', b'1p\n', args=('-r',))
    add('recover-missing', b'1p\n', args=('-r', 'nolog'))
    add('prompt-toggle', b'1p\n>\n2p\n<\n3p\n')
    add('subst-count', b'1,$s/e/E/gn\n1,$s/o/O/n\n1,$s/x/X/ng\n1,$p\nQ\n')
    add('newline-pattern', b'/t\n/\n?\n.p\ns/e\n')
    add('screenful', b'3"\n"\n&\n1&\n$"\n10%\n%\n1%\n.=\n')
    add('screenful-errors', b'$"\n')
    add('help', b'h\nhkeys\nh screen\n1p\n')
    add('linebreak', b'b+\nb-\nb\nbx\n1p\n', args=('t.n',), files={'t.n': TEXT})
    add('sure-y', b'1d\nq\ny\n')
    add('sure-n', b'1d\nq\nn\n1p\n')
    add('sure-eof', b'1d\nq\n')
    add('sure-other', b'1d\ne t\nxyz\n')
    add('zsubst', b'z/o/0/\n')
    add('zsubst-match', b'1z/o/0/\n')
    add('eflag-quit', b'2d\nq\n', args=('-e', 't'))
    add('eflag-name', b'r t\nw t\n', args=('-e', 't'))
    add('recover', b'3p\nq\n', args=('-r', 'log'), files={'t': TEXT, 'log': b'r t\n2p\n2d\n'})
    add('recover-dot', b'p\nQ\n', args=('-r', 'log'), files={'t': TEXT, 'log': b'r t\n5\n'})
    add('recover-arg', b'1p\n', args=('-r', 'log', 't'), files={'t': TEXT, 'log': b'1p\n'})
    add('eight-bit-commands', b'a\n\xe4\xf6\xfc \x80x\n.\n1,$l\ns/\xf6/Q/\n1,$l\ns/Q/\xdf/\n1,$l\nw\nq\n')
    add('eof-byte', b'1p\n\xff2p\n')
    add('nul-command', b'1p\n\x00\n2p\n')
    add('o-screen', b'o\n')
    return out


def cases(tool):
    return ed_cases() if tool == 'ed' else em_cases()


def run_case(c, keep=None):
    return c.name, compare(c, keep)


def summarize(results, show=20):
    from collections import Counter
    cnt = Counter()
    why = Counter()
    bad = []
    for name, (kind, detail) in results:
        cnt[kind] += 1
        if kind in ('refused', 'refused-endless'):
            why[(kind, detail.split(' - ')[0][:90])] += 1
        if kind in ('DIFFERENT', 'BAD-REFUSAL'):
            bad.append((name, kind, detail))
    for k, v in sorted(cnt.items()):
        print('  %-16s %d' % (k, v))
    for (k, d), v in sorted(why.items()):
        print('    %-16s %3d  %s' % (k, v, d))
    for name, kind, detail in bad[:show]:
        print('%s %s: %s' % (name, kind, detail[:500]))
    if len(bad) > show:
        print('... and %d more' % (len(bad) - show))
    return len(bad)


def main(argv):
    if not argv or argv[0] not in ('cases', 'run'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    if argv[0] == 'run':
        tool, args = argv[1], argv[2:]
        files = {}
        for n in os.listdir('.'):
            if os.path.isfile(n):
                with open(n, 'rb') as f:
                    files[n] = f.read()
        c = Case('run', tool, args, sys.stdin.buffer.read(), files)
        with tempfile.TemporaryDirectory(prefix='edcmp-run') as tmp:
            do, dp, ro, rp = _prepare(tmp, c.files)
            o = run_orig(tool, args, do, ro, c.script)
            p = run_port(tool, args, dp, rp, c.script)
            print('original: exit %s, why %r\n%s\n--- stderr\n%s' % (
                o[0], o[3], o[1].decode('latin1'), o[2].decode('latin1')))
            print('port:     exit %s\n%s\n--- stderr\n%s' % (
                p[0], p[1].decode('latin1'), p[2].decode('latin1')))
            print(judge(o[0], o[1], o[2], o[3], p[0], p[1], p[2], do, dp, ro, rp))
        return 0
    jobs, show, keep, tools = 1, 20, None, []
    a = argv[1:]
    while a:
        if a[0] == '-j':
            jobs, a = int(a[1]), a[2:]
        elif a[0] == '--show':
            show, a = int(a[1]), a[2:]
        elif a[0] == '--keep':
            keep, a = a[1], a[2:]
        else:
            tools, a = tools + [a[0]], a[1:]
    allc = []
    for t in tools or TOOLS:
        allc += cases(t)
    print('%d cases' % len(allc))
    t0 = time.time()
    if jobs > 1:
        with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
            results = list(ex.map(run_case, allc, [keep] * len(allc)))
    else:
        results = [run_case(c, keep) for c in allc]
    nbad = summarize(results, show)
    print('%.0f s' % (time.time() - t0))
    return 1 if nbad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
