#!/usr/bin/env python3
"""ldcmp.py - mutos_ld, the port of the real ld, against the original.

The original MUTOS 1700 ld (mutos/mutos_bin/ld.golden.base64.txt) runs under
mutosemu.py; mutos_ld (src/mutos_ld/) runs on the host. Each gets the
same command line in a copy of the same directory and the same MUTOS root
(lib/crt0.o, lib/libc.a at its __.SYMDEF time, usr/lib/, tmp/, and any
library a case adds); afterwards their messages, exit status and the
directories - every file's name, bytes and mode - must be the same. The
temporary output name "..ldtmp<pid digits>" counts as one name.

A refusal of the port (exit status 2 and a last message ending in
"refused") counts apart: what it wrote to stdout must be a prefix of what
the original wrote, also where the original does not finish (a CPU fault,
an endless loop), and there the port must have refused. Where both are
stopped by the limits (an archive scan that does not advance), their
messages must agree as far as the shorter goes. -r and -d are the port's
extension: the original refuses them, so a command line with them is not
compared.

Usage:
    ldcmp.py goldens [-j N] [--port PATH] [--show N] [--keep DIR]
        every .s.golden of tests/mutos_cc, assembled by mutos_as, linked
        by both with a matrix of command lines (cc's -X crt0.o x.o -lc,
        the other options, libc.a by name with and without a current
        table of contents, the object alone, twice, ...)
    ldcmp.py cases [-j N] [--port PATH] [--show N] [--keep DIR]
        hand-written cases: the messages, -v, -D, -e, -u, long -l names,
        archives without a table of contents, bad and short files
    ldcmp.py files [ARG...]
        one command line, in a copy of the current directory

As a module: compare(args, srcdir, root, ...) -> (kind, detail), kind
one of 'same', 'refused', 'orig-fails' (the original does not finish,
the port refused), 'endless' (both stopped by the limits), 'DIFFERENT',
'BAD-REFUSAL'; make_root(), ar() and ranlib() build roots and archives.

Requires the Python module "unicorn" (pip install unicorn).
"""
import base64
import concurrent.futures
import glob
import io
import os
import re
import resource
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_ld', 'mutos_ld')
AS = os.path.join(ROOT, 'src', 'mutos_as', 'mutos_as')
CRT0 = os.path.join(ROOT, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt')
LIBC = os.path.join(ROOT, 'tests', 'mutos1700_libc', 'libc.a.base64.txt')
LIBC_SYMDEF_TIME = 602282477      # 1989-01-31 20:41:17 UTC
MAX_INSNS = 100_000_000           # a link of half of libc.a takes 2M
OUT_LIMIT = 1 << 20               # messages past this: stopped
PORT_CPU = 5                      # seconds
TMPNAME = re.compile(r'^\.\.ldtmp.{6}$')


def crt0():
    return base64.b64decode(open(CRT0, 'rb').read())


def libc():
    return base64.b64decode(open(LIBC, 'rb').read())


# ---- archives ---------------------------------------------------------

def _pdp(v):
    """A v7 long as the archive keeps it: the high word first."""
    v &= 0xffffffff
    return struct.pack('<HH', v >> 16, v & 0xffff)


def ar(members, mtime=566554203):
    """An archive of members [(name, bytes)]: the magic 0177545, then per
    member its 26-byte header (name, time, uid, gid, mode, size) and its
    bytes, padded to even."""
    out = bytearray(struct.pack('<H', 0o177545))
    for name, data in members:
        n = name.encode('latin1')[:14].ljust(14, b'\0')
        out += n + _pdp(mtime) + bytes([0, 1]) + struct.pack('<H', 0o100664)
        out += _pdp(len(data)) + data + (b'\0' if len(data) & 1 else b'')
    return bytes(out)


def obj_symbols(data):
    """[(name bytes, type, value)] of an a.out's symbol table."""
    magic, t, d, b, s = struct.unpack_from('<5H', data, 0)
    t, d = (t + 1) & ~1, (d + 1) & ~1
    off = 16 + 2 * (t + d)
    return [(data[i:i + 8], data[i + 8], struct.unpack_from('<H', data, i + 10)[0])
            for i in range(off, min(off + s, len(data)) - 11, 12)]


def ranlib(members, symdef_time=LIBC_SYMDEF_TIME, mtime=566554203):
    """An archive with a table of contents: __.SYMDEF first, an entry per
    external symbol a member defines (its name, the byte offset of the
    member's header), as ranlib makes it."""
    entries = []
    pos = 2 + 26 + 0
    tab_size = None
    # two passes: the table's own size decides where the members start
    for _ in range(2):
        entries = []
        pos = 2 + 26 + (tab_size or 0) + ((tab_size or 0) & 1)
        for name, data in members:
            if len(data) >= 16 and struct.unpack_from('<H', data)[0] == 0o407:
                for n, ty, _v in obj_symbols(data):
                    if ty & 0x20 and ty & 0x1f in (1, 2, 3, 4):
                        entries.append(n.ljust(8, b'\0')[:8] + _pdp(pos))
            pos += 26 + len(data) + (len(data) & 1)
        tab_size = 12 * len(entries)
    tab = b''.join(entries)
    head = ar([('__.SYMDEF', tab)], symdef_time)
    rest = ar(members, mtime)[2:]
    return head + rest


# ---- the root and the directories ------------------------------------------

def make_root(path, extra=()):
    """A MUTOS root for both: lib/crt0.o, lib/libc.a at its __.SYMDEF time,
    usr/lib/, usr/include/, tmp/; extra: (relative path, bytes, mtime or
    None) for a case's own libraries."""
    for sub in ('lib', 'bin', 'usr/lib', 'usr/include', 'tmp'):
        os.makedirs(os.path.join(path, sub), exist_ok=True)
    with open(os.path.join(path, 'lib', 'crt0.o'), 'wb') as f:
        f.write(crt0())
    with open(os.path.join(path, 'lib', 'libc.a'), 'wb') as f:
        f.write(libc())
    os.utime(os.path.join(path, 'lib', 'libc.a'),
             (LIBC_SYMDEF_TIME, LIBC_SYMDEF_TIME))
    for rel, data, mtime in extra:
        p = os.path.join(path, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)
        if mtime is not None:
            os.utime(p, (mtime, mtime))
    return path


def snapshot(d):
    """{name: (bytes, mode)} of a directory's files (and its subdirectories'
    names), the temporary output name made one name."""
    out = {}
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        key = '..ldtmp<pid>' if TMPNAME.match(name) else name
        st = os.lstat(p)
        if stat.S_ISDIR(st.st_mode):
            out[key + '/'] = (b'', st.st_mode & 0o777)
        else:
            with open(p, 'rb') as f:
                out[key] = (f.read(), st.st_mode & 0o777)
    return out


class _Capped(io.BytesIO):
    def write(self, b):
        n = super().write(b)
        if self.tell() > OUT_LIMIT:
            raise _Limit()
        return n


class _Limit(Exception):
    pass


def run_orig(args, cwd, root, max_insns=MAX_INSNS):
    """(status or None, stdout, stderr, why it did not finish)."""
    out, err = _Capped(), _Capped()
    emu = mutosemu.Emu(mutosemu.load_image('ld'), ['ld'] + list(args), cwd=cwd,
                       stdin=io.BytesIO(b''), stdout=out, stderr=err, root=root)
    why = ''
    st = None
    try:
        st = emu.run(max_insns)
    except _Limit:
        why = 'endless: messages past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why:
            why = 'endless: ' + why
    return st, out.getvalue(), err.getvalue(), why


def _limits():
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def run_port(args, cwd, root, port=PORT):
    """(status, stdout, stderr); status None for a run a limit stopped."""
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        r = subprocess.run([port] + list(args), cwd=cwd, stdout=fo, stderr=fe,
                           env=env, preexec_fn=_limits)
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read()[:OUT_LIMIT + 1], fe.read()
    st = r.returncode
    if st < 0:
        st = None
    return st, out, err


def diff_at(a, b):
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
             min(len(a), len(b)))
    return 'differs at byte %d (%d vs %d bytes)' % (n, len(a), len(b))


