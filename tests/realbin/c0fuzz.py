#!/usr/bin/env python3
"""c0fuzz.py - differential fuzzing of the c0 port against the original c0.

The port of c0 (src/mutos_cc/c0port/, docs/C0_PORT_PLAN.md) must do what
the original binary does for any input, not only for the goldens - and
c0 is mostly declarations and error handling, which the goldens touch
little. This script generates random programs and feeds each, as it is
(c0 reads cpp's output, which is C without directives; the programs
here have no directives but may carry "# line" lines, comments and
cpp's SOH markers, which c0 handles itself), to the original c0 under
mutosemu.py and to the port, and compares them with c0cmp.compare():
temp1, temp2, the messages and the exit status, byte for byte.

Generators (--gen):
    c0      (default) declaration-heavy programs: storage classes,
            typedefs, structures, unions, enums, bit-fields, arrays with
            constant-expression bounds, initializers of every kind,
            K&R parameters (register, array, float), nested blocks with
            shadowing, labels and gotos, switches, the old "=+"
            operators, MIN/MAX, casts, sizeof, string and character
            constants with escapes, long and floating constants;
    c1fuzz  tests/realbin/c1fuzz.py's expression programs;
    fuzz_c  tests/mutos_cc/fuzz/fuzz_c.py's programs.
--mutate P: each program is, with probability P, mutated at the token
level (tokens deleted, duplicated, swapped or replaced) - the error
paths: the messages, errflush() and what is still written after them.

Usage:
    c0fuzz.py [-n COUNT] [-s SEED] [-j JOBS] [--gen c0|c1fuzz|fuzz_c]
              [--mutate P] [--port PATH] [--keep DIR] [--show N]

Exit status 1 if any program differs (or a refusal wrote more than the
original). A fixed seed gives the same programs every time.

Requires the Python module "unicorn" (pip install unicorn).
"""
import argparse
import collections
import concurrent.futures
import os
import random
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import c0cmp          # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))


# ---------------------------------------------------------------------
# The declaration-heavy generator.
# ---------------------------------------------------------------------

SCALARS = ('int', 'char', 'unsigned', 'long', 'float', 'double', 'short',
           'unsigned int', 'long int', 'long float', 'unsigned short')


