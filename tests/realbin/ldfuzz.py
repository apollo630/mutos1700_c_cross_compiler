#!/usr/bin/env python3
"""ldfuzz.py - differential fuzzing of mutos_ld against the original ld.

Each case is a directory of input files, a MUTOS root (lib/crt0.o,
lib/libc.a and any libraries the case adds to lib/ or usr/lib/) and a
command line; ldcmp.py's compare() runs the original (under mutosemu.py)
and the port on copies of it and compares the messages, the exit status
and the files left in the directory, byte for byte and mode for mode.

What a case is made of:
  * objects of the generator's own: text and data of random words, each
    word's relocation one of absolute, text, data, BSS or an external
    symbol of the object's table, with the pc-relative bit and the
    byte-shift bit 15 (the word starting at the next byte) set at random;
    symbol tables with external definitions in every segment, absolutes,
    undefined symbols, commons (undefined with a size), locals, "L"
    locals, file names (037), names shared between the objects (multiple
    definitions, commons meeting definitions), names of 8 characters,
    now and then a spare byte other than 0, an odd size, a header with
    no relocation bits or another magic number;
  * the real crt0.o, real libc.a members and the real libc.a;
  * archives of those: plain, with a current table of contents (ranlib),
    with one out of date - by name on the command line or through -l
    from the root's lib/ or usr/lib/;
  * random options: -x -X -s -S -i -n -O -t, -o, -u, -e (a symbol of the
    case, or not), -D (a size, or too small), -v, -l with every kind of
    name, an option's argument missing, an unknown flag.
With --mutate P, each file is damaged with probability P (bytes changed,
words set to extreme values, the file cut short), for the error paths.

Usage:
    ldfuzz.py [-n N] [-s SEED] [--mutate P] [-j N] [--port PATH]
              [--show N] [--keep DIR]

The port's refusals are checked as ldcmp.py checks them: what it wrote
before must be a prefix of the original's messages, and where the
original does not finish the port must have refused. A command line with
-r or -d (the port's extension) is never generated.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import os
import random
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ldcmp          # noqa: E402

NAMES = ['_main', '_foo', '_bar', '_baz', '_x', '_y', '_buf', '_cnt',
         '_printf', '_exit', '_atof', '_ecvt', '_errno', '_environ',
         '_etext', '_edata', '_end', 'start', '_longnam', '_abcdefg',
         '_qq', 'Ltmp', 'L1', '_strlen', '_malloc', '_write', '_read']


def libc_members():
    img = ldcmp.libc()
    out = []
    p = 2
    while p + 26 <= len(img):
        name = img[p:p + 14].split(b'\0')[0].decode('latin1')
        hi, lo = struct.unpack_from('<HH', img, p + 22)
        size = (hi << 16) | lo
        out.append((name, img[p + 26:p + 26 + size]))
        p += 26 + size + (size & 1)
    return [m for m in out if m[0] != '__.SYMDEF']


class Gen:
    def __init__(self, seed, mutate):
        self.r = random.Random(seed)
        self.mutate = mutate
        self.members = libc_members()

    def name(self):
        r = self.r
        if r.random() < 0.85:
            return r.choice(NAMES)
        n = r.randint(1, 9)
        return ''.join(r.choice('_abcLxyz0123456789') for _ in range(n))

    def obj(self, shared):
        """A random object; shared: names to reuse."""
        r = self.r
        nt = r.choice([0, 1, 2, 3, 4, 6, 8, 16, 40])
        nd = r.choice([0, 0, 1, 2, 4, 8])
        syms = []
        ext = []
        for _ in range(r.randint(0, 10)):
            reuse = shared and r.random() < 0.6
            nm = r.choice(shared) if reuse else self.name()
            kind = r.random()
            if reuse and kind < 0.7:
                kind = 0.6          # mostly a reference to a shared name
            if kind < 0.3:
                ty, val = 0x22, r.randrange(0, max(2, 2 * nt))
            elif kind < 0.4:
                ty, val = 0x23, r.randrange(0, max(2, 2 * nd))
            elif kind < 0.5:
                ty, val = 0x24, r.randrange(0, 8)
            elif kind < 0.55:
                ty, val = 0x21, r.randrange(0, 0x10000)
            elif kind < 0.75:
                ty, val = 0x20, 0
            elif kind < 0.82:
                ty, val = 0x20, r.choice([1, 2, 7, 100, 0x7fff, 0xfffe])
            elif kind < 0.9:
                ty, val = r.choice([2, 3, 4, 1]), r.randrange(0, 16)
                nm = r.choice(['L', 'Lx', 'loc', 'x']) + str(r.randrange(10))
            elif kind < 0.93:
                ty, val = 0o37, 0
            else:
                ty, val = r.randrange(0, 0x40), r.randrange(0, 0x10000)
            spare = r.randrange(1, 256) if r.random() < 0.03 else 0
            syms.append((nm, ty, val, spare))
            if ty == 0x20 and not spare:
                ext.append(len(syms) - 1)

        def words(n):
            ws, rs = [], []
            for _ in range(n):
                ws.append(r.choice([0, 0x90c3, 0xfeeb, r.randrange(0x10000),
                                    r.randrange(16)]))
                k = r.random()
                if k < 0.45:
                    rel = 0
                elif k < 0.55:
                    rel = 2
                elif k < 0.62:
                    rel = 4
                elif k < 0.67:
                    rel = 6
                elif k < 0.93 and (ext or r.random() < 0.1):
                    idx = r.choice(ext) if ext and r.random() < 0.97 else \
                        r.randrange(len(syms) + 2)
                    rel = (idx << 4) | 8
                else:
                    rel = r.choice([0, 2, 4, 6, 8, 10, 12, 14])
                if r.random() < 0.15:
                    rel |= 1
                if r.random() < 0.3:
                    rel |= 0x8000
                rs.append(rel & 0xffff)
            return ws, rs

        tw, tr = words(nt)
        dw, dr = words(nd)
        pack = lambda v: struct.pack('<%dH' % len(v), *v)
        text, data = pack(tw), pack(dw)
        trel, drel = pack(tr), pack(dr)
        st = b''.join(nm.encode('latin1')[:8].ljust(8, b'\0') + bytes([ty, sp])
                      + struct.pack('<H', val) for nm, ty, val, sp in syms)
        flag = 1 if r.random() < 0.03 else 0
        magic = 0o407 if r.random() < 0.97 else r.choice([0o410, 0o411, 0o405, 0])
        tsize, dsize = len(text), len(data)
        if r.random() < 0.05 and tsize:
            tsize -= 1          # an odd size (the word is still there)
        bss = r.choice([0, 0, 2, 3, 10, 100])
        hdr = struct.pack('<8H', magic, tsize, dsize, bss, len(st), 0, 0, flag)
        body = text + data + (b'' if flag else trel + drel) + st
        return hdr + body, [s[0] for s in syms]

    def damage(self, b):
        r = self.r
        if not b or r.random() >= self.mutate:
            return b
        b = bytearray(b)
        for _ in range(r.randint(1, 4)):
            if not b:
                break
            k = r.random()
            if k < 0.4:
                b[r.randrange(len(b))] = r.randrange(256)
            elif k < 0.7 and len(b) >= 2:
                i = r.randrange(len(b) - 1) & ~1
                b[i:i + 2] = struct.pack('<H', r.choice([0, 1, 0x7fff, 0x8000,
                                                         0xffff, 0xfffe]))
            elif k < 0.9:
                del b[r.randrange(len(b)):]
            else:
                b += bytes(r.randrange(256) for _ in range(r.randint(1, 30)))
        return bytes(b)

    def case(self):
        r = self.r
        files, extra = {}, []
        shared = r.sample(NAMES, 6)
        objs = []
        for i in range(r.randint(0, 4)):
            data, names = self.obj(shared)
            fn = 'o%d.o' % i
            files[fn] = data
            objs.append(fn)
            shared += names[:2]
        inputs = list(objs)
        if r.random() < 0.5:
            files['crt0.o'] = ldcmp.crt0()
            inputs.insert(0, 'crt0.o')
        if r.random() < 0.3 and self.members:
            for m in r.sample(self.members, r.randint(1, 3)):
                files[m[0]] = m[1]
                inputs.append(m[0])
        libs = []
        if r.random() < 0.5:
            libs.append('-lc')
        # an archive of its own: plain, current table, out of date
        if r.random() < 0.4:
            mem = [(fn, files[fn]) for fn in objs[:2]]
            mem += r.sample(self.members, r.randint(0, 4))
            for _ in range(r.randint(0, 2)):
                d, _n = self.obj(shared)
                mem.append(('g%d.o' % r.randrange(100), d))
            r.shuffle(mem)
            kind = r.choice(['plain', 'toc', 'stale'])
            a = ldcmp.ar(mem) if kind == 'plain' else ldcmp.ranlib(mem)
            t = None if kind == 'plain' else \
                ldcmp.LIBC_SYMDEF_TIME + (0 if kind == 'toc' else 77)
            where = r.choice(['file', 'lib', 'usr/lib'])
            a = self.damage(a)
            if where == 'file':
                files['z.a'] = a
                libs.append('z.a')
                if t is not None:
                    files['z.a'] = (a, t)
            else:
                extra.append(('%s/libz.a' % where, a, t))
                libs.append('-lz')
        files = {fn: (v if isinstance(v, tuple) else (self.damage(v), None))
                 for fn, v in files.items()}
        # the command line: groups (an input, an option with its argument),
        # shuffled now and then; an option without its argument goes last
        groups = [[a] for a in inputs + libs]
        last = []
        for _ in range(r.randint(0, 4)):
            k = r.random()
            if k < 0.45:
                g = ['-' + ''.join(r.sample('xXsSinOt', r.randint(1, 3)))]
            elif k < 0.55:
                g = ['-o', r.choice(['out', 'a.out', 'p', 'x.o', 'q'])]
            elif k < 0.65:
                g = ['-u', r.choice(shared + ['_atof', '_nothere'])]
            elif k < 0.72:
                g = ['-e', r.choice(shared + ['start', '_nothere'])]
            elif k < 0.78:
                g = ['-D', str(r.choice([0, 2, 10, 100, 4000, -3]))]
            elif k < 0.83:
                g = ['-v', r.choice(['v1', 'v2'])]
            elif k < 0.9:
                g = ['-l' + r.choice(['', 'c', 'z', 'nothere',
                                      'q' * r.randint(10, 40)])]
            elif k < 0.95:
                last.append(r.choice(['-q', '-', '-o', '-u', '-e', '-D', '-v']))
                continue
            else:
                g = ['sub' if r.random() < 0.2 else 'nothere.o']
            groups.insert(r.randint(0, len(groups)), g)
        if r.random() < 0.2:
            r.shuffle(groups)
        args = [a for g in groups for a in g] + last[:1]
        if 'sub' in args:
            files['sub/x'] = (b'', None)
        return args, files, extra


def _one(job):
    seed, mutate, port, keep = job
    g = Gen(seed, mutate)
    args, files, extra = g.case()
    with tempfile.TemporaryDirectory(prefix='ldfuzz') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        for fn, (data, t) in files.items():
            p = os.path.join(src, fn)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(data)
            if t is not None:
                os.utime(p, (t, t))
        root = ldcmp.make_root(os.path.join(tmp, 'root'), extra)
        kind, detail = ldcmp.compare(args, src, root, port, keep)
    return seed, args, kind, detail


def main(argv):
    n, seed, mutate, jobs = 200, 1, 0.0, os.cpu_count() or 1
    port, show, keep = ldcmp.PORT, 0, None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '-n':
            n = int(argv[i + 1])
        elif a == '-s':
            seed = int(argv[i + 1])
        elif a == '--mutate':
            mutate = float(argv[i + 1])
        elif a == '-j':
            jobs = int(argv[i + 1])
        elif a == '--port':
            port = os.path.abspath(argv[i + 1])
        elif a == '--show':
            show = int(argv[i + 1])
        elif a == '--keep':
            keep = os.path.abspath(argv[i + 1])
        else:
            sys.exit(__doc__.split('Usage:')[1].split('The port')[0])
        i += 2
    work = [(seed * 1000003 + k, mutate, port, keep) for k in range(n)]
    counts, bad, shown = {}, [], 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for s, args, kind, detail in ex.map(_one, work):
            counts[kind] = counts.get(kind, 0) + 1
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('case %d (ld %s): %s: %s' % (s, ' '.join(args), kind,
                                                       detail))
            elif kind != 'same' and shown < show:
                shown += 1
                print('case %d: %s: %s' % (s, kind, detail))
    for b in bad[:max(show, 20)]:
        print(b)
    print('ld fuzz (seed %d%s): %d cases, %s' % (
        seed, ', mutate %g' % mutate if mutate else '', n,
        ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
