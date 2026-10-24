#!/usr/bin/env python3
"""ccfuzz.py - differential fuzzing of mutos_cc against the original cc.

Each case is a directory of files, a command line and, now and then, a
pass that fails; cccmp.py's compare() runs the original (under
mutosemu.py, its passes traced) and the port (its passes stubs, see
ccstub.c) on copies of it and compares the passes run with their
arguments, what cc wrote to stdout and stderr before, between and after
them, the exit status, and the files left in the directory and in the
root's tmp/.

What a command line is made of, in random order and number:
  * every option the original knows - -c -S -O -P -E -p -f -a -2 -20
    -K<n> -t<passes> -B<prefix> -D -I -U -C -o <name> - alone, repeated
    and combined, -o's name a plain one, a .c, a .o, a .s, one in a
    subdirectory, an option, or missing at the end; and what it does not
    know: other letters (ld's -x, -s, -i, -lX, ...), a lone "-", letters
    0200..0377 (signed chars in the original's switch);
  * names: sources (.c), assembler sources (.s), objects (.o), an
    archive, names without a suffix, of 14 and 15 characters after their
    last '/', in a subdirectory, absent files, empty suffixes ("a.",
    ".c"), duplicates; long paths to short sources, which the original
    copies whole into ts[] (a long name with more than 14 characters
    after its last '/' has no suffix for getsuf() and is not copied);
  * at the limits, with a small probability: 50 to 110 -D options (ptemp[]
    and "Too many DIUC options"), up to 103 -P (ptemp[] runs into main()'s
    own pv), 95 to 101 sources or objects ("Too many ..."), dozens of
    objects (av[] runs into clist[] and llist[]), dozens of sources with
    long paths (ts[] fills, a name runs past it into av[], malloc() gives
    block after block), -K and -B arguments that run over kopt[]
    and pass0[] .. passp[];
  * a pass that ends with an exit status (0 to 255) or a signal.
-K's and -B's arguments hold no '/' past their first byte: a name the
command line writes over one of the original's paths would be a host path
for the port and a MUTOS path for the original, which the path rule
(docs/TOOLCHAIN_PLAN.md, decision 4) does not decide.

Usage:
    ccfuzz.py [-n N] [-s SEED] [-j N] [--port PATH] [--show N] [--keep DIR]

The port's refusals are checked as cccmp.py checks them: what it ran and
wrote before must be what the original ran and wrote up to there.

Requires the Python module "unicorn" (pip install unicorn) and a C
compiler ($CC, default cc) for ccstub.c.
"""
import concurrent.futures
import os
import random
import shutil
import string
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cccmp          # noqa: E402

