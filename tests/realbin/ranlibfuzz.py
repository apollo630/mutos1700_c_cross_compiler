#!/usr/bin/env python3
"""ranlibfuzz.py - differential fuzzing of mutos_ranlib against the original ranlib.

Each case is a directory of files and a random sequence of steps - ar and
ranlib command lines, and changes of the files and of the archive between
them - that ranlibcmp.py's compare_seq() runs on the original ranlib and
ar (under mutosemu.py) and on the ports, in copies of one directory,
comparing after every step the messages, the exit status and both trees,
byte for byte and mode for mode (the date ranlib gives its table aside).

What a case is made of:
  * members: a.out objects of the generator's own - symbols of every type
    (undefined, absolute, text, data, bss, the file-name and register
    types, local and external, 1 to 8 characters), a spare byte behind the
    type now and then, the four magic numbers and some that are none, a
    header without relocation, odd sizes, no symbols at all (the "no name
    list" message, which prints the member's name on through its header),
    a symbol table cut short or a size that is not a multiple of 12, bytes
    after it - and files that are no objects;
  * names of 1 to 14 characters (the 14 of them make the name run on into
    the header), owners, modes and times with no 0 byte among them and
    with, and the names of the archive;
  * the commands: ar to make the archive and to change it between two
    ranlibs (r, d, m, with a position), ranlib on one archive or several,
    with names that are no archive, are not there, are empty, or are
    the archive twice, and now and then more than seventeen of them (the
    stdio slots a file that is no archive keeps);
  * between the commands: the archive cut short, or bytes of it changed
    (with --mutate), for the error paths.

Usage:
    ranlibfuzz.py [-n N] [-s SEED] [--steps K] [--mutate P] [-j N]
                  [--port PATH] [--ar PATH] [--show N] [--keep DIR]

The port's refusals are counted apart as in ranlibcmp.py.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arcmp          # noqa: E402
import ranlibcmp      # noqa: E402
from arcmp import F   # noqa: E402

TYPES = [0x00, 0x01, 0x02, 0x03, 0x04, 0x14, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
         0x34, 0x3f, 0x22, 0x22, 0x23, 0x24, 0x20, 0x02, 0x03]
LETTERS = 'abcdefghijklmnopqrstuvwxyz_0123456789ABC'
NAMES = ['x.a', 'x.a', 'x.a', 'lib.a', 'sub/l.a', 'a-b.a']


def sym_name(rng):
    n = rng.choice([1, 2, 3, 4, 6, 7, 8, 8])
    return '_' + ''.join(rng.choice(LETTERS) for _ in range(n - 1))


def make_obj(rng):
    kind = rng.random()
    if kind < .08:
        return b'plain text, no object\n' * rng.randrange(1, 4)
    if kind < .10:
        return bytes(rng.randrange(256) for _ in range(rng.randrange(0, 60)))
    magic = rng.choice([0o407] * 8 + [0o410, 0o411, 0o405, 0o412, 0o406, 0])
    nsyms = rng.choice([0, 1, 1, 2, 3, 5, 8, 20])
    syms = [(sym_name(rng), rng.choice(TYPES)) for _ in range(nsyms)]
    if rng.random() < .15:
        syms = [(sym_name(rng), 0x20)] * rng.randrange(1, 4)    # undefined only
    spare = rng.choice([0] * 6 + [1, 7, 0xff])
    text = bytes(rng.randrange(256) for _ in range(rng.choice([0, 1, 2, 3, 10, 64])))
    data = bytes(rng.randrange(256) for _ in range(rng.choice([0, 0, 1, 2, 7])))
    o = ranlibcmp.obj(syms, magic=magic, text=text, data=data,
                      bss=rng.choice([0, 0, 4]), flag=rng.choice([0, 0, 0, 1]),
                      spare=spare)
    r = rng.random()
    if r < .08 and len(o) > 20:
        o = o[:len(o) - rng.randrange(1, 14)]               # a table cut short
    elif r < .13:
        o += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 30)))
    elif r < .16 and len(o) > 16:
        # a_syms not a multiple of 12
        o = o[:8] + struct.pack('<H', struct.unpack_from('<H', o, 8)[0] + 5) + o[10:]
    return o


def nonzero_bytes(rng, n):
    return bytes(rng.randrange(1, 256) for _ in range(n))


def make_case(seed, nsteps, mutate):
    rng = random.Random(seed)
    files = {}
    members = []
    for i in range(rng.randrange(2, 8)):
        ln = rng.choice([1, 2, 5, 8, 9, 13, 14, 14, 14])
        name = ''.join(rng.choice('abcdefgh_.xyz') for _ in range(ln)).lstrip('.') or 'm'
        name = name[:14]
        if name in files:
            continue
        data = make_obj(rng)
        if rng.random() < .4:
            files[name] = F(data, mtime=rng.choice([0, 1, 1234567890, 0x01010101,
                                                    2147483647, 3000000000,
                                                    rng.randrange(0, 2 ** 31)]),
                            mode=rng.choice([0o644, 0o755, 0o666, 0o600]),
                            uid=rng.choice([0, 1, 255, 128, 100]),
                            gid=rng.choice([0, 1, 255, 127, 7]))
        else:
            files[name] = F(data, mtime=0x01010101 * rng.randrange(1, 100) & 0x7fffffff,
                            mode=0o644, uid=rng.randrange(1, 256), gid=rng.randrange(1, 256))
        members.append(name)
    files['empty'] = F(b'')
    files['tiny'] = F(b'1')
    files['text'] = F(b'text\n')
    archive = rng.choice(NAMES)
    if rng.random() < .1:
        mem = []
        for _ in range(rng.randrange(0, 4)):
            ln = rng.choice([3, 8, 14, 14])
            mem.append((''.join(rng.choice('abcdefgh') for _ in range(ln)), make_obj(rng),
                        rng.randrange(1, 2 ** 31), rng.randrange(1, 256),
                        rng.randrange(1, 256), 0o100644))
        files[archive] = F(arcmp.mkar(mem))
    steps = [['ar', 'rc', archive] + rng.sample(members, rng.randrange(1, len(members) + 1))]
    for _ in range(nsteps):
        r = rng.random()
        if r < .45:
            args = [archive]
            if rng.random() < .3:
                args = [rng.choice(['tiny', 'nothere', 'empty', 'text', 'x.a', 'lib.a']),
                        archive]
            if rng.random() < .2:
                args = args + [archive]
            if rng.random() < .05:
                args = ['tiny'] * rng.randrange(14, 20) + args
            steps.append(args)
        elif r < .75:
            cmd = rng.choice(['r', 'r', 'rc', 'd', 'm', 'ma', 'mb', 'ra', 'rb', 'rv', 'tv'])
            args = ['ar', cmd]
            pos = rng.choice(members + ['__.SYMDEF'])
            if len(cmd) == 2 and cmd[1] in 'ab':
                args.append(pos)
            args.append(archive)
            if cmd[0] in 'rdm':
                args += rng.sample(members + ['__.SYMDEF'], rng.randrange(1, 3))
            steps.append(args)
        elif r < .82:
            n = rng.choice(members)
            steps.append(('write', n, F(make_obj(rng), mtime=rng.randrange(0, 2 ** 31))))
        elif r < .82 + mutate:
            if rng.random() < .5:
                steps.append(('trunc', archive, rng.randrange(0, 1500)))
            else:
                steps.append(('poke', archive, rng.randrange(0, 300),
                              bytes(rng.randrange(256) for _ in range(rng.randrange(1, 5)))))
        else:
            steps.append([archive])
    return files, steps


def _one(job):
    seed, nsteps, mutate, port, ar_port, keep = job
    files, steps = make_case(seed, nsteps, mutate)
    res = ranlibcmp.compare_seq(steps, files, port, ar_port, keep, 'seed %d' % seed)
    kind, detail, at = 'same', '', len(steps)
    for i, k, d in res:
        if k != 'same':
            kind, detail, at = k, d, i
            break
    return seed, kind, detail, at, steps


def main(argv):
    n, seed, steps, mutate, jobs = 200, 1, 8, 0.0, os.cpu_count() or 1
    port, ar_port, show, keep = ranlibcmp.PORT, ranlibcmp.AR_PORT, 0, None
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
        elif a == '--ar':
            ar_port = os.path.abspath(argv[i + 1])
        elif a == '--show':
            show = int(argv[i + 1])
        elif a == '--keep':
            keep = os.path.abspath(argv[i + 1])
        else:
            sys.exit(__doc__.split('Usage:')[1].split('The port')[0])
        i += 2
    work = [(seed * 1000003 + k, steps, mutate, port, ar_port, keep) for k in range(n)]
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
    print('ranlib fuzz (seed %d%s): %d cases, %s' % (
        seed, ', mutate %g' % mutate if mutate else '', n,
        ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
