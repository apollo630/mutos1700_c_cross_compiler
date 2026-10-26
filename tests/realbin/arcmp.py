#!/usr/bin/env python3
"""arcmp.py - mutos_ar, the port of the real ar, against the original.

The original MUTOS 1700 ar (mutos/mutos_bin/ar.golden.base64.txt) runs
under mutosemu.py; mutos_ar (src/mutos_ar/) runs on the host, with TZ=UTC
(the emulator's ftime() says UTC). A case is a directory of files and a
sequence of steps - an ar command line, or a change of a file - run in
two copies of the directory, each with a MUTOS root of its own (an empty
tmp/ for the temporary files, bin/ for what ar's callers need). After
every step their messages, exit status and both trees - every file's
name, bytes and mode - must be the same; the temporary names (v12345,
/tmp/v1a2345) count as one name, and the date of an archive member that is
within the seconds the step ran is zeroed (an archive appended to itself
carries its own time, and the two runs are seconds apart).

A refusal of the port (exit status 125 and a last message ending in
"refused") counts apart: what it wrote to stdout must be a prefix of what
the original wrote, and the sequence ends there. The emulated original
cannot answer the opening of a directory (the host says ENOENT where
MUTOS reads the directory's entries); a case that does it only shows the
port refuses.

Two more classes are counted apart and are no difference: "limits" (the
port wrote past the harness's 16 MB file size limit - a damaged header
that makes ar copy a member of a gigabyte, or print one: the original's
output is cut at 1 MB, the port's by the limit), and "slow" (the original is
stopped by the instruction budget on such a member while the port, on the
host, finishes; what the original said by then must be a prefix of the
port's).

Usage:
    arcmp.py cases [-j N] [--port PATH] [--show N] [--keep DIR]
        the hand-written cases: every key, the messages, the archives'
        odd members, names, modes, owners and times
    arcmp.py run [ARG...]
        one command line, in a copy of the current directory

As a module: Case, F, mkar(), compare_seq(), plus the pieces ldcmp.py
has for ld (the judge there is this one's model).

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
import stat
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_ar', 'mutos_ar')
MAX_INSNS = 200_000_000
OUT_LIMIT = 1 << 20
PORT_CPU = 5
TMPNAME = re.compile(r'^v[12]?[0-9a-y]{5}$')
ARMAG = 0o177545


# ---- files and archives -----------------------------------------------------

class F:
    """A file of a case: bytes, and the mtime, mode, uid and gid it gets."""

    def __init__(self, data=b'', mtime=600000000, mode=0o644, uid=0, gid=0):
        self.data, self.mtime, self.mode = data, mtime, mode
        self.uid, self.gid = uid, gid


def _pdp(v):
    """A v7 long as the archive keeps it: the high word first."""
    v &= 0xffffffff
    return struct.pack('<HH', v >> 16, v & 0xffff)


def hdr(name, size, mtime=566554203, uid=0, gid=1, mode=0o100664):
    """A 26-byte archive header. name: bytes, cut or padded to 14."""
    if isinstance(name, str):
        name = name.encode('latin1')
    return (name[:14].ljust(14, b'\0') + _pdp(mtime)
            + struct.pack('<BBH', uid & 0xff, gid & 0xff, mode & 0xffff)
            + _pdp(size))


def mkar(members, magic=ARMAG):
    """An archive. members: (name, data) or (name, data, mtime, uid, gid,
    mode); the data padded to even."""
    out = bytearray(struct.pack('<H', magic))
    for m in members:
        name, data = m[0], m[1]
        out += hdr(name, len(data), *m[2:]) + data + (b'\0' if len(data) & 1 else b'')
    return bytes(out)


def place(dirpath, files):
    """Write a case's files: name -> bytes | F; a name ending in '/' is a
    directory."""
    for name, f in files.items():
        p = os.path.join(dirpath, name)
        if name.endswith('/'):
            os.makedirs(p, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if isinstance(f, bytes):
            f = F(f)
        with open(p, 'wb') as fh:
            fh.write(f.data)
        os.chmod(p, f.mode)
        os.chown(p, f.uid, f.gid)
        os.utime(p, (f.mtime, f.mtime))


def snapshot(d, window=None):
    """{relative name: (bytes, mode)} of a tree, the temporary names one
    name. window (lo, hi): the dates that come from the clock - in an
    archive, the date of any member within [lo, hi], the seconds the step
    has run - are zeroed (the archive that is appended to itself carries
    its own time; the runs of the original and the port are seconds
    apart)."""
    out = {}
    for dp, dns, fns in os.walk(d):
        for name in sorted(dns + fns):
            p = os.path.join(dp, name)
            rel = os.path.relpath(p, d)
            parts = rel.split(os.sep)
            parts = ['<tmp>' if TMPNAME.match(x) else x for x in parts]
            key = '/'.join(parts)
            st = os.lstat(p)
            if stat.S_ISDIR(st.st_mode):
                out[key + '/'] = (b'', st.st_mode & 0o7777)
            else:
                with open(p, 'rb') as f:
                    out[key] = (f.read(), st.st_mode & 0o7777)
    if window:
        for name, (data, mode) in list(out.items()):
            out[name] = (_zero_clock_dates(data, *window), mode)
    return out


def _zero_clock_dates(data, lo, hi):
    """data, if it is an archive, with the date of every member within
    [lo, hi] zeroed."""
    if len(data) < 2 + 26 or data[:2] != struct.pack('<H', ARMAG):
        return data
    buf = bytearray(data)
    p = 2
    while p + 26 <= len(buf):
        hi_w, lo_w = struct.unpack_from('<HH', buf, p + 14)
        if lo <= ((hi_w << 16) | lo_w) <= hi:
            buf[p + 14:p + 18] = b'\0\0\0\0'
        shi, slo = struct.unpack_from('<HH', buf, p + 22)
        size = (shi << 16) | slo
        if size >= 1 << 30:
            break
        p += 26 + size + (size & 1)
    return bytes(buf)


NOW = 1_790_000_000     # the time a file written by a step has afterwards


def settle_times(d):
    """The files a step has written carry the time it ran at (an extracted
    member, the archive): two runs seconds apart would put different times
    into the next step's headers. Any file newer than an hour before the
    run gets the fixed time NOW."""
    limit = time.time() - 3600
    for dp, dns, fns in os.walk(d):
        for fn in fns:
            p = os.path.join(dp, fn)
            if os.lstat(p).st_mtime > limit:
                os.utime(p, (NOW, NOW), follow_symlinks=False)


# ---- the two runs -------------------------------------------------------------

class _Capped(io.BytesIO):
    def write(self, b):
        n = super().write(b)
        if self.tell() > OUT_LIMIT:
            raise _Limit()
        return n


class _Limit(Exception):
    pass


class _FullFile(io.RawIOBase):
    """A standard output that cannot take a byte: /dev/full."""

    def write(self, b):
        raise OSError(errno.ENOSPC, 'No space left on device')


def run_orig(args, cwd, root, max_insns=MAX_INSNS, full=False):
    """(status or None, stdout, stderr, why it did not finish); full: the
    standard output is /dev/full (and nothing is captured of it)."""
    out, err = (_FullFile() if full else _Capped()), _Capped()
    emu = mutosemu.Emu(mutosemu.load_image('ar'), ['ar'] + list(args), cwd=cwd,
                       stdin=io.BytesIO(b''), stdout=out, stderr=err, root=root)
    why, st = '', None
    try:
        st = emu.run(max_insns)
    except _Limit:
        why = 'endless: messages past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why:
            why = 'endless: ' + why
    return st, (b'' if full else out.getvalue()), err.getvalue(), why


def _limits():
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def run_port(args, cwd, root, port=PORT, full=False):
    """(status, stdout, stderr); status None for a run a limit stopped."""
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    env['TZ'] = 'UTC'
    # the arguments are latin-1 characters for the program's bytes
    args = [os.fsdecode(a.encode('latin1')) for a in args]
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        if full:
            fo = open('/dev/full', 'wb')
        r = subprocess.run([port] + list(args), cwd=cwd, stdout=fo, stderr=fe,
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


def diff_at(a, b):
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
             min(len(a), len(b)))
    return 'differs at byte %d (%d vs %d bytes)' % (n, len(a), len(b))


def _same_trees(ta, tb, parts, what='port', other='original'):
    if ta == tb:
        return
    for name in sorted(set(ta) | set(tb)):
        if name not in tb:
            parts.append('%s: only the %s\'s' % (name, what))
        elif name not in ta:
            parts.append('%s: only the %s\'s' % (name, other))
        elif ta[name] != tb[name]:
            (ad, am), (bd, bm) = ta[name], tb[name]
            if ad != bd:
                parts.append('%s: %s' % (name, diff_at(ad, bd)))
            if am != bm:
                parts.append('%s: mode %o, %s %o' % (name, am, other, bm))


def judge(ost, oout, oerr, why, pst, pout, perr, do, dp, window=None):
    """(kind, detail) of one step: 'same', 'refused', 'orig-fails',
    'endless', 'DIFFERENT', 'BAD-REFUSAL'. The trees are compared by the
    caller's snapshot of both directories (do, dp)."""
    if pst == 'XFSZ':
        # a file past the size limit (a header read back from a file ar has
        # just overwritten: the original wrote 272 MB): not compared
        return 'limits', 'the port wrote past the file size limit'
    perr_s = perr.decode('latin1')
    lines = perr_s.rstrip('\n').split('\n')
    refused = (pst == 125 and lines[-1].startswith('ar: ')
               and lines[-1].endswith('refused'))
    if (ost is None and why.startswith('endless: messages past') and pst == 1
            and lines[-1].startswith('ar write error')):
        # `p` of a member of some hundreds of megabytes (a damaged header):
        # the harness cuts the original's output at OUT_LIMIT, and the
        # file size limit cuts the port's - a write that crosses it comes
        # back short, which ar reports as a write error
        n = min(len(oout), len(pout))
        if pout[:n] == oout[:n]:
            return 'limits', 'the port wrote past the file size limit ' \
                '(standard output)'
    if ost is None and pst is None and why.startswith('endless'):
        n = min(len(pout), len(oout))
        if pout[:n] == oout[:n]:
            return 'endless', why
        return 'DIFFERENT', 'both endless (%s), the messages differ: %s' % (
            why, diff_at(pout, oout))
    if (ost is None and pst is not None and not refused
            and why.startswith('endless: program stopped')):
        # ar's loops end with the sizes in the archive's headers, but a
        # member of some hundreds of megabytes (a damaged header) takes the
        # emulated original more than MAX_INSNS instructions where the port
        # takes a moment: not compared (the case was seen to finish, with
        # the same messages, under a larger budget)
        n = min(len(oout), len(pout))
        if pout[:n] == oout[:n]:
            return 'slow', 'the original is stopped by the instruction budget ' \
                '(%s); the port finished, exit %s' % (why, pst)
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original ' \
            '%s' % ('exit %d' % ost if ost is not None else why)
    ok = False
    if refused:
        ok = oout.startswith(pout) and len(lines) == 1
    if ost is None:
        if refused and ok:
            return 'orig-fails', 'the original does not finish (%s); the port ' \
                'refused: %s' % (why, lines[-1])
        if refused:
            return 'BAD-REFUSAL', 'the original does not finish (%s); the port\'s ' \
                'output not a prefix of the original\'s: %s' % (why, lines[-1])
        return 'DIFFERENT', 'the original does not finish (%s); the port exit ' \
            '%s' % (why, pst)
    if refused:
        return ('refused' if ok else 'BAD-REFUSAL'), lines[-1]
    parts = []
    if pst != ost:
        parts.append('exit %s, original %d' % (pst, ost))
    if pout != oout:
        parts.append('messages %r, original %r' % (pout[:300], oout[:300]))
    if perr != oerr:
        parts.append('stderr %r, original %r' % (perr[:300], oerr[:300]))
    _same_trees(snapshot(dp, window), snapshot(do, window), parts)
    if parts:
        return 'DIFFERENT', '; '.join(parts)
    return 'same', ''