def is_extension(args):
    """-r or -d anywhere in an option (the port's extension)."""
    for a in args:
        if a.startswith('-') and ('r' in a[1:] or 'd' in a[1:]):
            # an option argument (-o rd.out) is not an option; good enough
            return True
    return False


def compare(args, srcdir, root, port=PORT, keep=None):
    """The port against the original: the same arguments in copies of
    srcdir. keep: a directory to leave both copies in on a difference."""
    with tempfile.TemporaryDirectory(prefix='ldcmp') as tmp:
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        shutil.copytree(srcdir, do, symlinks=True)
        shutil.copytree(srcdir, dp, symlinks=True)
        ost, oout, oerr, why = run_orig(args, do, root)
        pst, pout, perr = run_port(args, dp, root, port)
        kind, detail = _judge(args, ost, oout, oerr, why, pst, pout, perr, do, dp)
        if keep and kind in ('DIFFERENT', 'BAD-REFUSAL'):
            os.makedirs(keep, exist_ok=True)
            k = tempfile.mkdtemp(dir=keep, prefix='case')
            shutil.copytree(do, os.path.join(k, 'orig'), symlinks=True)
            shutil.copytree(dp, os.path.join(k, 'port'), symlinks=True)
            shutil.copytree(srcdir, os.path.join(k, 'src'), symlinks=True)
            with open(os.path.join(k, 'args'), 'w') as f:
                f.write(' '.join(args) + '\n' + kind + ': ' + detail + '\n')
        return kind, detail


