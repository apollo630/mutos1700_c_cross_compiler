#!/usr/bin/env python3
"""makefuzz.py - differential fuzzing of mutos_make against the original make.

Each case is a directory of files, a random makefile and a random
sequence of steps - make command lines, and changes of the files between
them - that makecmp.py's compare_seq() runs on the original (under
mutosemu.py) and on the port, in copies of one directory, comparing after
every step the messages, the exit status and both trees, byte for byte
and mode for mode. The commands the makefile runs are the host's, the
same for both (makecmp.py: /bin/sh, and fake cc, as, ld, yacc, lex, cpp,
c0 in a root's bin/, so that the built-in and the suffix rules run).

What a case is made of:
  * a pool of files: sources, objects, headers, a library with members,
    in the directory and in a subdirectory, with times that tie
    now and then (the dependency is out of date only when it is newer);
  * a makefile: macros (a value of other macros, spaces, an empty one),
    explicit rules (one and several targets, ":" and "::", dependencies
    from the pool, macros, wildcards, library members and names that do
    not exist, commands with the prefixes - and @, with ";", with
    continuation lines and comments), suffix rules and .SUFFIXES lines,
    .DEFAULT, .SILENT, .IGNORE and .PRECIOUS;
  * command lines: the flags n s i k S r t q d p at random, targets of
    the makefile and of the pool, macros, -f;
  * between the runs: a file written anew, removed or touched to another
    time;
  * with --mutate P: the makefile mutated with probability P - a byte
    replaced, dropped or inserted, a line cut - for the error paths and
    the syntax errors.

Usage:
    makefuzz.py [-n N] [-s SEED] [--steps K] [--mutate P] [-j N]
                [--port PATH] [--show N] [--keep DIR]

The port's refusals are counted apart as in makecmp.py.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arcmp          # noqa: E402
import makecmp        # noqa: E402
from arcmp import F   # noqa: E402

OLD = makecmp.OLD
TIMES = [OLD, OLD + 10, OLD + 10, OLD + 20, OLD + 20, OLD + 30, 0, 1, OLD - 5000]
BASES = ['a', 'b', 'c', 'd', 'e', 'f1', 'main', 'util']
EXTS = ['.c', '.o', '.s', '.y', '.l', '.h', '.f', '']
MACROS = ['A', 'B', 'CC', 'CFLAGS', 'OBJS', 'SRCS', 'LIBS', 'X', 'Y1', 'AS', 'LD']
TARGETS = ['all', 'prog', 'test', 'clean', 'x', 'y', 't1', 't2', 't3', 'lib.a']
WORDS = ['one', 'two', '-O', '-c', 'a.c', 'b.o', 'x', '', 'sub/f.c']
CMDS = ['echo $@', 'echo $<', 'echo $*', 'echo $?', 'echo $(A) $(B)', 'echo [$@] [$?]',
        'touch $@', 'cp $< $@', 'false', 'true', 'sh -c "exit 3"', 'echo quiet',
        'cc -c $<', 'cc -o $@ $?', 'rm -f $@', 'echo a; echo b', 'echo x > $@',
        'test -f $@', 'echo $(CC) $(CFLAGS)', 'echo $(OBJS)', 'ls', 'echo $$HOME',
        'as -o $@ $<', 'ld -o $@ $(OBJS)', 'nosuchtool x', 'echo ${A}${B}', 'echo $(X)y',
        'echo "$(A)"', 'echo $(Y1) $(X) $(SRCS) $(LIBS)']
PREFIX = ['', '', '', '', '@', '-', '-@', '@-']
FLAGS = 'nsikSrtqdp'


def pick(rng, seq):
    return seq[rng.randrange(len(seq))]


def pool_names(rng):
    names = []
    for b in BASES:
        for e in EXTS:
            if rng.random() < .12:
                names.append(b + e)
    for _ in range(rng.randrange(0, 4)):
        names.append('sub/' + pick(rng, BASES) + pick(rng, EXTS))
    return sorted(set(names))


def make_pool(rng):
    files = {}
    for n in pool_names(rng):
        files[n] = F(('data of %s\n' % n).encode() * rng.randrange(0, 3),
                     mtime=pick(rng, TIMES))
    if rng.random() < .3:
        mem = [(pick(rng, ['a.o', 'b.o', 'c.o', 'x.o', 'main.o']),
                b'obj', pick(rng, TIMES) + 100) for _ in range(rng.randrange(1, 4))]
        files['lib.a'] = F(arcmp.mkar(mem), mtime=pick(rng, TIMES))
    return files


def dep(rng, pool):
    r = rng.random()
    names = list(pool)
    if r < .5 and names:
        return pick(rng, names)
    if r < .62:
        return pick(rng, TARGETS)
    if r < .70:
        return pick(rng, ['$(OBJS)', '$(SRCS)', '$(A)', '${B}', '$X', '$(LIBS)'])
    if r < .78:
        return pick(rng, ['*.c', '?.c', '[ab].c', '*.o', 'sub/*', '*.h', '*', '[a-c].*'])
    if r < .85:
        return 'lib.a(%s)' % pick(rng, ['a.o', 'b.o', 'c.o', 'x.o', 'main.o', 'none.o'])
    if r < .88:
        return 'lib.a((%s))' % pick(rng, ['_main', '_none'])
    return pick(rng, BASES) + pick(rng, EXTS[:7])


def command_line(rng):
    c = pick(rng, CMDS)
    return '\t' + pick(rng, PREFIX) + c


def makefile(rng, pool):
    lines = []
    for _ in range(rng.randrange(0, 5)):
        name = pick(rng, MACROS)
        val = ' '.join(pick(rng, WORDS + ['$(%s)' % pick(rng, MACROS), '${%s}' % pick(rng, MACROS),
                                          '$%s' % pick(rng, MACROS[:3])])
                       for _ in range(rng.randrange(0, 4)))
        lines.append('%s%s=%s%s' % (name, pick(rng, ['', ' ', '\t']), pick(rng, ['', ' ']), val))
    if rng.random() < .3:
        lines.append('.SUFFIXES:' + ''.join(' ' + s for s in
                                             rng.sample(['.o', '.c', '.s', '.y', '.l', '.x', '.f'],
                                                        rng.randrange(0, 5))))
    for _ in range(rng.randrange(0, 3)):
        a, b = rng.sample(['.c', '.o', '.s', '.y', '.x', '.l', '.f'], 2)
        lines.append(a + b + ':')
        for _ in range(rng.randrange(0, 3)):
            lines.append(command_line(rng))
    if rng.random() < .15:
        lines.append(pick(rng, ['.DEFAULT:', '.SILENT:', '.IGNORE:', '.PRECIOUS: x y', '.PRECIOUS: *.o']))
        if lines[-1] == '.DEFAULT:':
            lines.append(command_line(rng))
    for _ in range(rng.randrange(1, 8)):
        nt = pick(rng, [1, 1, 1, 2, 3])
        targets = ' '.join(pick(rng, TARGETS + list(pool)[:6] + ['$(X)', '$(OBJS)'])
                           for _ in range(nt))
        colon = pick(rng, [':', ':', ':', ':', '::'])
        deps = ' '.join(dep(rng, pool) for _ in range(rng.randrange(0, 5)))
        line = '%s%s %s' % (targets, colon, deps)
        r = rng.random()
        if r < .1:
            line += ' ; ' + pick(rng, CMDS)
        elif r < .15:
            line += ' # comment'
        elif r < .2 and deps:
            line = line.replace(' ', ' \\\n  ', 1)
        lines.append(line)
        for _ in range(pick(rng, [0, 1, 1, 2, 3])):
            lines.append(command_line(rng))
            if rng.random() < .05:
                lines[-1] += ' \\\n\t' + pick(rng, CMDS)
        if rng.random() < .1:
            lines.append(pick(rng, ['', '# a comment', '\t', '   ']))
    text = '\n'.join(lines) + pick(rng, ['\n', '\n', '\n', ''])
    if rng.random() < .03:
        text = text.replace('\n', '\r\n')
    return text.encode('latin1')


def mutate(rng, data):
    b = bytearray(data)
    for _ in range(rng.randrange(1, 4)):
        if not b:
            break
        i = rng.randrange(len(b))
        r = rng.random()
        if r < .35:
            b[i] = pick(rng, b':;=$()\t\n#\\@- *?[]') if rng.random() < .8 else rng.randrange(256)
        elif r < .6:
            del b[i]
        elif r < .85:
            b.insert(i, pick(rng, b':;=$()\t\n#\\@- *?[]'))
        else:
            j = data.find(b'\n', i)
            del b[i:j if j >= 0 else len(b)]
    return bytes(b)


def run_line(rng, text):
    args = []
    if rng.random() < .55:
        args.append('-' + ''.join(rng.sample(FLAGS, pick(rng, [1, 1, 2, 3]))))
    if rng.random() < .15:
        args.append('-' + ''.join(rng.sample(FLAGS, 1)))
    for _ in range(pick(rng, [0, 0, 1, 1, 2])):
        args.append(pick(rng, MACROS) + '=' + pick(rng, WORDS + ['a b', '$(B)']))
    targets = [t for t in TARGETS if t.encode() in text]
    for _ in range(pick(rng, [0, 0, 1, 1, 2, 3])):
        args.append(pick(rng, targets + TARGETS + ['a.o', 'nothere', 'sub/f.c']))
    rng.shuffle(args)
    if rng.random() < .08:
        args = ['-f', 'makefile'] + args
    return args


def case(seed, nsteps, mutate_p):
    rng = random.Random(seed)
    files = make_pool(rng)
    text = makefile(rng, files)
    if rng.random() < mutate_p:
        text = mutate(rng, text)
    files['makefile'] = F(text, mtime=OLD)
    steps = []
    names = [n for n in files if n != 'makefile']
    for _ in range(nsteps):
        steps.append(run_line(rng, text))
        r = rng.random()
        if r < .12 and names:
            steps.append(('write', pick(rng, names), F(b'new\n', mtime=pick(rng, TIMES))))
        elif r < .18 and names:
            steps.append(('rm', pick(rng, names)))
        elif r < .26 and names:
            steps.append(('touch', pick(rng, names), pick(rng, TIMES)))
    return files, steps


def _one(job):
    seed, nsteps, mutate_p, port, keep = job
    files, steps = case(seed, nsteps, mutate_p)
    res = makecmp.compare_seq(steps, files, port, keep, 'seed %d' % seed)
    kind, detail, at = 'same', '', len(steps)
    for i, k, d in res:
        if k != 'same':
            kind, detail, at = k, d, i
            break
    return seed, kind, detail, at, steps


def main(argv):
    n, seed, steps, mutate_p, jobs = 100, 1, 3, 0.0, os.cpu_count() or 1
    port, show, keep = makecmp.PORT, 0, None
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
            mutate_p = float(argv[i + 1])
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
    work = [(seed * 1000003 + k, steps, mutate_p, port, keep) for k in range(n)]
    counts, bad, shown = {}, [], 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for s, kind, detail, at, st in ex.map(_one, work):
            counts[kind] = counts.get(kind, 0) + 1
            detail = detail.encode('utf-8', 'backslashreplace').decode()
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('case %d (step %d, %s): %s: %s' % (
                    s, at, ' '.join(st[at]) if at < len(st) and not isinstance(st[at], tuple)
                    else (st[at][:2] if at < len(st) else ''), kind, detail))
            elif kind != 'same' and shown < show:
                shown += 1
                print('case %d: %s: %s' % (s, kind, detail))
    for b in bad[:max(show, 20)]:
        print(b)
    print('make fuzz (seed %d%s): %d cases, %s' % (
        seed, ', mutate %g' % mutate_p if mutate_p else '', n,
        ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