class Gen0:
    def __init__(self, rnd):
        self.r = rnd
        self.n = 0
        self.structs = []       # tag names defined so far
        self.typedefs = []      # (name, kind) kind: 'scalar', 'struct', 'ptr', 'array'
        self.enums = []         # enum constant names
        self.globals = []       # (name, kind) kind 'i', 'c', 'p', 's', 'a', 'f', 'l', 'd'
        self.funcs = []

    def pick(self, seq):
        return self.r.choice(seq)

    def chance(self, p):
        return self.r.random() < p

    def name(self, prefix='v'):
        self.n += 1
        # some names longer than the 8 significant characters - a few of
        # them the same name to c0
        if self.chance(0.01):
            return '%slongname%d' % (prefix, self.n)
        if self.chance(0.08):
            return '%s%dlongname' % (prefix, self.n)
        return '%s%d' % (prefix, self.n)

    # -- constants and constant expressions ---------------------------
    def intconst(self):
        return self.pick(['0', '1', '2', '3', '7', '10', '127', '128', '255',
                          '256', '-1', '32767', '0377', '0x7f', '0xFF', '017',
                          "'a'", "'\\n'", "'\\0'", "'\\377'", "'ab'", "'\\t'",
                          "'\\\\'", "'\\''", str(self.r.randint(0, 1000)),
                          '070000', '0177777', '65535', '32768'])

    def cexpr(self, depth=2):
        """A constant expression c0's fold() can reduce (mostly)."""
        if depth <= 0 or self.chance(0.4):
            if self.enums and self.chance(0.2):
                return self.pick(self.enums)
            if self.chance(0.1):
                return 'sizeof(%s)' % self.pick(['int', 'char', 'long', 'double',
                                                   'char *'] +
                                                  ['struct ' + s for s in self.structs])
            return self.intconst()
        op = self.pick(['+', '-', '*', '/', '%', '<<', '>>', '&', '|', '^',
                        '==', '!=', '<', '>', '<=', '>='])
        if op in ('<<', '>>') and self.chance(0.9):
            return '(%s %s %d)' % (self.cexpr(depth - 1), op, self.r.randint(0, 17))
        if self.chance(0.15):
            return '(%s ? %s : %s)' % (self.cexpr(depth - 1), self.cexpr(depth - 1),
                                       self.cexpr(depth - 1))
        if self.chance(0.1):
            return '(%s%s)' % (self.pick(['-', '~', '!']), self.cexpr(depth - 1))
        return '(%s %s %s)' % (self.cexpr(depth - 1), op, self.cexpr(depth - 1))

    def dim(self):
        return self.pick(['3', '1', '10', '2', '5', str(self.r.randint(1, 20))]) \
            if self.chance(0.8) else '(%s & 7) + 1' % self.cexpr(1)

    def fltconst(self):
        return self.pick(['1.0', '0.5', '2.', '.25', '1e3', '1.5e-3', '3.14159',
                          '0.0', '100.0E+2', '7e0', '123456789.0'])

    def longconst(self):
        return self.pick(['0L', '1L', '100000L', '65536', '-1L', '2147483647L',
                          '0x10000L', '070000000L', '40000', '32768L'])

    def string(self):
        return self.pick(['"abc"', '""', '"a\\nb"', '"\\t\\\\\\"x"',
                          '"\\101\\102\\0\\7"', '"%d\\n"', '"0123456789abcdef"',
                          '"a very long string to cross the fifteen bytes line"'])

    # -- types --------------------------------------------------------
    def basetype(self):
        r = self.r.random()
        if r < 0.55 or not (self.structs or self.typedefs):
            return self.pick(SCALARS)
        if r < 0.8 and self.structs:
            return 'struct ' + self.pick(self.structs)
        if self.typedefs:
            return self.pick(self.typedefs)[0]
        return 'int'

    def declarator(self, name, allow_func=False):
        r = self.r.random()
        if r < 0.45:
            return name
        if r < 0.65:
            return '*' + name
        if r < 0.8:
            return '%s[%s]' % (name, self.dim())
        if r < 0.85:
            return '%s[%s][%s]' % (name, self.dim(), self.dim())
        if r < 0.9:
            return '*%s[%s]' % (name, self.dim())
        if r < 0.95:
            return '(*%s)()' % name
        if allow_func:
            return '%s()' % name
        return '**' + name

    # -- definitions --------------------------------------------------
    def struct_def(self):
        kind = self.pick(['struct', 'struct', 'union'])
        tag = self.name('s')
        lines = ['%s %s {' % (kind, tag)]
        for _ in range(self.r.randint(1, 7)):
            r = self.r.random()
            m = self.name('m')
            if r < 0.15:
                lines.append('\t%s %s:%d;' % (self.pick(['int', 'unsigned', 'char']), m,
                                               self.r.randint(1, 16)))
            elif r < 0.2:
                lines.append('\tint :%d;' % self.r.randint(0, 12))
            elif r < 0.27 and self.structs:
                lines.append('\tstruct %s %s;' % (self.pick(self.structs), m))
            elif r < 0.32:
                lines.append('\tstruct %s *%s;' % (tag, m))
            else:
                lines.append('\t%s %s;' % (self.pick(SCALARS),
                                            self.declarator(m)))
        lines.append('};')
        self.structs.append(tag)
        return '\n'.join(lines)

    def enum_def(self):
        tag = self.name('e')
        names = []
        parts = []
        for _ in range(self.r.randint(1, 6)):
            c = self.name('E')
            names.append(c)
            if self.chance(0.4):
                parts.append('%s = %s' % (c, self.cexpr(1)))
            else:
                parts.append(c)
        self.enums += names
        text = 'enum %s { %s }' % (tag, ', '.join(parts))
        if self.chance(0.5):
            v = self.name('g')
            self.globals.append((v, 'i'))
            return '%s %s;' % (text, v)
        return text + ';'

    def typedef_def(self):
        t = self.name('T')
        r = self.r.random()
        if r < 0.4:
            self.typedefs.append((t, 'scalar'))
            return 'typedef %s %s;' % (self.pick(SCALARS), t)
        if r < 0.6 and self.structs:
            self.typedefs.append((t, 'struct'))
            return 'typedef struct %s %s;' % (self.pick(self.structs), t)
        if r < 0.8:
            self.typedefs.append((t, 'ptr'))
            return 'typedef %s *%s;' % (self.pick(['char', 'int', 'long']), t)
        self.typedefs.append((t, 'array'))
        return 'typedef int %s[%s];' % (t, self.dim())

    def initializer(self, kind):
        r = self.r.random()
        if kind == 'array':
            items = [self.pick([self.cexpr(1), self.intconst()])
                     for _ in range(self.r.randint(0, 8))]
            if self.chance(0.2):
                return '{ %s, }' % ', '.join(items)
            return '{ %s }' % ', '.join(items)
        if kind == 'chararray':
            return self.string() if r < 0.7 else '{ %s }' % ', '.join(
                self.intconst() for _ in range(self.r.randint(1, 5)))
        if kind == 'struct':
            items = []
            for _ in range(self.r.randint(1, 5)):
                items.append(self.pick([self.intconst(), self.fltconst(),
                                        '{ 1, 2 }', self.string(), '0']))
            return '{ %s }' % ', '.join(items) if r < 0.9 else '0'
        if kind == 'ptr':
            return self.pick([self.string(), '0', '&' + (self.pick(self.globals)[0]
                                                         if self.globals else 'x'),
                              '(char *)%s' % self.intconst()])
        if kind == 'float':
            return self.pick([self.fltconst(), self.intconst(), '-' + self.fltconst()])
        if kind == 'long':
            return self.pick([self.longconst(), self.intconst(), '-' + self.longconst()])
        return self.cexpr(2)

    def global_def(self):
        sc = self.pick(['', '', 'static ', 'extern '])
        r = self.r.random()
        v = self.name('g')
        if r < 0.25:
            t = self.pick(['int', 'char', 'unsigned', 'long', 'float', 'double'])
            k = {'int': 'i', 'char': 'c', 'unsigned': 'i', 'long': 'l',
                 'float': 'f', 'double': 'd'}[t]
            self.globals.append((v, k))
            init = ''
            if sc != 'extern ' and self.chance(0.5):
                init = ' = ' + self.initializer({'f': 'float', 'd': 'float',
                                                 'l': 'long'}.get(k, 'int'))
            more = ''
            if self.chance(0.3):
                w = self.name('g')
                self.globals.append((w, k))
                more = ', %s' % w
            return '%s%s %s%s%s;' % (sc, t, v, init, more)
        if r < 0.45:
            self.globals.append((v, 'a'))
            d = self.pick(['', self.dim()])
            init = ''
            if sc != 'extern ' and (d == '' or self.chance(0.5)):
                init = ' = ' + self.initializer('array')
            return '%s%s %s[%s]%s;' % (sc, self.pick(['int', 'long', 'unsigned']), v, d, init)
        if r < 0.6:
            self.globals.append((v, 'a'))
            d = self.pick(['', self.dim()])
            init = ''
            if sc != 'extern ' and (d == '' or self.chance(0.6)):
                init = ' = ' + self.initializer('chararray')
            return '%schar %s[%s]%s;' % (sc, v, d, init)
        if r < 0.72:
            self.globals.append((v, 'p'))
            init = ''
            if sc != 'extern ' and self.chance(0.4):
                init = ' = ' + self.initializer('ptr')
            return '%schar *%s%s;' % (sc, v, init)
        if r < 0.85 and self.structs:
            self.globals.append((v, 's'))
            tag = self.pick(self.structs)
            init = ''
            if sc != 'extern ' and self.chance(0.4):
                init = ' = ' + self.initializer('struct')
            if self.chance(0.3):
                return '%sstruct %s %s[%s]%s;' % (sc, tag, v, self.dim(), init)
            return '%sstruct %s %s%s;' % (sc, tag, v, init)
        if self.typedefs and r < 0.95:
            t, kind = self.pick(self.typedefs)
            self.globals.append((v, 'i'))
            return '%s%s %s;' % (sc, t, v)
        return '%s%s %s;' % (sc, self.basetype(), self.declarator(v, True))

    # -- expressions inside functions ---------------------------------
    def lvalue(self, locs):
        cands = [n for n, k in locs if k in 'icldf'] + \
                [n for n, k in self.globals if k in 'icldf']
        if not cands:
            return 'x'
        v = self.pick(cands)
        return v

    def expr(self, locs, depth):
        if depth <= 0 or self.chance(0.25):
            r = self.r.random()
            if r < 0.5:
                return self.lvalue(locs)
            if r < 0.7:
                return self.intconst()
            if r < 0.8:
                return self.string()
            if r < 0.88:
                return self.fltconst()
            if r < 0.95:
                return self.longconst()
            return self.pick(self.enums) if self.enums else '0'
        r = self.r.random()
        e = lambda: self.expr(locs, depth - 1)
        if r < 0.25:
            op = self.pick(['+', '-', '*', '/', '%', '<<', '>>', '&', '|', '^',
                            '<', '>', '<=', '>=', '==', '!=', '&&', '||',
                            '\\/', '/\\'])
            return '(%s %s %s)' % (e(), op, e())
        if r < 0.4:
            op = self.pick(['=', '=+', '=-', '=*', '=/', '=%', '=&', '=|', '=^',
                            '=<<', '=>>', '+=', '-=', '*=', '<<=', '= -', '= *'])
            return '(%s %s %s)' % (self.lvalue(locs), op, e())
        if r < 0.5:
            return '(%s ? %s : %s)' % (e(), e(), e())
        if r < 0.58:
            return '(%s%s)' % (self.pick(['-', '~', '!', '*', '&', '++', '--']),
                               self.lvalue(locs))
        if r < 0.63:
            return '%s%s' % (self.lvalue(locs), self.pick(['++', '--']))
        if r < 0.71:
            return '((%s)%s)' % (self.pick(['int', 'char', 'long', 'unsigned',
                                            'double', 'float', 'char *', 'int *',
                                            'struct %s *' % (self.pick(self.structs)
                                                             if self.structs else 'x')]),
                                 e())
        if r < 0.76:
            return 'sizeof %s' % self.pick(['(%s)' % self.basetype(), e(), '(%s)' % e()])
        if r < 0.84 and self.funcs:
            return '%s(%s)' % (self.pick(self.funcs),
                               ', '.join(e() for _ in range(self.r.randint(0, 3))))
        if r < 0.9:
            ss = [n for n, k in self.globals if k == 's']
            if ss and self.structs:
                return '%s.%s' % (self.pick(ss), 'm%d' % self.r.randint(1, self.n))
        if r < 0.95:
            arrs = [n for n, k in self.globals + locs if k == 'a']
            if arrs:
                return '%s[%s]' % (self.pick(arrs), e())
        return '(%s, %s)' % (e(), e())

    def stmt(self, locs, depth, labels):
        r = self.r.random()
        e = lambda: self.expr(locs, 3)
        if depth <= 0 or r < 0.35:
            return '%s;' % e()
        s = lambda: self.stmt(locs, depth - 1, labels)
        if r < 0.45:
            text = 'if (%s) %s' % (e(), s())
            if self.chance(0.4):
                text += ' else %s' % s()
            return text
        if r < 0.5:
            return 'if (%s) %s;' % (e(), self.pick(['goto %s' % self.pick(labels),
                                                    'return', 'break', 'continue',
                                                    'return %s' % e()]))
        if r < 0.56:
            return 'while (%s) %s' % (e(), s())
        if r < 0.6:
            return 'do %s while (%s);' % (s(), e())
        if r < 0.66:
            return 'for (%s; %s; %s) %s' % (self.pick([e(), '']), self.pick([e(), '']),
                                            self.pick([e(), '']), s())
        if r < 0.74:
            cases = []
            for _ in range(self.r.randint(0, 6)):
                cases.append('case %s: %s' % (self.cexpr(1), s()))
            if self.chance(0.5):
                cases.append('default: %s' % s())
            return 'switch (%s) { %s }' % (e(), ' '.join(cases))
        if r < 0.78:
            lab = self.pick(labels)
            return '%s: %s' % (lab, s())
        if r < 0.82:
            return 'goto %s;' % self.pick(labels)
        if r < 0.86:
            return 'return%s;' % (' ' + e() if self.chance(0.6) else '')
        if r < 0.9:
            return self.pick(['break;', 'continue;', ';'])
        return self.block(locs, depth - 1, labels)

    def block(self, locs, depth, labels):
        locs = list(locs)
        decls = []
        for _ in range(self.r.randint(0, 4)):
            r = self.r.random()
            v = self.name('l') if self.chance(0.7) or not locs else self.pick(locs)[0]
            if r < 0.15:
                decls.append('register %s %s;' % (self.pick(['int', 'char', 'unsigned',
                                                             'long', 'char *', 'int *',
                                                             'struct %s *' % (
                                                                 self.pick(self.structs)
                                                                 if self.structs else 'z')]),
                                                   v))
                locs.append((v, 'i'))
            elif r < 0.25:
                decls.append('static %s %s = %s;' % (self.pick(['int', 'char', 'long']),
                                                     v, self.cexpr(1)))
                locs.append((v, 'i'))
            elif r < 0.33:
                decls.append('static char %s[] = %s;' % (v, self.string()))
                locs.append((v, 'a'))
            elif r < 0.4:
                decls.append('int %s[%s];' % (v, self.dim()))
                locs.append((v, 'a'))
            elif r < 0.45:
                decls.append('extern int %s;' % v)
                locs.append((v, 'i'))
            elif r < 0.5 and self.structs:
                decls.append('struct %s %s, *%sp;' % (self.pick(self.structs), v, v))
            elif r < 0.55:
                decls.append(self.pick([self.struct_def(), self.enum_def(),
                                        self.typedef_def()]))
            else:
                t = self.pick(['int', 'char', 'long', 'double', 'float', 'unsigned'])
                init = ' = %s' % self.expr(locs, 2) if self.chance(0.4) else ''
                decls.append('%s %s%s;' % (t, v, init))
                locs.append((v, {'int': 'i', 'char': 'c', 'long': 'l',
                                 'double': 'd', 'float': 'f', 'unsigned': 'i'}[t]))
        body = [self.stmt(locs, depth, labels) for _ in range(self.r.randint(1, 6))]
        return '{\n%s\n%s\n}' % ('\n'.join(decls), '\n'.join(body))

    def function(self):
        f = self.name('f')
        params = []
        pdecl = []
        locs = []
        for _ in range(self.r.randint(0, 4)):
            p = self.name('p')
            params.append(p)
            r = self.r.random()
            if r < 0.6 or self.chance(0.3):
                t = self.pick(['int', 'char', 'register int', 'register char *',
                               'float', 'double', 'long', 'char', 'int',
                               'register', 'unsigned'])
                d = self.pick(['%s', '*%s', '%s[]', '%s[3]', '(*%s)()']) % p
                pdecl.append('%s %s;' % (t, d))
            locs.append((p, 'i'))
        rtype = self.pick(['', '', 'char ', 'long ', 'double ', 'int *',
                           'struct %s *' % (self.pick(self.structs) if self.structs
                                            else 'q'), 'unsigned '])
        labels = [self.name('L') for _ in range(self.r.randint(1, 3))]
        body = self.block(locs, self.r.randint(1, 3), labels)
        self.funcs.append(f)
        return '%s%s(%s)\n%s\n%s' % (rtype, f, ', '.join(params),
                                     '\n'.join(pdecl), body)

    def program(self):
        parts = []
        for _ in range(self.r.randint(3, 12)):
            r = self.r.random()
            if r < 0.15:
                parts.append(self.struct_def() + ('' if self.chance(0.7) else ' x%d;'
                                                  % self.n))
            elif r < 0.25:
                parts.append(self.enum_def())
            elif r < 0.35:
                parts.append(self.typedef_def())
            elif r < 0.65:
                parts.append(self.global_def())
            else:
                parts.append(self.function())
        text = '\n'.join(parts) + '\n'
        # the lexer's own business: comments, "# line" lines, SOH, tabs
        if self.chance(0.2):
            text = text.replace('{\n', '{ /* a comment */\n', 1)
        if self.chance(0.15):
            text = '# 10 "file.c"\n' + text
        if self.chance(0.1):
            text = text.replace('\n', '\n\001', 1)
        if self.chance(0.1):
            text = text.replace(' ', '\t', 3)
        return text


