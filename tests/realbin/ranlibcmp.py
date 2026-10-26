#!/usr/bin/env python3
"""ranlibcmp.py - mutos_ranlib, the port of the real ranlib, against the original.

The original MUTOS 1700 ranlib (mutos/mutos_bin/ranlib.golden.base64.txt)
runs under mutosemu.py, and so does the ar it starts through /bin/sh -c:
the faked exece() of the emulator hands "ar ..." to the original ar
(mutos/mutos_bin/ar.golden.base64.txt) in the same directory and root.
mutos_ranlib (src/mutos_ranlib/) runs on the host with TZ=UTC, and starts
$MUTOS_ROOT/bin/ar - here a link to mutos_ar. A case is a directory of
files and a sequence of steps - ranlib command lines, ar command lines
(["ar", ...]), changes of the files - run in two copies of the directory,
each with a MUTOS root of its own. After every step their messages, exit
status and both trees - every file's name, bytes and mode - must be the
same. The dates that come from the clock - the table's (five seconds past
ranlib's clock), the new __.SYMDEF file's, the first member's when it is no
table - cannot be: a member's date within the seconds the case has run
(and the five) counts as the same date.

A refusal of the port (exit status 125 and a last message ending in
"refused") counts apart: what it wrote before must be a prefix of what the
original wrote, and the sequence ends there. "limits" (the port wrote past
the harness's 16 MB file size limit) and "slow" (an ar step on a damaged
archive whose member is some hundreds of megabytes long: the emulated
original is stopped by the instruction budget, the port finishes) are
counted apart as well and are no difference; "endless" is a run neither of
them finishes (a negative member size).

Usage:
    ranlibcmp.py cases [-j N] [--port PATH] [--ar PATH] [--show N] [--keep DIR]
        the hand-written cases: the members of every kind, the messages,
        the leak of the stdio slots, a table of 700 and of 701 symbols,
        the tables of archives that have one already, the real libc.a
    ranlibcmp.py libc [--ar PATH] [--port PATH]
        libc.a rebuilt from its 167 objects with mutos_ar and mutos_ranlib:
        the same bytes as the original archive, __.SYMDEF's date, group
        and mode aside
    ranlibcmp.py run [ARG...]
        one command line, in a copy of the current directory

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import io
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arcmp          # noqa: E402
import ldcmp          # noqa: E402
import mutosemu       # noqa: E402
from arcmp import F   # noqa: E402

ROOT = arcmp.ROOT
PORT = os.path.join(ROOT, 'src', 'mutos_ranlib', 'mutos_ranlib')
AR_PORT = arcmp.PORT
ARMAG = 0o177545
MAX_INSNS = 40_000_000  # the whole libc.a takes a few million: past this it is endless


# ---- the originals ------------------------------------------------------------

def _on_exec(events):
    """An on_exec for mutosemu.Emu: /bin/sh -c "ar ..." runs the original
    ar (nested, in the same directory, root and open files); any other
    command is "not found" (127)."""
    def on_exec(emu, path, argv, envp, pid):
        events.append((path, list(argv)))
        if path != '/bin/sh' or argv[:2] != ['sh', '-c'] or len(argv) < 3:
            host = emu._path(path)
            return None if not os.path.isfile(host) else 0
        words = argv[2].split()
        if not words or words[0] != 'ar':
            return 127 << 8
        child = mutosemu.Emu(mutosemu.load_image('ar'), words, cwd=emu.cwd,
                             root=emu.root, fds=emu.fds, pid=pid,
                             on_exec=on_exec, umask=emu.umask, envp=envp)
        st = child.run(MAX_INSNS)
        if child.killed:
            return child.killed
        return (st & 0xFF) << 8
    return on_exec


def run_orig(tool, args, cwd, root, max_insns=MAX_INSNS):
    """(status or None, stdout, stderr, why it did not finish); tool: 'ranlib'
    or 'ar'."""
    out, err = arcmp._Capped(), arcmp._Capped()
    emu = mutosemu.Emu(mutosemu.load_image(tool), [tool] + list(args), cwd=cwd,
                       stdin=io.BytesIO(b''), stdout=out, stderr=err, root=root,
                       on_exec=_on_exec([]))
    why, st = '', None
    try:
        st = emu.run(max_insns)
    except arcmp._Limit:
        why = 'endless: messages past %d bytes' % arcmp.OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why:
            why = 'endless: ' + why
    return st, out.getvalue(), err.getvalue(), why


def run_port(tool, args, cwd, root, ranlib_port, ar_port):
    if tool == 'ar':
        return arcmp.run_port(args, cwd, root, ar_port)
    return arcmp.run_port(args, cwd, root, ranlib_port)


# ---- the trees ----------------------------------------------------------------

def snapshot(d, lo, hi):
    """arcmp.snapshot, with the dates that come from the clock: in an
    archive, the date of any member within [lo, hi] - the seconds the case
    has run, five added for the table's date - is zeroed. (The dates ranlib
    and ar give: a table's five seconds past the clock, the new __.SYMDEF
    file's, the first member's when it is no table; the runs of the
    original and the port are seconds apart.)"""
    return arcmp.snapshot(d, (lo, hi))


def judge(ost, oout, oerr, why, pst, pout, perr, do, dp, window, tool='ranlib'):
    """arcmp.judge for ranlib: the refusal is ranlib's own line."""
    if pst == 'XFSZ':
        return 'limits', 'the port wrote past the file size limit'
    lines = perr.decode('latin1').rstrip('\n').split('\n')
    refused = (pst == 125 and lines[-1].startswith(('ranlib: ', 'ar: '))
               and lines[-1].endswith('refused'))
    if ost is None and pst is None and why.startswith('endless'):
        n = min(len(pout), len(oout))
        if pout[:n] == oout[:n]:
            return 'endless', why
        return 'DIFFERENT', 'both endless (%s), the messages differ' % why
    if (ost is None and tool == 'ar' and pst is not None and not refused
            and why.startswith('endless: program stopped')):
        # ar's loops end with the sizes in the archive's headers, but a
        # member of some hundreds of megabytes (a damaged header) takes the
        # emulated original more than MAX_INSNS instructions where the port
        # takes a moment: not compared (arcmp.py's budget is larger, and
        # the case was seen to finish with the same messages)
        n = min(len(oout), len(pout))
        if pout[:n] == oout[:n]:
            return 'slow', 'the original is stopped by the instruction budget ' \
                '(%s); the port finished, exit %s' % (why, pst)
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original ' \
            '%s' % ('exit %d' % ost if ost is not None else why)
    ok = False
    if refused:
        # what it said before the refusal must be what the original said
        before = '\n'.join(lines[:-1]).encode('latin1')
        before += b'\n' if before else b''
        ok = oout.startswith(pout) and oerr.startswith(before)
    if ost is None:
        if refused and ok:
            return 'orig-fails', 'the original does not finish (%s); the port ' \
                'refused: %s' % (why, lines[-1])
        if refused:
            return 'BAD-REFUSAL', 'the original does not finish; the port\'s ' \
                'output is no prefix: %s' % lines[-1]
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
    arcmp._same_trees(snapshot(dp, *window), snapshot(do, *window), parts)
    if parts:
        return 'DIFFERENT', '; '.join(parts)
    return 'same', ''