def _judge(args, ost, oout, oerr, why, pst, pout, perr, do, dp):
    perr_s = perr.decode('latin1')
    lines = perr_s.rstrip('\n').split('\n')
    refused = (pst == 2 and lines[-1].startswith('ld: ')
               and lines[-1].endswith('refused'))
    if ost is None and pst is None and why.startswith('endless'):
        n = min(len(pout), len(oout))
        if pout[:n] == oout[:n]:
            return 'endless', why
        return 'DIFFERENT', 'both endless (%s), the messages differ: %s' % (
            why, diff_at(pout, oout))
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original ' \
            '%s' % ('exit %d' % ost if ost is not None else why)
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
    so, sp = snapshot(do), snapshot(dp)
    if so != sp:
        for name in sorted(set(so) | set(sp)):
            if name not in so:
                parts.append('%s: only the port\'s' % name)
            elif name not in sp:
                parts.append('%s: only the original\'s' % name)
            elif so[name] != sp[name]:
                (od, om), (pd, pm) = so[name], sp[name]
                if od != pd:
                    parts.append('%s: %s' % (name, diff_at(pd, od)))
                if om != pm:
                    parts.append('%s: mode %o, original %o' % (name, pm, om))
    if parts:
        return 'DIFFERENT', '; '.join(parts)
    return 'same', ''


# ---- the goldens -------------------------------------------------------------

def golden_sources():
    inv = set()
    p = os.path.join(ROOT, 'tests', 'mutos_cc', 'invalid_goldens.txt')
    if os.path.exists(p):
        for line in open(p):
            line = line.split('#')[0].strip()
            if line:
                inv.add(line.split()[0])
    out = []
    for g in sorted(glob.glob(os.path.join(ROOT, 'tests', 'mutos_cc', '**',
                                           '*.s.golden'), recursive=True)):
        rel = os.path.relpath(g, os.path.join(ROOT, 'tests', 'mutos_cc'))
        stem = rel[:-len('.s.golden')]
        if any(stem == i or stem.endswith('/' + i) or rel.startswith(i)
               for i in inv):
            continue
        out.append(g)
    return out