PROG = b'main()\n{\n\treturn 0;\n}\n'
PASSES = ('/lib/cpp', '/lib/c0', '/lib/c1', '/lib/c2', '/bin/as', '/bin/ld')
UNKNOWN = 'xsinXduveghjklmqrwyzABFGHJLMNQRTVWYZ013456789!$%.=_'


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def word(self, n, alphabet=string.ascii_lowercase + string.digits):
        return ''.join(self.r.choice(alphabet) for _ in range(n))

    def files(self):
        """The case's directory: name -> bytes."""
        r = self.r
        f = {'a.c': PROG, 'b.c': PROG, 'c.s': b'.text\n', 'y.o': cccmp._obj(),
             'z.o': cccmp._obj(), 'lib.a': b'\x65\xff', 'noext': b'',
             'h.h': b'#define X 1\n', 'sub/d.c': PROG, 'sub/e.o': cccmp._obj(),
             'abcdefghijkl.c': PROG, 'abcdefghijklm.c': PROG}
        for _ in range(r.randrange(4)):
            f[self.word(r.randrange(1, 9)) + r.choice(['.c', '.o', '.s'])] = PROG
        return f

    def name(self, files):
        r = self.r
        k = r.random()
        if k < 0.45:
            return r.choice(sorted(files))
        if k < 0.55:
            return self.word(r.randrange(1, 8)) + r.choice(
                ['.c', '.o', '.s', '.i', '.a', '', '.', '.cc', '.C'])
        if k < 0.62:
            return r.choice(['.c', 'a.', '.o', 'x', 'c', '/', 'sub/', '..c',
                             'sub/.c', './a.c', 'sub//d.c', 'a.c/'])
        if k < 0.7:
            # 13, 14, 15, 16 characters after the last '/'
            n = r.choice([11, 12, 13, 14])
            pre = r.choice(['', 'sub/', 'p/q/'])
            return pre + self.word(n) + r.choice(['.c', '.o', '.s'])
        if k < 0.72:
            # a long name the original does not copy (more than 14
            # characters after its last '/': no suffix for getsuf())
            return self.word(r.randrange(15, 140)) + r.choice(['.c', '.o'])
        if k < 0.78:
            # a long path to a short source: copied whole into ts[]
            return self.word(r.randrange(10, 300), 'pq') + '/' + \
                self.word(r.randrange(1, 10)) + r.choice(['.c', '.s'])
        if k < 0.8:
            return '-l' + self.word(r.randrange(0, 4))
        return r.choice(['a.c', 'b.c', 'y.o', 'z.o', 'c.s'])

    def option(self, files):
        """One option, as one or two arguments."""
        r = self.r
        k = r.random()
        if k < 0.35:
            return ['-' + r.choice('cSOPEpfa')]
        if k < 0.42:
            return [r.choice(['-2', '-20', '-2x', '-21'])]
        if k < 0.52:
            return ['-' + r.choice('DIUC') + self.word(r.randrange(0, 5))]
        if k < 0.62:
            nxt = r.random()
            if nxt < 0.4:
                return ['-o', self.word(r.randrange(1, 6))]
            if nxt < 0.8:
                return ['-o', self.name(files)]
            return ['-o', '-' + r.choice('cSOPlx')]
        if k < 0.68:
            return ['-K' + self.word(r.choice([0, 1, 2, 3, 5, 8, 9, 12, 20]),
                                     string.digits + 'xyz')]
        if k < 0.74:
            return ['-t' + ''.join(r.choice('012p3x') for _ in
                                   range(r.randrange(0, 5)))]
        if k < 0.79:
            first = r.choice(['', '/', 'n', '.'])
            return ['-B' + first + self.word(r.randrange(0, 22),
                                             string.ascii_lowercase)]
        if k < 0.9:
            return ['-' + ''.join(r.choice(UNKNOWN)
                                  for _ in range(r.randrange(1, 3)))]
        if k < 0.94:
            return [r.choice(['-', '--', '-o'])]
        return ['-' + chr(r.randrange(0x80, 0x100))]

    def limits(self, files):
        """A command line at one of the original's limits."""
        r = self.r
        k = r.randrange(9)
        if k == 0:
            n = r.randrange(50, 111)
            return ['-D' + self.word(r.randrange(0, 3)) for _ in range(n)] \
                + [r.choice(['a.c', 'b.c'])]
        if k == 1:
            n = r.randrange(97, 104)
            return ['-P'] * n + [r.choice(['-DX', '-E', '-c'])] + ['a.c']
        if k == 2:
            return [r.choice(['a.c', 'b.c', 'c.s']) for _ in
                    range(r.randrange(95, 101))]
        if k == 3:
            return ['o%d.o' % i for i in range(r.randrange(95, 101))]
        if k == 4:
            return [r.choice(['a.c', '-o', 'p'])] + \
                ['n%d.o' % i for i in range(r.randrange(40, 90))]
        if k == 5:
            # sources with long paths: cc copies each whole - ts[] fills,
            # a name runs past it into av[], malloc() gives more blocks
            opts = r.choice([[], ['-c'], ['-S'], ['-P'], ['-O', '-c']])
            return opts + [self.word(r.randrange(10, 250), 'gh') + '/'
                           + self.word(r.randrange(1, 4)) + '%d.c' % i
                           for i in range(r.randrange(4, 40))]
        if k == 6:
            return ['-K' + self.word(r.randrange(8, 200), string.digits + 'k'),
                    'a.c']
        if k == 7:
            return ['-B' + self.word(r.randrange(10, 90), 'bq'),
                    r.choice(['-t', '-t0', '-tp12', '-c']), 'a.c']
        return ['-DX%d' % i for i in range(r.randrange(40, 60))] + \
            ['a.c', 'b.c', 'y.o']

    def case(self):
        r = self.r
        files = self.files()
        if r.random() < 0.15:
            args = self.limits(files)
            if r.random() < 0.5:
                args = self.option(files) + args
        else:
            args = []
            for _ in range(r.randrange(0, 8)):
                if r.random() < 0.55:
                    args += self.option(files)
                else:
                    args.append(self.name(files))
        status = {}
        if r.random() < 0.3:
            for _ in range(r.randrange(1, 3)):
                p = r.choice(PASSES)
                if r.random() < 0.75:
                    status[p] = r.choice([1, 2, 3, 4, 8, 100, 255,
                                          r.randrange(256)]) << 8
                else:
                    status[p] = r.choice([2, 3, 6, 9, 11, 15])
        extra = ()
        if r.random() < 0.1:
            extra = ('lib/c0f', 'lib/c1f', 'bin/asf', 'usr/src/cmd/c/c0',
                     'usr/src/cmd/c/oc1', 'usr/src/cmd/c/cpp')
        return args, files, status, extra


def _one(job):
    seed, port, keep, stub = job
    cccmp._stub = stub
    args, files, status, extra = Gen(seed).case()
    with tempfile.TemporaryDirectory(prefix='ccfuzz') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        cccmp._write_files(src, files)
        kind, detail = cccmp.compare(args, src, port, status=status,
                                     extra=extra, keep=keep)
    return seed, args, status, kind, detail


def main(argv):
    n, seed, jobs = 200, 1, os.cpu_count() or 1
    port, show, keep = cccmp.PORT, 0, None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '-n':
            n = int(argv[i + 1])
        elif a == '-s':
            seed = int(argv[i + 1])
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
    tmp = tempfile.mkdtemp(prefix='ccstub')
    try:
        stub = cccmp.build_stub(tmp)
        work = [(seed * 1000003 + k, port, keep, stub) for k in range(n)]
        counts, bad, shown = {}, [], 0
        with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
            for s, args, status, kind, detail in ex.map(_one, work):
                counts[kind] = counts.get(kind, 0) + 1
                line = 'case %d (cc %s%s): %s: %s' % (
                    s, ' '.join(args)[:300],
                    ' with %r' % status if status else '', kind, detail)
                if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    bad.append(line)
                elif kind != 'same' and shown < show:
                    shown += 1
                    print(line)
        for b in bad[:max(show, 20)]:
            print(b)
        print('cc fuzz (seed %d): %d cases, %s' % (
            seed, n, ', '.join('%d %s' % (c, k)
                               for k, c in sorted(counts.items()))))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