def compare_seq(steps, files, port=PORT, ar_port=AR_PORT, keep=None, name=''):
    """arcmp.compare_seq for ranlib: a step is a list of ranlib's arguments,
    ['ar', ...] for ar's, or a file operation as arcmp.compare_seq has them
    (('full', ...) excepted)."""
    results = []
    t_case = time.time()
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    import resource
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))
    with tempfile.TemporaryDirectory(prefix='rlcmp') as tmp:
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        ro, rp = os.path.join(tmp, 'orig-root'), os.path.join(tmp, 'port-root')
        for d in (do, dp):
            os.makedirs(d)
            arcmp.place(d, files)
        for r in (ro, rp):
            for sub in ('tmp', 'bin'):
                os.makedirs(os.path.join(r, sub))
        os.symlink(os.path.abspath(ar_port), os.path.join(rp, 'bin', 'ar'))
        for i, st in enumerate(steps):
            if isinstance(st, tuple):
                for d in (do, dp):
                    p = os.path.join(d, st[1])
                    if st[0] == 'write':
                        arcmp.place(d, {st[1]: st[2]})
                    elif st[0] == 'mkdir':
                        os.makedirs(p)
                    elif not os.path.exists(p):
                        pass
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
            tool, args = ('ar', st[1:]) if st[:1] == ['ar'] else ('ranlib', st)
            ost, oout, oerr, why = run_orig(tool, args, do, ro)
            pst, pout, perr = run_port(tool, args, dp, rp, port, ar_port)
            window = (int(t_case) - 2, int(time.time()) + 7)
            kind, detail = judge(ost, oout, oerr, why, pst, pout, perr, do, dp,
                                 window, tool)
            if kind == 'same':
                parts = []
                # the roots' tmp/ (bin/ holds the link to the port's ar)
                sr = {k: v for k, v in arcmp.snapshot(rp).items()
                      if not k.startswith('bin')}
                so = {k: v for k, v in arcmp.snapshot(ro).items()
                      if not k.startswith('bin')}
                arcmp._same_trees(sr, so, parts)
                if parts:
                    kind, detail = 'DIFFERENT', 'root: ' + '; '.join(parts)
            for d in (do, dp):
                arcmp.settle_times(d)
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