# The command lines of the goldens' matrix: x.o is the golden's object,
# crt0.o a copy of the real one, libc.a (if named) a copy of the real one
# in the directory - "old": at its __.SYMDEF time (the table is used),
# "new": a time after it (out of date: a linear scan).
MATRIX = [
    ('cc', ['-X', 'crt0.o', 'x.o', '-lc'], None),
    ('plain', ['crt0.o', 'x.o', '-lc'], None),
    ('-x', ['-x', 'crt0.o', 'x.o', '-lc'], None),
    ('-s -o', ['-s', '-o', 'prog', 'crt0.o', 'x.o', '-lc'], None),
    ('-i', ['-i', '-X', 'crt0.o', 'x.o', '-lc'], None),
    ('-n', ['-n', 'crt0.o', 'x.o', '-lc'], None),
    ('-S -O', ['-S', '-O', 'crt0.o', 'x.o', '-lc'], None),
    ('alone', ['x.o'], None),
    ('-t -u -e libc.a new', ['-t', '-u', '_printf', '-e', '_main', '-X',
                             'crt0.o', 'x.o', 'libc.a'], 'new'),
    ('libc.a old', ['-X', 'crt0.o', 'x.o', 'libc.a'], 'old'),
    ('-D', ['-D', '3000', '-X', 'crt0.o', 'x.o', '-lc'], None),
    ('twice', ['-X', 'crt0.o', 'x.o', 'x.o', '-lc'], None),
]


def _assemble(golden, out):
    r = subprocess.run([AS, '-o', out, golden], capture_output=True)
    return r.returncode == 0 and os.path.exists(out)


def _golden_one(job):
    golden, port, keep, root = job
    results = []
    with tempfile.TemporaryDirectory(prefix='ldgold') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        if not _assemble(golden, os.path.join(src, 'x.o')):
            return golden, [('assemble', 'DIFFERENT', 'mutos_as fails')]
        with open(os.path.join(src, 'crt0.o'), 'wb') as f:
            f.write(crt0())
        for name, args, lib in MATRIX:
            lp = os.path.join(src, 'libc.a')
            if lib:
                with open(lp, 'wb') as f:
                    f.write(libc())
                t = LIBC_SYMDEF_TIME + (0 if lib == 'old' else 1000)
                os.utime(lp, (t, t))
            elif os.path.exists(lp):
                os.unlink(lp)
            kind, detail = compare(args, src, root, port, keep)
            results.append((name, kind, detail))
    return golden, results


# ---- the hand-written cases ---------------------------------------------------

def _hdr(magic=0o407, t=0, d=0, b=0, s=0, e=0, flag=0):
    return struct.pack('<8H', magic, t, d, b, s, e, 0, flag)


def _sym(name, ty, val):
    return name.encode('latin1')[:8].ljust(8, b'\0') + bytes([ty, 0]) + \
        struct.pack('<H', val & 0xffff)


def mkobj(text=b'', data=b'', bss=0, trel=None, drel=None, syms=(), flag=0,
          magic=0o407, entry=0):
    """An a.out object: text and data (even lengths), their relocation
    words (default all 0), symbols [(name, type, value)]."""
    if len(text) & 1:
        text += b'\0'
    if len(data) & 1:
        data += b'\0'
    trel = trel if trel is not None else b'\0' * len(text)
    drel = drel if drel is not None else b'\0' * len(data)
    st = b''.join(_sym(*s) for s in syms)
    body = text + data + (b'' if flag else trel + drel) + st
    return _hdr(magic, len(text), len(data), bss, len(st), entry, flag) + body


