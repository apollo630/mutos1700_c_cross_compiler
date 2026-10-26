#!/usr/bin/env python3
"""arfuzz.py - differential fuzzing of mutos_ar against the original ar.

Each case is a directory of files and a random sequence of steps -
ar command lines, and changes of the files and of the archive between
them - that arcmp.py's compare_seq() runs on the original (under
mutosemu.py) and on the port, in copies of one directory, comparing after
every step the messages, the exit status and both trees, byte for byte and
mode for mode.

What a case is made of:
  * a pool of files: sizes of 0 to 1500 bytes (odd and even, the block
    edges 511 to 513 among them), names of 1 to 20 characters (past
    DIRSIZ = 14 now and then), in the directory and in a subdirectory,
    modes with the set-id and sticky bits, uids and gids above 127,
    times from 1970 to 2038 and past it (a time that is a negative
    long);
  * the commands r q d x p t m, with the keys v u c l a b i at random (and
    the odd repeated or unknown key), the files drawn from the pool, the
    archive's members, names that exist nowhere, a name twice, a path
    (./f, sub/f, f/), the position names of a, b and i drawn from the
    archive's members, from the pool and from nowhere;
  * between the commands: a file written anew, removed, touched to
    another time; the archive cut short, or a byte of it changed
    (with --mutate), for the error paths.

Usage:
    arfuzz.py [-n N] [-s SEED] [--steps K] [--mutate P] [-j N] [--port PATH]
              [--show N] [--keep DIR]

The port's refusals are counted apart as in arcmp.py; the opening of a
directory is never generated.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arcmp          # noqa: E402
from arcmp import F   # noqa: E402

SIZES = [0, 1, 2, 3, 5, 10, 100, 255, 256, 257, 510, 511, 512, 513, 514, 1023,
         1024, 1025, 1500]
TIMES = [0, 1, 59, 3600, 86400, 566554203, 600000000, 946684800, 1000000000,
         1234567890, 1600000000, 1700000000, 2147483647, 2147483648, 3000000000,
         4294967295]
MODES = [0o644, 0o755, 0o600, 0o444, 0o666, 0o4755, 0o2755, 0o1777, 0o7777, 0o000,
         0o100, 0o311]
OWNERS = [(0, 0), (0, 1), (1, 1), (100, 50), (127, 127), (128, 128), (200, 255),
          (255, 255), (65535, 65535), (1000, 1000), (256, 257)]
LONG = ['a_name_longer_than_14', 'exactly14chars', 'fifteen_chars_15',
        'twenty_chars_in_name_', 'x' * 14, 'y' * 15]
KEYS = 'vucl'


def blob(rng, n):
    kind = rng.randrange(4)
    if kind == 0:
        return bytes(rng.randrange(256) for _ in range(n))
    if kind == 1:
        return (b'line of text\n' * (n // 13 + 1))[:n]
    if kind == 2:
        return bytes([rng.randrange(256)]) * n
    return bytes(i & 255 for i in range(n))


def make_file(rng, n=None):
    uid, gid = rng.choice(OWNERS)
    return F(blob(rng, rng.choice(SIZES) if n is None else n),
             mtime=rng.choice(TIMES) if rng.random() < .5 else
             rng.randrange(0, 2 ** 31), mode=rng.choice(MODES), uid=uid, gid=gid)


def make_pool(rng):
    names = ['f%d' % i for i in range(rng.randrange(3, 9))]
    names += rng.sample(LONG, rng.randrange(0, 3))
    pool = {}
    for n in names:
        pool[n] = make_file(rng)
    for i in range(rng.randrange(0, 3)):
        pool['sub/s%d' % i] = make_file(rng)
    return pool


def members_of(rng, files):
    """Names that may be archive members after a few steps: the pool's
    files, as the command line would name them or as ar stores them."""
    out = []
    for n in files:
        out.append(n)
        out.append(os.path.basename(n))
    return out


def a_file(rng, pool, archive_names):
    """A name for the command line."""
    r = rng.random()
    name = rng.choice(list(pool))
    if r < .08:
        return 'nothere%d' % rng.randrange(3)
    if r < .12 and archive_names:
        return rng.choice(archive_names)
    if r < .16:
        return './' + name
    if r < .19:
        return name + '/'
    if r < .22:
        return 'sub/' + os.path.basename(name)
    return name


def command(rng, pool, archive, archive_names):
    cmd = rng.choice('rrrrqdxptmm' + 'rr')
    keys = [cmd]
    for k in KEYS:
        if rng.random() < .25:
            keys.append(k)
    if rng.random() < .05:
        keys.append(rng.choice('vvz-0'))
    pos = None
    if cmd in 'rm' and rng.random() < .35:
        keys.append(rng.choice('abi'))
        if rng.random() < .05:
            keys.append(rng.choice('abi'))
        r = rng.random()
        if r < .6 and archive_names:
            pos = rng.choice(archive_names)
        elif r < .85:
            pos = a_file(rng, pool, archive_names)
        else:
            pos = 'nothere'
    elif cmd == 'q' and rng.random() < .05:
        keys.append(rng.choice('abi'))
        pos = a_file(rng, pool, archive_names)
    rng.shuffle(keys)
    if rng.random() < .03:
        keys = ['u']                       # u alone is r
        if pos is not None:
            pos = None
    nfiles = rng.choice([0, 1, 1, 1, 2, 2, 3, 4, 6])
    if cmd in 'xptd' and rng.random() < .4:
        nfiles = 0 if cmd != 'd' else nfiles
    files = [a_file(rng, pool, archive_names) for _ in range(nfiles)]
    if files and rng.random() < .1:
        files.append(rng.choice(files))
    if 'l' in keys and rng.random() < .5:
        keys.remove('l')
    args = [''.join(keys)]
    if pos is not None:
        args.append(pos)
    args.append(archive)
    args += files
    return args


def case(seed, nsteps, mutate):
    rng = random.Random(seed)
    pool = make_pool(rng)
    archive = rng.choice(['x.a', 'x.a', 'x.a', 'lib.a', 'sub/s.a'])
    files = dict(pool)
    archive_names = []
    steps = []
    if rng.random() < .15:
        # a start archive of its own
        mem = []
        for _ in range(rng.randrange(0, 5)):
            n = rng.choice(list(pool)) if rng.random() < .7 else rng.choice(LONG)
            uid, gid = rng.choice(OWNERS)
            mem.append((os.path.basename(n), blob(rng, rng.choice(SIZES)),
                        rng.choice(TIMES), uid, gid, rng.choice(MODES) | 0o100000))
        files[archive] = F(arcmp.mkar(mem))
        archive_names = [m[0] for m in mem]
    for _ in range(nsteps):
        r = rng.random()
        if r < .08:
            n = rng.choice(list(pool))
            steps.append(('write', n, make_file(rng)))
        elif r < .10:
            n = rng.choice(list(pool))
            steps.append(('rm', n))
            pool.pop(n, None) if len(pool) > 2 else None
        elif r < .14:
            steps.append(('touch', rng.choice(list(pool)), rng.choice(TIMES)))
        elif r < .14 + mutate:
            if rng.random() < .5:
                steps.append(('trunc', archive, rng.randrange(0, 1200)))
            else:
                steps.append(('poke', archive, rng.randrange(0, 200),
                              bytes(rng.randrange(256) for _ in range(rng.randrange(1, 5)))))
        else:
            steps.append(command(rng, pool, archive, archive_names))
            for f in steps[-1][1:]:
                base = os.path.basename(f.rstrip('/'))
                if base and base not in archive_names and len(archive_names) < 40:
                    archive_names.append(base)
    return files, steps


def _one(job):
    seed, nsteps, mutate, port, keep = job
    files, steps = case(seed, nsteps, mutate)
    res = arcmp.compare_seq(steps, files, port, keep, 'seed %d' % seed)
    kind, detail, at = 'same', '', len(steps)
    for i, k, d in res:
        if k != 'same':
            kind, detail, at = k, d, i
            break
    return seed, kind, detail, at, steps


def main(argv):
    n, seed, steps, mutate, jobs = 200, 1, 8, 0.0, os.cpu_count() or 1
    port, show, keep = arcmp.PORT, 0, None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '-n':
            n = int(argv[i + 1])
        elif a == '-s':
            seed = int(argv[i + 1])
        elif a == '--steps':
            steps = int(argv[i + 1])
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
    work = [(seed * 1000003 + k, steps, mutate, port, keep) for k in range(n)]
    counts, bad, shown = {}, [], 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for s, kind, detail, at, st in ex.map(_one, work):
            counts[kind] = counts.get(kind, 0) + 1
            detail = detail.encode('utf-8', 'backslashreplace').decode()
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('case %d (step %d, %s): %s: %s' % (
                    s, at, ' '.join(st[at]) if not isinstance(st[at], tuple)
                    else st[at][:2], kind, detail))
            elif kind != 'same' and shown < show:
                shown += 1
                print('case %d: %s: %s' % (s, kind, detail))
    for b in bad[:max(show, 20)]:
        print(b)
    print('ar fuzz (seed %d%s): %d cases, %s' % (
        seed, ', mutate %g' % mutate if mutate else '', n,
        ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