# ---- objects and archives -------------------------------------------------------

W = lambda *v: struct.pack('<%dH' % len(v), *v)


def obj(syms, magic=0o407, text=b'\xc3\x90', data=b'', bss=0, flag=0, spare=0,
        extra=b''):
    """An a.out member with the symbols [(name, type)]; spare: the byte
    behind n_type; extra: bytes after the symbol table."""
    ents = b''
    for n, t in syms:
        ents += n.encode('latin1')[:8].ljust(8, b'\0') + bytes([t & 0xff, spare]) \
            + W(0)
    text += b'\0' * (len(text) & 1)
    data += b'\0' * (len(data) & 1)
    rel = b'' if flag else b'\0' * (len(text) + len(data))
    return struct.pack('<8H', magic, len(text), len(data), bss, len(ents), 0, 0,
                       flag) + text + data + rel + ents + extra


EXT_TEXT, EXT_DATA, EXT_BSS, EXT_ABS, EXT_UNDF = 0x22, 0x23, 0x24, 0x21, 0x20
LOC_TEXT, LOC_DATA = 0x02, 0x03


def libc_members():
    """[(name, bytes)] of the real libc.a, __.SYMDEF excluded, and its
    __.SYMDEF."""
    lib = ldcmp.libc()
    p, out, symdef = 2, [], None
    while p + 26 <= len(lib):
        name = lib[p:p + 14].split(b'\0')[0].decode('latin1')
        hi, lo = struct.unpack_from('<HH', lib, p + 14)
        shi, slo = struct.unpack_from('<HH', lib, p + 22)
        size = (shi << 16) | slo
        data = lib[p + 26:p + 26 + size]
        if name == '__.SYMDEF':
            symdef = data
        else:
            out.append((name, data, (hi << 16) | lo))
        p += 26 + size + (size & 1)
    return out, symdef


