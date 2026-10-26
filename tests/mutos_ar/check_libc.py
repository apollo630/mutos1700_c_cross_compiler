#!/usr/bin/env python3
"""check_libc.py - mutos_ar and mutos_ranlib rebuild libc.a.

The reference is the real libc.a (tests/mutos1700_libc/libc.a.base64.txt)
and its 167 objects (the *.o.base64.txt beside it). This checks, with the
ports only (no emulator, plain Python 3 - the comparison with the original
under the emulator is tests/realbin/arcmp.py and ranlibcmp.py):

  1. the reference is consistent: the archive's members are the objects
     of that directory, byte for byte, and the archive starts with its
     __.SYMDEF, the table the original ranlib made;
  2. "ar rc libc2.a <the 167 objects>" followed by "ranlib libc2.a" gives
     the same archive byte for byte but for __.SYMDEF's header - its time
     (five seconds past the clock, ranlib's "fixdate"), its group and its
     mode (those of the file ranlib wrote) -, with the objects' times,
     owner and modes as the originals had them (the owner when the build
     may chown, else the user's own);
  3. "ar t" lists the 168 names in the archive's order, "ar x" gives the
     167 objects back byte for byte and "ar p" prints one;
  4. a second "ranlib libc2.a" changes nothing but __.SYMDEF's time, and
     "ar d" of one object followed by "ar rb" of it puts it back where it
     was.

mutos_ranlib runs mutos_ar through the MUTOS root's bin/ar, as the original
runs "ar"; the root made here (in a temporary directory) has just that link
and tmp/.

Usage: check_libc.py [--ar PATH] [--ranlib PATH]
Exit status 1 on any failure.
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
REPO = os.path.dirname(os.path.dirname(HERE))
LIBDIR = os.path.join(REPO, 'tests', 'mutos1700_libc')
AR = os.path.join(REPO, 'src', 'mutos_ar', 'mutos_ar')
RANLIB = os.path.join(REPO, 'src', 'mutos_ranlib', 'mutos_ranlib')

HDR = 26                  # struct ar_hdr
OFF_DATE, OFF_UID, OFF_GID, OFF_MODE, OFF_SIZE = 14, 18, 19, 20, 22

FAILED = []


def fail(msg):
    FAILED.append(msg)
    print('FAILED: ' + msg)


def b64(path):
    with open(path, 'rb') as fh:
        return base64.b64decode(fh.read())


def members(arch):
    """[(name, header bytes, data)] of an archive."""
    if arch[:2] != b'\x65\xff':
        raise ValueError('no archive magic')
    out, p = [], 2
    while p + HDR <= len(arch):
        h = arch[p:p + HDR]
        name = h[:14].split(b'\0')[0].decode('latin1')
        hi, lo = struct.unpack_from('<HH', h, OFF_SIZE)
        size = (hi << 16) | lo
        out.append((name, h, arch[p + HDR:p + HDR + size]))
        p += HDR + size + (size & 1)
    if p != len(arch):
        raise ValueError('archive ends inside a member')
    return out


def date_of(h):
    hi, lo = struct.unpack_from('<HH', h, OFF_DATE)
    return (hi << 16) | lo


def run(prog, args, cwd, root, ok=(0,)):
    env = dict(os.environ)
    env['MUTOS_ROOT'] = root
    env['TZ'] = 'UTC'
    r = subprocess.run([prog] + args, cwd=cwd, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode not in ok or (r.stderr and 0 in ok and r.returncode == 0):
        fail('%s %s: status %d, stderr %r' % (os.path.basename(prog), ' '.join(args),
                                               r.returncode, r.stderr[:200]))
    return r


def main(argv):
    ar, ranlib = AR, RANLIB
    i = 0
    while i < len(argv):
        if argv[i] == '--ar' and i + 1 < len(argv):
            ar = os.path.abspath(argv[i + 1])
        elif argv[i] == '--ranlib' and i + 1 < len(argv):
            ranlib = os.path.abspath(argv[i + 1])
        else:
            sys.exit(__doc__.split('Usage:')[1])
        i += 2
    for t in (ar, ranlib):
        if not os.access(t, os.X_OK):
            print('%s is not built (make ar ranlib)' % t)
            return 1

    lib = b64(os.path.join(LIBDIR, 'libc.a.base64.txt'))
    ref = members(lib)
    objs = [m for m in ref if m[0] != '__.SYMDEF']
    # 1. the reference
    if len(objs) != 167 or ref[0][0] != '__.SYMDEF' or len(ref) != 168:
        fail('libc.a holds %d members, the first %r' % (len(ref), ref[0][0]))
    for name, h, data in objs:
        f = os.path.join(LIBDIR, name + '.base64.txt')
        if not os.path.exists(f):
            fail('%s: no %s.base64.txt beside libc.a' % (name, name))
        elif b64(f) != data:
            fail('%s differs from the member of libc.a' % name)
    names = [m[0] for m in objs]
    print('reference: libc.a has %d members, its %d objects the files beside it'
          % (len(ref), len(objs)))

    can_chown = os.geteuid() == 0
    with tempfile.TemporaryDirectory(prefix='checklibc') as tmp:
        d, root = os.path.join(tmp, 'd'), os.path.join(tmp, 'root')
        os.makedirs(d)
        os.makedirs(os.path.join(root, 'tmp'))
        os.makedirs(os.path.join(root, 'bin'))
        os.symlink(ar, os.path.join(root, 'bin', 'ar'))
        for name, h, data in objs:
            p = os.path.join(d, name)
            with open(p, 'wb') as fh:
                fh.write(data)
            os.chmod(p, 0o664)
            if can_chown:
                os.chown(p, h[OFF_UID], h[OFF_GID])
            t = date_of(h)
            os.utime(p, (t, t))
        # 2. the build
        t0 = time.time()
        run(ar, ['rc', 'libc2.a'] + names, d, root)
        run(ranlib, ['libc2.a'], d, root)
        t1 = time.time()
        new_bytes = open(os.path.join(d, 'libc2.a'), 'rb').read()
        try:
            new = members(new_bytes)
        except ValueError as e:
            fail('libc2.a: ' + str(e))
            new = []
        if new and (new_bytes[:2] != lib[:2] or len(new) != len(ref)):
            fail('libc2.a holds %d members, libc.a %d' % (len(new), len(ref)))
        elif new:
            for (rn, rh, rd), (nn, nh, nd) in zip(ref, new):
                if rn != nn:
                    fail('member %s where libc.a has %s' % (nn, rn))
                    break
                if rd != nd:
                    fail('%s: contents differ' % rn)
                if rn == '__.SYMDEF':
                    t = date_of(nh)
                    if not int(t0) + 5 - 1 <= t <= int(t1) + 5 + 1:
                        fail('__.SYMDEF is dated %d, not five seconds past the clock' % t)
                    if rh[:OFF_DATE] != nh[:OFF_DATE]:
                        fail('__.SYMDEF: name differs')
                    if rh[OFF_SIZE:] != nh[OFF_SIZE:]:
                        fail('__.SYMDEF: size differs')
                    continue
                mask = bytearray(HDR)
                if not can_chown:           # the user's own owner, not libc.a's
                    mask[OFF_UID] = mask[OFF_GID] = 0xff
                a = bytes(x & ~m for x, m in zip(rh, mask))
                b = bytes(x & ~m for x, m in zip(nh, mask))
                if a != b:
                    fail('%s: header %s, libc.a %s' % (rn, nh.hex(), rh.hex()))
            print('rebuilt: ar rc + ranlib give %d members, the objects and '
                  'headers libc.a has (__.SYMDEF\'s time, group and mode aside)'
                  % len(new))
        # 3. t, x, p
        r = run(ar, ['t', 'libc2.a'], d, root)
        listed = r.stdout.decode('latin1').split()
        if listed != ['__.SYMDEF'] + names:
            fail('ar t lists %d names, not __.SYMDEF and the 167 objects in order'
                 % len(listed))
        x = os.path.join(tmp, 'x')
        os.makedirs(x)
        run(ar, ['x', os.path.join(d, 'libc2.a')], x, root)
        for name, h, data in objs:
            p = os.path.join(x, name)
            if not os.path.exists(p) or open(p, 'rb').read() != data:
                fail('ar x: %s is not the member' % name)
                break
        r = run(ar, ['p', 'libc2.a', names[0]], d, root)
        if r.stdout != objs[0][2]:
            fail('ar p %s: not the member' % names[0])
        print('t, x and p: %d names listed, %d objects extracted and equal' % (
            len(listed), len(objs)))
        # 4. ranlib again, d + rb
        before = open(os.path.join(d, 'libc2.a'), 'rb').read()
        run(ranlib, ['libc2.a'], d, root)
        again = open(os.path.join(d, 'libc2.a'), 'rb').read()
        if len(again) != len(before):
            fail('a second ranlib changed the size')
        else:
            a = bytearray(again)
            b = bytearray(before)
            # blank __.SYMDEF's time in both
            for arch in (a, b):
                pos = 2
                while pos + HDR <= len(arch):
                    nm = bytes(arch[pos:pos + 14]).split(b'\0')[0]
                    hi, lo = struct.unpack_from('<HH', arch, pos + OFF_SIZE)
                    sz = (hi << 16) | lo
                    if nm == b'__.SYMDEF':
                        arch[pos + OFF_DATE:pos + OFF_DATE + 4] = b'\0\0\0\0'
                    pos += HDR + sz + (sz & 1)
            if a != b:
                fail('a second ranlib changed more than __.SYMDEF\'s time')
        mid = names[len(names) // 2]
        after = names[len(names) // 2 + 1]
        run(ar, ['d', 'libc2.a', mid], d, root)
        gone = run(ar, ['t', 'libc2.a'], d, root).stdout.decode().split()
        if mid in gone:
            fail('ar d left %s in the archive' % mid)
        run(ar, ['rb', after, 'libc2.a', mid], d, root)
        back = run(ar, ['t', 'libc2.a'], d, root).stdout.decode().split()
        if back != listed:
            fail('ar d + rb did not put %s back in its place' % mid)
        print('ranlib twice and ar d + ar rb: as expected')
    if FAILED:
        print('%d checks failed' % len(FAILED))
        return 1
    print('mutos_ar and mutos_ranlib rebuild libc.a: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
