#!/usr/bin/env python3
"""utilcmp.py - mutos_size, mutos_strip, mutos_nm, mutos_tsort and
mutos_lorder (step 8), the ports of the real size, strip, nm, tsort and
lorder, against the originals.

The original MUTOS 1700 tools (mutos/mutos_bin/<tool>.golden.base64.txt) run
under mutosemu.py; the ports (src/mutos_<tool>/) run on the host. A case is
a directory of files and a sequence of steps - a command line (with the
standard input it reads, if any), or a change of a file - run in two
copies of the directory, each with a MUTOS root of its own (an empty tmp/ for
strip's temporary file, bin/ for lorder's tools). After every step the
messages, the exit status and both trees - every file's name, bytes and mode
- must be the same. nm and tsort write to the standard error as well as to the
standard output: they are run a second time with both on one pipe, to compare
the order of the messages too (the stream libc.a's stdio buffers, the other
it does not).

lorder is a shell script: the original script runs under the host's sh with
the emulated original nm - a wrapper in the case's root - and the port's
script runs with mutos_nm; sed, sort and join are the host's, with LC_ALL=C.
The step ("pipe", lorder-args, tsort-args) runs "lorder ... | tsort ..." with
the emulated original tsort, resp. mutos_tsort.

A refusal of the port (exit status 125 and a last message ending in
"refused") counts apart: what it wrote to the standard output must be a
prefix of what the original wrote, and the sequence ends there. The
emulated original cannot answer the opening of a directory (the host says
ENOENT where MUTOS reads the directory's entries); a case that does it only
shows that the port refuses.

Usage:
    utilcmp.py cases [TOOL ...] [-j N] [--show N] [--keep DIR]
        the hand-written cases of the tools named (all of them without a
        name): the compiled objects, libc.a and its members, executables
        linked from them, with every option, damaged files, the messages
    utilcmp.py run [--stdin] TOOL [ARG...]
        one command line, in a copy of the current directory (the standard
        input is empty, or the host's with --stdin)

As a module: Case, F, compare_seq(), corpus() - utilfuzz.py is built on them.

Requires the Python module "unicorn" (pip install unicorn).
"""
import base64
import concurrent.futures
import errno
import glob
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
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arcmp          # noqa: E402
import mutosemu       # noqa: E402
from arcmp import F, place, snapshot, settle_times, diff_at, _same_trees  # noqa: E402
from arcmp import _Capped, _Limit, _FullFile  # noqa: E402

ROOT = arcmp.ROOT
MAX_INSNS = 400_000_000
OUT_LIMIT = 1 << 20
PORT_CPU = 20
TOOLS = ('size', 'strip', 'nm', 'tsort', 'lorder')
STATELESS = ('size', 'nm', 'tsort')     # tools that change no file: a refusal does not end a case
PORTS = {t: os.path.join(ROOT, 'src', 'mutos_' + t, 'mutos_' + t) for t in TOOLS}
PORTS['lorder'] = os.path.join(ROOT, 'src', 'mutos_lorder', 'mutos_lorder')
EXEC = '8H'

# ---- the files -----------------------------------------------------------------


def _b64(path):
    with open(path, 'rb') as f:
        return base64.b64decode(f.read())


_CORPUS = None


