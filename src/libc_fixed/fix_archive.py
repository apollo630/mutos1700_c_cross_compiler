#!/usr/bin/env python3
"""fix_archive.py - an archive with the corrected members of libc.a's
runtime (docs/TOOLCHAIN_PLAN.md, section 7.3, step F3b; decision 14).

For every corrected source fixed/<name>.s whose object <name>.o is a member
of the archive: the source is assembled ("mutos_as -C", as the sources in
orig/ are), the member is required to be the original libc.a's member byte
for byte - a correction is put only over the code it was made for - and is
replaced in place ("mutos_ar r": the order of the members is kept; the new
member's header gets the old one's date, owner, group and mode); then, if
the archive has a table of contents (__.SYMDEF), "mutos_ranlib" makes it
anew - its header too gets the old table's owner, group and mode, its date
is the new table's -, and the file's modification time is set to the
table's date (or to now, if ranlib dated the table ahead), so that ld finds
it up to date. Every other member stays as
it was: with nothing corrected, the result is the archive itself.

Made with it:
  * the corrected copy of libc.a (the default: the original archive,
    tests/mutos1700_libc/libc.a.base64.txt), which make builds as
    src/libc_fixed/libc.a and the MUTOS root holds as lib/fixed/libc.a;
  * a corrected copy of another archive with some of these members - the
    MUTOS kernel's conf/arith.a has aldiv.o, almul.o, alrem.o, ldiv.o,
    lmul.o and lrem.o (decided 2026-11-09), libc.a's byte for byte; make
    builds src/libc_fixed/arith.a from the project owner's
    tests/mutos1700_arith/arith.a.base64.txt.

mutos_ar and mutos_ranlib run with a MUTOS root of their own in a
temporary directory (its tmp/ and bin/ar), so that this works before the
build tree's root exists.

Usage:
    fix_archive.py [-o OUT] [ARCHIVE]
        ARCHIVE: an archive, or its base64 text (a name ending in
        .base64.txt, as tests/ keeps them). OUT: default fixed_<name of
        ARCHIVE> in the current directory (for the default ARCHIVE:
        libc.a). Prints a line for each member
        replaced; exit status 1 if a member to replace is not the
        original's, or a tool fails.
"""

import base64
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '..', '..'))
MUTOS_AS = os.path.join(TOP, 'src', 'mutos_as', 'mutos_as')
MUTOS_AR = os.path.join(TOP, 'src', 'mutos_ar', 'mutos_ar')
MUTOS_RANLIB = os.path.join(TOP, 'src', 'mutos_ranlib', 'mutos_ranlib')
LIBC = os.path.join(TOP, 'tests', 'mutos1700_libc')
FIXED = os.path.join(HERE, 'fixed')
ARMAG = b'\x65\xff'
SYMDEF = '__.SYMDEF'


def getlong(b, off):
    """A long as the 8086 compiler stores it: the high word first."""
    hi, lo = struct.unpack_from('<HH', b, off)
    return hi << 16 | lo


def members(data):
    """[(name, date, header offset, content)] of an archive's members."""
    if data[:2] != ARMAG:
        raise ValueError('not an archive')
    out = []
    off = 2
    while off + 26 <= len(data):
        name = data[off:off + 14].split(b'\0')[0].decode('latin-1')
        date = getlong(data, off + 14)
        size = getlong(data, off + 22)
        out.append((name, date, off, data[off + 26:off + 26 + size]))
        off += 26 + size + (size & 1)
    return out


def read_archive(path):
    """An archive's bytes - from its base64 text for a .base64.txt."""
    with open(path, 'rb') as f:
        data = f.read()
    return base64.b64decode(data) if path.endswith('.base64.txt') else data


def original_libc():
    with open(os.path.join(LIBC, 'libc.a.base64.txt'), 'rb') as f:
        return base64.b64decode(f.read())


def original_member(name):
    path = os.path.join(LIBC, name + '.base64.txt')
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        return base64.b64decode(f.read())


