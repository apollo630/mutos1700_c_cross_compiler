#!/usr/bin/env python3
"""utilfuzz.py - differential fuzzing of mutos_size, mutos_strip, mutos_nm,
mutos_tsort and mutos_lorder against the original tools.

Each case is a directory of files and a random sequence of steps - command
lines, and changes of the files between them - that utilcmp.py's
compare_seq() runs on the original (under mutosemu.py) and on the port, in
copies of one directory, comparing after every step the messages, the exit
status and both trees, byte for byte and mode for mode. The tools are taken
in turn: case k is for TOOLS[k % 5] (or the tools named with --tool).

What a case is made of:
  * objects of the generator's own: symbols of every type (undefined,
    absolute, text, data, bss, common, the file-name and register types,
    local and external), names of 1 to 8 characters, with bytes of 0200 and
    more, equal names and equal values (the order qsort() leaves them in),
    a spare byte behind the type, the four magic numbers and some that are
    none, a header with and without "relocation stripped", odd sizes, no
    symbols at all, a symbol table cut short or a size that is not a
    multiple of 12, bytes after it - and files that are no objects;
  * archives of such members: names of 1 to 14 characters (the 14 make the
    name run on into the header), members that are not objects, a size that
    does not fit, an odd number of bytes, and a few members of the real
    libc.a;
  * the commands: size and strip on files (some missing, empty, or named
    twice), nm with any of its options and some that are none, tsort on a
    graph from the standard input (blanks, tabs and newlines between the
    items; items of 1 to 49 characters; chains, cycles, self-loops, repeated
    pairs, an odd number of items) or from a file, lorder on files, piped to
    tsort;
  * between the commands (--mutate P): a file cut short or some of its bytes
    changed, for the error paths.

Usage:
    utilfuzz.py [-n N] [-s SEED] [--steps K] [--mutate P] [-j N]
                [--tool TOOL ...] [--show N] [--keep DIR]

The port's refusals are counted apart, as in utilcmp.py.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arcmp      # noqa: E402
import utilcmp    # noqa: E402
from arcmp import F        # noqa: E402
from utilcmp import aout, sym, corpus, TOOLS   # noqa: E402

LETTERS = 'abcdefghijklmnopqrstuvwxyz_0123456789ABCXYZ'
# n_type: undefined, absolute, text, data, bss, common (extern + undefined with a
# value), file name, register - local and external - and some that are none
TYPES = [0x00, 0x01, 0x02, 0x03, 0x04, 0x14, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
         0x34, 0x3f, 0x22, 0x22, 0x23, 0x24, 0x20, 0x02, 0x03, 0x05, 0x1e, 0x40, 0x60]
MAGICS = [0o407] * 10 + [0o410, 0o411, 0o405, 0o412, 0o406, 0, 0o177545 & 0xffff]
NAMES = ['a.o', 'b.o', 'c.o', 'main.o', 'x', 'obj', 'f1', 'f2']
ARNAMES = ['x.a', 'lib.a', 'l.a']
BLANKS = [' ', ' ', ' ', '\n', '\n', '\t', ' \n', '\n\n', '  ']


def sym_name(rng, pool=None):
    if pool and rng.random() < .35:
        return rng.choice(pool)
    n = rng.choice([1, 2, 3, 4, 6, 7, 8, 8])
    s = '_' + ''.join(rng.choice(LETTERS) for _ in range(n - 1))
    b = s.encode('latin1')
    if rng.random() < .04:      # a byte of 0200 or more: signed compares
        i = rng.randrange(len(b))
        b = b[:i] + bytes([rng.randrange(0x80, 0x100)]) + b[i + 1:]
    return b


def rand_obj(rng, pool=None):
    kind = rng.random()
    if kind < .06:
        return b'plain text, no object\n' * rng.randrange(1, 4)
    if kind < .08:
        return bytes(rng.randrange(256) for _ in range(rng.randrange(0, 60)))
    magic = rng.choice(MAGICS)
    nsyms = rng.choice([0, 1, 1, 2, 3, 5, 8, 20, 40])
    value = rng.choice([0, 0, 0x10, 0xffff, 0x8000, 0x7fff])
    syms = []
    for _ in range(nsyms):
        t = rng.choice(TYPES)
        v = rng.choice([value, value, rng.randrange(0, 0x10000), rng.randrange(0, 64) * 2])
        syms.append(sym(sym_name(rng, pool), t | (rng.choice([0, 0, 0, 1, 0xff]) << 8)
                        if rng.random() < .05 else t, v))
    o = aout(syms, text=bytes(rng.randrange(256) for _ in range(rng.choice([0, 1, 2, 3, 10, 64]))),
             data=bytes(rng.randrange(256) for _ in range(rng.choice([0, 0, 1, 2, 7]))),
             bss=rng.choice([0, 0, 4, 0x100]), flag=rng.choice([1, 1, 0, 0, 3]), magic=magic,
             reloc=rng.random() < .3)
    r = rng.random()
    if r < .07 and len(o) > 20:
        o = o[:len(o) - rng.randrange(1, 14)]               # a table cut short
    elif r < .12:
        o += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 30)))
    elif r < .15 and len(o) > 16:
        # a_syms not a multiple of 12
        o = o[:8] + struct.pack('<H', struct.unpack_from('<H', o, 8)[0] + 5) + o[10:]
    elif r < .17 and len(o) > 16:
        # a_text larger than the file
        o = o[:2] + struct.pack('<H', rng.choice([0x2000, 0xffff])) + o[4:]
    return o


def rand_archive(rng, pool, cor):
    mem = []
    for _ in range(rng.randrange(0, 6)):
        ln = rng.choice([1, 3, 8, 13, 14, 14])
        name = ''.join(rng.choice('abcdefgh_.xyz') for _ in range(ln)).lstrip('.') or 'm'
        data = rand_obj(rng, pool)
        if rng.random() < .05:      # a member from the real libc.a
            lc = [n for n in cor if n.startswith('lc_')]
            data = cor[rng.choice(lc)]
        mem.append((name, data, rng.randrange(1, 2 ** 31), rng.randrange(1, 256),
                    rng.randrange(1, 256), 0o100644))
    a = arcmp.mkar(mem)
    r = rng.random()
    if r < .08 and len(a) > 4:
        a = a[:len(a) - rng.randrange(1, 12)]
    elif r < .12 and len(a) > 40:
        # a size that does not fit
        i = 2 + 14 + 8
        a = a[:i] + struct.pack('<HH', 0, rng.choice([0, 1, 5, 0x7fff, 0xffff])) + a[i + 4:]
    elif r < .15:
        a = struct.pack('<H', rng.choice([0o177545, 0o177545, 0o407, 0])) + a[2:]
    return a


def make_files(rng, cor, lo=False):
    pool = [sym_name(rng) for _ in range(rng.choice([2, 4, 10]))]
    files = {'empty': F(b''), 'tiny': F(b'1'), 'text': F(b'text\n')}
    objs, archs = [], []
    for n in rng.sample(NAMES, rng.randrange(2, len(NAMES) + 1)):
        files[n] = F(rand_obj(rng, pool), mode=rng.choice([0o644, 0o644, 0o600, 0o755]))
        objs.append(n)
    if not lo or rng.random() < .5:
        for n in rng.sample(ARNAMES, rng.randrange(0, 3)):
            files[n] = F(rand_archive(rng, pool, cor))
            archs.append(n)
    r = rng.random()
    if r < .12:
        pick = rng.choice([n for n in cor if n.startswith(('lc_', 'g_'))])
        files['real.o'] = F(cor[pick])
        objs.append('real.o')
    elif r < .16:
        files['crt0.o'] = F(cor['crt0.o'])
        objs.append('crt0.o')
    return files, objs, archs


def pick_names(rng, objs, archs, many=3):
    names = objs + archs
    out = rng.sample(names, rng.randrange(1, min(len(names), many) + 1)) if names else []
    if rng.random() < .15:
        out.append(rng.choice(['empty', 'tiny', 'text', 'nothere']))
    if rng.random() < .1 and out:
        out.append(out[0])
    rng.shuffle(out)
    return out or ['nothere']


def nm_args(rng, objs, archs):
    opts = ''
    r = rng.random()
    if r < .75:
        opts = '-' + ''.join(rng.sample('gnopru', rng.randrange(1, 5)))
    elif r < .82:
        opts = rng.choice(['-x', '-gx', '-', '--', '-nn', '-ug', '-pr'])
    names = pick_names(rng, objs, archs, 4)
    if rng.random() < .05:
        return ([opts] if opts else [])
    return ([opts] if opts else []) + names


def graph(rng):
    """The text a tsort reads."""
    n = rng.choice([1, 2, 3, 5, 8, 20])
    vocab = ['%s%d' % (rng.choice('abxyz_'), i) for i in range(n)]
    if rng.random() < .2:
        vocab = [''.join(rng.choice(LETTERS) for _ in range(rng.choice([1, 5, 20, 40, 49])))
                 for _ in range(n)]
    kind = rng.random()
    pairs = []
    if kind < .45:                       # a DAG
        order = vocab[:]
        rng.shuffle(order)
        for _ in range(rng.randrange(1, 3 * n + 2)):
            i, j = sorted(rng.sample(range(len(order)), 2)) if len(order) > 1 else (0, 0)
            pairs.append((order[i], order[j]))
    elif kind < .8:                      # anything: cycles, self-loops, repeats
        for _ in range(rng.randrange(1, 3 * n + 2)):
            pairs.append((rng.choice(vocab), rng.choice(vocab)))
    else:                                # a chain
        order = vocab[:]
        rng.shuffle(order)
        pairs = list(zip(order, order[1:])) or [(order[0], order[0])]
    out = ''
    for a, b in pairs:
        out += a + rng.choice(BLANKS) + b + rng.choice(BLANKS)
    if rng.random() < .08:
        out += rng.choice(vocab)         # an odd number of items
    if rng.random() < .04:
        out = out.replace(' ', '\r', 1)  # CR is no separator
    if rng.random() < .03:
        out = ''
    return out.encode('latin1')


def damage(rng, files):
    """A step that cuts a file or changes some of its bytes."""
    names = [n for n, f in files.items() if len(f.data) > 4 and n != 'real.o']
    if not names:
        return ('write', 'late.o', F(rand_obj(rng)))
    n = rng.choice(names)
    if rng.random() < .5:
        return ('trunc', n, rng.randrange(0, min(len(files[n].data), 1500)))
    return ('poke', n, rng.randrange(0, min(len(files[n].data), 300)),
            bytes(rng.randrange(256) for _ in range(rng.randrange(1, 5))))


def make_case(seed, tool, nsteps, mutate):
    rng = random.Random(seed)
    cor = corpus()
    files, objs, archs = make_files(rng, cor, tool == 'lorder')
    steps = []
    for _ in range(nsteps):
        r = rng.random()
        if r < mutate:
            steps.append(damage(rng, files))
            continue
        if r < mutate + .06:
            n = rng.choice(NAMES)
            steps.append(('write', n, F(rand_obj(rng))))
            if n not in objs:
                objs.append(n)
            continue
        if tool == 'size':
            steps.append(pick_names(rng, objs, archs, 4) if rng.random() < .95 else [])
        elif tool == 'strip':
            steps.append(pick_names(rng, objs, archs, 3))
        elif tool == 'nm':
            steps.append(nm_args(rng, objs, archs))
        elif tool == 'tsort':
            if rng.random() < .15:
                name = 'g.txt'
                steps.append(('write', name, F(graph(rng))))
                steps.append([name])
            elif rng.random() < .05:
                steps.append([rng.choice(['nothere', 'empty', 'text'])])
            else:
                steps.append(('stdin', graph(rng), []))
        else:                            # lorder
            if rng.random() < .5:
                steps.append(('pipe', pick_names(rng, objs, archs, 4), []))
            else:
                steps.append(pick_names(rng, objs, archs, 4))
    return files, steps


def _one(job):
    seed, tool, nsteps, mutate, keep = job
    files, steps = make_case(seed, tool, nsteps, mutate)
    res = utilcmp.compare_seq(tool, steps, files, keep, '%s seed %d' % (tool, seed))
    kind, detail, at = 'same', '', len(steps)
    for i, k, d in res:
        if k in ('DIFFERENT', 'BAD-REFUSAL'):
            kind, detail, at = k, d, i
            break
    else:
        for i, k, d in res:
            if k in ('refused', 'orig-fails', 'endless'):
                kind, detail = k, d
                break
    return seed, tool, kind, detail, at, steps


def main(argv):
    n, seed, steps, mutate = 200, 1, 6, 0.0
    jobs, show, keep, tools = os.cpu_count() or 1, 0, None, []
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
        elif a == '--tool':
            if argv[i + 1] not in TOOLS:
                sys.exit('unknown tool %s' % argv[i + 1])
            tools.append(argv[i + 1])
        elif a == '--show':
            show = int(argv[i + 1])
        elif a == '--keep':
            keep = os.path.abspath(argv[i + 1])
        else:
            sys.exit(__doc__.split('Usage:')[1].split('The port')[0])
        i += 2
    tools = tools or list(TOOLS)
    work = [(seed * 1000003 + k, tools[k % len(tools)], steps, mutate, keep) for k in range(n)]
    counts, bad, shown = {}, [], 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for s, tool, kind, detail, at, st in ex.map(_one, work):
            counts[(tool, kind)] = counts.get((tool, kind), 0) + 1
            detail = detail.encode('utf-8', 'backslashreplace').decode()
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                step = st[at] if at < len(st) else None
                bad.append('%s case %d (step %d, %s): %s: %s' % (
                    tool, s, at, step if isinstance(step, tuple) else ' '.join(step or []),
                    kind, detail))
            elif kind != 'same' and shown < show:
                shown += 1
                print('%s case %d: %s: %s' % (tool, s, kind, detail))
    for b in bad[:max(show, 20)]:
        print(b)
    for t in tools:
        print('%s fuzz (seed %d%s): %s' % (
            t, seed, ', mutate %g' % mutate if mutate else '',
            ', '.join('%d %s' % (c, k) for (tt, k), c in sorted(counts.items()) if tt == t)
            or 'no case'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
