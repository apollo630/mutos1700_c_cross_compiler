#!/usr/bin/env python3
"""v30kit.py - the hardware check of step V4: the 80186/V30 code of -1
run on the MUTOS 1700 hardware (the owner's A7100 with the NEC V30).

docs/TOOLCHAIN_PLAN.md, section 7.4, step V4: "probes of each form with
edge values, a program per rule that prints its values, ... the gain
measured". The hardware has no tool that knows -1 - its as has none of
the 80186 instructions -, so everything is built here, by the cross
toolchain, and the kit carries the linked programs. This tool makes the
kit and predicts, line for line, what the V30 must print:

  - probe.v: the probes. One routine for every 80186/V30 form the three
    rules of -1 write (docs/RULES.md, "The V30 rules"), assembled by
    mutos_as -1: "push *n." and "push #n" (bytes, words, addresses);
    "sal", "sar" and "shr" of each of ax, bx, cx, dx, si, di by each
    count from 3 to 15; "imul ax,*n." and "imul ax,#n". Its driver (C,
    8086 code) calls each routine with edge values and with the six
    arithmetic flags all clear and all set beforehand, and prints the
    result, the flags the NEC manual defines for the form (push: all
    six, unchanged; a shift: CY, P, Z, S; imul: CY, V) and how many of
    the other registers changed (none may: CX is not loaded, DX not
    written; for a push the stack pointer's step, 2). Beside each
    routine stands its twin, the 8086 sequence the rule replaces ("mov
    cx,*n." / "sal r,cl" ...): the driver counts, on the standard error,
    in how many cases the twin's result or any of its six flags differs
    - the flags a form leaves undefined among them, which is for the
    record (C1-15 branches on one).
  - a program per rule that prints its values - push (calls with
    constants of every kind), shift (every count, type and use), imul
    (constants of every kind, in place, tested, under an index) -, each
    as 8086 code (N.8, "cc") and as V30 code (N.v, "cc -1"); shift and
    imul, rules of c1, without -O and with it (shift, shiftO), push, a
    rule of c2, with -O.
  - a timing program per rule (bpush, bshift, bimul; .8 and .v): a loop
    of the rule's instructions, which prints a sum and, on the standard
    error, the seconds it took (time()).
  - v30check.sh, for the hardware's sh: runs them all and compares what
    they print with expect/ (cmp without -s, so that the reason of a
    difference - or a missing expect/ file - is in the log), the timing
    programs under MUTOS's time (its user time in tenths of a second);
    README, with the table of what to expect.

The prediction: what a program must print is computed here in Python
(C's arithmetic on 16 bits; for the probes the forms' definitions), not
by the compiler under test - and every program, run under
tests/realbin/mutosemu.py, must print exactly that, as 8086 code and as
V30 code. Checked besides: each N.v has the rule's instructions (as's
report) and each N.8 none; every 80186/V30 instruction in the kit's
programs and in the kernel sources of the tree (cc -O -S -1) is one of
the probed forms; a timing program's V30 clocks, with and without -1,
are counted by tests/mutos_cc/fuzz/x86sim.py. A disagreement is
reported and no kit is written.

Usage:
    v30kit.py [--out DIR] [--tar FILE] [--seconds N] [--check]

--out: the kit's directory (default "v30kit"; it must not exist or be
empty). --tar: also a tar file of the kit. --seconds: the time a timing
program's 8086 code is to take by its V30 clocks at 4.9152 MHz (default
40). --check: the prediction only, no kit written (in "make
check-realbin", with --seconds 2). Exit status 0 if the prediction
holds, 1 otherwise. Requires the Python module "unicorn" and a built
tree ("make").
"""

import io
import os
import random
import re
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'tests', 'realbin'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'mutos_cc', 'fuzz'))
sys.path.insert(0, HERE)

try:
    import mutosemu  # noqa: E402
except ImportError:
    sys.exit('v30kit.py: needs the Python module "unicorn" (pip install unicorn)')
import x86sim  # noqa: E402
sys.path.append(os.path.join(ROOT, 'scripts'))
import v30report  # noqa: E402

MUTOS_ROOT = os.path.join(ROOT, 'mutos_root')
CC = os.path.join(MUTOS_ROOT, 'bin', 'cc')
AS = os.path.join(MUTOS_ROOT, 'bin', 'as')
NM = os.path.join(MUTOS_ROOT, 'bin', 'nm')
HZ = 4915200            # the A7100's processor clock
MAX_INSNS = 400000000

# ---- the probes -------------------------------------------------------------

CF, PF, AF, ZF, SF, OF = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800
ARITH = CF | PF | AF | ZF | SF | OF
KINDS = ('push', 'shift', 'imul')
# the flags the NEC V20/V30 user's manual defines after each form
MASK = {'push': ARITH, 'shift': CF | PF | ZF | SF, 'imul': CF | OF}
REGS = ('ax', 'bx', 'cx', 'dx', 'si', 'di')
SENT = (0xa5a1, 0xa5a2, 0xa5a3, 0xa5a4, 0xa5a5, 0xa5a6)
VALS = (0, 1, 2, 3, 0x7f, 0x80, 0xb5, 0xb6, 0xff, 0x100, 0x1234, 0x5555,
        0x7fff, 0x8000, 0x8001, 0xaaaa, 0xff4b, 0xfffe, 0xffff)
PUSH_BYTES = (-128, -127, -1, 0, 1, 2, 100, 126, 127)
PUSH_WORDS = (-32768, -32767, -129, 128, 255, 256, 1000, 0x1234, 32767)
PUSH_ADDRS = ('_pr', '_pr+4', '_vals', '_ptab+2', '_main')
SHIFTS = ('sal', 'sar', 'shr')
COUNTS = tuple(range(3, 16))
IMUL_BYTES = (-128, -127, -100, -7, -3, -1, 0, 1, 3, 5, 6, 7, 10, 100, 126, 127)
IMUL_WORDS = (-32768, -32767, -1000, -129, 128, 129, 142, 255, 256, 257, 300,
              1000, 12345, 32767)


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def u16(v):
    return v & 0xffff


def num(n):
    """A number as the assembler reads it (decimal with a dot)."""
    return '%d.' % n