# ---------------------------------------------------------------------
# Mutations: the error paths.
# ---------------------------------------------------------------------

TOKEN = re.compile(r'"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'|[A-Za-z_]\w*'
                   r'|\d[\w.]*|\.\d+\w*|->|\+\+|--|<<=?|>>=?|[=!<>]=|&&|\|\||'
                   r'=[-+*/%&|^]|\\/|/\\|\S')
POOL = [';', '}', '{', ')', '(', ',', '=', 'int', 'char', 'struct', 'union',
        'x', '3', '"s"', "'c'", '*', '[', ']', ':', '?', '.', '->', 'sizeof',
        'goto', 'case', 'default', 'else', 'if', 'return', 'typedef', 'enum',
        'register', 'static', 'extern', '0x', '1e', '.5', '07L', '\\', '@',
        "'abc'", '"', '/*', '#', 'auto', 'long', 'unsigned', 'float', '&', '!']


def mutate(rnd, text):
    toks = TOKEN.findall(text)
    if not toks:
        return text
    for _ in range(rnd.randint(1, 3)):
        i = rnd.randrange(len(toks))
        k = rnd.random()
        if k < 0.3:
            del toks[i]
        elif k < 0.45:
            toks.insert(i, toks[i])
        elif k < 0.6 and i + 1 < len(toks):
            toks[i], toks[i + 1] = toks[i + 1], toks[i]
        else:
            toks.insert(i, rnd.choice(POOL))
        if not toks:
            return '\n'
    out = []
    for t in toks:
        out.append(t)
        out.append('\n' if t in (';', '{', '}') else ' ')
    return ''.join(out) + '\n'