def cases():
    """[(name, args, files {name: bytes}, root extra [(rel, bytes, mtime)])]"""
    w = lambda *v: struct.pack('<%dH' % len(v), *v)
    main_o = mkobj(text=w(0xe8, 0, 0x90c3), trel=w(0, 0x8018, 0),
                   syms=[('_main', 0x22, 0), ('_foo', 0x20, 0)])
    foo_o = mkobj(text=w(0x90c3), data=w(1, 2), bss=4,
                  syms=[('_foo', 0x22, 0), ('_fdat', 0x23, 0), ('_fbss', 0x24, 0),
                        ('Lloc', 0x02, 0), ('local', 0x03, 2)])
    com_o = mkobj(text=w(0x90c3), syms=[('_cmn', 0x20, 7), ('_cm2', 0x20, 300),
                                        ('_bar', 0x22, 0)])
    shift_o = mkobj(text=w(0x00b8, 0x9000, 0x00a1, 0x0000, 0x90c3),
                    trel=w(0x8002, 0, 0x8004, 0, 0),
                    data=w(0x1234, 0x5678), drel=w(0x8002, 0),
                    syms=[('_shift', 0x22, 0)])
    pcrel_o = mkobj(text=w(0xe8, 0x0000, 0xe890, 0, 0x90c3),
                    trel=w(0, 0x8019, 0x8019, 0, 0),
                    syms=[('_pc', 0x22, 0), ('_foo', 0x20, 0), ('_far', 0x20, 0)])
    norel_o = mkobj(text=w(0x90c3), syms=[('_nr', 0x22, 0)], flag=1)
    plain_a = ar([('foo.o', foo_o), ('com.o', com_o)])
    toc_a = ranlib([('foo.o', foo_o), ('com.o', com_o)])
    c = []

    def case(name, args, files=None, extra=()):
        c.append((name, args, dict(files or {}), list(extra)))

    base = {'main.o': main_o, 'foo.o': foo_o, 'crt0.o': crt0()}
    case('no arguments', [])
    case('main foo', ['-X', 'crt0.o', 'main.o', 'foo.o', '-lc'], base)
    case('undefined', ['main.o'], base)
    case('commons', ['-X', 'main.o', 'foo.o', 'com.o'], dict(base, **{'com.o': com_o}))
    case('commons -s', ['-s', 'com.o'], {'com.o': com_o})
    case('byte-shifted', ['shift.o'], {'shift.o': shift_o})
    case('byte-shifted -i', ['-i', 'foo.o', 'shift.o'], {'shift.o': shift_o, 'foo.o': foo_o})
    case('pc-relative', ['pcrel.o', 'foo.o'], {'pcrel.o': pcrel_o, 'foo.o': foo_o})
    case('no relocation bits', ['nr.o', 'main.o', 'foo.o'],
         dict(base, **{'nr.o': norel_o}))
    case('multiply defined', ['foo.o', 'foo.o'], base)
    case('-e not text', ['-e', '_fdat', 'foo.o'], base)
    case('-e undefined', ['-e', '_nothing', 'foo.o'], base)
    case('-e text', ['-e', '_foo', 'foo.o'], base)
    case('-e not text, an entry word', ['-e', '_fdat', 'fooe.o'], {'fooe.o': mkobj(
        text=w(0x90c3), data=w(1, 2), syms=[('_foo', 0x22, 0), ('_fdat', 0x23, 0)],
        entry=0x1234)})
    case('-u from libc', ['-u', '_atof', '-u', '_ecvt', 'crt0.o', '-lc'], base)
    case('_etext _edata _end', ['-u', '_etext', '-u', '_end', 'foo.o'], base)
    case('_end defined twice', ['foo.o', 'end.o'],
         dict(base, **{'end.o': mkobj(text=w(0x90c3), syms=[('_end', 0x22, 0)])}))
    case('-D', ['-D', '40', 'foo.o', 'main.o'], base)
    case('-D too small', ['main.o', 'foo.o', '-D', '1'], base)
    case('-D missing', ['foo.o', '-D'], base)
    case('-o missing', ['foo.o', '-o'], base)
    case('-u missing', ['foo.o', '-u'], base)
    case('-e missing', ['foo.o', '-e'], base)
    case('-v missing', ['foo.o', '-v'], base)
    case('-v once', ['-v', 'ov1', 'foo.o', 'main.o'], base)
    case('-v twice', ['main.o', '-v', 'ov1', 'foo.o', '-v', 'ov2', 'foo.o'], base)
    case('-v again', ['main.o', '-v', 'ov1', 'foo.o', '-v', 'ov1', 'foo.o'], base)
    case('-v 17', ['foo.o'] + sum((['-v', 'v%d' % i] for i in range(17)), [])
         + ['main.o'], base)
    case('bad flag', ['-q', 'foo.o'], base)
    case('bad flag high byte', ['-\xe9', 'foo.o'], base)
    case('-t', ['-t', '-X', 'crt0.o', 'main.o', 'foo.o', '-lc'], base)
    case('-xt', ['-xt', 'foo.o'], base)
    case('-X locals', ['-X', 'foo.o'], base)
    case('-S', ['-S', 'foo.o', 'com.o'], dict(base, **{'com.o': com_o}))
    case('-O', ['-O', 'foo.o'], base)
    case('-o', ['-o', 'out', 'foo.o'], base)
    case('-o twice', ['-o', 'o1', '-o', 'o2', 'foo.o'], base)
    case('-o a.out', ['-o', 'a.out', 'foo.o'], base)
    case('a.out exists', ['foo.o'], dict(base, **{'a.out': b'old'}))
    case('cannot open', ['nothere.o'], base)
    case('-lnone', ['foo.o', '-lnone'], base)
    case('-l', ['foo.o', '-l'], base)
    case('-la', ['foo.o', '-l'], base, [('lib/liba.a', plain_a, None)])
    case('-lp plain usr/lib', ['main.o', '-lp'], base, [('usr/lib/libp.a', plain_a, None)])
    case('-lp plain lib', ['main.o', '-lp'], base, [('lib/libp.a', plain_a, None)])
    case('-lt toc', ['main.o', '-lt'], base,
         [('lib/libt.a', toc_a, LIBC_SYMDEF_TIME)])
    case('-lt toc out of date', ['main.o', '-lt'], base,
         [('lib/libt.a', toc_a, LIBC_SYMDEF_TIME + 5)])
    case('-ltoc twice', ['main.o', '-lt', '-lt', '-lc'], base,
         [('lib/libt.a', toc_a, LIBC_SYMDEF_TIME)])
    for n in (12, 13, 14, 15, 20, 30, 60, 100):
        case('-l of %d characters' % n, ['main.o', '-l' + 'q' * n], base)
    case('-l overwriting the formats', ['main.o', '-l' + 'q' * 40, 'nothere.o'], base)
    case('-lc -lc -lc', ['-X', 'crt0.o', 'main.o', 'foo.o'] + ['-lc'] * 3, base)
    case('empty file', ['empty.o'], {'empty.o': b''})
    case('one byte', ['one.o'], {'one.o': b'\x07'})
    case('bad magic', ['bad.o'], {'bad.o': mkobj(magic=0o410, text=w(0x90c3))})
    case('short object', ['short.o'], {'short.o': main_o[:30]})
    case('short symbols', ['short.o'], {'short.o': main_o[:-6]})
    case('empty archive', ['e.a'], {'e.a': struct.pack('<H', 0o177545)})
    case('archive member short', ['main.o', 's.a'],
         dict(base, **{'s.a': plain_a[:60]}))
    case('archive by name', ['main.o', 'p.a'], dict(base, **{'p.a': plain_a}))
    case('archive not needed', ['foo.o', 'p.a'], dict(base, **{'p.a': plain_a}))
    case('directory', ['sub'], {'sub/x': b''})
    case('libc.a alone', ['libc.a'], {'libc.a': libc()})
    # the tables' limits
    many = lambda n, ty, pre='_s': [('%s%d' % (pre, i), ty, 0 if ty == 0x20 else 2 * i)
                                    for i in range(n)]
    case('1102 symbols', ['big.o'], {'big.o': mkobj(text=w(*([0x90c3] * 1200)),
                                                   syms=many(1102, 0x22))})
    case('1103 symbols', ['big.o'], {'big.o': mkobj(text=w(*([0x90c3] * 1200)),
                                                   syms=many(1103, 0x22))})
    case('1104 symbols', ['big.o'], {'big.o': mkobj(text=w(*([0x90c3] * 1200)),
                                                   syms=many(1104, 0x22))})
    defs = mkobj(text=w(*([0x90c3] * 1001)), syms=many(1001, 0x22, '_u'))
    case('1000 undefined', ['use.o', 'defs.o'],
         {'use.o': mkobj(syms=many(1000, 0x20, '_u')), 'defs.o': defs})
    case('1001 undefined', ['use.o', 'defs.o'],
         {'use.o': mkobj(syms=many(1001, 0x20, '_u')), 'defs.o': defs})
    case('1001 undefined -S', ['-S', 'use.o', 'defs.o'],
         {'use.o': mkobj(syms=many(1001, 0x20, '_u')), 'defs.o': defs})
    locs = mkobj(text=w(0x90c3), syms=many(1300, 0x02, 'l'))
    case('symbol table overflow', ['l1.o', 'l2.o', 'l3.o', 'l4.o', 'l5.o'],
         {'l%d.o' % i: locs for i in range(1, 6)})
    case('symbol table overflow -x', ['-x', 'l1.o', 'l2.o', 'l3.o', 'l4.o', 'l5.o'],
         {'l%d.o' % i: locs for i in range(1, 6)})
    t40k = mkobj(text=b'\x90' * 30000, syms=[('_t1', 0x22, 0)])
    case('text overflow', ['a.o', 'b.o', 'c.o'], {'a.o': t40k, 'b.o': t40k,
                                                  'c.o': t40k})
    d40k = mkobj(data=b'\x01' * 30000, syms=[('_d1', 0x23, 0)])
    case('data overflow', ['a.o', 'b.o', 'c.o'], {'a.o': d40k, 'b.o': d40k,
                                                  'c.o': d40k})
    case('bss overflow', ['a.o'], {'a.o': mkobj(bss=40000, syms=[
        ('_c1', 0x20, 30000), ('_c2', 0x20, 30000)])})
    case('text 65530 (6 bytes wrap)', ['big.o'],
         {'big.o': mkobj(text=b'\x90' * 65530, syms=[('_big', 0x22, 0)])})
    big = bytearray(b'\x90' * 0x8000)
    big[0:3] = b'\xe8\0\0'
    rel = bytearray(0x8000)
    rel[0:2] = w(0x8018)
    case('text 32K', ['big.o', 'foo.o'], {'big.o': mkobj(
        text=bytes(big), trel=bytes(rel), syms=[('_main', 0x22, 0),
                                                ('_foo', 0x20, 0)]), 'foo.o': foo_o})
    toc700 = [('m%d.o' % i, mkobj(text=w(0x90c3), syms=[('_t%d' % i, 0x22, 0)]))
              for i in range(700)]
    case('table of 699', ['main.o', '-lt'], base,
         [('lib/libt.a', ranlib(toc700[:699]), LIBC_SYMDEF_TIME)])
    case('table of 700', ['main.o', '-lt'], base,
         [('lib/libt.a', ranlib(toc700), LIBC_SYMDEF_TIME)])
    users = mkobj(syms=[('_t%d' % i, 0x20, 0) for i in range(300)])
    case('300 members from a table', ['use.o', '-lt'], {'use.o': users},
         [('lib/libt.a', ranlib(toc700[:400]), LIBC_SYMDEF_TIME)])
    case('300 members by a scan', ['use.o', '-lt'], {'use.o': users},
         [('lib/libt.a', ar(toc700[:400]), None)])
    users = mkobj(syms=[('_t%d' % i, 0x20, 0) for i in range(250)])
    case('250 members from a table', ['use.o', '-lt'], {'use.o': users},
         [('lib/libt.a', ranlib(toc700[:400]), LIBC_SYMDEF_TIME)])
    case('-lc 300 times', ['-X', 'crt0.o', 'main.o', 'foo.o'] + ['-lc'] * 300, base)
    nr_member = mkobj(text=w(0x90c3), syms=[('_nr', 0x22, 0)], flag=1)
    case('a warning clears an error', ['main.o', 'foo.o', 'p.a', '-lt'],
         dict(base, **{'p.a': ar([('nr.o', nr_member)])}),
         [('lib/libt.a', ranlib([('foo.o', foo_o)]), LIBC_SYMDEF_TIME + 9)])
    tight = mkobj(data=w(0x0100, 0x0300, 0x0000), drel=w(0x8004, 0x8000, 0),
                  syms=[('_und', 0x20, 0)])
    case('byte-shifted before an undefined', ['tight.o'], {'tight.o': mkobj(
        data=w(0xff00, 0x0000, 0x0000), drel=w(0x8004, 0x8008, 0),
        syms=[('_und', 0x20, 0)])})
    case('byte-shifted pair', ['tight.o', 'foo.o'], {'tight.o': tight,
                                                     'foo.o': foo_o})
    return c