def probes():
    """The probes in their order: dicts with kind, the form's lines, the
    twin's lines (None: no twin), the result's register and what the model
    needs (n; op; sym)."""
    out = []
    for n in PUSH_BYTES:
        out.append(dict(kind='push', form=['push\t*%s' % num(n)], n=n, reg=0,
                        twin=['mov\tax,#%s' % num(n), 'push\tax']))
    for n in PUSH_WORDS:
        out.append(dict(kind='push', form=['push\t#%s' % num(n)], n=n, reg=0,
                        twin=['mov\tax,#%s' % num(n), 'push\tax']))
    for a in PUSH_ADDRS:
        out.append(dict(kind='push', form=['push\t#%s' % a], sym=a, reg=0,
                        twin=['mov\tax,#%s' % a, 'push\tax']))
    for op in SHIFTS:
        for r, reg in enumerate(REGS):
            for n in COUNTS:
                twin = None if reg == 'cx' else ['mov\tcx,*%s' % num(n),
                                                 '%s\t%s,cl' % (op, reg)]
                out.append(dict(kind='shift', form=['%s\t%s,*%s' % (op, reg, num(n))],
                                op=op, n=n, reg=r, twin=twin))
    for n in IMUL_BYTES:
        out.append(dict(kind='imul', form=['imul\tax,*%s' % num(n)], n=n, reg=0,
                        twin=['mov\tcx,*%s' % num(n), 'imul\tcx']))
    for n in IMUL_WORDS:
        out.append(dict(kind='imul', form=['imul\tax,#%s' % num(n)], n=n, reg=0,
                        twin=['mov\tcx,#%s' % num(n), 'imul\tcx']))
    return out


SETFLAGS = '''\tpushf
\tpop\tax
\tand\tax,#/f72a
\tor\tax,*6.(bp)
\tpush\tax
'''


def routine(label, p, lines):
    """One routine f(value, flags): the six arithmetic flags set as asked,
    the lines executed, the flags then into _pf and the registers (for a
    push: the word pushed and the stack pointer's step) into _pr."""
    body = '\n'.join('\t' + ln for ln in lines) + '\n'
    if p['kind'] == 'push':
        return (label + ':\tpush\tbp\n\tmov\tbp,sp\n' + SETFLAGS +
                '\tpopf\n\tmov\tdx,sp\n' + body +
                '\tpushf\n\tpop\t_pf\n\tmov\tcx,sp\n\tpop\tax\n\tsub\tdx,cx\n'
                '\tmov\t_pr,ax\n\tmov\t_pr+2,dx\n\tpop\tbp\n\tret\n')
    load = ''.join('\tmov\t%s,#/%x\n' % (r, s) for r, s in zip(REGS, SENT))
    store = ''.join('\tmov\t_pr+%s,%s\n' % (num(2 * i), r) for i, r in enumerate(REGS))
    return (label + ':\tpush\tbp\n\tmov\tbp,sp\n\tpush\tsi\n\tpush\tdi\n' + SETFLAGS +
            load + '\tmov\t%s,*4.(bp)\n\tpopf\n' % REGS[p['reg']] + body +
            '\tpushf\n\tpop\t_pf\n' + store + '\tpop\tdi\n\tpop\tsi\n\tpop\tbp\n\tret\n')


MODULES = ('push', 'sal', 'sar', 'shr', 'imul')


def module_of(p):
    return p.get('op', p['kind'])


def probe_s(ps):
    """The probes' assembler sources, {name: text}, one module for the
    pushes, one for each shift and one for the products - the original ld
    cannot link an object of 32K or more of text (LD-07). Each module's
    data is its part of the table the driver walks, three words a probe
    (the routine, its twin or 0, the kind times 16 plus the result's
    register): linked in this order the parts are one table, _ptab, which
    the last module ends with a 0."""
    out = {}
    for mi, mod in enumerate(MODULES):
        t = ['| pr%d.s - made by tests/rules/v30kit.py: the 80186/V30 forms of -1\n'
             '| (%s), a routine each, and their 8086 twins. Assembled with mutos_as -1.\n'
             '.globl\t_ptab, _pr, _pf\n.data\n' % (mi, mod)]
        if mi == 0:
            t.append('_ptab:\n')
        mine = [(i, p) for i, p in enumerate(ps) if module_of(p) == mod]
        for i, p in mine:
            t.append('\t.word\tp%d, %s, %s\n' % (
                i, 't%d' % i if p['twin'] else '0',
                num(KINDS.index(p['kind']) * 16 + p['reg'])))
        if mi == len(MODULES) - 1:
            t.append('\t.word\t0, 0, 0\n')
        t.append('.text\n.even\n')
        for i, p in mine:
            t.append(routine('p%d' % i, p, p['form']))
            if p['twin']:
                t.append(routine('t%d' % i, p, p['twin']))
        out['pr%d.s' % mi] = ''.join(t)
    return out


PROBE_C = '''/* probe.c - made by tests/rules/v30kit.py: the driver of the probes. */
#include <stdio.h>
extern struct pe {
	int (*fn)();
	int (*tw)();
	int k;
} ptab[];
int pr[6], pf;
int vals[%(nv)d] = { %(vals)s };
int sent[6] = { %(sent)s };
int mask[3] = { %(mask)s };
int bit[6] = { 01, 04, 020, 0100, 0200, 04000 };
char *kind[3] = { "push", "shift", "imul" };
int ntw[3], nres[3], nfl[3][6];

main()
{
	register int i, j;
	int k, t, v, f, fin, res, raw, oth, d, nv;

	for (i = 0; ptab[i].fn; i++) {
		k = ptab[i].k >> 4;
		t = ptab[i].k & 017;
		nv = k ? %(nv)d : 1;
		for (v = 0; v < nv; v++)
			for (f = 0; f < 2; f++) {
				fin = f ? %(arith)s : 0;
				(*ptab[i].fn)(vals[v], fin);
				res = pr[t];
				raw = pf & %(arith)s;
				if (k == 0)
					oth = pr[1];
				else {
					oth = 0;
					for (j = 0; j < 6; j++)
						if (j != t && pr[j] != sent[j])
							oth++;
				}
				printf("%%d %%x %%d %%x %%x %%d\\n", i, vals[v], f, res,
					raw & mask[k], oth);
				if (ptab[i].tw) {
					(*ptab[i].tw)(vals[v], fin);
					ntw[k]++;
					if (pr[t] != res)
						nres[k]++;
					d = (pf & %(arith)s) ^ raw;
					for (j = 0; j < 6; j++)
						if (d & bit[j])
							nfl[k][j]++;
				}
			}
	}
	for (k = 0; k < 3; k++)
		fprintf(stderr, "%%s: %%d twins, result differs %%d, flag differs: CY %%d P %%d AC %%d Z %%d S %%d V %%d\\n",
			kind[k], ntw[k], nres[k], nfl[k][0], nfl[k][1], nfl[k][2],
			nfl[k][3], nfl[k][4], nfl[k][5]);
	return 0;
}
'''


