#!/usr/bin/env python3
"""cppfuzz.py - differential fuzzing of mutos_cpp against the original cpp.

Random programs for the preprocessor - macros (object-like and with
parameters, formals in strings, recursion, redefinitions, calls across
lines with comments and strings in the actuals, missing and extra
actuals, unterminated calls), conditionals (#if expressions with every
operator, numbers in every base, character constants, defined, macros;
#ifdef, #ifndef, #else, #endif, and the missing ones), includes (" " and
< >, -I directories, the MUTOS root's /usr/include, nesting, a file
including itself, a name from a macro), comments (long ones, under -C),
#line, __LINE__, __FILE__, backslash-newlines, odd bytes (NUL, 0200 and
up), long tokens and long input (the buffer's refills, the side
buffers) - each run through the emulated original and the port with
random options (-P, -C, -R, -D, -U, -I, an output file, the input on
stdin), compared by cppcmp.compare(). --mutate P deletes, duplicates,
swaps or inserts tokens in a share P of the programs, for the error
paths.

Usage:
    cppfuzz.py [-n N] [-s SEED] [-j JOBS] [--mutate P] [--keep DIR]
               [--port PATH] [--show N]

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import os
import random
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cppcmp        # noqa: E402

IDS = ['a', 'b', 'c', 'x', 'y', 'n', 'foo', 'bar', 'f', 'g', 'h', 'max',
       'min', 'X1', '_t', 'a$b', 'NULL', 'unix', 'mutos', 'pdp11', 'long1',
       'abcdefgh', 'abcdefghi', 'abcdefghij', 'k']
NUMS = ['0', '1', '2', '7', '10', '077', '0x1f', '0X10', '08', '09', '1L',
        '0x', '12l', '100', '32767', '32768', '65535', '1a', '0xffff', '3']
CHARS = ["'a'", "'\\n'", "'\\0'", "'\\12'", "'\\\\'", "'\\''", "'x'",
         "'\\t'", "'\\377'", "'0'", "'\\8'", "'\\b'"]
BINOPS = ['+', '-', '*', '/', '%', '<<', '>>', '<', '>', '<=', '>=', '==',
          '!=', '&', '^', '|', '&&', '||', ',']
TEXTOPS = ['+', '-', '*', '/', '=', '==', '!=', '<', '>', '<=', '>=', '&&',
           '||', '&', '|', '^', '!', '~', '(', ')', '[', ']', '{', '}', ';',
           ',', '.', '->', '?', ':', '<<', '>>', '=+', '=-', '#', '$', '@',
           '`']
STRS = ['"hello"', '"a(b,c)"', '"x\\"y"', '"/* no */"', "'('", "','",
        '"line\\\nnext"', '"unterminated', "'q", '""', '"a\\\\"']


class Gen:
    def __init__(self, rnd, depth=0):
        self.r = rnd
        self.macros = {}      # name -> number of params (-1: object-like)
        self.depth = depth

    def ident(self):
        r = self.r
        if r.random() < 0.08:
            return 'L' * r.choice([9, 40, 127, 128, 129, 140])
        if self.macros and r.random() < 0.45:
            return r.choice(list(self.macros))
        return r.choice(IDS)

    def ws(self):
        return self.r.choice([' ', ' ', ' ', '\t', '', '  ', ' /* c */ ',
                              '/**/', ' \\\n ', '\v', '\f', '\r'])

    def comment(self):
        r = self.r
        k = r.random()
        if k < 0.6:
            return '/* ' + ' '.join(r.choice(IDS) for _ in range(r.randint(0, 4))) + ' */'
        if k < 0.85:
            return '/*\n' + '\n'.join('  ' + r.choice(IDS) for _ in range(r.randint(1, 4))) + '\n*/'
        if k < 0.95:    # long: past BUFSIZ, under -C it is split
            return '/*' + ('*' if r.random() < 0.3 else '') + \
                ''.join(r.choice('ab c*/\n') for _ in range(r.randint(400, 1300))) + '*/'
        return '/* ** /* nested? */'

    def text_token(self):
        r = self.r
        k = r.random()
        if k < 0.35:
            return self.ident()
        if k < 0.45:
            return r.choice(NUMS + ['1.e4', '3.14', '.5', '1e10'])
        if k < 0.6:
            return r.choice(TEXTOPS)
        if k < 0.68:
            return r.choice(STRS)
        if k < 0.72:
            return r.choice(CHARS)
        if k < 0.77:
            return self.call()
        if k < 0.8:
            return r.choice(['__LINE__', '__FILE__'])
        if k < 0.83:
            return self.comment()
        if k < 0.85:
            return r.choice(['\\', '\\\n', '\0', '\x80', '\xe9', '\xfe', '\xff',
                             '\x01', '\x7f', '}', '~'])
        return self.ident()

    def text_line(self):
        r = self.r
        return self.ws().join(self.text_token() for _ in range(r.randint(0, 8)))

    def actual(self, nest=0):
        r = self.r
        parts = []
        for _ in range(r.randint(0, 3)):
            k = r.random()
            if k < 0.5:
                parts.append(self.text_token())
            elif k < 0.6 and nest < 2:
                parts.append('(' + ','.join(self.actual(nest + 1)
                                            for _ in range(r.randint(0, 2))) + ')')
            elif k < 0.7:
                parts.append('\n')
            elif k < 0.75:
                parts.append(self.comment())
            elif k < 0.8:
                parts.append('\\\n')
            else:
                parts.append(r.choice(STRS + CHARS + NUMS))
        return self.ws().join(parts)

    def call(self):
        r = self.r
        fm = [m for m, n in self.macros.items() if n >= 0]
        name = r.choice(fm) if fm and r.random() < 0.8 else self.ident()
        n = self.macros.get(name, 1)
        nargs = max(0, n + r.choice([0, 0, 0, 0, -1, 1, 2]))
        if r.random() < 0.05:
            return name     # no actuals at all
        sep = r.choice(['', '', ' ', '\n', ' \n ', '/**/', '\t'])
        args = ','.join(self.actual() for _ in range(nargs))
        close = ')' if r.random() > 0.03 else ''
        return name + sep + '(' + args + close

    def define(self):
        r = self.r
        name = self.ident() if r.random() > 0.04 else r.choice(['1', '"s"', '(', ''])
        k = r.random()
        if k < 0.45:
            body = self.ws().join(self.text_token() for _ in range(r.randint(0, 6)))
            self.macros[name] = -1
            sep = r.choice([' ', ' ', '\t', '', '+', '  '])
            return '#define %s%s%s' % (name, sep, body)
        nf = r.choice([0, 1, 1, 2, 2, 3, 4, 5])
        formals = r.sample(['a', 'b', 'c', 'x', 'y', 'z', 'arg', 'p1'], nf)
        if r.random() < 0.05:
            formals.append(r.choice(['1', '"q"', 'a']))     # bad or repeated
        fl = ','.join(self.ws() + f for f in formals)
        close = ')' if r.random() > 0.03 else ''
        toks = []
        for _ in range(r.randint(0, 7)):
            q = r.random()
            if formals and q < 0.45:
                toks.append(r.choice(formals))
            elif formals and q < 0.55:
                f = r.choice(formals)
                toks.append(r.choice(['"%s"', "'%s'", '"%s and %s"', '"<%s>"'])
                            .replace('%s', f))
            elif q < 0.6:
                toks.append('\\\n')
            elif q < 0.63:
                toks.append(self.comment())
            elif q < 0.65:
                toks.append(r.choice(['\xfe', '\x01\xfe', '\x02\xfe']))
            else:
                toks.append(self.text_token())
        self.macros[name] = len(formals) if formals else 0
        return '#define %s(%s%s%s' % (name, fl, close, ' ' + self.ws().join(toks))

    def ifexpr(self, depth=0):
        r = self.r
        k = r.random()
        if depth > 3 or k < 0.3:
            q = r.random()
            if q < 0.4:
                return r.choice(NUMS)
            if q < 0.55:
                return r.choice(CHARS)
            if q < 0.7:
                return 'defined(%s)' % self.ident() if r.random() < 0.5 \
                    else 'defined %s' % self.ident()
            if q < 0.85:
                return self.ident()
            if q < 0.92 and self.macros:
                return self.call()
            return r.choice(['', '@', '"s"', '1.5', '\\\n1', '-'])
        if k < 0.75:
            op = r.choice(BINOPS)
            right = self.ifexpr(depth + 1)
            if op in ('/', '%') and r.random() < 0.9:
                right = r.choice(['1', '2', '3', '7', '0x10', '-1'])
            return self.ifexpr(depth + 1) + self.ws() + op + self.ws() + right
        if k < 0.85:
            return r.choice(['-', '!', '~']) + self.ifexpr(depth + 1)
        if k < 0.93:
            return '(' + self.ifexpr(depth + 1) + ')'
        return self.ifexpr(depth + 1) + ' ? ' + self.ifexpr(depth + 1) + ' : ' + \
            self.ifexpr(depth + 1)

    def directive(self, includes):
        r = self.r
        k = r.random()
        lead = '#' if r.random() > 0.05 else r.choice(['# ', ' #', '#\t'])
        if k < 0.3:
            return self.define().replace('#', lead, 1)
        if k < 0.38:
            return lead + 'undef ' + self.ident() + r.choice(['', ' junk'])
        if k < 0.55:
            return lead + 'if ' + self.ifexpr()
        if k < 0.62:
            return lead + r.choice(['ifdef ', 'ifndef ']) + self.ident()
        if k < 0.7:
            return lead + r.choice(['else', 'endif', 'endif X', 'else /* c */'])
        if k < 0.8 and includes:
            f = r.choice(includes)
            q = r.random()
            if q < 0.5:
                return lead + 'include "%s"' % f
            if q < 0.7:
                return lead + 'include <%s>' % f
            if q < 0.8:
                return lead + 'include %s' % r.choice(['"nope.h"', '<nope.h>', 'bad',
                                                       '"%s' % f, '<%s' % f, '""'])
            return lead + 'include %s' % self.ident()
        if k < 0.85:
            return lead + r.choice(['line %d "%s"' % (r.randint(0, 99), r.choice(['x.c', 'a/b.h', ''])),
                                    'line', 'line 5', '%d "z.c"' % r.randint(1, 9),
                                    'pragma x', '', ' ', 'ident "q"', 'elif 1'])
        return '#' + self.text_line()

    def program(self, includes, lines=None):
        r = self.r
        out = []
        n = lines if lines is not None else r.choice([3, 8, 15, 30, 60, 120])
        for _ in range(n):
            if r.random() < 0.4:
                out.append(self.directive(includes))
            else:
                out.append(self.text_line())
        if r.random() < 0.05:   # a long token: "token too long"
            out.append('x' * r.choice([600, 1100, 1600]))
        text = '\n'.join(out)
        if r.random() > 0.08:
            text += '\n'
        return text


def mutate(rnd, text):
    toks = []
    cur = ''
    for ch in text:
        if ch.isalnum() or ch == '_':
            cur += ch
        else:
            if cur:
                toks.append(cur)
                cur = ''
            toks.append(ch)
    if cur:
        toks.append(cur)
    for _ in range(rnd.randint(1, 4)):
        if not toks:
            break
        i = rnd.randrange(len(toks))
        op = rnd.random()
        if op < 0.3:
            del toks[i]
        elif op < 0.55:
            toks.insert(i, toks[i])
        elif op < 0.8:
            j = rnd.randrange(len(toks))
            toks[i], toks[j] = toks[j], toks[i]
        else:
            toks.insert(i, rnd.choice(['(', ')', ',', '"', "'", '#', '\n', '/*',
                                       '*/', '\\', '#if 1\n', '#endif\n', '#else\n']))
    return ''.join(toks)


def stress(r, g, incl):
    """A program at one of cpp's limits."""
    k = r.randrange(8)
    if k == 0:      # the string space (32000 bytes): too much defining, no space
        return '\n'.join('#define S%d %s' % (i, 'v' * r.randint(50, 300))
                         for i in range(r.randint(100, 400))) + '\nS1 S99\n'
    if k == 1:      # the symbol table (1000 entries): too many defines
        return '\n'.join('#define N%d %d' % (i, i) for i in range(r.randint(900, 1100)))
    if k == 2:      # pushback: non-tail and tail recursion, deep expansion
        return r.choice(['#define a b>\n#define b a<\na\n',
                         '#define a <b\n#define b >a\na\n',
                         '#define a a\na\n',
                         '#define f(x) f(x x)\nf(1)\n',
                         '#define g(x) x x x x\ng(g(g(g(g(g(1))))))\n'])
    if k == 3:      # the yacc stack (150 states): deep #if
        n = r.choice([60, 140, 148, 149, 150, 151, 160])
        return '#if %s1%s\nyes\n#endif\n' % ('(' * n, ')' * n)
    if k == 4:      # long tokens, strings, comments, lines
        n = r.choice([500, 511, 512, 513, 1000, 1023, 1024, 1025, 1500])
        return r.choice(['x' * n, '"' + 's' * n + '"', '/*' + 'c' * n + '*/',
                         '1' * n, ' '.join(['w'] * (n // 2))]) + '\nafter\n'
    if k == 5:      # many actuals, long actuals
        n = r.choice([30, 31, 32, 40])
        fl = ','.join('p%d' % i for i in range(min(n, 31)))
        return '#define M(%s) %s\nM(%s)\nM(%s)\n' % (
            fl, ' '.join('p%d' % i for i in range(min(n, 31))),
            ','.join(str(i) for i in range(n)),
            ','.join('q' * r.randint(1, 30) for _ in range(r.randint(1, 31))))
    if k == 6:      # a macro call across many lines, comments in it
        return '#define F(a,b) [a|b]\nF(%s,\n%s)\n__LINE__\n' % (
            '\n'.join(r.choice(['x', '/* c\n */', '"s"', '']) for _ in range(r.randint(2, 30))),
            '\n'.join(r.choice(['y', '\\', '(z)']) for _ in range(r.randint(1, 10))))
    return '\n'.join(r.choice(['#if 1', '#ifdef X', '#ifndef X', '#else', '#endif'])
                     for _ in range(r.randint(5, 40))) + '\nend\n'


def make_case(seed, mutate_p, stress_p=0.1):
    """Write one case into a new directory; return (dir, args, stdin, outfile)."""
    r = random.Random(seed)
    d = tempfile.mkdtemp(prefix='cppfuzz')
    root = os.path.join(d, 'root')
    os.makedirs(os.path.join(root, 'usr', 'include', 'sys'))
    os.makedirs(os.path.join(d, 'w', 'inc'))
    w = os.path.join(d, 'w')
    g = Gen(r)
    names = ['a.h', 'b.h', 'inc/c.h', 'sys/t.h', 'self.h', 'std.h']
    files = {
        'a.h': g.program(['b.h', 'std.h'], r.choice([2, 5, 10])),
        'b.h': g.program([], r.choice([1, 3, 6])),
        'inc/c.h': g.program(['a.h'], r.choice([2, 4])),
        'self.h': '#include "self.h"\nself\n',
    }
    for f, t in files.items():
        with open(os.path.join(w, f), 'w', encoding='latin1') as fh:
            fh.write(t)
    with open(os.path.join(root, 'usr', 'include', 'std.h'), 'w', encoding='latin1') as fh:
        fh.write(g.program(['sys/t.h'], r.choice([2, 5])))
    with open(os.path.join(root, 'usr', 'include', 'sys', 't.h'), 'w', encoding='latin1') as fh:
        fh.write(g.program([], 2))
    incl = names + ['/usr/include/std.h', 'c.h']
    text = g.program(incl)
    if r.random() < 0.15:   # a big one: refills, side buffers, pushback
        text = '\n'.join(g.program(incl, 40) for _ in range(r.randint(3, 8)))
    if r.random() < stress_p:
        text = stress(r, g, incl) + ('\n' + text if r.random() < 0.5 else '')
    if r.random() < mutate_p:
        text = mutate(r, text)
    with open(os.path.join(w, 'm.c'), 'w', encoding='latin1') as fh:
        fh.write(text)
    args = []
    for _ in range(r.choice([0, 0, 1, 2, 3])):
        args.append(r.choice(['-P', '-C', '-R', '-E', '-DX', '-DY=2', '-DZ=a b',
                              '-Dfoo(a)=a', '-D', '-Ux', '-Ufoo', '-Iinc', '-I',
                              '-Q', '-', '-DM=1/**/', '-D1x']))
    if r.random() < 0.1:
        args += ['-Iinc'] * r.randint(7, 9)     # dirs[] filled (pflag at dirs[10])
    stdin = b''
    if r.random() < 0.08:
        stdin = text.encode('latin1')
    else:
        args.append('m.c')
    outfile = r.random() < 0.1 and stdin == b''
    return d, w, root, args, stdin, outfile


def reason_key(kind, detail):
    """A refusal's or an unfinished run's reason, without its addresses."""
    import re
    if kind == 'orig-fails':
        orig, _, port = detail.partition('; the port refused: ')
        orig = re.sub(r'at [0-9a-f]{4}:[0-9a-f]{4}', 'at ...', orig)
        return '%s / port: %s' % (orig[:70], reason_key('refused', port))
    if kind == 'endless':
        return 'endless (both): ' + re.sub(r'at [0-9a-f]{4}', 'at ...', detail)[:60]
    d = re.sub(r' [0-9a-f]{4},', ' ....,', detail)
    return d.split(' - the original traps')[0][:110]


def run_one(job):
    seed, mutate_p, port, keep = job
    d, w, root, args, stdin, outfile = make_case(seed, mutate_p)
    try:
        kind, detail = cppcmp.compare(args, w, port, root=root, stdin=stdin,
                                      outfile=outfile)
        if keep and (kind not in ('same', 'refused', 'orig-fails', 'endless') or os.environ.get('CPPFUZZ_KEEPALL')):
            k = os.path.join(keep, 'seed%d' % seed)
            shutil.copytree(d, k, dirs_exist_ok=True)
            with open(os.path.join(k, 'ARGS'), 'w') as f:
                f.write('%r stdin=%d outfile=%s\n%s: %s\n'
                        % (args, len(stdin), outfile, kind, detail))
    finally:
        shutil.rmtree(d, ignore_errors=True)
    return seed, kind, detail


def main(argv):
    n, seed, jobs, mutate_p, keep, port, show = 200, 1, os.cpu_count() or 1, \
        0.0, None, cppcmp.PORT, 0
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '-n':
            n = int(argv[i + 1]); i += 2
        elif a == '-s':
            seed = int(argv[i + 1]); i += 2
        elif a == '-j':
            jobs = int(argv[i + 1]); i += 2
        elif a == '--mutate':
            mutate_p = float(argv[i + 1]); i += 2
        elif a == '--keep':
            keep = os.path.abspath(argv[i + 1]); i += 2
        elif a == '--port':
            port = os.path.abspath(argv[i + 1]); i += 2
        elif a == '--show':
            show = int(argv[i + 1]); i += 2
        else:
            sys.exit(__doc__.split('Usage:')[1].split('Requires')[0])
    counts = {}
    reasons = {}
    bad = []
    work = [(seed * 1000003 + k, mutate_p, port, keep) for k in range(n)]
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for s, kind, detail in ex.map(run_one, work, chunksize=4):
            counts[kind] = counts.get(kind, 0) + 1
            if kind in ('refused', 'orig-fails', 'endless'):
                reasons.setdefault(reason_key(kind, detail), []).append(s)
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('seed %d: %s: %s' % (s, kind, detail[:400]))
    for b in bad[:show or 20]:
        print(b)
    if show:
        for k, v in sorted(reasons.items(), key=lambda kv: -len(kv[1])):
            print('  %4d  %s  (seed %d)' % (len(v), k, v[0]))
    print('cppfuzz: %d programs (seed %d, mutate %.2f): %s'
          % (n, seed, mutate_p, ', '.join('%d %s' % (c, k)
                                          for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