def corpus():
    """name -> bytes of every object and archive of the tree: the 76 .o goldens of
    tests/mutos_as/, crt0.o, libc.a and its 167 members."""
    global _CORPUS
    if _CORPUS is not None:
        return _CORPUS
    out = {}
    out['crt0.o'] = _b64(os.path.join(ROOT, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt'))
    out['libc.a'] = _b64(os.path.join(ROOT, 'tests', 'mutos1700_libc', 'libc.a.base64.txt'))
    for p in sorted(glob.glob(os.path.join(ROOT, 'tests', 'mutos1700_libc', '*.o.base64.txt'))):
        out['lc_' + os.path.basename(p)[:-len('.base64.txt')]] = _b64(p)
    for p in sorted(glob.glob(os.path.join(ROOT, 'tests', 'mutos_as', '*', '*.o.golden'))):
        out['g_' + os.path.basename(p)[:-len('.golden')]] = open(p, 'rb').read()
    _CORPUS = out
    return out


def struct_pack_magic():
    return struct.pack('<H', 0o177545)


def header(magic=0o407, text=0, data=0, bss=0, syms=0, entry=0, unused=0, flag=0):
    return struct.pack('<8H', magic, text, data, bss, syms, entry, unused, flag)


def sym(name, typ, value):
    if isinstance(name, str):
        name = name.encode('latin1')
    return name[:8].ljust(8, b'\0') + struct.pack('<HH', typ & 0xffff, value & 0xffff)


def aout(syms=(), text=b'', data=b'', bss=0, flag=1, magic=0o407, reloc=False):
    """An a.out file: header, text, data, (the relocation, zeros), the symbols."""
    body = text + data
    if reloc:
        flag &= ~1
        body += b'\0' * (2 * len(body))
    st = b''.join(syms)
    return header(magic, len(text), len(data), bss, len(st), 0, 0, flag) + body + st


def linked():
    """Executables linked from corpus objects by the port of ld, with options that
    give the headers every shape: name -> bytes. {} without mutos_ld."""
    ld = os.path.join(ROOT, 'src', 'mutos_ld', 'mutos_ld')
    root = os.path.join(ROOT, 'mutos_root')
    if not os.path.exists(ld) or not os.path.isdir(root):
        return {}
    cor = corpus()
    out = {}
    with tempfile.TemporaryDirectory(prefix='utilcmp-ld') as tmp:
        for tag in ('g_01_floatbas.o', 'g_fltopen.o', 'g_myhello.o'):
            if tag not in cor:
                continue
            open(os.path.join(tmp, 'x.o'), 'wb').write(cor[tag])
            for opt in ([], ['-i'], ['-n'], ['-s'], ['-x'], ['-X'], ['-i', '-s']):
                p = subprocess.run([ld] + opt + ['-o', 'a.out', os.path.join(root, 'lib', 'crt0.o'),
                                                  'x.o', os.path.join(root, 'lib', 'libc.a')],
                                   cwd=tmp, capture_output=True,
                                   env=dict(os.environ, MUTOS_ROOT=root))
                a = os.path.join(tmp, 'a.out')
                if p.returncode == 0 and os.path.exists(a):
                    out['ex_%s%s' % (tag[2:-2], ''.join(opt))] = open(a, 'rb').read()
                    os.unlink(a)
    return out


# ---- the two runs -------------------------------------------------------------

_WRAP = '#!/bin/sh\nexec python3 %s --root %s %s "$@"\n'
_ENV = {'LC_ALL': 'C', 'PATH': '/usr/bin:/bin'}


def _orig_script(tool):
    """The original lorder's text."""
    return _b64(os.path.join(ROOT, 'mutos', 'mutos_bin', 'lorder.golden.base64.txt'))


def prepare_root(root, orig, bins=False):
    """tmp/ of a MUTOS root (strip's temporary file); with bins (the lorder
    cases) bin/ too: for the original side the wrappers that run the emulated
    originals (nm, tsort), for the port's side links to the ports."""
    os.makedirs(os.path.join(root, 'tmp'), exist_ok=True)
    if not bins:
        return
    os.makedirs(os.path.join(root, 'bin'), exist_ok=True)
    for t in ('nm', 'tsort'):
        p = os.path.join(root, 'bin', t)
        if orig:
            with open(p, 'w') as f:
                f.write(_WRAP % (os.path.join(HERE, 'mutosemu.py'), root, t))
            os.chmod(p, 0o755)
        else:
            os.symlink(PORTS[t], p)


def _emu_run(tool, args, cwd, root, stdin, full, merged, max_insns):
    out, err = (_FullFile() if full else _Capped()), _Capped()
    if merged:
        err = out
    emu = mutosemu.Emu(mutosemu.load_image(tool), [tool] + list(args), cwd=cwd,
                       stdin=io.BytesIO(stdin), stdout=out, stderr=err, root=root)
    why, st = '', None
    try:
        st = emu.run(max_insns)
    except _Limit:
        why = 'endless: messages past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why:
            why = 'endless: ' + why
    o = b'' if full else out.getvalue()
    return st, o, (b'' if merged else err.getvalue()), why


def _limits():
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def _host_run(argv, cwd, env, stdin, full, merged):
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe, \
            tempfile.TemporaryFile() as fi:
        fi.write(stdin)
        fi.seek(0)
        if full:
            fo = open('/dev/full', 'wb')
        r = subprocess.run(argv, cwd=cwd, stdin=fi, stdout=fo,
                           stderr=(subprocess.STDOUT if merged else fe),
                           env=env, preexec_fn=_limits)
        if full:
            fo.close()
            out = b''
        else:
            fo.seek(0)
            out = fo.read()[:OUT_LIMIT + 1]
        fe.seek(0)
        err = fe.read()
    st = r.returncode
    if st == -signal.SIGXFSZ:
        return 'XFSZ', out, err
    return (None if st < 0 else st), out, err


def run_orig(tool, args, cwd, root, stdin=b'', full=False, merged=False,
             max_insns=MAX_INSNS):
    """(status or None, stdout, stderr, why it did not finish)."""
    if tool == 'lorder':
        return _lorder_orig(args, cwd, root, stdin, full, merged)
    return _emu_run(tool, args, cwd, root, stdin, full, merged, max_insns)


def run_port(tool, args, cwd, root, stdin=b'', full=False, merged=False, port=None):
    """(status, stdout, stderr); status None for a run a limit stopped."""
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    env['TZ'] = 'UTC'
    args = [os.fsdecode(a.encode('latin1')) for a in args]
    argv = [port or PORTS[tool]] + list(args)
    if tool == 'lorder':
        env.update(_ENV)
        env['PATH'] = os.path.join(root, 'bin') + ':' + _ENV['PATH']
        argv = ['sh', port or PORTS[tool]] + list(args)
    return _host_run(argv, cwd, env, stdin, full, merged)


def _lorder_orig(args, cwd, root, stdin, full, merged):
    """The original script, sh, the emulated nm."""
    script = os.path.join(root, 'lorder.orig')
    if not os.path.exists(script):
        with open(script, 'wb') as f:
            f.write(_orig_script('lorder'))
    env = dict(os.environ)
    env.update(_ENV)
    env['PATH'] = os.path.join(root, 'bin') + ':' + _ENV['PATH']
    env['MUTOS_ROOT'] = root
    args = [os.fsdecode(a.encode('latin1')) for a in args]
    st, out, err = _host_run(['sh', script] + args, cwd, env, stdin, full, merged)
    return st, out, err, ''


# ---- judging --------------------------------------------------------------------

def judge(tool, ost, oout, oerr, why, pst, pout, perr, do, dp, ro, rp):
    """(kind, detail): 'same', 'refused', 'orig-fails', 'endless',
    'DIFFERENT', 'BAD-REFUSAL', 'limits'. The trees of both directories and
    both roots are compared here."""
    if pst == 'XFSZ':
        return 'limits', 'the port wrote past the file size limit'
    if tool == 'lorder':
        # the host's join names the script's working files, which carry the process number
        oerr, perr = (re.sub(rb'\b\d+(symref|symdef|symst)\b', rb'PID\1', e) for e in (oerr, perr))
    lines = perr.decode('latin1').rstrip('\n').split('\n')
    prefixes = (tool + ': ',) if tool != 'lorder' else ('nm: ', 'lorder: ', 'tsort: ')
    refused = (pst == 125 and lines[-1].startswith(prefixes)
               and lines[-1].endswith('refused'))
    if ost is None and pst is None and why.startswith('endless'):
        n = min(len(pout), len(oout))
        if pout[:n] == oout[:n]:
            return 'endless', why
        return 'DIFFERENT', 'both endless (%s), the messages differ: %s' % (
            why, diff_at(pout, oout))
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original %s' % (
            'exit %d' % ost if ost is not None else why)
    if refused:
        ok = oout.startswith(pout)
        if ost is None:
            return ('orig-fails' if ok else 'BAD-REFUSAL'), 'the original does not ' \
                'finish (%s); the port refused: %s' % (why, lines[-1])
        return ('refused' if ok else 'BAD-REFUSAL'), lines[-1]
    if ost is None:
        return 'DIFFERENT', 'the original does not finish (%s); the port exit %s' % (why, pst)
    parts = []
    if pst != ost:
        parts.append('exit %s, original %d' % (pst, ost))
    if pout != oout:
        parts.append('messages %r, original %r' % (pout[:300], oout[:300]))
    if perr != oerr:
        parts.append('stderr %r, original %r' % (perr[:300], oerr[:300]))
    _same_trees(snapshot(dp), snapshot(do), parts)
    if parts:
        return 'DIFFERENT', '; '.join(parts)
    return 'same', ''


def _tree_roots(tool, ro, rp):
    """The two roots' tmp/ must agree (a temporary file left behind); the
    wrappers in bin/ and the script of the original lorder are the harness's."""
    ta = snapshot(os.path.join(rp, 'tmp'))
    tb = snapshot(os.path.join(ro, 'tmp'))
    parts = []
    _same_trees(ta, tb, parts)
    return parts


# ---- sequences ----------------------------------------------------------------

class Case:
    """A named case: files (name -> bytes | F) and steps."""

    def __init__(self, name, tool, files, steps):
        self.name, self.tool, self.files, self.steps = name, tool, files, steps


def _apply(d, st):
    p = os.path.join(d, st[1])
    if st[0] == 'write':
        place(d, {st[1]: st[2]})
    elif st[0] == 'mkdir':
        os.makedirs(p, exist_ok=True)
    elif not os.path.lexists(p):
        pass
    elif st[0] == 'rm':
        os.unlink(p)
    elif st[0] == 'chmod':
        os.chmod(p, st[2])
    elif st[0] == 'trunc':
        os.truncate(p, st[2])
    elif st[0] == 'poke':
        with open(p, 'r+b') as fh:
            fh.seek(st[2])
            fh.write(st[3])
    elif st[0] == 'symlink':
        os.symlink(st[2], p)
    else:
        raise ValueError(st)


def _command(tool, st):
    """(tool, args, stdin, full, second tool args) of a step."""
    full, stdin = False, b''
    if isinstance(st, tuple) and st[0] == 'full':
        full, st = True, st[1]
    if isinstance(st, tuple) and st[0] == 'stdin':
        stdin, st = st[1], st[2]
    return full, stdin, st


def compare_seq(tool, steps, files, keep=None, name='', merged_check=True):
    """Run the steps in two copies of a directory made from files; return
    [(step number, kind, detail)] up to the first difference or refusal. A step: a
    list of the tool's arguments, ('stdin', bytes, args), ('full', step) (the standard
    output is /dev/full), ('pipe', lorder args, tsort args), or ('write', name,
    F|bytes), ('rm', name), ('mkdir', name), ('chmod', name, mode), ('trunc',
    name, size), ('poke', name, offset, bytes), ('symlink', name, target), ('rmtmp',)
    (the MUTOS root's tmp/ is removed)."""
    results = []
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))
    with tempfile.TemporaryDirectory(prefix='utilcmp') as tmp:
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        ro, rp = os.path.join(tmp, 'orig-root'), os.path.join(tmp, 'port-root')
        for d in (do, dp):
            os.makedirs(d)
            place(d, files)
        prepare_root(ro, True, tool == 'lorder')
        prepare_root(rp, False, tool == 'lorder')
        for i, st in enumerate(steps):
            if isinstance(st, tuple) and st[0] == 'rmtmp':
                for r in (ro, rp):
                    shutil.rmtree(os.path.join(r, 'tmp'), ignore_errors=True)
                continue
            if isinstance(st, tuple) and st[0] in ('write', 'mkdir', 'rm', 'chmod',
                                                   'trunc', 'poke', 'symlink'):
                for d in (do, dp):
                    _apply(d, st)
                continue
            ispipe = isinstance(st, tuple) and st[0] == 'pipe'
            if ispipe:
                ost, oout, oerr, why = _pipe(True, st, do, ro)
                pst, pout, perr = _pipe(False, st, dp, rp)
                shown = 'lorder %s | tsort %s' % (' '.join(st[1]), ' '.join(st[2]))
                jt = 'lorder'
            else:
                full, stdin, args = _command(tool, st)
                ost, oout, oerr, why = run_orig(tool, args, do, ro, stdin, full)
                pst, pout, perr = run_port(tool, args, dp, rp, stdin, full)
                shown = ' '.join(args)
                jt = tool
            kind, detail = judge(jt, ost, oout, oerr, why, pst, pout, perr, do, dp, ro, rp)
            if kind == 'same' and merged_check and oerr and tool in ('nm', 'tsort') \
                    and not ispipe:
                # the order of the standard output's blocks and the messages
                _, mo, _, mwhy = run_orig(tool, args, do, ro, stdin, full, merged=True)
                _, mp, _ = run_port(tool, args, dp, rp, stdin, full, merged=True)
                if mo != mp:
                    kind, detail = 'DIFFERENT', 'merged output %r, original %r' % (
                        mp[:300], mo[:300])
            if kind == 'same':
                parts = _tree_roots(tool, ro, rp)
                if parts:
                    kind, detail = 'DIFFERENT', 'root: ' + '; '.join(parts)
            results.append((i, kind, detail))
            for d in (do, dp):
                settle_times(d)
            if kind in ('refused', 'orig-fails', 'endless') and tool in STATELESS:
                continue
            if kind != 'same':
                if keep and kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    os.makedirs(keep, exist_ok=True)
                    k = tempfile.mkdtemp(dir=keep, prefix='case')
                    shutil.copytree(do, os.path.join(k, 'orig'), symlinks=True)
                    shutil.copytree(dp, os.path.join(k, 'port'), symlinks=True)
                    with open(os.path.join(k, 'args'), 'w', errors='backslashreplace') as f:
                        f.write('%s\nstep %d: %s\n%s: %s\n' % (name, i, shown, kind, detail))
                break
    return results