def probe_c():
    return PROBE_C % {'nv': len(VALS), 'vals': ', '.join('0%o' % v for v in VALS),
                      'sent': ', '.join('0%o' % v for v in SENT),
                      'mask': ', '.join('0%o' % MASK[k] for k in KINDS),
                      'arith': '0%o' % ARITH}


def szp(res):
    f = 0
    if res == 0:
        f |= ZF
    if res & 0x8000:
        f |= SF
    if bin(res & 0xff).count('1') % 2 == 0:
        f |= PF
    return f


def probe_model(ps, syms):
    """The lines probe.v must print: the forms by their definitions."""
    out = []
    for i, p in enumerate(ps):
        k = p['kind']
        for v in (VALS if k != 'push' else VALS[:1]):
            for f in (0, 1):
                fin = ARITH if f else 0
                if k == 'push':
                    if 'sym' in p:
                        name, _, off = p['sym'].partition('+')
                        res = u16(syms[name] + int(off or 0))
                    else:
                        res = u16(p['n'])
                    fl, oth = fin, 2
                elif k == 'shift':
                    n = p['n']
                    if p['op'] == 'sal':
                        res, cy = u16(v << n), v >> (16 - n) & 1
                    elif p['op'] == 'shr':
                        res, cy = v >> n, v >> (n - 1) & 1
                    else:
                        res, cy = u16(s16(v) >> n), s16(v) >> (n - 1) & 1
                    fl, oth = szp(res) | cy, 0
                else:
                    prod = s16(v) * p['n']
                    res = u16(prod)
                    fl, oth = (0 if -32768 <= prod <= 32767 else CF | OF), 0
                out.append('%d %x %d %x %x %d\n' % (i, v, f, res, fl & MASK[k], oth))
    return ''.join(out)


# ---- a program per rule -----------------------------------------------------

VT = (0, 1, 2, 3, 7, 100, 127, 128, 181, 255, 256, 1000, 4660, 21845, 32767,
      -1, -2, -3, -128, -129, -182, -1000, -21846, -32767, -32768)


class Out:
    """What a program prints, collected beside its text."""

    def __init__(self):
        self.lines = []

    def p(self, fmt, *vals):
        self.lines.append(fmt % vals)

    def text(self):
        return ''.join(self.lines)


PUSH_HEAD = '''/* pushO.c - made by tests/rules/v30kit.py: V30-PUSH, calls with constants. */
int g1 = 11, g2 = -7, ga[6] = { 1, 2, 3, 5, 8, 13 };
h1(a)
{
	return a;
}
h2(a, b)
{
	return a - b;
}
h3(a, b, c)
{
	return a + (b ^ c);
}
h4(a, b, c, d)
{
	return (a - b) ^ (c + d);
}
hp(p, i)
int *p;
{
	return p[i];
}
hs(s, i)
char *s;
{
	return s[i];
}
hr(n, k)
{
	if (n <= 0)
		return k;
	return hr(n - 1, k + 3);
}
'''
PUSH_CONSTS = (0, 1, -1, 2, 5, 7, 100, 126, 127, 128, 129, -127, -128, -129, 255, 256,
               300, 1000, -1000, 4660, 32767, -32767)
PUSH_GA = (1, 2, 3, 5, 8, 13)
PUSH_STRS = ('abc', 'MUTOS 1700', 'x')


class Pushes:
    """Calls with constants of every kind - bytes, words, characters,
    addresses, strings -, with variables and with calls as arguments, as
    values, in conditions and in loops. Every function is pure, so the
    value of a call is computed beside its text."""

    def __init__(self, rnd):
        self.rnd = rnd
        self.env = {}

    def const(self):
        r = self.rnd
        k = r.random()
        if k < 0.7:
            n = r.choice(PUSH_CONSTS)
        elif k < 0.8:
            c = r.choice('aZ0~ ')
            return "'%s'" % c, ord(c)
        else:
            n = r.randint(-200, 200)
        return ('(%d)' % n if n < 0 else str(n)), n

    def arg(self, depth):
        r = self.rnd.random()
        if r < 0.6:
            return self.const()
        if r < 0.8 or depth > 1:
            name = self.rnd.choice(sorted(self.env))
            return name, self.env[name]
        return self.call(depth + 1)

    def call(self, depth=0):
        r = self.rnd
        k = r.random()
        if k < 0.15:
            a, va = self.arg(depth)
            return 'h1(%s)' % a, va
        if k < 0.4:
            (a, va), (b, vb) = self.arg(depth), self.arg(depth)
            return 'h2(%s, %s)' % (a, b), s16(va - vb)
        if k < 0.6:
            (a, va), (b, vb), (c, vc) = self.arg(depth), self.arg(depth), self.arg(depth)
            return 'h3(%s, %s, %s)' % (a, b, c), s16(va + (vb ^ vc))
        if k < 0.75:
            a, b, c, d = (self.arg(depth) for _ in range(4))
            return ('h4(%s, %s, %s, %s)' % (a[0], b[0], c[0], d[0]),
                    s16(s16(a[1] - b[1]) ^ s16(c[1] + d[1])))
        if k < 0.85:
            i = r.randint(0, 5)
            j = r.randint(0, 5 - i)
            kind = r.random()
            if kind < 0.3 and i == 0:
                return 'hp(ga, %d)' % j, PUSH_GA[j]
            if kind < 0.45:
                return 'hp(&g1, 0)', self.env['g1']
            return 'hp(&ga[%d], %d)' % (i, j), PUSH_GA[i + j]
        if k < 0.93:
            s = r.choice(PUSH_STRS)
            i = r.randint(0, len(s))
            return 'hs("%s", %d)' % (s, i), (ord(s[i]) if i < len(s) else 0)
        n = r.randint(0, 4)
        a, va = self.arg(depth)
        return 'hr(%d, %s)' % (n, a), s16(va + 3 * n)

    def function(self, k, out):
        r = self.rnd
        self.env = {'x': 3 + k, 'y': -2 - k, 'g1': 11, 'g2': -7}
        t = ['t%d()\n{\n\tint x, y, i, s;\n\n\tx = %d;\n\ty = %d;\n' % (k, 3 + k, -2 - k)]
        for _ in range(r.randint(14, 22)):
            kind = r.random()
            if kind < 0.5:
                c, v = self.call()
                t.append('\tprintf("%%d\\n", %s);\n' % c)
                out.p('%d\n', v)
            elif kind < 0.7:
                (c, v), (n, vn) = self.call(), self.const()
                rel = r.choice(['>', '<', '==', '!=', '<=', '>='])
                (a, va), (b, vb) = self.call(), self.call()
                t.append('\tif (%s %s %s)\n\t\tprintf("%%d\\n", %s);\n\telse\n'
                         '\t\tprintf("%%d\\n", %s);\n' % (c, rel, n, a, b))
                yes = {'>': v > vn, '<': v < vn, '==': v == vn, '!=': v != vn,
                       '<=': v <= vn, '>=': v >= vn}[rel]
                out.p('%d\n', va if yes else vb)
            elif kind < 0.85:
                n = r.randint(2, 5)
                (c, vc) = self.const()
                t.append('\ts = 0;\n\tfor (i = 0; i < %d; i++)\n\t\ts = s + h3(i, %s, s);\n'
                         '\tprintf("%%d\\n", s);\n' % (n, c))
                s = 0
                for i in range(n):
                    s = s16(s + s16(i + (vc ^ s)))
                out.p('%d\n', s)
            else:
                name = r.choice(['x', 'y'])
                c, v = self.call()
                t.append('\t%s = %s;\n' % (name, c))
                self.env[name] = v
        t.append('}\n')
        return ''.join(t)

    def program(self, nfun=12):
        out = Out()
        funs = [self.function(k, out) for k in range(nfun)]
        main = 'main()\n{\n' + ''.join('\tt%d();\n' % k for k in range(nfun)) + \
            '\treturn 0;\n}\n'
        return PUSH_HEAD + ''.join(funs) + main, out.text()