def corrected_sources():
    """{member name: source path} of fixed/."""
    if not os.path.isdir(FIXED):
        return {}
    return {n[:-2] + '.o': os.path.join(FIXED, n) for n in sorted(os.listdir(FIXED))
            if n.endswith('.s')}


def fix(data, log=print):
    """The archive (bytes) with its corrected members; the names replaced."""
    for tool in (MUTOS_AS, MUTOS_AR, MUTOS_RANLIB):
        if not os.access(tool, os.X_OK):
            raise RuntimeError('%s is not built - run "make" first' % tool)
    have = {m[0]: m for m in members(data)}
    todo = [(n, s) for n, s in corrected_sources().items() if n in have]
    if not todo:
        return data, []
    work = tempfile.mkdtemp(prefix='fix_archive.')
    try:
        root = os.path.join(work, 'root')
        os.makedirs(os.path.join(root, 'tmp'))
        os.makedirs(os.path.join(root, 'bin'))
        os.symlink(MUTOS_AR, os.path.join(root, 'bin', 'ar'))
        env = dict(os.environ, MUTOS_ROOT=root)
        ar = os.path.join(work, 'out.a')
        with open(ar, 'wb') as f:
            f.write(data)
        for name, src in todo:
            orig = original_member(name)
            if orig is None or have[name][3] != orig:
                raise RuntimeError('%s: the member is not libc.a\'s original %s - the '
                                   'correction was made for that code' % (name, name))
            obj = os.path.join(work, name)
            r = subprocess.run([MUTOS_AS, '-C', '-o', obj, src], capture_output=True, text=True)
            if r.returncode:
                raise RuntimeError('mutos_as %s: %s' % (src, r.stdout + r.stderr))
            date = have[name][1]
            os.utime(obj, (date, date))
        r = subprocess.run([MUTOS_AR, 'r', ar] + [n for n, _ in todo], cwd=work, env=env,
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError('mutos_ar r: %s' % (r.stdout + r.stderr))
        if SYMDEF in have:
            r = subprocess.run([MUTOS_RANLIB, ar], cwd=work, env=env, capture_output=True,
                               text=True)
            if r.returncode:
                raise RuntimeError('mutos_ranlib: %s' % (r.stdout + r.stderr))
        with open(ar, 'rb') as f:
            out = bytearray(f.read())
        # the owner, group and mode of the old headers (bytes 18 to 21:
        # ar_uid, ar_gid, ar_mode); ar r and ranlib take them from the
        # files they write
        keep = {n for n, _ in todo} | {SYMDEF}
        for name, _, off, _ in members(bytes(out)):
            if name in keep and name in have:
                old = have[name][2]
                out[off + 18:off + 22] = data[old + 18:old + 22]
        out = bytes(out)
        for name, src in todo:
            log('fix_archive: %s from %s' % (name, os.path.relpath(src)))
        return out, [n for n, _ in todo]
    finally:
        shutil.rmtree(work)


def main(argv):
    args = list(argv)
    out = None
    archive = None
    while args:
        a = args.pop(0)
        if a == '-o' and args:
            out = args.pop(0)
        elif archive is None and not a.startswith('-'):
            archive = a
        else:
            print(__doc__)
            return 2
    if archive:
        data = read_archive(archive)
        base = os.path.basename(archive)
        out = out or 'fixed_' + (base[:-len('.base64.txt')] if base.endswith('.base64.txt')
                                 else base)
    else:
        data = original_libc()
        out = out or 'libc.a'
    try:
        result, done = fix(data)
    except (ValueError, RuntimeError) as e:
        sys.stderr.write('fix_archive: %s\n' % e)
        return 1
    tmp = out + '.new'
    with open(tmp, 'wb') as f:
        f.write(result)
    ms = members(result)
    if ms and ms[0][0] == SYMDEF:
        # ld: "out of date" if the file is newer than its table; ranlib
        # dates the table a few seconds ahead, and a file time in the
        # future makes make warn of clock skew - then the file keeps now
        t = min(ms[0][1], int(time.time()))
        os.utime(tmp, (t, t))
    os.replace(tmp, out)
    if not done:
        print('fix_archive: %s: nothing corrected, the archive as it is' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