# ---- sequences ----------------------------------------------------------------

def compare_seq(steps, files, port=PORT, keep=None, name=''):
    """Run the steps in two copies of a directory made from files; return
    [(step number, kind, detail)] up to the first difference or refusal.
    A step: a list of ar's arguments, ('full', args) (the standard output is
    /dev/full), ('write', name, F|bytes), ('rm',
    name), ('mkdir', name), ('touch', name, mtime), ('trunc', name, size)
    or ('poke', name, offset, bytes) - those of a file that is not there
    change nothing."""
    results = []
    # the emulated original gets the port's file size limit (and an error
    # for a write past it instead of a signal)
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))
    with tempfile.TemporaryDirectory(prefix='arcmp') as tmp:
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        ro, rp = os.path.join(tmp, 'orig-root'), os.path.join(tmp, 'port-root')
        for d in (do, dp):
            os.makedirs(d)
            place(d, files)
        for r in (ro, rp):
            for sub in ('tmp', 'bin'):
                os.makedirs(os.path.join(r, sub))
        for i, st in enumerate(steps):
            full = False
            if isinstance(st, tuple) and st[0] == 'full':
                full, st = True, st[1]
            if isinstance(st, tuple):
                for d in (do, dp):
                    p = os.path.join(d, st[1])
                    if st[0] == 'write':
                        place(d, {st[1]: st[2]})
                    elif st[0] == 'mkdir':
                        os.makedirs(p)
                    elif not os.path.exists(p):
                        pass            # nothing there to change
                    elif st[0] == 'rm':
                        os.unlink(p)
                    elif st[0] == 'touch':
                        os.utime(p, (st[2], st[2]))
                    elif st[0] == 'trunc':
                        os.truncate(p, st[2])
                    elif st[0] == 'poke':
                        with open(p, 'r+b') as fh:
                            fh.seek(st[2])
                            fh.write(st[3])
                    else:
                        raise ValueError(st)
                continue
            t_step = time.time()
            ost, oout, oerr, why = run_orig(st, do, ro, full=full)
            pst, pout, perr = run_port(st, dp, rp, port, full=full)
            kind, detail = judge(ost, oout, oerr, why, pst, pout, perr, do, dp,
                                 (int(t_step) - 2, int(time.time()) + 2))
            for d in (do, dp):
                settle_times(d)
            if kind == 'same':
                # the roots must agree too (a temporary file left behind)
                parts = []
                _same_trees(snapshot(rp), snapshot(ro), parts)
                if parts:
                    kind, detail = 'DIFFERENT', 'root: ' + '; '.join(parts)
            results.append((i, kind, detail))
            if kind != 'same':
                if keep and kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    os.makedirs(keep, exist_ok=True)
                    k = tempfile.mkdtemp(dir=keep, prefix='case')
                    shutil.copytree(do, os.path.join(k, 'orig'), symlinks=True)
                    shutil.copytree(dp, os.path.join(k, 'port'), symlinks=True)
                    with open(os.path.join(k, 'args'), 'w', errors='backslashreplace') as f:
                        f.write('%s\nstep %d: %s\n%s: %s\n' % (
                            name, i, ' '.join(st), kind, detail))
                break
    return results