SHIFT_COUNTS = (1, 2) + COUNTS      # 1 and 2 are not V30-SHIFT's: the borders


def shift_program():
    """Every count from 3 to 15 (and 1 and 2, which the rule leaves): of an
    int, an unsigned, a char, register variables, as values, in place, as
    conditions and under an index."""
    t = ['/* shift.c - made by tests/rules/v30kit.py: V30-SHIFT, every count. */\n'
         'int vt[%d] = { %s };\nint ga[16] = { %s };\n'
         % (len(VT), ', '.join(str(v) for v in VT),
            ', '.join(str(3 * i + 1) for i in range(16)))]
    for n in SHIFT_COUNTS:
        m = 16 - n
        t.append('''s%(n)d(x)
{
	register int r;
	register unsigned q;
	unsigned u;
	char c;
	int y;

	u = x;
	c = x;
	printf("%%d %%d %%u %%u\\n", x << %(n)d, x >> %(n)d, u >> %(n)d, u << %(n)d);
	printf("%%d %%d\\n", c << %(n)d, c >> %(n)d);
	r = x;
	r <<= %(n)d;
	printf("%%d\\n", r);
	r = x;
	r >>= %(n)d;
	printf("%%d\\n", r);
	q = x;
	q >>= %(n)d;
	printf("%%u\\n", q);
	q = x;
	q <<= %(n)d;
	printf("%%u\\n", q);
	y = 0;
	if (x << %(n)d)
		y = y + 1;
	if (u >> %(n)d)
		y = y + 2;
	if (x >> %(n)d)
		y = y + 4;
	printf("%%d\\n", y);
	printf("%%d %%d\\n", ga[(u >> %(n)d) & 15], ga[(x << %(n)d) & 15]);
	printf("%%u\\n", (u << %(n)d) | (u >> %(m)d));
}
''' % {'n': n, 'm': m})
    t.append('main()\n{\n\tint i;\n\n\tfor (i = 0; i < %d; i++) {\n' % len(VT) +
             ''.join('\t\ts%d(vt[i]);\n' % n for n in SHIFT_COUNTS) +
             '\t}\n\treturn 0;\n}\n')
    out = Out()
    ga = [3 * i + 1 for i in range(16)]
    for x in VT:
        u = u16(x)
        c = s16(x) & 0xff
        c = c - 256 if c & 0x80 else c
        for n in SHIFT_COUNTS:
            out.p('%d %d %d %d\n', s16(x << n), x >> n, u >> n, u16(u << n))
            out.p('%d %d\n', s16(c << n), c >> n)
            out.p('%d\n', s16(x << n))
            out.p('%d\n', x >> n)
            out.p('%d\n', u >> n)
            out.p('%d\n', u16(u << n))
            out.p('%d\n', (1 if u16(x << n) else 0) + (2 if u >> n else 0) +
                  (4 if x >> n else 0))
            out.p('%d %d\n', ga[(u >> n) & 15], ga[u16(x << n) & 15])
            out.p('%d\n', u16(u << n) | (u >> (16 - n)))
    return ''.join(t), out.text()


# 2 is a shift, and 0 and 1 are folded: the borders of V30-IMUL
IMUL_CONSTS = (3, 5, 6, 7, 10, 12, 30, 100, 126, 127, 129, 142, 255, 257, 300, 1000,
               12345, 32767, -3, -7, -100, -127, -128, -129, -1000, -32767, 2)


def imul_program():
    """Products by constants of every kind: of an int, an unsigned, a char,
    a register variable, in place (a register, a file-scope int, through a
    pointer, an element), tested, compared, and under the index of arrays
    of structures."""
    t = ['/* imul.c - made by tests/rules/v30kit.py: V30-IMUL, constants of every kind. */\n'
         'int vt[%d] = { %s };\nint g, ga[8];\n'
         'struct s { int a, b, v; } st[4];\nstruct t { int w[71]; } tt[3];\n'
         % (len(VT), ', '.join(str(v) for v in VT))]
    for j, k in enumerate(IMUL_CONSTS):
        ks = '(%d)' % k if k < 0 else str(k)
        if k == -32768:
            ks = '(-32767 - 1)'
        t.append('''m%(j)d(x)
{
	register int r;
	unsigned u;
	char c;
	int *p, y;

	u = x;
	c = x;
	printf("%%d %%u %%d %%d\\n", x * %(k)s, u * %(k)s, c * %(k)s, %(k)s * x);
	r = x;
	r *= %(k)s;
	printf("%%d\\n", r);
	g = x;
	g *= %(k)s;
	printf("%%d\\n", g);
	p = &ga[3];
	*p = x;
	*p *= %(k)s;
	printf("%%d\\n", ga[3]);
	ga[x & 7] = x;
	ga[x & 7] *= %(k)s;
	printf("%%d\\n", ga[x & 7]);
	y = 0;
	if (x * %(k)s)
		y = y + 1;
	if (x * %(k)s > 100)
		y = y + 2;
	if (x * %(k)s == %(k)s)
		y = y + 4;
	printf("%%d\\n", y);
	printf("%%d\\n", x * %(k)s + (x >> 2) * %(k)s - %(k)s);
}
''' % {'j': j, 'k': ks})
    t.append('main()\n{\n\tint i, j;\n\n'
             '\tfor (i = 0; i < 4; i++)\n\t\tst[i].v = 100 + i;\n'
             '\tfor (i = 0; i < 3; i++)\n\t\ttt[i].w[2] = 200 + i;\n'
             '\tfor (i = 0; i < %d; i++) {\n' % len(VT) +
             ''.join('\t\tm%d(vt[i]);\n' % j for j in range(len(IMUL_CONSTS))) +
             '\t\tj = vt[i] & 3;\n\t\tprintf("%d %d\\n", st[j].v, tt[j % 3].w[2]);\n'
             '\t}\n\treturn 0;\n}\n')
    out = Out()
    for x in VT:
        u = u16(x)
        c = u & 0xff
        c = c - 256 if c & 0x80 else c
        for k in IMUL_CONSTS:
            xk = s16(x * k)
            out.p('%d %d %d %d\n', xk, u16(u * k), s16(c * k), xk)
            for _ in range(4):
                out.p('%d\n', xk)
            out.p('%d\n', (1 if xk else 0) + (2 if xk > 100 else 0) + (4 if xk == k else 0))
            out.p('%d\n', s16(xk + s16((x >> 2) * k) - k))
        j = x & 3
        out.p('%d %d\n', 100 + j, 200 + j % 3)
    return ''.join(t), out.text()