# ---------------------------------------------------------------------

def make_programs(args):
    rnd = random.Random(args.seed)
    progs = []
    for _ in range(args.n):
        if args.gen == 'c0':
            text = Gen0(rnd).program()
        elif args.gen == 'c1fuzz':
            import c1fuzz
            text = c1fuzz.Gen(rnd).program()
        else:
            sys.path.insert(0, os.path.join(ROOT, 'tests', 'mutos_cc', 'fuzz'))
            import fuzz_c
            text = fuzz_c.generate(rnd, True, rnd.random() < 0.5)[0]
        if rnd.random() < args.mutate:
            text = mutate(rnd, text)
        progs.append(text)
    return progs


def run_one(job):
    i, text, port, keep = job
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'p%05d.i' % i)
        with open(src, 'w', encoding='latin1') as f:
            f.write(text)
        return c0cmp.compare(src, port, keep)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-n', type=int, default=200)
    ap.add_argument('-s', '--seed', type=int, default=1)
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 1)
    ap.add_argument('--gen', choices=('c0', 'c1fuzz', 'fuzz_c'), default='c0')
    ap.add_argument('--mutate', type=float, default=0.0)
    ap.add_argument('--port', default=c0cmp.PORT)
    ap.add_argument('--keep')
    ap.add_argument('--show', type=int, default=10)
    args = ap.parse_args()
    port = os.path.abspath(args.port)
    keep = os.path.abspath(args.keep) if args.keep else None
    progs = make_programs(args)
    counts = collections.Counter()
    reasons = collections.Counter()
    bad = []
    errs = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        for i, (kind, detail) in enumerate(ex.map(
                run_one, [(i, p, port, keep) for i, p in enumerate(progs)])):
            counts[kind] += 1
            if kind in ('refused', 'orig-fails'):
                reasons[re.sub(r'^\d+: ', '', detail)[:110]] += 1
            elif kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('program %d: %s: %s' % (i, kind, detail))
    for r, n in reasons.most_common():
        print('%6d  %s' % (n, r))
    for b in bad[:args.show]:
        print(b)
    print('c0fuzz: %d programs (%s%s), seed %d: %s' % (
        args.n, args.gen, ', mutated %g' % args.mutate if args.mutate else '',
        args.seed, ', '.join('%d %s' % (n, k) for k, n in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