def _case_one(job):
    (name, args, files, extra), port, keep = job
    with tempfile.TemporaryDirectory(prefix='ldcase') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        for fn, data in files.items():
            p = os.path.join(src, fn)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(data)
        root = make_root(os.path.join(tmp, 'root'), extra)
        return name, [('', ) + compare(args, src, root, port, keep)]


def _files_one(job):
    args, port, keep = job
    with tempfile.TemporaryDirectory(prefix='ldfiles') as tmp:
        root = make_root(os.path.join(tmp, 'root'))
        return ' '.join(args), [('', ) + compare(args, os.getcwd(), root, port, keep)]


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
        elif args and args[0] != 'files' and a in ('-j', '--port', '--show', '--keep'):
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
    if not args or args[0] not in ('goldens', 'cases', 'files'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    tmproot = None
    if args[0] == 'goldens':
        tmproot = tempfile.mkdtemp(prefix='ldroot')
        root = make_root(tmproot)
        work = [(g, port, keep, root) for g in golden_sources()]
        fn = _golden_one
    elif args[0] == 'cases':
        work = [(c, port, keep) for c in cases()]
        fn = _case_one
    else:
        work = [(args[1:], port, keep)]
        fn = _files_one
    counts = {}
    bad = []
    n = 0
    try:
        with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
            for name, results in ex.map(fn, work):
                for mode, kind, detail in results:
                    n += 1
                    counts[kind] = counts.get(kind, 0) + 1
                    label = os.path.relpath(name, ROOT) if os.path.isabs(name) \
                        else name
                    if mode:
                        label += ' [%s]' % mode
                    if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                        bad.append('%s: %s: %s' % (label, kind, detail))
                    elif (kind != 'same' and show) or args[0] == 'files':
                        print('%s: %s%s' % (label, kind, ': ' + detail if detail
                                            else ''))
    finally:
        if tmproot:
            shutil.rmtree(tmproot, ignore_errors=True)
    for b in bad[:show or 20]:
        print(b)
    print('ld port against the original (%s): %d runs, %s'
          % (args[0], n, ', '.join('%d %s' % (c, k)
                                   for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