# ---- the timing programs ----------------------------------------------------

BENCH = {
    'bpush': '''int g;
f3(a, b, c)
{
	return a + b + c;
}
f2(a, b)
{
	return a ^ b;
}
work(n)
{
	register int i, s;

	do {
		s = 0;
		for (i = 0; i < 1000; i++) {
			s += f3(i, 7, 300);
			s ^= f2(1000, i);
			s += f3(-5, 100, i);
			s ^= f2(s, 12345);
		}
	} while (--n);
	return s;
}
''',
    'bshift': '''work(n)
{
	register int i, s;
	register unsigned u;

	do {
		s = 0;
		for (i = 0; i < 1000; i++) {
			u = i;
			s += i << 5;
			s ^= u >> 3;
			s += (i << 12) + (s >> 4);
			u <<= 7;
			s ^= u >> 9;
			s += (s << 3) ^ (i >> 6);
		}
	} while (--n);
	return s;
}
''',
    'bimul': '''work(n)
{
	register int i, s;

	do {
		s = 0;
		for (i = 0; i < 1000; i++) {
			s += i * 10;
			s ^= i * 300;
			s += (s >> 1) * 7;
			s ^= s * 100 + i * 1000;
		}
	} while (--n);
	return s;
}
''',
}
BENCH_MAIN = '''#include <stdio.h>
long time();
main()
{
	long t0, t1;
	int s;

	time(&t0);
	s = work(%d);
	time(&t1);
	printf("%%d\\n", s);
	fprintf(stderr, "%%d s\\n", (int)(t1 - t0));
	return 0;
}
'''

# ---- building and running ---------------------------------------------------

PROGRAMS = (                     # name, the rule, cc's options besides -1
    ('pushO', 'V30-PUSH', ['-O']),
    ('shift', 'V30-SHIFT', []),
    ('shiftO', 'V30-SHIFT', ['-O']),
    ('imul', 'V30-IMUL', []),
    ('imulO', 'V30-IMUL', ['-O']),
)
WORD = {'V30-PUSH': 'push', 'V30-SHIFT': 'shift', 'V30-IMUL': 'imul'}
REPORT = re.compile(r'^as: -1: (?:no|\d+) 80186/V30 instructions?(?:: (.*))?$')
# the 80186/V30 instructions cc -S -1 writes, by their text, and the forms
# among them the kit probes: scripts/v30report.py's, which checks a kernel
# build with -1 against them (its --forms)
V30_LINE = v30report.V30_LINE
PROBED = v30report.PROBED


def env():
    e = dict(os.environ, MUTOS_ROOT=MUTOS_ROOT)
    for k in ('MUTOS_MODE', 'MUTOS_RULELOG'):
        e.pop(k, None)
    return e


def run(argv, cwd):
    r = subprocess.run(argv, cwd=cwd, env=env(), capture_output=True)
    return r.returncode, (r.stdout + r.stderr).decode('latin-1')


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def write(path, text):
    with open(path, 'w') as f:
        f.write(text)


def emulate(image, cwd):
    """(exit status, stdout, stderr) of an image under the emulator, or
    (None, why, b'')."""
    o, e = io.BytesIO(), io.BytesIO()
    try:
        emu = mutosemu.Emu(image, ['a.out'], cwd=cwd, stdin=io.BytesIO(b''),
                           stdout=o, stderr=e, eof_limit=1000, now=500000000)
        return emu.run(MAX_INSNS), o.getvalue().decode('latin-1'), e.getvalue().decode('latin-1')
    except RuntimeError as x:
        return None, str(x), ''


def counts(msgs):
    """{kind: n} of the lines "as: -1: ..." among cc's messages, summed;
    None if there is another message."""
    total = {}
    for ln in msgs.splitlines():
        m = REPORT.match(ln)
        if m is None:
            return None
        for part in (m.group(1) or '').split(', '):
            if part:
                w, n = part.split()
                total[w] = total.get(w, 0) + int(n)
    return total


unprobed = v30report.unprobed


def symbols(image_path, cwd):
    st, out = run([NM, image_path], cwd)
    syms = {}
    for ln in out.splitlines():
        f = ln.split()
        if len(f) == 3:
            syms[f[2]] = int(f[0], 16)
    return syms