# ---- the hand-written cases ---------------------------------------------------

def base_files():
    """The files most cases start from."""
    big = bytes(range(256)) * 5 + b'x'              # 1281 bytes: odd, > 2 blocks
    even = bytes(range(256)) * 4                    # 1024: even, exact blocks
    return {
        'a.txt': F(b'hello\n', mtime=600000000),
        'b.txt': F(b'odd', mtime=600000100),
        'c.txt': F(b'', mtime=600000200),
        'big.bin': F(big, mtime=600000300),
        'even.bin': F(even, mtime=600000400),
        'blk512.bin': F(b'\x55' * 512, mtime=600000500),
        'blk513.bin': F(b'\xaa' * 513, mtime=600000600),
        'exactly14chars': F(b'e14\n', mtime=600000700),
        'fifteen_chars_15': F(b'f15\n', mtime=600000800),
        'a_name_longer_than_fourteen.c': F(b'long\n', mtime=600000900),
        'sub/e.txt': F(b'in sub\n', mtime=600001000),
        'exec.sh': F(b'#!/bin/sh\n', mtime=600001100, mode=0o755),
        'suid': F(b'x', mtime=600001200, mode=0o4755),
        'sgid': F(b'y', mtime=600001300, mode=0o2755),
        'sticky': F(b'z', mtime=600001400, mode=0o1777),
        'ro.txt': F(b'read only\n', mtime=600001500, mode=0o444),
        'own.txt': F(b'owned\n', mtime=600001600, uid=200, gid=255),
        'own2.txt': F(b'owned\n', mtime=600001700, uid=65535, gid=129),
        'zero.txt': F(b'epoch\n', mtime=0),
        'one.txt': F(b'one\n', mtime=1),
        'late.txt': F(b'late\n', mtime=2147483647),
        'neg.txt': F(b'negative\n', mtime=3000000000),
        'y2k.txt': F(b'y2k\n', mtime=946684800),
        'dst.txt': F(b'dst\n', mtime=1000000000),
    }