def cases():
    """[(name, files, steps)]"""
    out = []

    def case(name, files, steps):
        out.append((name, files, steps))

    A = 'x.a'
    o1 = obj([('_main', EXT_TEXT), ('_foo', EXT_UNDF)])
    o2 = obj([('_foo', EXT_TEXT), ('_fdat', EXT_DATA), ('_fbss', EXT_BSS),
              ('Lloc', LOC_TEXT), ('local', LOC_DATA)], data=W(1, 2), bss=4)
    o3 = obj([('_abs', EXT_ABS), ('_fname', 0x3f), ('_reg', 0x34), ('_x', 0x25),
              ('_y', 0x26), ('_z', 0x3e), ('_w', 0x5f), ('_v', 0x40 | 0x22),
              ('_long_name_x', EXT_TEXT)])
    o4 = obj([('_und1', EXT_UNDF), ('_und2', EXT_UNDF), ('loc', LOC_TEXT)])
    o5 = obj([])
    o407 = obj([('_m407', EXT_TEXT)], magic=0o407)
    o410 = obj([('_m410', EXT_TEXT)], magic=0o410)
    o411 = obj([('_m411', EXT_TEXT)], magic=0o411)
    o405 = obj([('_m405', EXT_TEXT)], magic=0o405)
    o412 = obj([('_m412', EXT_TEXT)], magic=0o412)
    ostrip = obj([('_strip', EXT_TEXT)], flag=1)
    oodd = obj([('_odd', EXT_TEXT)], text=b'\xc3', data=b'\x01')   # odd text
    ospare = obj([('_spare', EXT_TEXT), ('_spar2', 0x20)], spare=7)
    ocut = obj([('_cut1', EXT_TEXT), ('_cut2', EXT_TEXT)])[:-5]    # cut in an entry
    otail = obj([('_tail', EXT_TEXT)], extra=b'junk after the symbols')
    text = b'not an object file, just text\n'
    files = {
        'm1.o': o1, 'm2.o': o2, 'm3.o': o3, 'm4.o': o4, 'm5.o': o5,
        'm407.o': o407, 'm410.o': o410, 'm411.o': o411, 'm405.o': o405,
        'm412.o': o412, 'mstrip.o': ostrip, 'modd.o': oodd, 'mspare.o': ospare,
        'mcut.o': ocut, 'mtail.o': otail, 'mtext.c': text, 'tiny': b'1',
        'empty': b'', 'garbage': bytes(range(256)) * 3,
    }
    files = {k: F(v) for k, v in files.items()}

    def ar(*names):
        return [['ar', 'rc', A] + list(names)]

    case('no arguments', files, [[]])
    case('missing file', files, [['nothere'], ['nothere', 'nothere2']])
    case('not archives', files, [['m1.o'], ['tiny'], ['empty'], ['garbage'],
                                 ['mtext.c', 'empty', 'tiny']])
    case('empty archive', dict(files, **{'e.a': F(arcmp.mkar([]))}),
         [['e.a'], ['ar', 'tv', 'e.a']])
    case('magic and a half header', dict(files, **{'h.a': F(
        arcmp.mkar([]) + b'half a hea')}), [['h.a'], ['ar', 'tv', 'h.a']])
    case('one member', files, ar('m1.o') + [['x.a'], ['ar', 'tv', A], ['x.a'],
                                             ['ar', 'tv', A]])
    case('two members', files, ar('m1.o', 'm2.o') + [['x.a'], ['ar', 'tv', A],
                                                     ['x.a'], ['ar', 'tv', A]])
    case('every symbol type', files, ar('m3.o', 'm4.o', 'm5.o') +
         [['x.a'], ['ar', 'tv', A], ['x.a']])
    case('every magic number', files, ar('m407.o', 'm410.o', 'm411.o', 'm405.o',
                                         'm412.o', 'mtext.c', 'tiny', 'empty') +
         [['x.a'], ['ar', 'tv', A]])
    case('a header without relocation', files, ar('mstrip.o', 'm1.o', 'mstrip.o') +
         [['x.a']])
    case('odd members', files, ar('modd.o', 'm1.o', 'modd.o', 'm2.o') + [['x.a']])
    case('a spare byte in the type', files, ar('mspare.o', 'm1.o') + [['x.a']])
    case('a symbol table cut short', files, ar('m1.o', 'mcut.o', 'm2.o') +
         [['x.a'], ['ar', 'tv', A]])
    case('a symbol table cut short first', files, ar('mcut.o', 'm2.o') +
         [['x.a'], ['ar', 'tv', A]])
    case('bytes after the symbols', files, ar('mtail.o', 'm1.o') + [['x.a']])
    case('no name list', files, ar('m5.o', 'm1.o', 'm5.o') + [['x.a'], ['ar', 'tv', A]])
    case('no name list, a long name', dict(files, **{
        'a_name_of_fourteen_chars.o': F(o5, uid=7, gid=9, mtime=1234567890),
        'exactly14chars': F(o5, uid=7, gid=9, mtime=1234567890),
        'exactly14chars2': F(o5, uid=0, gid=0, mtime=1234567890),
        'x' * 14: F(o5, uid=255, gid=255, mtime=0x01020304),
        'w' * 13: F(o5, uid=1, gid=2, mtime=0x01010101)}),
         ar('a_name_of_fourteen_chars.o', 'exactly14chars', 'exactly14chars2',
            'x' * 14, 'w' * 13, 'm1.o') + [['x.a']])
    case('named 14 or 15', files, ar('m1.o', 'm2.o') + [['x.a'], ['x.a'], ['x.a']])
    case('a table in the middle', files, ar('m1.o') + [['x.a']] + [
        ['ar', 'rc', A, 'm2.o'], ['x.a'], ['ar', 'tv', A], ['ar', 'ma', '__.SYMDEF', A, 'm1.o'],
        ['x.a'], ['ar', 'tv', A]])
    case('twice and again', files, ar('m1.o', 'm2.o') + [['x.a'], ['x.a'], ['x.a'],
                                                         ['ar', 'tv', A]])
    case('two archives', files, [['ar', 'rc', 'a.a', 'm1.o'], ['ar', 'rc', 'b.a', 'm2.o'],
                                 ['a.a', 'b.a'], ['ar', 'tv', 'a.a'], ['ar', 'tv', 'b.a']])
    case('a good one after bad ones', files, [['ar', 'rc', 'a.a', 'm1.o', 'm2.o'],
                                              ['nothere', 'tiny', 'empty', 'a.a', 'garbage'],
                                              ['ar', 'tv', 'a.a']])
    case('an empty file after an archive', files, [['ar', 'rc', 'a.a', 'm1.o', 'm2.o'],
                                                   ['a.a', 'empty'], ['empty', 'a.a', 'empty']])
    case('a one byte file after an archive', files, [['ar', 'rc', 'a.a', 'm1.o'],
                                                     ['a.a', 'tiny']])
    case('17 files that are no archives', files, [['m1.o'] * 17, ['m1.o'] * 17 + ['nothere']])
    case('16 files that are no archives', files, [['ar', 'rc', 'a.a', 'm1.o', 'm2.o'],
                                                  ['tiny'] * 16 + ['a.a'], ['ar', 'tv', 'a.a']])
    case('17 non archives and a good one', files, [['ar', 'rc', 'a.a', 'm1.o', 'm2.o'],
                                                   ['tiny'] * 17 + ['a.a'],
                                                   ['ar', 'tv', 'a.a']])
    case('18 non archives', files, [['ar', 'rc', 'a.a', 'm1.o'], ['tiny'] * 18 + ['a.a']])
    case('missing files do not leak', files, [['ar', 'rc', 'a.a', 'm1.o'],
                                              ['nothere'] * 30 + ['a.a'],
                                              ['ar', 'tv', 'a.a']])
    case('archives do not leak', files, [['ar', 'rc', 'a.a', 'm1.o'], ['a.a'] * 40,
                                         ['ar', 'tv', 'a.a']])
    # the table's limits
    many = lambda n: obj([('_s%d' % i, EXT_TEXT) for i in range(n)])
    case('699 symbols', dict(files, **{'big.o': F(many(699))}),
         ar('big.o') + [['x.a'], ['ar', 'tv', A]])
    case('700 symbols', dict(files, **{'big.o': F(many(700))}),
         ar('big.o') + [['x.a'], ['ar', 'tv', A]])
    case('701 symbols', dict(files, **{'big.o': F(many(701))}),
         ar('big.o') + [['x.a'], ['ar', 'tv', A]])
    case('700 symbols in 5 members', dict(files, **{'big.o': F(many(100))}),
         ar(*(['big.o'] * 7)) + [['x.a']])
    case('701 symbols in 5 members', dict(files, **{'big.o': F(many(100)),
                                                     'one.o': F(obj([('_one', EXT_TEXT)]))}),
         ar(*(['big.o'] * 7 + ['one.o'])) + [['x.a']])
    case('symbols, not defined', dict(files, **{'big.o': F(obj(
        [('_u%d' % i, EXT_UNDF) for i in range(1000)]))}), ar('big.o') + [['x.a']])
    # the layout of a header in the archive
    case('members with odd owners and times', dict(files, **{
        'u1.o': F(o1, mtime=0, uid=255, gid=255, mode=0o4755),
        'u2.o': F(o2, mtime=2147483647, uid=128, gid=127, mode=0o2755),
        'u3.o': F(o3, mtime=3000000000, uid=1, gid=1, mode=0o1777)}),
         ar('u1.o', 'u2.o', 'u3.o') + [['x.a'], ['ar', 'tv', A]])
    # a damaged archive
    good = arcmp.mkar([('m1.o', o1), ('m2.o', o2), ('m4.o', o4)])
    case('a member size past the end', dict(files, **{'d.a': F(
        good + arcmp.hdr('m3.o', 5000) + o3)}), [['d.a'], ['ar', 'tv', 'd.a']])
    case('an odd size at the end', dict(files, **{'d.a': F(
        good + arcmp.hdr('m3.o', len(o3) | 1) + o3)}), [['d.a'], ['ar', 'tv', 'd.a']])
    case('a size of 0, and negative', dict(files, **{'d.a': F(
        good + arcmp.hdr('z.o', 0) + arcmp.hdr('m3.o', len(o3)) + o3)}),
         [['d.a'], ['ar', 'tv', 'd.a']])
    case('a short member', dict(files, **{'d.a': F(
        arcmp.mkar([('s.o', o1[:10]), ('m2.o', o2)]))}), [['d.a'], ['ar', 'tv', 'd.a']])
    case('a member that is only a header', dict(files, **{'d.a': F(
        arcmp.mkar([('s.o', o1[:16]), ('m2.o', o2)]))}), [['d.a'], ['ar', 'tv', 'd.a']])
    case('a big a_syms', dict(files, **{'d.a': F(arcmp.mkar([('s.o', struct.pack(
        '<8H', 0o407, 2, 0, 0, 65535, 0, 0, 1) + b'\xc3\x90' + b'\0' * 100)]))}),
         [['d.a'], ['ar', 'tv', 'd.a']])
    case('text and data past the end', dict(files, **{'d.a': F(arcmp.mkar([(
        's.o', struct.pack('<8H', 0o407, 60000, 60000, 0, 24, 0, 0, 0) + b'\0' * 40),
        ('m2.o', o2)]))}), [['d.a'], ['ar', 'tv', 'd.a']])
    case('a negative member size', dict(files, **{'d.a': F(
        arcmp.mkar([('m1.o', o1)]) + arcmp.hdr('n.o', -2) + b'tail of nothing')}),
         [['d.a'], ['ar', 'tv', 'd.a']])
    case('a huge member size', dict(files, **{'d.a': F(
        arcmp.mkar([('m1.o', o1)]) + arcmp.hdr('n.o', 0x7ffffff0) + b'tail')}),
         [['d.a'], ['ar', 'tv', 'd.a']])
    # names for ar
    case('a name with a dash', dict(files, **{'-x.a': F(good)}), [['-x.a']])
    case('a name with a path', dict(files, **{'sub/x.a': F(good)}), [['sub/x.a'],
                                                                      ['ar', 'tv', 'sub/x.a']])
    case('a dot path', dict(files, **{'x.a': F(good)}), [['./x.a'], ['./x.a']])
    case('names the shell would split', dict(files, **{'a b.a': F(good), 'c;d.a': F(good),
                                                        '$x.a': F(good), '*.a': F(good)}),
         [['a b.a'], ['c;d.a'], ['$x.a'], ['*.a'], ['']])
    case('a long name', dict(files, **{'a' * 230 + '.a': F(good)}), [['a' * 230 + '.a']])
    case('a name past the buffer', dict(files, **{'d/' * 120 + 'x.a': F(good)}),
         [['d/' * 120 + 'x.a']])
    case('a directory', dict(files, **{'dir/x': F(b'1')}), [['dir'], ['dir', 'tiny']])
    case('a read-only directory entry for the table', dict(files, **{'__.SYMDEF/': b''}),
         ar('m1.o') + [['x.a']])
    case('a table file already there', dict(files, **{'__.SYMDEF': F(b'old')}),
         ar('m1.o') + [['x.a'], ['ar', 'tv', A]])
    # the real libc.a
    lib = ldcmp.libc()
    case('the real libc.a', dict(files, **{'libc.a': F(lib)}),
         [['libc.a'], ['libc.a']])
    members, symdef = libc_members()
    sub = [m for m in members][:12]
    case('twelve objects of libc.a', {n: F(d, mtime=t, gid=1, mode=0o664)
                                      for n, d, t in sub},
         [['ar', 'rc', 'l.a'] + [n for n, d, t in sub], ['l.a'], ['ar', 'tv', 'l.a'],
          ['l.a']])
    return out