def build_probe(work, bad):
    ps = probes()
    mods = probe_s(ps)
    write(os.path.join(work, 'probe.c'), probe_c())
    st, msgs = run([CC, '-c', 'probe.c'], work)
    if st or msgs:
        bad.append('probe.c: cc -c: exit %d %s' % (st, msgs))
        return None
    want = {k: sum(1 for p in ps if p['kind'] == k) for k in KINDS}
    got, objs = {}, []
    for name in sorted(mods):
        write(os.path.join(work, name), mods[name])
        obj = name[:-2] + '.o'
        st, msgs = run([AS, '-1', '-o', obj, name], work)
        n = counts(msgs)
        if st or n is None:
            bad.append('%s: as -1: exit %d, %r' % (name, st, msgs))
            return None
        for w, c in n.items():
            got[w] = got.get(w, 0) + c
        st, msgs = run([AS, '-o', 'no1.o', name], work)
        if st == 0:
            bad.append('%s: as without -1 takes it' % name)
        objs.append(obj)
    if got != want:
        bad.append('the probes: as -1 reports %r, not %r' % (got, want))
    st, msgs = run([CC, '-o', 'probe.v', 'probe.o'] + objs, work)
    if st or msgs:
        bad.append('probe.v: cc -o: exit %d %s' % (st, msgs))
        return None
    image = read(os.path.join(work, 'probe.v'))
    model = probe_model(ps, symbols('probe.v', work))
    st, out, err = emulate(image, work)
    if st != 0 or out != model:
        bad.append('probe.v under the emulator: exit %r; its output is %s the model\'s%s'
                   % (st, 'not' if out != model else '', first_diff(out, model)))
    lines = err.splitlines()
    twins = {k: sum(2 * (len(VALS) if k != 'push' else 1) for p in ps
                    if p['kind'] == k and p['twin']) for k in KINDS}
    for k, ln in zip(KINDS, lines + [''] * 3):
        m = re.match(r'%s: (\d+) twins, result differs (\d+), flag differs: CY (\d+) P (\d+) '
                     r'AC (\d+) Z (\d+) S (\d+) V (\d+)$' % k, ln)
        if not m or int(m.group(1)) != twins[k] or int(m.group(2)):
            bad.append('probe.v: the twins of %s: %r' % (k, ln))
            continue
        d = dict(zip(('CY', 'P', 'AC', 'Z', 'S', 'V'), (int(x) for x in m.groups()[2:])))
        defined = {'push': d, 'shift': {f: d[f] for f in ('CY', 'P', 'Z', 'S')},
                   'imul': {f: d[f] for f in ('CY', 'V')}}[k]
        if any(defined.values()):
            bad.append('probe.v: a twin of %s differs in a defined flag: %r' % (k, ln))
    source = dict(mods)
    source['probe.c'] = probe_c()
    return dict(image=image, out=model, err=err, n=len(ps), lines=model.count('\n'),
                want=want, source=source)


def first_diff(got, want):
    g, w = got.splitlines(), want.splitlines()
    for i in range(max(len(g), len(w))):
        a = g[i] if i < len(g) else '(none)'
        b = w[i] if i < len(w) else '(none)'
        if a != b:
            return ' (line %d: %r, the model %r)' % (i + 1, a, b)
    return ''


def build_program(work, name, rule, opts, src, model, bad):
    d = os.path.join(work, name)
    os.makedirs(d)
    write(os.path.join(d, name + '.c'), src)
    res = {'source': src, 'out': model, 'lines': model.count('\n')}
    for tag, one in (('8', []), ('v', ['-1'])):
        st, msgs = run([CC] + opts + one + ['-o', '%s.%s' % (name, tag), name + '.c'], d)
        n = counts(msgs)
        if st or n is None or (tag == '8' and msgs):
            bad.append('%s.%s: cc: exit %d %s' % (name, tag, st, msgs))
            return None
        if tag == 'v':
            if not n.get(WORD[rule]):
                bad.append('%s.v: no %s among %r' % (name, WORD[rule], n))
            res['n'] = n
            st, msgs = run([CC] + opts + ['-1', '-S', name + '.c'], d)
            text = read(os.path.join(d, name + '.s')).decode('latin-1')
            for ln in unprobed(text):
                bad.append('%s.v: an 80186/V30 instruction that is no probed form: %s'
                           % (name, ln))
        image = read(os.path.join(d, '%s.%s' % (name, tag)))
        st, out, err = emulate(image, d)
        if st != 0 or err or out != model:
            bad.append('%s.%s under the emulator: exit %r %r; its output is %s the model\'s%s'
                       % (name, tag, st, err, 'not' if out != model else '',
                          first_diff(out, model)))
        res[tag] = image
    return res


def clocks(work, name, src, v30):
    """The V30 clocks (lowest, highest) of one turn of work()'s outer
    loop: x86sim.py on "cc -O -S [-1]", work(2) less work(1)."""
    got = []
    for n in (1, 2):
        d = os.path.join(work, '%s.sim%d%d' % (name, n, v30))
        os.makedirs(d)
        write(os.path.join(d, 'p.c'), src + 'main()\n{\n\treturn work(%d);\n}\n' % n)
        st, msgs = run([CC, '-O', '-S'] + (['-1'] if v30 else []) + ['p.c'], d)
        if st:
            return 'cc: %s' % msgs
        try:
            r = x86sim.run(read(os.path.join(d, 'p.s')).decode('latin-1'), v30=bool(v30))
        except x86sim.SimError as e:
            return 'x86sim: %s' % e
        got.append((r.clocks, r.ret))
    (c1, r1), (c2, r2) = got
    return (c2[0] - c1[0], c2[1] - c1[1]), s16(r1)


def build_bench(work, name, seconds, bad):
    src = BENCH[name]
    c8 = clocks(work, name, src, 0)
    cv = clocks(work, name, src, 1)
    if isinstance(c8, str) or isinstance(cv, str) or c8[1] != cv[1]:
        bad.append('%s: x86sim.py: %r, with -1 %r' % (name, c8, cv))
        return None
    (lo8, hi8), sum_ = c8
    (lov, hiv), _ = cv
    rep = max(1, min(32767, round(seconds * HZ / ((lo8 + hi8) / 2))))
    d = os.path.join(work, name)
    os.makedirs(d)
    full = src + BENCH_MAIN % rep
    write(os.path.join(d, name + '.c'), full)
    res = {'source': full, 'out': '%d\n' % sum_, 'rep': rep,
           'clocks8': (lo8 * rep, hi8 * rep), 'clocksv': (lov * rep, hiv * rep)}
    for tag, one in (('8', []), ('v', ['-1'])):
        st, msgs = run([CC, '-O'] + one + ['-o', '%s.%s' % (name, tag), name + '.c'], d)
        n = counts(msgs)
        if st or n is None or (tag == '8' and msgs) or (tag == 'v' and not n):
            bad.append('%s.%s: cc: exit %d %s' % (name, tag, st, msgs))
            return None
        if tag == 'v':
            res['n'] = n
            run([CC, '-O', '-1', '-S', name + '.c'], d)
            for ln in unprobed(read(os.path.join(d, name + '.s')).decode('latin-1')):
                bad.append('%s.v: an 80186/V30 instruction that is no probed form: %s'
                           % (name, ln))
        image = read(os.path.join(d, '%s.%s' % (name, tag)))
        st, out, err = emulate(image, d)
        if st != 0 or out != res['out'] or err != '0 s\n':
            bad.append('%s.%s under the emulator: exit %r, %r %r, not %r'
                       % (name, tag, st, out, err, res['out']))
        res[tag] = image
    return res