def cases():
    """[(name, files, steps)]"""
    b = base_files()
    out = []

    def case(name, steps, files=None):
        out.append((name, files if files is not None else b, steps))

    A = 'x.a'
    # the messages and the key parsing
    case('no arguments', [[]])
    case('one argument', [['r'], ['rv'], ['t']])
    case('two arguments', [['r', A], ['t', A]])
    case('bad key', [['z', A, 'a.txt'], ['rz', A, 'a.txt'], ['r-', A, 'a.txt']])
    case('no command', [['v', A, 'a.txt'], ['c', A], ['l', A, 'a.txt'],
                        ['a', 'a.txt', A, 'b.txt']])
    case('two commands', [['rd', A, 'a.txt'], ['tx', A], ['qq', A, 'a.txt'],
                          ['rr', A, 'a.txt']])
    case('u alone is r', [['u', A, 'a.txt'], ['uv', A, 'a.txt']])
    case('u on an old member', [['rv', A, 'a.txt'], ['uv', A, 'a.txt'],
                                ('touch', 'a.txt', 700000000), ['uv', A, 'a.txt'],
                                ['ru', A, 'a.txt', 'b.txt'], ['tv', A]])
    case('q with a, b, i', [['qa', 'a.txt', A, 'b.txt'], ['qb', 'a.txt', A, 'b.txt'],
                           ['qi', 'a.txt', A, 'b.txt']])
    case('repeated keys', [['rvvv', A, 'a.txt', 'b.txt'], ['tvvv', A],
                           ['r' + 'v' * 130, A, 'c.txt'], ['t' + 'v' * 256, A]])
    case('missing archive', [['t', A], ['tv', A], ['x', A], ['p', A], ['d', A, 'a.txt'],
                             ['m', A, 'a.txt'], ['xv', A, 'a.txt']])
    case('not an archive', [['t', 'a.txt'], ['x', 'a.txt'], ['r', 'a.txt', 'b.txt'],
                            ['q', 'a.txt', 'b.txt'], ['t', 'c.txt']])
    case('one byte archive', [['t', 'one']], dict(b, one=b'\x65'))
    case('empty archive', [['t', 'e.a'], ['tv', 'e.a'], ['x', 'e.a'], ['p', 'e.a'],
                           ['r', 'e.a', 'a.txt'], ['d', 'e.a', 'a.txt']],
         dict(b, **{'e.a': mkar([])}))
    case('magic only, nonexistent members', [['rv', A, 'nothere.txt'],
                                             ['qv', A, 'nothere.txt'],
                                             ['q', A, 'a.txt', 'nothere.txt', 'b.txt'],
                                             ['tv', A]])
    # creating and the keys
    case('r creates', [['r', A, 'a.txt'], ['t', A]])
    case('rc creates', [['rc', A, 'a.txt'], ['t', A]])
    case('rv creates', [['rv', A, 'a.txt', 'b.txt', 'c.txt'], ['tv', A]])
    case('q creates', [['q', A, 'a.txt'], ['t', A]])
    case('qc creates', [['qc', A, 'a.txt'], ['t', A]])
    case('qv creates', [['qv', A, 'a.txt', 'b.txt'], ['tv', A]])
    case('all the sizes', [['rv', A, 'a.txt', 'b.txt', 'c.txt', 'big.bin', 'even.bin',
                            'blk512.bin', 'blk513.bin'], ['tv', A], ['xv', A],
                           ['p', A], ['pv', A]])
    case('q all the sizes', [['qv', A, 'big.bin', 'b.txt', 'blk513.bin', 'c.txt',
                              'even.bin', 'blk512.bin'], ['tv', A], ['x', A]])
    case('modes and owners', [['r', A, 'exec.sh', 'suid', 'sgid', 'sticky', 'ro.txt',
                               'own.txt', 'own2.txt'], ['tv', A]])
    case('x restores modes', [['r', A, 'exec.sh', 'suid', 'sgid', 'sticky', 'ro.txt',
                               'own.txt'], ('rm', 'exec.sh'), ('rm', 'suid'),
                              ('rm', 'sgid'), ('rm', 'sticky'), ('rm', 'ro.txt'),
                              ('rm', 'own.txt'), ['x', A], ['xv', A]])
    case('times', [['r', A, 'zero.txt', 'one.txt', 'late.txt', 'neg.txt', 'y2k.txt',
                    'dst.txt', 'a.txt'], ['tv', A]])
    case('times u', [['r', A, 'zero.txt', 'one.txt', 'late.txt', 'neg.txt', 'y2k.txt'],
                     ['ru', A, 'zero.txt', 'one.txt', 'late.txt', 'neg.txt', 'y2k.txt']])
    case('names', [['rv', A, 'exactly14chars', 'fifteen_chars_15',
                    'a_name_longer_than_fourteen.c', 'sub/e.txt', './a.txt'],
                   ['tv', A], ['t', A, 'exactly14chars'], ['t', A, 'fifteen_chars_15'],
                   ['t', A, 'a_name_longer_than_fourteen.c'], ['t', A, 'e.txt'],
                   ['t', A, 'sub/e.txt'], ['t', A, 'a.txt/']])
    case('long names added twice', [['r', A, 'a_name_longer_than_fourteen.c'],
                                    ['r', A, 'a_name_longer_than_fourteen.c'], ['tv', A],
                                    ['x', A, 'a_name_longer_than_fourteen.c'],
                                    ['d', A, 'a_name_longer_than_fourteen.c'],
                                    ['d', A, 'a_name_longer_than'], ['t', A]])
    case('AR-04: a key 256 times', [['r' + 'v' * 256, A, 'a.txt'], ['r' + 'v' * 255, 'y.a', 'a.txt'],
                                    ['rc' + 'c' * 255, 'z.a', 'a.txt'],
                                    ['rc' + 'c' * 254, 'w.a', 'a.txt']])
    case('trailing slashes', [['r', A, 'a.txt/'], ['r', A, 'sub/e.txt/'],
                              ['t', A], ['t', A, 'a.txt//'], ['d', A, 'a.txt//']])
    case('the same file twice', [['rv', A, 'a.txt', 'a.txt'], ['tv', A],
                                 ['rv', A, 'a.txt', 'a.txt', 'b.txt'], ['tv', A],
                                 ['qv', A, 'a.txt', 'a.txt']])
    case('r replaces', [['r', A, 'a.txt', 'b.txt', 'c.txt'],
                        ('write', 'b.txt', F(b'a new b, longer\n', mtime=700000000)),
                        ['rv', A, 'b.txt'], ['tv', A], ['rv', A, 'c.txt', 'a.txt'],
                        ['tv', A], ['p', A]])
    case('r with a missing file', [['r', A, 'a.txt', 'b.txt'],
                                   ['rv', A, 'nothere', 'b.txt'], ['tv', A],
                                   ['rv', A, 'nothere'], ['rv', A],
                                   ['rv', A, 'a.txt', 'nothere', 'c.txt', 'nothere2']])
    case('q appends', [['q', A, 'a.txt'], ['q', A, 'b.txt', 'c.txt'],
                       ['q', A, 'a.txt'], ['tv', A]])
    # position
    pos = [['rv', A, 'a.txt', 'b.txt', 'c.txt', 'big.bin']]
    case('ra', pos + [['rav', 'b.txt', A, 'exec.sh', 'suid'], ['tv', A]])
    case('rb', pos + [['rbv', 'b.txt', A, 'exec.sh', 'suid'], ['tv', A]])
    case('ri', pos + [['riv', 'b.txt', A, 'exec.sh', 'suid'], ['tv', A]])
    case('ra first', pos + [['ra', 'a.txt', A, 'exec.sh'], ['tv', A]])
    case('ra last', pos + [['ra', 'big.bin', A, 'exec.sh'], ['tv', A]])
    case('rb first', pos + [['rb', 'a.txt', A, 'exec.sh'], ['tv', A]])
    case('ra replacing', pos + [['rav', 'a.txt', A, 'b.txt', 'c.txt'], ['tv', A],
                               ['rbv', 'c.txt', A, 'a.txt'], ['tv', A]])
    case('ra the replaced one itself', pos + [['rav', 'b.txt', A, 'b.txt'], ['tv', A],
                                              ['rbv', 'b.txt', A, 'b.txt'], ['tv', A]])
    case('ra missing position', pos + [['rav', 'nothere', A, 'exec.sh', 'suid'],
                                       ['tv', A]])
    case('ra with a path', pos + [['rav', 'sub/b.txt', A, 'exec.sh'], ['tv', A],
                                  ['rav', './b.txt', A, 'suid'], ['tv', A]])
    longpos = ['a_name_longer_than_fourteen.c']
    case('ra with a long position name', [['r', A, 'a_name_longer_than_fourteen.c', 'a.txt'],
                                          ['rav', 'a_name_longer_than_fourteen.c', A, 'b.txt'],
                                          ['tv', A], ['rbv', 'a_name_longer_', A, 'c.txt'],
                                          ['tv', A], ['mav', 'a_name_longer_than', A, 'a.txt'],
                                          ['tv', A], ['mbv', 'a_name_longer_', A, 'a.txt'],
                                          ['tv', A]])
    case('ra no files', pos + [['ra', 'a.txt', A], ['rb', 'a.txt', A], ['ra', A]])
    case('ra missing file', pos + [['rav', 'b.txt', A, 'nothere', 'exec.sh'],
                                   ['tv', A]])
    case('ra ru', pos + [['rau', 'b.txt', A, 'exec.sh', 'a.txt', 'b.txt'], ['tv', A]])
    case('ra and ab both', pos + [['rab', 'b.txt', A, 'exec.sh'], ['tv', A],
                                  ['raa', 'b.txt', A, 'suid'], ['tv', A]])
    case('ma mb mi', pos + [['mav', 'c.txt', A, 'a.txt'], ['tv', A],
                           ['mbv', 'big.bin', A, 'b.txt', 'c.txt'], ['tv', A],
                           ['miv', 'a.txt', A, 'big.bin'], ['tv', A]])
    case('m to the end', pos + [['mv', A, 'a.txt'], ['tv', A], ['mv', A, 'a.txt', 'b.txt'],
                               ['tv', A], ['mv', A, 'nothere'], ['mv', A]])
    case('m onto itself', pos + [['mav', 'a.txt', A, 'a.txt'], ['tv', A],
                                ['mbv', 'b.txt', A, 'b.txt'], ['tv', A]])
    case('m missing position', pos + [['mav', 'nothere', A, 'a.txt'], ['tv', A]])
    case('qa error', pos + [['qav', 'a.txt', A, 'exec.sh'], ['tv', A]])
    # l: the temporary files in the current directory
    case('rl', pos + [['rl', A, 'exec.sh'], ['rlv', A, 'a.txt'], ['dl', A, 'a.txt'],
                      ['ml', A, 'b.txt'], ['ral', 'c.txt', A, 'suid'], ['tv', A]])
    case('rl qualified', pos + [['rail', 'c.txt', A, 'suid'], ['tv', A]])
    # d, x, p, t
    case('d', pos + [['d', A, 'b.txt'], ['tv', A], ['dv', A, 'a.txt', 'c.txt'], ['tv', A],
                     ['dv', A, 'nothere'], ['dv', A], ['dv', A, 'big.bin'], ['tv', A]])
    case('d a long name', pos + [['d', A, 'a_name_longer_than_fourteen.c'], ['tv', A],
                                 ['dv', A, 'big.bin', 'big.bin'], ['tv', A]])
    case('d vv', pos + [['dvv', A, 'b.txt'], ['tv', A], ['dvv', A, 'a.txt', 'a.txt']])
    case('d twice', pos + [['dv', A, 'a.txt', 'a.txt'], ['tv', A]])
    case('x', pos + [('rm', 'a.txt'), ('rm', 'b.txt'), ('rm', 'c.txt'), ('rm', 'big.bin'),
                     ['x', A], ['xv', A], ['xv', A, 'b.txt'], ['xv', A, 'nothere'],
                     ['x', A, 'a.txt', 'nothere']])
    case('x partly', pos + [['xv', A, 'c.txt'], ['xv', A, 'big.bin', 'a.txt'],
                            ['xvv', A, 'b.txt']])
    case('x overwrites', pos + [('write', 'a.txt', F(b'XXXXXXXXXXXXXXXXXXXXXXXX')),
                                ['xv', A, 'a.txt'], ['p', A, 'a.txt']])
    case('x a file that cannot be created',
         pos + [('rm', 'a.txt'), ('mkdir', 'a.txt'), ['xv', A, 'a.txt'],
                ['xv', A, 'b.txt', 'a.txt']])
    case('write errors', pos + [('full', ['p', A]), ('full', ['pv', A, 'a.txt']),
                                ('full', ['t', A]), ('full', ['tv', A]),
                                ('full', ['xv', A, 'a.txt']), ('full', ['rv', A, 'c.txt'])])
    case('p', pos + [['p', A], ['pv', A], ['p', A, 'b.txt'], ['pv', A, 'b.txt', 'a.txt'],
                     ['p', A, 'nothere'], ['pv', A, 'nothere']])
    case('t', pos + [['t', A], ['t', A, 'a.txt'], ['t', A, 'big.bin', 'a.txt'],
                     ['t', A, 'nothere'], ['tv', A, 'nothere', 'b.txt'], ['tvv', A],
                     ['t', A, 'a.txt', 'a.txt']])
    case('t with paths', pos + [['t', A, 'sub/b.txt'], ['t', A, './b.txt'],
                                ['tv', A, 'b.txt/']])
    # damaged archives
    good = mkar([('a.txt', b'hello\n'), ('b.txt', b'odd\0'[:3]), ('c.txt', b'')])
    case('truncated in a header', [['t', 'bad.a'], ['tv', 'bad.a'], ['x', 'bad.a'],
                                   ['p', 'bad.a'], ['r', 'bad.a', 'a.txt'], ['tv', 'bad.a'],
                                   ['d', 'bad.a', 'a.txt'], ['m', 'bad.a', 'a.txt']],
         dict(b, **{'bad.a': good + b'half a hea'}))
    case('truncated in data', [['tv', 'bad.a'], ['x', 'bad.a'], ['p', 'bad.a'],
                               ['r', 'bad.a', 'a.txt'], ['tv', 'bad.a'], ['d', 'bad.a', 'a.txt'],
                               ['m', 'bad.a', 'a.txt'], ['q', 'bad.a', 'b.txt'],
                               ['tv', 'bad.a']],
         dict(b, **{'bad.a': good + hdr('d.txt', 100) + b'only a few'}))
    # copyfil()'s count of short reads is a 16-bit int: 65536 of them are none
    case('AR-05: 65536 phase errors', [['t', 'pe0.a'], ['t', 'pe1.a'], ['t', 'pe2.a'],
                                       ['tv', 'pe0.a']],
         dict(b, **{'pe0.a': struct.pack('<H', ARMAG) + hdr('w.txt', 65536 * 512),
                    'pe1.a': struct.pack('<H', ARMAG) + hdr('w.txt', 65535 * 512),
                    'pe2.a': struct.pack('<H', ARMAG) + hdr('w.txt', 65537 * 512)}))
    case('truncated in the pad byte', [['tv', 'bad.a'], ['x', 'bad.a'], ['p', 'bad.a'],
                                       ['r', 'bad.a', 'a.txt'], ['tv', 'bad.a']],
         dict(b, **{'bad.a': mkar([('a.txt', b'hello\n')]) + hdr('o.txt', 3) + b'odd'}))
    case('an empty member then a short one', [['tv', 'bad.a'], ['x', 'bad.a'],
                                              ['d', 'bad.a', 'a.txt'], ['m', 'bad.a', 'a.txt']],
         dict(b, **{'bad.a': mkar([('e.txt', b'')]) + hdr('s.txt', 1) + b''}))
    case('negative and large sizes', [['tv', 'neg.a'], ['x', 'neg.a'], ['p', 'neg.a'],
                                     ['d', 'neg.a', 'a.txt'], ['r', 'neg.a', 'b.txt']],
         dict(b, **{'neg.a': struct.pack('<H', ARMAG) + hdr('n.txt', -2)
                    + hdr('m.txt', 70000) + b'tail'}))
    case('size 65536 and 32768', [['tv', 'big.a'], ['x', 'big.a'], ['p', 'big.a']],
         dict(b, **{'big.a': struct.pack('<H', ARMAG) + hdr('p.txt', 65536)
                    + b'\x31' * 65536 + hdr('q.txt', 32768) + b'\x32' * 32768}))
    case('odd sizes on the edge', [['tv', 'odd.a'], ['x', 'odd.a'], ['d', 'odd.a', 'x.txt'],
                                   ['tv', 'odd.a']],
         dict(b, **{'odd.a': mkar([('x.txt', b'1'), ('y.txt', b'123'),
                                   ('z.txt', b'12345')])}))
    case('names without a NUL', [['tv', 'nn.a'], ['x', 'nn.a'], ['t', 'nn.a', 'abcdefghijklmn'],
                                 ['d', 'nn.a', 'abcdefghijklmn'], ['tv', 'nn.a']],
         dict(b, **{'nn.a': mkar([(b'abcdefghijklmn', b'full name'),
                                  (b'abc\0defghijklmn', b'garbage after the NUL'),
                                  (b'\0' * 14, b'empty name'),
                                  (b'/ab/c', b'slash in a name'),
                                  (b'dir/', b'dir slash')])}))
    case('names with high bytes', [['tv', 'nh.a'], ['x', 'nh.a'], ['t', 'nh.a', '\xe9\xe8']],
         dict(b, **{'nh.a': mkar([(b'\xe9\xe8t.txt', b'high'), (b'-dash', b'dash'),
                                  (b'sp ace', b'space'), (b'tab\there', b'tab')])}))
    case('modes and owners in an archive', [['tv', 'mo.a'], ['xv', 'mo.a'], ['pv', 'mo.a']],
         dict(b, **{'mo.a': mkar([
             ('m0', b'0', 566554203, 0, 0, 0), ('m777', b'7', 566554203, 1, 1, 0o777),
             ('m4755', b'4', 566554203, 255, 128, 0o4755),
             ('m2755', b'2', 566554203, 127, 255, 0o2755),
             ('m1777', b'1', 566554203, 99, 100, 0o1777),
             ('mfull', b'f', 566554203, 200, 200, 0o177777),
             ('m100000', b'f', 566554203, 128, 129, 0o100000),
             ('m40755', b'd', 566554203, 5, 6, 0o40755),
             ('mtimeneg', b'n', 3000000000, 1, 2, 0o644),
             ('mtimemax', b'n', 4294967295, 1, 2, 0o644),
             ('mtimemin', b'n', 0x80000000, 1, 2, 0o644),
             ('mtimeeven', b'n', 86400, 1, 2, 0o644),
             ('mtime1969', b'n', 0xffffffff - 86400, 1, 2, 0o644)])}))
    case('dates', [['tv', 'dt.a']],
         dict(b, **{'dt.a': mkar([
             ('d%d' % i, b'.', t) for i, t in enumerate(
                 [0, 1, 59, 60, 3599, 3600, 86399, 86400, 951782400, 951868800,
                  978220800, 1009756800, 1078012800, 1109548800, 1330473600,
                  1583020800, 1635724799, 2147483647, 2147483648, 4294967295,
                  4000000000, 3000000000, 2500000000, 1000000000, 1234567890,
                  631152000, 315532800, 567993600, 1999999999, 2000000000,
                  1500000000, 1700000000, 1900000000, 1950000000])])}))
    # the roots' temporary files
    case('failed r leaves nothing', [['r', 'nothere/x.a', 'a.txt'], ['r', A, 'a.txt'],
                                     ['rv', 'a.txt', 'b.txt']])
    # directories: the emulated original cannot open them (ENOENT), the
    # port refuses where the original would read the entries
    case('a directory as the archive', [['t', 'sub'], ['x', 'sub'], ['r', 'sub', 'a.txt'],
                                        ['q', 'sub', 'a.txt'], ['d', 'sub', 'a.txt']])
    case('a directory as a member', [['r', A, 'sub'], ['q', A, 'a.txt', 'sub'],
                                     ['r', A, 'a.txt', 'sub', 'b.txt']])
    case('a directory as a position', [['r', A, 'a.txt'], ['ra', 'sub', A, 'b.txt']])
    case('archive in a subdirectory', [['rv', 'sub/s.a', 'a.txt'], ['tv', 'sub/s.a'],
                                       ['rv', 'sub/s.a', 'b.txt'], ['q', 'sub/s.a', 'c.txt'],
                                       ['x', 'sub/s.a']])
    case('archive named like a member', [['rv', 'a.txt', 'b.txt']])
    case('archive as a member', [['r', A, 'a.txt'], ['rv', A, A], ['tv', A], ['qv', A, A]])
    case('everything', [['rv', A] + sorted(k for k in b if not k.endswith('/')
                                           and k not in ('neg.txt',)),
                        ['tv', A], ['xv', A], ['dv', A, 'a.txt', 'big.bin'], ['tv', A]])
    case('many members', [['rv', A] + ['m%03d' % i for i in range(120)], ['tv', A],
                          ['dv', A] + ['m%03d' % i for i in range(0, 120, 3)], ['t', A]],
         dict(b, **{'m%03d' % i: F(b'member %d\n' % i, mtime=600000000 + i)
                    for i in range(120)}))
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
        if a == '-j' and not args:
            jobs = int(argv[i + 1]); i += 2
        elif a == '--port' and not args:
            port = os.path.abspath(argv[i + 1]); i += 2
        elif a == '--show' and not args:
            show = int(argv[i + 1]); i += 2
        elif a == '--keep' and not args:
            keep = os.path.abspath(argv[i + 1]); i += 2
        elif args and args[0] == 'cases' and a in ('-j', '--port', '--show', '--keep'):
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
            args.append(a); i += 1
    if not args or args[0] not in ('cases', 'run'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    if args[0] == 'cases':
        work, fn = [(c, port, keep) for c in cases()], _case_one
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
    for b in bad[:show or 20]:
        print(b)
    print('ar port against the original (%s): %d runs, %s'
          % (args[0], n, ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