def _pipe(orig, st, d, root):
    """lorder ARGS | tsort ARGS: (status of the last, stdout, stderr[, why]) -
    the stderr of both."""
    lt = 'lorder'
    if orig:
        a = run_orig(lt, st[1], d, root)
        lo, le = a[1], a[2]
        if a[0] is None:
            return None, b'', le, a[3]
        t = run_orig('tsort', st[2], d, root, lo)
        return t[0], t[1], le + t[2], t[3]
    a = run_port(lt, st[1], d, root)
    if a[0] is None or a[0] == 'XFSZ' or a[0] == 125:
        return a[0], b'', a[2]
    t = run_port('tsort', st[2], d, root, a[1])
    return t[0], t[1], a[2] + t[2]


# ---- the cases ---------------------------------------------------------------------

def _names(cor, pick=None):
    return [n for n in cor if pick is None or pick(n)]


def cases(tool):
    """[Case] of a tool."""
    cor = corpus()
    exes = linked()
    allf = dict(cor)
    allf.update(exes)
    out = []

    def add(name, files, steps):
        out.append(Case('%s/%s' % (tool, name), tool, files, steps))

    objs = [n for n in allf if not n.endswith('.a')]
    if tool == 'size':
        for n in allf:
            add('one/' + n, {n: allf[n]}, [[n]])
        for n in list(allf)[::7]:
            add('twice/' + n, {n: allf[n]}, [[n, n]])
        add('many', {n: allf[n] for n in list(allf)[:30]}, [list(allf)[:30]])
        add('default', {'a.out': allf['crt0.o'], 'b': b''}, [[], ['nofile'], ['b'], ['a.out', 'b']])
        add('stale', {'x.o': allf['crt0.o'], 'e': b'', 'one': b'\x07', 'two': b'\x07\x01', 'y': allf['libc.a']},
            [['x.o', 'e', 'one', 'two', 'y', 'e'], ['one', 'x.o', 'one'], ['nofile', 'x.o', 'nofile']])
        for mg in (0o407, 0o410, 0o411, 0o405, 0o406, 0o412, 0, 0o177545, 0xffff):
            add('magic%o' % mg, {'m': header(mg, 65535, 65535, 65535) + b'x'}, [['m'], ['m', 'm']])
        add('maxsum', {'m': header(0o407, 65535, 65535, 65535, 0, 0, 0, 1)}, [['m']])
        add('short', {'s%d' % k: header()[:k] for k in range(0, 16)}, [['s%d' % k for k in range(16)]])
        add('missing', {}, [['nofile'], ['nofile', 'nofile2'], ['']])
        add('full', {'x': allf['crt0.o']}, [('full', ['x']), ('full', ['nofile']),
                                            ('full', ['x', 'nofile', 'nofile', 'nofile'])])
        add('options', {'x': allf['crt0.o']}, [['-x'], ['-', 'x'], ['--'], ['x', '-']])
        add('dir', {'d/': b''}, [['d'], ['d', 'd']])
        add('dir_after', {'x': allf['crt0.o'], 'd/': b''}, [['x', 'd', 'x']])

    if tool == 'strip':
        for n in allf:
            add('one/' + n, {n: allf[n]}, [[n], [n]])
        add('many', {n: allf[n] for n in list(allf)[:40]}, [list(allf)[:40], list(allf)[:40]])
        add('modes', {'a': F(allf['crt0.o'], mode=0o755), 'b': F(allf['crt0.o'], mode=0o600),
                      'c': F(allf['crt0.o'], mode=0o444), 'd': F(allf['crt0.o'], mode=0o4755)},
            [['a', 'b', 'c', 'd']])
        add('symlink', {'x': allf['crt0.o']}, [('symlink', 'ln', 'x'), ['ln'], ['x']])
        add('dup', {'x': allf['crt0.o']}, [['x', 'x', 'x']])
        add('none', {}, [[], ['nofile'], [''], ['nofile', 'nofile2', 'nofile']])
        add('option', {'x': allf['crt0.o']}, [['-x'], ['-', 'x'], ['--'], ['x', '--']])
        add('dir', {'d/': b'', 'x': allf['crt0.o']}, [['x', 'd'], ['d', 'x']])
        add('full', {'x': allf['crt0.o'], 'y': allf['crt0.o']},
            [('full', ['nofile']), ('full', ['x']), ('full', ['x', 'y', 'nofile'])])
        add('notmp', {'x': allf['crt0.o']}, [('rmtmp',), ['x'], ['nofile'], []])
        # headers of every shape
        sy = [sym('_x', 0o042, 0), sym('_y', 0o004, 9)]
        for mg in (0o407, 0o410, 0o411, 0o405, 0o406, 0o412, 0, 0o177545):
            add('magic%o' % mg, {'m': aout(sy, b'ab', b'cd', 4, 0, mg)}, [['m'], ['m']])
        for fl in (0, 1, 2, 3, 0x8000, 0xffff):
            for syms in ([], sy):
                add('flag%x/%d' % (fl, len(syms)),
                    {'m': aout(syms, b'tt', b'dd', 2, fl)}, [['m'], ['m']])
        add('reloc', {'m': aout(sy, b'ttt0', b'ddd0', 2, 0, reloc=True)}, [['m'], ['m']])
        add('odd', {'m': aout(sy, b'ttt', b'd', 0, 0)}, [['m']])
        add('empty-text', {'m': aout(sy, b'', b'', 0, 0)}, [['m'], ['m']])
        add('bss-only', {'m': aout(sy, b'', b'', 600, 0)}, [['m']])
        # a file shorter than its header says, by one byte or a lot
        full = aout(sy, bytes(range(256)) * 3, b'D' * 700, 0, 0)
        for cut in (1, 2, 17, 600, 1000, len(full) - 16):
            add('short%d' % cut, {'m': full[:len(full) - cut] if cut < len(full) - 16 else full[:16 + 5]},
                [['m'], ['m']])
        add('exact', {'m': full[:16 + 768 + 700]}, [['m'], ['m']])        # no symbols behind
        add('exact-1', {'m': full[:16 + 768 + 699]}, [['m']])
        add('exact+1', {'m': full[:16 + 768 + 701]}, [['m']])
        for size in (1, 511, 512, 513, 1023, 1024, 1025, 4096):
            body = bytes((i * 7 + 3) & 0xff for i in range(size))
            add('size%d' % size, {'m': aout(sy, body, b'', 0, 0)}, [['m'], ['m']])
        big = aout(sy, bytes((i * 13) & 0xff for i in range(65535)),
                   bytes((i * 11) & 0xff for i in range(65535)), 7, 0)
        add('64k', {'m': big}, [['m'], ['m']])
        add('64k-short', {'m': big[:-40000]}, [['m']])
        # the header is a global: a short read leaves the old bytes
        for k in range(0, 16):
            add('stale%d' % k, {'a': aout(sy, b'ab', b'cd', 4, 0), 'b': aout(sy, b'ab', b'cd', 4, 0)[:k],
                                'c': aout([], b'ab', b'cd', 4, 1)[:k], 'd': allf['crt0.o'][:k]},
                [['a', 'b', 'c', 'd', 'b'], ['b', 'a', 'b'], ['d', 'b']])
        add('stale-magic', {'a': aout(sy, b'ab', b'cd', 4, 0), 'b': b'\x07\x01', 'c': b'\x08\x01',
                            'e': b''},
            [['a', 'b'], ['b'], ['a', 'c', 'e', 'a']])
        add('stale-text', {'a': aout(sy, b'abcd', b'efgh', 4, 0), 'b': header(0o407, 6, 6, 0, 8, 0, 0, 0)[:6] + b'xyz'},
            [['a', 'b'], ['b']])
        add('a-out-obj', {'x': allf['crt0.o'], 'y': allf['g_01_floatbas.o'] if 'g_01_floatbas.o' in allf else allf['crt0.o']},
            [['x', 'y'], ['x', 'y']])

    if tool == 'nm':
        import random
        opts = [[], ['-g'], ['-n'], ['-u'], ['-r'], ['-p'], ['-o'], ['-gn'], ['-nr'], ['-gu'],
                ['-ro'], ['-nu'], ['-pg'], ['-gnopru'], ['-ru'], ['-og'], ['-np']]
        for n in allf:
            add('one/' + n, {n: allf[n]}, [[n], ['-g', n], ['-nr', n]])
        big = ['libc.a', 'crt0.o', 'g_fltopen.o', 'g_01_floatbas.o'] + [n for n in allf if n.startswith('ex_')][:2]
        for n in big:
            if n in allf:
                add('opts/' + n, {n: allf[n]}, [o + [n] for o in opts])
        pick = list(allf)[::9]
        for n in pick:
            add('allopts/' + n, {n: allf[n]}, [o + [n] for o in opts])
        add('many', {n: allf[n] for n in list(allf)[:30]},
            [list(allf)[:30], ['-o'] + list(allf)[:30], ['-n'] + list(allf)[:30]])
        add('arch-then-obj', {'a': allf['libc.a'], 'o': allf['crt0.o'], 'p': allf['g_fltopen.o'] if 'g_fltopen.o' in allf else allf['crt0.o']},
            [['a', 'o', 'p'], ['-o', 'a', 'o'], ['o', 'a', 'o'], ['-g', 'o', 'a'], ['o', 'p']])
        add('two-archives', {'a': allf['libc.a'], 'b': allf['libc.a']}, [['-g', 'a', 'b'], ['-u', 'a', 'b']])
        add('default', {'a.out': allf['crt0.o']}, [[], ['-g'], ['-n'], ['-o']])
        add('nodefault', {}, [[], ['-g'], ['-gn']])
        add('missing', {}, [['nofile'], ['nofile', 'nofile2'], [''], ['-', 'nofile']])
        add('option-error', {'x': allf['crt0.o']}, [['-x', 'x'], ['-gx', 'x'], ['-xg'], ['-g-', 'x'], ['x', '-g'], ['-g', '-n', 'x'],
                                                    ['--'], ['-', 'x'], ['-n', '-'], ['-gg', 'x'], ['-nrnr', 'x']])
        add('full', {'x': allf['crt0.o'], 'y': allf['libc.a']},
            [('full', ['x']), ('full', ['nofile']), ('full', ['x', 'y']), ('full', ['y']), ('full', ['-o', 'y'])])
        add('dir', {'d/': b'', 'x': allf['crt0.o']}, [['d'], ['x', 'd'], ['-g', 'd', 'x']])
        # hand-built symbol tables
        rnd = random.Random(7)
        names = ['_a', '_b', '_c', '_aa', 'a', 'b', '_main', '_x', '\x80z', '\xffq', '_\x7f', '', '12345678', 'abcdefgh']
        def mk(n, nm=names, types=(0, 1, 2, 3, 4, 0o14, 0o20, 0o24, 0o37, 0o40, 0o41, 0o42, 0o43, 0o44,
                                      0o60, 0o77, 0o100, 0o141, 0o1000, 0xffff, 0x8002, 0x1042), vals=(0, 1, 2, 0x10, 0xff, 0x1000, 0x7fff, 0x8000, 0xffff)):
            return [sym(rnd.choice(nm), rnd.choice(types), rnd.choice(vals)) for _ in range(n)]
        for n in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 17, 25, 33, 50, 100, 200, 500):
            for rep in range(2 if n < 100 else 1):
                f = aout(mk(n), b'tttt', b'dd', 4, 1)
                add('rnd%d_%d' % (n, rep), {'m': f}, [o + ['m'] for o in opts])
        for n in (2, 3, 5, 10, 30, 100, 400):
            small = mk(n, nm=['_a', '_b'], types=(0o42, 0o43, 0o2, 0o4, 0), vals=(1, 2))
            add('dups%d' % n, {'m': aout(small, b'', b'', 0, 1)}, [o + ['m'] for o in opts[:12]])
            same = [sym('_same', t, 5) for t in [0o42, 0o43, 0o2, 0o4, 0o3, 0o5, 0o12, 0o14, 0o141] * (n // 9 + 1)][:n]
            add('same%d' % n, {'m': aout(same, b'', b'', 0, 1)}, [o + ['m'] for o in opts[:12]])
            asc = [sym('_s%04d' % i, 0o42, i) for i in range(n)]
            add('asc%d' % n, {'m': aout(asc, b'', b'', 0, 1)}, [o + ['m'] for o in opts[:12]])
            add('desc%d' % n, {'m': aout(asc[::-1], b'', b'', 0, 1)}, [o + ['m'] for o in opts[:12]])
        for n in (1000, 2000):
            add('big%d' % n, {'m': aout(mk(n), b'', b'', 0, 1)}, [[], ['-nr'], ['-p'], ['-gu']])
        # type letters
        allt = [sym('_t%02d' % t, t | e, t * 3 + 1) for e in (0, 0o40) for t in range(0o40)]
        add('types', {'m': aout(allt, b'', b'', 0, 1)}, [o + ['m'] for o in opts])
        add('wordtype', {'m': aout([sym('_w%d' % k, k << 8 | 0o2, k) for k in range(8)] +
                                   [sym('_x%d' % k, (k << 8) | 0o42, k) for k in range(8)], b'', b'', 0, 1)},
            [['m'], ['-g', 'm'], ['-u', 'm']])
        add('names', {'m': aout([sym(b'\x01\x02\x03', 2, 1), sym(b'abcdefgh', 2, 2), sym(b'\xff\xfe\x00\x41', 0o42, 3),
                                  sym(b'a\x00b', 2, 4), sym(b'\x80', 2, 4), sym(b'\x7f', 2, 4), sym(b'%s%d%n', 2, 5),
                                  sym(b'  ', 2, 6), sym(b'\n\n', 2, 7)], b'', b'', 0, 1)},
            [o + ['m'] for o in opts])
        # a.out headers
        sy = [sym('_x', 0o042, 0x10), sym('_y', 0o004, 9), sym('_z', 0, 0), sym('_c', 0, 0x20)]
        for mg in (0o407, 0o410, 0o411, 0o405, 0o406, 0o412, 0, 0o177545, 0xffff, 0o177555, 0o177546):
            add('magic%o' % mg, {'m': aout(sy, b'ab', b'cd', 4, 1, mg)}, [['m'], ['-o', 'm'], ['-g', 'm', 'm']])
        for fl in (0, 1, 2, 3, 0x8000, 0xffff):
            add('flag%x' % fl, {'m': aout(sy, b'abcd', b'efgh', 4, fl)}, [['m'], ['-n', 'm']])
        add('reloc', {'m': aout(sy, b'abcd', b'efgh', 4, 0, reloc=True)}, [['m'], ['-p', 'm']])
        add('reloc-short', {'m': aout(sy, b'abcd', b'efgh', 4, 0)}, [['m']])      # no relocation behind
        add('nosyms', {'m': aout([], b'ab', b'cd', 4, 1), 'n': header(0o407, 2, 2, 0, 11, 0, 0, 1) + b'abcd' + b'x' * 11},
            [['m'], ['n'], ['-g', 'm', 'n'], ['-o', 'm']])
        add('partial-sym', {'m': aout(sy, b'ab', b'cd', 4, 1) + b'xyz', 'n': aout(sy, b'ab', b'cd', 4, 1)[:-5],
                            'o': aout(sy, b'ab', b'cd', 4, 1)[:-12], 'p': aout(sy, b'ab', b'cd', 4, 1)[:-24]},
            [['m'], ['n'], ['o'], ['p'], ['-p', 'n'], ['-p', 'o', 'p'], ['n', 'p', 'o']])
        add('short-first', {'m': aout(sy, b'ab', b'cd', 4, 1)[:16 + 4 + 5], 'n': aout(sy, b'ab', b'cd', 4, 1)[:16 + 4 + 12 + 5],
                            'o': aout(sy, b'ab', b'cd', 4, 1)[:16 + 4]},
            [['m'], ['-p', 'm'], ['-p', 'n'], ['-p', 'o'], ['n', 'm'], ['m', 'n']])
        add('odd-syms', {'m': header(0o407, 2, 2, 0, 30, 0, 0, 1) + b'abcd' + b''.join(sy[:3])[:30],
                         'n': header(0o407, 2, 2, 0, 65535, 0, 0, 1) + b'abcd' + b''.join(sy)},
            [['m'], ['-p', 'n'], ['n']])
        add('header-short', {'h%d' % k: header(0o407, 2, 2, 0, 24, 0, 0, 1)[:k] for k in range(0, 16)},
            [['h%d' % k] for k in range(16)] + [['h%d' % k for k in range(16)]])
        add('empty-files', {'e': b'', 'f': b'\x07', 'g': b'\x07\x01', 'o': allf['crt0.o']},
            [['e'], ['f'], ['g'], ['o', 'e'], ['o', 'f'], ['o', 'g', 'e'], ['-g', 'e', 'o', 'e']])
        # archives
        arcsy = aout(sy, b'ab', b'cd', 4, 1)
        arc = arcmp.mkar
        add('arch-basic', {'a': arc([('a.o', arcsy), ('b.o', arcsy), ('c', b'text')]), 'e': arc([]),
                           'n': arc([('n.o', aout([], b'ab', b'cd', 4, 1))])},
            [o + [n] for n in ('a', 'e', 'n') for o in opts[:10]])
        add('arch-names', {'a': arc([('abcdefghijklmn', arcsy), ('x', arcsy)]),
                           'b': arc([('abcdefghijklmn', aout([], b'ab', b'cd', 4, 1)), ('', arcsy)]),
                           'c': arc([(b'a\xffb', arcsy), (b'12345678901234', arcsy, 0, 0, 0, 0)])},
            [o + [n] for n in ('a', 'b', 'c') for o in opts[:8]])
        add('arch-odd', {'a': arc([('a.o', arcsy + b'\0'), ('b.o', arcsy[:-1]), ('c.o', arcsy)]),
                         'b': arc([('a.o', arcsy[:20]), ('b.o', arcsy)]),
                         'c': arc([('a.o', b''), ('b.o', arcsy), ('c.o', b'')])},
            [o + [n] for n in ('a', 'b', 'c') for o in opts[:4]])
        for k in (0, 1, 2, 3, 10, 25, 26, 27, 30, 40, 41, 42, 43, 60):
            add('arch-cut%d' % k, {'a': arc([('a.o', arcsy), ('b.o', arcsy)])[:2 + k] if k < 40 else
                                   arc([('a.o', arcsy), ('b.o', arcsy)])[:-(k - 40)],
                                   'b': arc([('a.o', arcsy)])[:2 + k]},
                [['a'], ['-g', 'a', 'b'], ['b']])
        for sz in (0xffffffff, 0x80000000, 0x7fffffff, 0x80000001, 0x10000, 0xfffe, 0xffff, 1, 3, 100):
            member = hdr = arcmp.hdr('a.o', sz) + arcsy
            add('arch-size%x' % sz, {'a': struct_pack_magic() + hdr + arcmp.hdr('b.o', len(arcsy)) + arcsy},
                [['a'], ['-g', 'a'], ['-o', 'a', 'a']])
        add('arch-stale', {'a': arc([('a.o', arcsy)]), 'b': arc([('a.o', aout([], b'', b'', 0, 1))])[:2 + 26 + 3],
                           'c': arc([('abcdefghijklmn', arcsy)])[:2 + 20]},
            [['a', 'b'], ['b', 'a'], ['c', 'a'], ['a', 'c'], ['-p', 'b', 'c', 'a']])
        add('arch-nested-name', {'a': arc([('a.o', aout([], b'', b'', 0, 1)), ('abcdefghijklmn', aout([], b'', b'', 0, 1))])},
            [['a'], ['-g', 'a'], ['-o', 'a'], ['-u', 'a']])
        # slots
        for nbad in (1, 16, 17, 18, 19, 25):
            fl = {'bad%d' % k: b'zz' for k in range(nbad)}
            fl['ok'] = allf['crt0.o']
            fl['arc'] = arc([('a.o', arcsy)])
            add('slots%d' % nbad, fl, [['bad%d' % k for k in range(nbad)] + ['ok', 'arc', 'ok'],
                                        ['-g'] + ['bad%d' % k for k in range(nbad)] + ['nofile', 'ok']])
        # stdin is not read
        add('a-out-is-archive', {'a.out': arc([('a.o', arcsy)])}, [[], ['-g'], ['-o']])

    if tool == 'tsort':
        import random
        def pairs(*ps):
            return ('\n'.join('%s %s' % p for p in ps) + '\n').encode('latin1')
        def inp(data, *args):
            return ('stdin', data, list(args))
        chain = pairs(('a', 'b'), ('b', 'c'), ('c', 'd'))
        add('chain', {'f': chain}, [inp(chain), ['f'], ['f', 'extra'], ['nofile'], ['nofile', 'f'], ['']])
        diamond = pairs(('a', 'b'), ('a', 'c'), ('b', 'd'), ('c', 'd'))
        add('diamond', {}, [inp(diamond), inp(diamond[::-1])])
        add('self', {}, [inp(pairs(('a', 'a'))), inp(pairs(('a', 'a'), ('b', 'b'), ('a', 'b'))),
                          inp(pairs(('x', 'y'), ('x', 'y'), ('y', 'x'))), inp(pairs(('a', 'b'), ('b', 'a')))])
        add('empty', {'e': b''}, [inp(b''), inp(b' '), inp(b'\n\n'), inp(b'\t \n'), ['e'], inp(b'', 'e')])
        add('odd', {}, [inp(b'a'), inp(b'a\n'), inp(b'a b c'), inp(b'a b c\n'), inp(b'a b\nc'), inp(b'a b\nc d\ne\n'),
                        inp(b'a b c d e')])
        add('nonl', {}, [inp(b'a b'), inp(b'a b\nc d'), inp(b'a\nb\n'), inp(b'  a   b  '), inp(b'\ta\t\tb\t')])
        add('spacing', {}, [inp(b'a\nb\nc\nd\n'), inp(b'a b\r\nc d\r\n'), inp(b'a\x0bb\x0cc d'), inp(b'a b\n\n\n\nc d'),
                            inp(b'a\x00b c d'), inp(b'\x00 a'), inp(b'a b \x00 \x00'), inp(b'a\x7f b\x01')])
        add('cycles', {}, [inp(pairs(('a', 'b'), ('b', 'c'), ('c', 'a'))), inp(pairs(('a', 'b'), ('b', 'a'), ('c', 'd'), ('d', 'c'))),
                           inp(pairs(('a', 'b'), ('b', 'c'), ('c', 'd'), ('d', 'b'), ('d', 'e'))),
                           inp(pairs(('z', 'a'), ('a', 'b'), ('b', 'c'), ('c', 'a'), ('c', 'd'))),
                           inp(pairs(('a', 'b'), ('b', 'c'), ('c', 'a'), ('a', 'd'), ('d', 'e'), ('e', 'd'))),
                           inp(pairs(*[(x, y) for x in 'abcde' for y in 'abcde']))])
        for ln in list(range(1, 4)) + list(range(46, 60)) + list(range(96, 112)) + [200]:
            tok = 'q' * ln
            tok2 = ''.join(chr(97 + (k * 7) % 26) for k in range(ln))
            add('long%d' % ln, {}, [inp(pairs((tok, 'b'))), inp(pairs(('a', tok))), inp(pairs((tok2, 'b'), ('a', tok2))),
                                    inp(pairs(('a', tok), ('b', 'a'), ('a', 'b'))), inp(pairs((tok2, tok), ('x', 'y'))),
                                    inp(pairs(('a', 'b'), ('c', tok2), ('d', 'e')))])
        add('long-stale', {}, [inp(pairs(('a' * 49, 'b'), ('c', 'd' * 49), ('e', 'f' * 52), ('g', 'h')))])
        add('highbyte', {}, [inp(b'a b\n\x80 c'), inp(b'a\xffb c'), inp(b'\xa0 b'), inp(b'a b\x82c d'), inp(b'a b\x8d\nc d')])
        rnd = random.Random(11)
        for n, e in ((3, 3), (5, 8), (8, 10), (10, 30), (20, 25), (20, 60), (50, 70), (50, 200), (100, 150), (100, 400),
                     (200, 300), (400, 600)):
            for rep in range(3 if n < 100 else 1):
                nodes = ['n%d' % k for k in range(n)]
                names = rnd.sample(nodes, n)
                dag = rnd.random() < 0.5
                ps = []
                for _ in range(e):
                    a, b = rnd.randrange(n), rnd.randrange(n)
                    if dag and a > b:
                        a, b = b, a
                    ps.append((names[a], names[b]))
                data = pairs(*ps)
                add('rnd%d_%d_%d' % (n, e, rep), {'f': data}, [inp(data), ['f']])
        # output past one block, to a full disk
        big = pairs(*[('node%04d' % k, 'node%04d' % (k + 1)) for k in range(300)])
        add('full', {'f': big}, [('full', inp(big)), ('full', ['f']), ('full', inp(pairs(('a', 'b')))),
                                 ('full', inp(b'')), inp(big)])
        add('dir', {'d/': b''}, [['d'], ['d', 'x']])
        # a lot of nodes
        for n in (500, 1000, 1500, 2000):
            data = pairs(*[('node-with-a-longer-name-%05d' % k, 'node-with-a-longer-name-%05d' % ((k * 7 + 1) % n)) for k in range(n)])
            add('nodes%d' % n, {'f': data}, [['f']])
        for n in (500, 1000, 1500, 2000, 3000):
            data = pairs(*[('n%d' % rnd.randrange(60), 'n%d' % rnd.randrange(60)) for _ in range(n)])
            add('edges%d' % n, {'f': data}, [['f']])
        # the heap's end: a pair in a chain of items of eight characters costs 28 bytes; the
        # original sorts 2010 pairs and fails at 2011; the port stops at 54K (1974 pairs)
        for n in (1900, 1974, 1975, 2010, 2011):
            data = pairs(*[('n%07d' % k, 'n%07d' % (k + 1)) for k in range(n)])
            add('chain%d' % n, {'f': data}, [['f']])
        add('twice', {'f': chain}, [['f'], ['f'], inp(chain)])

    if tool == 'lorder':
        import random
        members = {n: allf[n] for n in allf if n.startswith('lc_') and n.endswith('.o')}
        mnames = list(members)
        add('libc', {'libc.a': allf['libc.a']}, [['libc.a'], ('pipe', ['libc.a'], []), ['libc.a', 'libc.a']])
        add('members', dict(members, **{'crt0.o': allf['crt0.o']}),
            [mnames[:20], mnames, ['crt0.o'] + mnames[:5], ('pipe', mnames, []), ('pipe', ['crt0.o'] + mnames, [])])
        add('single', {'crt0.o': allf['crt0.o'], 'libc.a': allf['libc.a'], 'x': allf['crt0.o']},
            [['crt0.o'], ['libc.a'], ['x'], ('pipe', ['crt0.o'], []), ('pipe', ['x'], [])])
        add('usage', {}, [[]])
        add('missing', {'crt0.o': allf['crt0.o']}, [['nofile'], ['nofile.o'], ['crt0.o', 'nofile.o'], [''], ['-g'], ['-x', 'crt0.o']])
        add('mixed', {'crt0.o': allf['crt0.o'], 'libc.a': allf['libc.a'], 'g': allf['g_01_floatbas.o'] if 'g_01_floatbas.o' in allf else allf['crt0.o'],
                      'junk': b'zz', 'e': b''},
            [['crt0.o', 'libc.a', 'g'], ['junk', 'crt0.o'], ['e', 'crt0.o'], ['crt0.o', 'junk', 'libc.a']])
        # hand-made objects
        def obj(defs, refs=(), bss=(), absolute=()):
            ss = [sym(n, 0o42, 0x10 * (k + 1)) for k, n in enumerate(defs)]
            ss += [sym(n, 0o43, 4 * k) for k, n in enumerate(['_d_' + d for d in defs[:1]])]
            ss += [sym(n, 0, 0) for n in refs]
            ss += [sym(n, 0o44, 8) for n in bss]
            ss += [sym(n, 0o41, 7) for n in absolute]
            ss += [sym('_loc', 0o2, 1), sym('_cm', 0, 4)]
            return aout(ss, b'tt', b'dd', 4, 1)
        chain = {'a.o': obj(['_a'], ['_b']), 'b.o': obj(['_b'], ['_c']), 'c.o': obj(['_c'], [])}
        add('chain', chain, [['a.o', 'b.o', 'c.o'], ('pipe', ['a.o', 'b.o', 'c.o'], []), ('pipe', ['c.o', 'b.o', 'a.o'], []),
                             ('pipe', ['a.o'], []), ['a.o']])
        cyc = {'a.o': obj(['_a'], ['_b']), 'b.o': obj(['_b'], ['_c']), 'c.o': obj(['_c'], ['_a']),
               'd.o': obj(['_d'], ['_a', '_nothing']), 'e.o': obj([], ['_a'], bss=['_bss']), 'f.o': obj(['_f', '_g'], ['_f'], absolute=['_abs'])}
        add('cycle', cyc, [list(cyc), ('pipe', list(cyc), []), ('pipe', list(cyc)[::-1], [])])
        dup = {'a.o': obj(['_a', '_x'], []), 'b.o': obj(['_x'], ['_a']), 'c.o': obj(['_c'], ['_x', '_a', '_x'])}
        add('dup', dup, [list(dup), ('pipe', list(dup), [])])
        arc = arcmp.mkar([(n, d) for n, d in chain.items()])
        arc2 = arcmp.mkar([(n, d) for n, d in cyc.items()] + [('readme', b'text')])
        add('archive', {'x.a': arc, 'y.a': arc2, 'z.o': obj(['_z'], ['_a'])},
            [['x.a'], ['y.a'], ['x.a', 'y.a'], ['x.a', 'z.o'], ('pipe', ['x.a'], []), ('pipe', ['y.a'], []),
             ('pipe', ['x.a', 'y.a'], []), ('pipe', ['z.o', 'x.a'], [])])
        # random objects
        rnd = random.Random(5)
        for n, rep in ((8, 0), (8, 1), (20, 0), (40, 0)):
            names = ['m%02d.o' % k for k in range(n)]
            syms = ['_s%d' % k for k in range(n * 2)]
            files = {}
            for nm in names:
                defs = rnd.sample(syms, rnd.randrange(1, 4))
                refs = rnd.sample(syms, rnd.randrange(0, 5))
                files[nm] = obj(defs, refs, bss=rnd.sample(syms, rnd.randrange(0, 2)))
            add('rnd%d_%d' % (n, rep), files, [names, ('pipe', names, []), ('pipe', names, ['unknown'])])
    return out


def run_case(c, keep=None):
    return c.name, compare_seq(c.tool, c.steps, c.files, keep, c.name)


def summarize(results, show=20):
    from collections import Counter
    cnt = Counter()
    why = Counter()
    bad = []
    for name, res in results:
        for i, kind, detail in res:
            cnt[kind] += 1
            if kind in ('refused', 'orig-fails', 'endless'):
                why[(kind, detail.split(' - ')[0][:90])] += 1
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append((name, i, kind, detail))
    for k, v in sorted(cnt.items()):
        print('  %-12s %d' % (k, v))
    for (k, d), v in sorted(why.items()):
        print('    %-10s %3d  %s' % (k, v, d))
    for name, i, kind, detail in bad[:show]:
        print('%s step %d %s: %s' % (name, i, kind, detail[:400]))
    if len(bad) > show:
        print('... and %d more' % (len(bad) - show))
    return len(bad)


def main(argv):
    if not argv or argv[0] not in ('cases', 'run'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    if argv[0] == 'run':
        use_stdin = len(argv) > 1 and argv[1] == '--stdin'
        argv = argv[:1] + argv[2:] if use_stdin else argv
        tool, args = argv[1], argv[2:]
        with tempfile.TemporaryDirectory(prefix='utilcmp-run') as tmp:
            do, dp = os.path.join(tmp, 'o'), os.path.join(tmp, 'p')
            shutil.copytree('.', do, symlinks=True)
            shutil.copytree('.', dp, symlinks=True)
            ro, rp = os.path.join(tmp, 'ro'), os.path.join(tmp, 'rp')
            prepare_root(ro, True, tool == 'lorder')
            prepare_root(rp, False, tool == 'lorder')
            stdin = sys.stdin.buffer.read() if use_stdin else b''
            o = run_orig(tool, args, do, ro, stdin)
            p = run_port(tool, args, dp, rp, stdin)
            print('original: exit %s, why %r\n%s\n--- stderr\n%s' % (
                o[0], o[3], o[1].decode('latin1'), o[2].decode('latin1')))
            print('port:     exit %s\n%s\n--- stderr\n%s' % (
                p[0], p[1].decode('latin1'), p[2].decode('latin1')))
            print(judge(tool, o[0], o[1], o[2], o[3], p[0], p[1], p[2], do, dp, ro, rp))
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