def kernel_forms(work, bad):
    """The kernel sources of the tree with cc -O -S -1: how many 80186/V30
    instructions, and that each is a probed form."""
    import check_accounting
    total, nsrc = {}, 0
    for i, u in enumerate(check_accounting.corpus_units()):
        if u.program:
            continue
        d = os.path.join(work, 'kernel%d' % i)
        for rel, data in u.files.items():
            os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
            with open(os.path.join(d, rel), 'wb') as f:
                f.write(data)
        sd = os.path.dirname(os.path.join(d, u.src))
        base = os.path.basename(u.src)
        st, msgs = run([CC, '-O', '-1', '-S', base], sd)
        s = os.path.join(sd, base[:-2] + '.s')
        if st or not os.path.exists(s):
            continue                      # a source the compiler refuses
        nsrc += 1
        text = read(s).decode('latin-1')
        for m in V30_LINE.finditer(text):
            w = 'push' if m.group(1).startswith('push') else \
                'imul' if m.group(1).startswith('imul') else 'shift'
            total[w] = total.get(w, 0) + 1
        for ln in unprobed(text):
            bad.append('%s: an 80186/V30 instruction that is no probed form: %s'
                       % (u.name, ln))
    return nsrc, total


# ---- the kit ----------------------------------------------------------------

SCRIPT = '''\
: v30check.sh - step V4 of the cross toolchain, the check of the 80186
: and V30 code of the switch -1 on the hardware. It needs the NEC V30.
: probe.v runs every form the rules of -1 write. Each program N prints
: its values twice, as 8086 code N.8 and as V30 code N.v. All of it is
: compared with expect, which must hold its files. Then three timing
: programs run, each as 8086 code and as V30 code, under time. Nothing
: outside this directory is changed. Run it here with sh v30check.sh,
: see README.
chmod 755 probe.v %(images)s
bad=
echo probe.v
./probe.v >probe.out 2>probe.tw
echo probe.v: exit status $?
if cmp probe.out expect/probe.out
then :
else
	echo probe.out: DIFFERENT from expect/probe.out
	bad=yes
fi
if cmp probe.tw expect/probe.tw
then
	echo probe.tw: as expected
else
	echo probe.tw: not as under the emulator, it is
	cat probe.tw
fi
for n in %(programs)s
do
	for x in 8 v
	do
		./$n.$x >$n.o$x
		echo $n.$x: exit status $?
		if cmp $n.o$x expect/$n.out
		then :
		else
			echo $n.o$x: DIFFERENT from expect/$n.out
			bad=yes
		fi
	done
done
for n in %(benches)s
do
	for x in 8 v
	do
		echo $n.$x takes
		time ./$n.$x >$n.o$x
		if cmp $n.o$x expect/$n.out
		then :
		else
			echo $n.o$x: DIFFERENT from expect/$n.out
			bad=yes
		fi
	done
done
case $bad in
yes)	echo values: DIFFERENT, see above ;;
*)	echo values: every output as expected ;;
esac
'''

README = '''\
The hardware check of step V4 of the MUTOS 1700 cross toolchain
===============================================================

Made by tests/rules/v30kit.py. The 80186/V30 code that the switch -1
of the cross toolchain makes (cc -1: push of a constant, a shift by a
constant, imul by a constant), run on the hardware.

THE PROGRAMS *.v AND probe.v NEED THE NEC V30 (or an 80186): on an 8086
they do not run. Nothing outside this directory is changed.

On MUTOS 1700, in this directory (with its subdirectory expect/ and
its files - unpack the tar on MUTOS, or copy the subdirectories with
their files: with expect/ empty every comparison fails, and the log
says "cmp: cannot open"):

    sh v30check.sh >log 2>&1
    cat log

It takes some minutes: probe.v prints %(probe_lines)d lines, and the six
timing runs take about %(bench_total)d seconds together by their clocks
(more on the machine). The kit and what the programs write need about
1.5 MB. src/ has the sources, for reading; the script does not use it.

1. probe.v - every form, with edge values

%(probe_n)d routines, one for each form the rules write: push of a byte
(*n.), of a word and of an address (#n) - %(n_push)d; sal, sar and shr of
ax, bx, cx, dx, si and di by each count from 3 to 15 - %(n_shift)d;
imul ax,*n. and imul ax,#n - %(n_imul)d. Each is called with %(nv)d values
(a push once) and with the flags CY, P, AC, Z, S, V all clear and all
set before. A line of probe.out:

    routine  value(hex)  flags-set(0/1)  result(hex)  flags(hex)  others

flags: those the NEC manual defines for the form (push: all six, as
before; a shift: CY, P, Z, S; imul: CY, V). others: how many of the
other registers changed (0; CX is not loaded, DX not written) - for a
push the step of the stack pointer (2). expect/probe.out is computed
from the forms' definitions; the emulator prints the same.

probe.tw (the standard error) compares each routine with its 8086 twin
(mov r,#n / push r; mov cx,*n. / sal r,cl; mov cx,n / imul cx), all six
flags, defined or not. Under the emulator:

%(twins)s

A result that differs, or a defined flag, would be a fault. Another
number for a flag the form leaves undefined (AC and V of a shift; P,
AC, Z, S of imul) is no fault, only something to bring back: it is
how the V30 differs from the emulator. The compiler's code reads none
of them - except through the original's bug C1-15, a signed
comparison of a shift with 0, which branches on V.

2. A program per rule

    program  rule       built with   80186/V30 instructions in N.v    lines
%(programs)s

N.8 is the program as 8086 code (cc), N.v the same source with -1.
Both must print expect/N.out, which is computed in Python from the
source's arithmetic (and which both print under the emulator).

3. The timing programs

Each runs a loop of its rule's instructions and prints a sum (compared)
and the seconds it took, by time(), on the standard error; the script
runs it under time, whose user time - in tenths of a second - is the
one to compare (in the log, after "N.8 takes" and "N.v takes"). By the
V30's clocks, counted by x86sim.py from the NEC manual's figures (at
%(mhz).4f MHz, the A7100's clock, the fastest and the slowest case, memory
without wait states; a machine with wait states takes longer, and gains
less where the code waits for the bus - tests/rules/v30bus.py models
that):

    program  turns   as 8086 code      as V30 code       gain
%(benches)s

What the log must end with:

    values: every output as expected

and before it "probe.tw: as expected" (or the three lines of the V30's
own probe.tw). If anything differs: bring back the log, probe.out,
probe.tw and the N.o8 and N.ov the script left.
'''