def libc_check(port, ar_port):
    """libc.a rebuilt from its 167 objects with the ports: ar r in the
    original's order, then ranlib; the same bytes as the original archive
    but __.SYMDEF's date, group and mode."""
    members, symdef = libc_members()
    lib = ldcmp.libc()
    with tempfile.TemporaryDirectory(prefix='rllibc') as tmp:
        d, root = os.path.join(tmp, 'd'), os.path.join(tmp, 'root')
        os.makedirs(d)
        for sub in ('tmp', 'bin'):
            os.makedirs(os.path.join(root, sub))
        os.symlink(os.path.abspath(ar_port), os.path.join(root, 'bin', 'ar'))
        arcmp.place(d, {n: F(data, mtime=t, uid=0, gid=1, mode=0o664)
                        for n, data, t in members})
        names = [n for n, data, t in members]
        t0 = time.time()
        r1 = arcmp.run_port(['rc', 'libc2.a'] + names, d, root, ar_port)
        r2 = arcmp.run_port(['libc2.a'], d, root, port)
        t1 = time.time()
        for r in (r1, r2):
            if r[0] != 0 or r[1] or r[2]:
                print('FAILED: %r' % (r,))
                return 1
        new = open(os.path.join(d, 'libc2.a'), 'rb').read()
    # __.SYMDEF's header: the date, the group, the mode of the file ranlib made
    if len(new) != len(lib):
        print('FAILED: libc2.a is %d bytes, libc.a %d' % (len(new), len(lib)))
        return 1
    hi_w, lo_w = struct.unpack_from('<HH', new, 16)
    date = (hi_w << 16) | lo_w
    if not int(t0) + 5 - 1 <= date <= int(t1) + 5 + 1:
        print('FAILED: __.SYMDEF is dated %d, not five seconds past the clock' % date)
        return 1
    fixed = bytearray(new)
    fixed[16:20] = lib[16:20]                 # the date
    fixed[20:22] = lib[20:22]                 # uid, gid
    fixed[22:24] = lib[22:24]                 # the mode
    if bytes(fixed) != lib:
        n = next(i for i in range(len(lib)) if fixed[i] != lib[i])
        print('FAILED: libc2.a differs from libc.a at byte %d' % n)
        return 1
    print('libc.a rebuilt from its %d objects: %d bytes, the same as the original '
          '(__.SYMDEF\'s date, group and mode aside: %d, %d, %o)'
          % (len(members), len(lib), date, new[20 + 1], struct.unpack_from('<H', new, 22)[0]))
    return 0