def secs(c):
    return '%.0f-%.0f s' % (c[0] / HZ, c[1] / HZ)


def write_kit(out, probe, progs, benches):
    os.makedirs(os.path.join(out, 'expect'))
    os.makedirs(os.path.join(out, 'src'))
    with open(os.path.join(out, 'probe.v'), 'wb') as f:
        f.write(probe['image'])
    write(os.path.join(out, 'expect', 'probe.out'), probe['out'])
    write(os.path.join(out, 'expect', 'probe.tw'), probe['err'])
    for name, text in probe['source'].items():
        write(os.path.join(out, 'src', name), text)
    images = []
    for name, res in list(progs.items()) + list(benches.items()):
        for tag in ('8', 'v'):
            with open(os.path.join(out, '%s.%s' % (name, tag)), 'wb') as f:
                f.write(res[tag])
            images.append('%s.%s' % (name, tag))
        write(os.path.join(out, 'expect', name + '.out'), res['out'])
        write(os.path.join(out, 'src', name + '.c'), res['source'])
    for p in [os.path.join(out, 'probe.v')] + [os.path.join(out, i) for i in images]:
        os.chmod(p, 0o755)
    write(os.path.join(out, 'v30check.sh'),
          SCRIPT % {'images': ' '.join(images), 'programs': ' '.join(progs),
                    'benches': ' '.join(benches)})
    prows = []
    for (name, rule, opts) in PROGRAMS:
        r = progs[name]
        n = ', '.join('%s %d' % (w, r['n'][w]) for w in KINDS if r['n'].get(w))
        prows.append('    %-8s %-10s %-12s %-32s %d' % (
            name, rule, 'cc ' + ' '.join(opts + ['-1']), n, r['lines']))
    brows, total = [], 0
    for name, r in benches.items():
        a, b = r['clocks8'], r['clocksv']
        gain = 100.0 * (1 - (b[0] + b[1]) / (a[0] + a[1]))
        total += (a[0] + a[1] + b[0] + b[1]) / 2 / HZ
        brows.append('    %-8s %-7d %-17s %-17s %.1f%%' % (name, r['rep'], secs(a), secs(b),
                                                           gain))
    write(os.path.join(out, 'README'), README % {
        'probe_lines': probe['lines'], 'probe_n': probe['n'], 'nv': len(VALS),
        'n_push': probe['want']['push'], 'n_shift': probe['want']['shift'],
        'n_imul': probe['want']['imul'],
        'twins': ''.join('    ' + ln + '\n' for ln in probe['err'].splitlines()).rstrip('\n'),
        'programs': '\n'.join(prows), 'benches': '\n'.join(brows), 'mhz': HZ / 1e6,
        'bench_total': total})
    return prows, brows


def write_tar(out, tarname):
    """Regular files only, under the directory's name, no directory
    entries (v7's tar makes them); the programs executable."""
    base = os.path.basename(os.path.normpath(out))
    paths = []
    for d, _subs, fs in os.walk(out):
        for f in fs:
            paths.append(os.path.relpath(os.path.join(d, f), out))
    with tarfile.open(tarname, 'w', format=tarfile.USTAR_FORMAT) as tar:
        for rel in sorted(paths):
            info = tar.gettarinfo(os.path.join(out, rel), arcname=base + '/' + rel)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o755 if rel.endswith(('.8', '.v')) else 0o644
            with open(os.path.join(out, rel), 'rb') as f:
                tar.addfile(info, f)


def main(argv):
    out, tarname, seconds, check = 'v30kit', None, 40, False
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--out' and args:
            out = args.pop(0)
        elif a == '--tar' and args:
            tarname = args.pop(0)
        elif a == '--seconds' and args:
            seconds = int(args.pop(0))
        elif a == '--check':
            check = True
        else:
            print(__doc__)
            return 2
    for tool in (CC, AS, NM):
        if not os.access(tool, os.X_OK):
            sys.exit('v30kit.py: %s is not built - run "make" first' % tool)
    if not check and os.path.exists(out) and (not os.path.isdir(out) or os.listdir(out)):
        sys.exit('v30kit.py: %s exists and is not an empty directory' % out)
    bad, progs, benches = [], {}, {}
    sources = {'pushO': Pushes(random.Random('v30kit pushes')).program()}
    sources['shift'] = sources['shiftO'] = shift_program()
    sources['imul'] = sources['imulO'] = imul_program()
    with tempfile.TemporaryDirectory(prefix='v30kit.') as work:
        probe = build_probe(work, bad)
        for name, rule, opts in PROGRAMS:
            src, model = sources[name]
            r = build_program(work, name, rule, opts, src, model, bad)
            if r:
                progs[name] = r
        for name in BENCH:
            r = build_bench(work, name, seconds, bad)
            if r:
                benches[name] = r
        nsrc, kforms = kernel_forms(work, bad)
    if bad or probe is None or len(progs) != len(PROGRAMS) or len(benches) != len(BENCH):
        for b in bad:
            print('  WRONG ' + b)
        print('v30kit: the prediction does not hold - no kit written')
        return 1
    if check:
        with tempfile.TemporaryDirectory(prefix='v30kit.') as scratch:
            prows, brows = write_kit(os.path.join(scratch, 'kit'), probe, progs, benches)
    else:
        prows, brows = write_kit(out, probe, progs, benches)
        if tarname:
            write_tar(out, tarname)
    print('v30kit: probe.v: %d routines (push %d, shift %d, imul %d), %d lines - the '
          'emulator prints what the forms\' definitions give; no twin differs in a result '
          'or a defined flag' % (probe['n'], probe['want']['push'], probe['want']['shift'],
                                 probe['want']['imul'], probe['lines']))
    for ln in probe['err'].splitlines():
        print('    ' + ln)
    print('v30kit: %d programs, as 8086 code and as V30 code: the emulator prints what '
          'the sources\' arithmetic gives' % len(progs))
    print('\n'.join(prows))
    print('v30kit: %d timing programs (V30 clocks at %.4f MHz)' % (len(benches), HZ / 1e6))
    print('\n'.join(brows))
    print('v30kit: the %d kernel sources of the tree with cc -O -S -1: %s - each a probed '
          'form' % (nsrc, ', '.join('%s %d' % (w, kforms.get(w, 0)) for w in KINDS)))
    if not check:
        print('v30kit: the kit is in %s%s' % (out, ' and %s' % tarname if tarname else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