# ---- the driver ---------------------------------------------------------------

def _case_one(job):
    (name, files, steps), port, ar_port, keep = job
    return name, [(str(i), k, d) for i, k, d in
                  compare_seq(steps, files, port, ar_port, keep, name)]


def _run_one(job):
    args, port, ar_port, keep = job
    cwd = os.getcwd()
    files = {}
    for dp, dns, fns in os.walk(cwd):
        for fn in fns:
            p = os.path.join(dp, fn)
            with open(p, 'rb') as fh:
                files[os.path.relpath(p, cwd)] = F(fh.read(), int(os.stat(p).st_mtime),
                                                   os.stat(p).st_mode & 0o7777)
    return ' '.join(args), [(str(i), k, d) for i, k, d in compare_seq(
        [args], files, port, ar_port, keep, ' '.join(args))]


def main(argv):
    jobs, port, ar_port, show, keep = os.cpu_count() or 1, PORT, AR_PORT, 0, None
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-j', '--port', '--ar', '--show', '--keep') and (
                not args or args[0] in ('cases', 'libc')):
            v = argv[i + 1]
            if a == '-j':
                jobs = int(v)
            elif a == '--port':
                port = os.path.abspath(v)
            elif a == '--ar':
                ar_port = os.path.abspath(v)
            elif a == '--show':
                show = int(v)
            else:
                keep = os.path.abspath(v)
            i += 2
        else:
            args.append(a)
            i += 1
    if not args or args[0] not in ('cases', 'libc', 'run'):
        sys.exit(__doc__.split('Usage:')[1].split('Requires')[0])
    if args[0] == 'libc':
        return libc_check(port, ar_port)
    if args[0] == 'cases':
        work, fn = [(c, port, ar_port, keep) for c in cases()], _case_one
    else:
        work, fn = [(args[1:], port, ar_port, keep)], _run_one
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
    print('ranlib port against the original (%s): %d runs, %s'
          % (args[0], n, ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
