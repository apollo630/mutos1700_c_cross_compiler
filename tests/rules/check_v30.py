#!/usr/bin/env python3
"""check_v30.py - the 80186/V30 rules of -1 compute what the same program
computes without -1 (docs/RULES.md; docs/TOOLCHAIN_PLAN.md, section 7.2,
"Semantics instead of bytes where no golden can exist", and 7.4).

tests/rules/check_accounting.py shows that every byte -1 changes belongs
to a rule; this script shows that what the rules write is right. The
reference for -1 is the program compiled without it: the V30 computes as
the 8086 does, so both must give the same values. Three parts:

  * Triggers: for each active V30 rule of docs/RULES.md, small programs
    that meet it, compiled by mutos_cc with "-O -S" and with "-O -S -1"
    (c2's code; a rule of c1 with "-S" and "-S -1", c1's code, and with
    -O too), executed by tests/mutos_cc/fuzz/x86sim.py. main()'s value is C's, pinned here,
    either way; with -1 the code has exactly the pinned number of
    80186/V30 instructions of the rule's kind and the rule log as many
    "applied" entries of the rule (those with chunk=1 apart: c2's entry
    for the function the rule was applied in); without -1 there is none, and the log
    has "met" where -1 applies the rule. The -1 code is no 8086 code:
    x86sim.py without v30 stops on it, and mutos_as without -1 refuses
    it. "cc -c -1" reports the pinned number ("as: -1: ...").
    With --realbin the triggers are also linked ("cc -o", with and
    without -1) and run under tests/realbin/mutosemu.py (which needs the
    Python module unicorn): the exit status is C's value modulo 256
    either way.
  * Programs of calls (--calls N, --seed): generated here - functions
    with two or three arguments, called with constants of every kind (a
    byte, a word, an address, 0), with variables and with calls as
    arguments, in conditions, loops and both arms of an if, which is
    what V30-PUSH and the branches around it meet. Each is compiled with
    "-O -S" with and without -1 and executed: main()'s value and every
    file-scope variable must be the same. (Without -1 the program must
    compile and run under x86sim.py at all; a generated program that does
    not is counted, and more than one in twenty fail the check.)
  * Programs of shifts (--shifts N): generated here - shifts by every
    count from 0 to 17 and by a variable, of ints, unsigneds, chars and
    register variables, as values, as assignments (=<<, =>>, in memory
    too), as conditions (the branch on the shift's flags) and under
    array indices, which is what V30-SHIFT meets. Each is compiled with
    "-S" and with "-O -S", with and without -1, and compared as the
    programs of calls are.
  * Programs of products (--products N): generated here - products by
    constants of every kind (a byte, a word, negative ones, powers of
    two) and by variables, of ints, unsigneds, chars and register
    variables, as values, in place (=*), as conditions and under the
    index of an array of structures, with branches over them, which is
    what V30-IMUL meets. Compiled and compared as the programs of shifts.
  * Programs of tests/mutos_cc/fuzz/fuzz_c.py (--fuzz N): expressions,
    arrays, longs - the same comparison, on fuzz_c.py's own judgement of
    the two codes (every variable at every statement against C's
    semantics): with -1 it must be what it is without. Compiled with
    "-O -S"; every other one with "-S" as well.

Besides (lengths()): c2 counts the bytes of the 80186/V30 forms when it
makes a branch short - a branch over ten products by a word stays long
with -1, and c2 without -1 on the same code makes it short, which
mutos_as refuses, as it does not reach.

It reports what -1 gained in the generated programs: the bytes of text
(from the objects of "cc -O -c") and the V30 clocks x86sim.py counted.

Every V30 rule that is active in docs/RULES.md must have a trigger here.

Usage:
    check_v30.py [--cc PATH] [--calls N] [--shifts N] [--products N] [--fuzz N]
                 [--seed S] [--realbin] [--keep DIR] [-v]

Exit status 1 if anything differs. Python 3; --realbin needs unicorn.
"""
import argparse
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
FUZZ = os.path.join(REPO, 'tests', 'mutos_cc', 'fuzz')
sys.path.insert(0, FUZZ)
import x86sim  # noqa: E402
import fuzz_c  # noqa: E402

CC = os.path.join(REPO, 'src', 'mutos_cc', 'mutos_cc')
AS = os.path.join(REPO, 'src', 'mutos_as', 'mutos_as')
C2 = os.path.join(REPO, 'src', 'mutos_cc', 'mutos_c2')
MUTOSEMU = os.path.join(REPO, 'tests', 'realbin', 'mutosemu.py')

# the instructions of a rule's kind in a .s, and the word mutos_as's
# report has for them ("as: -1: 3 80186/V30 instructions: push 3")
KIND = {
    'V30-PUSH': (re.compile(r'^(?:(?:L\d+|_\w+):)*push\t[*#][^(\n]*$', re.M), 'push'),
    # c1 writes the shift by 1 as "*1", without the dot
    'V30-SHIFT': (re.compile(r'^(?:(?:L\d+|_\w+):)*(?:sal|sar|shr)b?\t[^,\n]+,\*\d+\.$',
                             re.M), 'shift'),
    'V30-IMUL': (re.compile(r'^(?:(?:L\d+|_\w+):)*imul\t[^,\n]+,[*#][^(\n]*$', re.M),
                 'imul'),
}
# the options a rule's triggers are compiled with: c2's rules need -O, c1's
# are tried without it (c1's code) and with it (what c2 makes of it)
OPTS = {'V30-PUSH': [['-O']], 'V30-SHIFT': [[], ['-O']], 'V30-IMUL': [[], ['-O']]}
# the tool whose rule it is, and whose entries of the rule log are counted
# (V30-IMUL has a second half in c2, the length toofar() counts: its
# entries are c2's)
TOOL = {'V30-PUSH': 'c2', 'V30-SHIFT': 'c1', 'V30-IMUL': 'c1'}

# A branch over ten statements with a product by a word each: 130 bytes of
# code with -1 ("imul ax,#n" is 4 bytes), which toofar() of c2 takes for
# 110 unless V30-IMUL's half in c2 counts the constants - see lengths().
LONGIF = '''f(n)
{
	int x, y;

	x = 1;
	y = 2;
	if (n) {
%s	}
	return x + y;
}
main()
{
	return f(0) + f(1);
}
''' % ''.join('\t\tx = x * %d + y;\n\t\ty = y * %d + x;\n' % (301 + i, 401 + i)
              for i in range(5))

# (rule, name, source, C's value of main(), the 80186/V30 instructions of
# the rule's kind with -1)
CASES = [
    ('V30-PUSH', 'a byte, a word, a negative byte and 0',
     '''f(a, b, c, d)
{
	return a + b - c + d;
}
main()
{
	return f(5, 300, -3, 0);
}
''', 308, 4),
    ('V30-PUSH', 'the ends of the byte: -128 and 127 are "*n.", -129 and 128 "#n."',
     '''f(a, b, c, d)
{
	return (a + 129) * 1000 + (b - 126) * 100 + (c + 130) * 10 + (d - 127);
}
main()
{
	return f(-128, 127, -129, 128);
}
''', 1111, 4),
    ('V30-PUSH', 'the address of a variable and of an array element',
     '''int g, a[4];
set(p, v)
int *p;
{
	*p = v;
	return 1;
}
main()
{
	return set(&g, 40) + set(&a[2], 2) + g + a[2];
}
''', 44, 4),
    ('V30-PUSH', 'the register read again: one constant for two arguments stays a mov',
     '''f(a, b)
{
	return a * 10 + b;
}
main()
{
	return f(7, 7);
}
''', 77, 0),
    ('V30-PUSH', 'calls in both arms of an if and in a loop: branches over the pushes',
     '''int n;
f(a, b, c)
{
	n = n + a;
	return b - c;
}
main()
{
	int i, s;

	s = 0;
	for (i = 0; i < 5; i++) {
		if (i & 1)
			s = s + f(1, 100, i);
		else
			s = s + f(2, i, 1000);
	}
	return s + n;
}
''', -2790, 4),
    ('V30-PUSH', 'a call as an argument, and a constant after it',
     '''f(a, b)
{
	return a - b;
}
main()
{
	return f(f(1000, 1), f(2, 30000));
}
''', 30997, 4),
    ('V30-PUSH', 'a long constant for a helper (mov, cwd, two pushes): ax is read again',
     '''long l;
main()
{
	l = 70000;
	l = l * 10;
	l = l / 3;
	return (int)(l >> 4);
}
''', 14583, 0),
    ('V30-SHIFT', 'the three shifts of a register: sal, sar of an int, shr of an unsigned',
     '''int a;
unsigned u;
main()
{
	int x;

	a = -1000;
	u = 50000;
	x = 7;
	return (x << 5) + (a >> 3) + (u >> 9);
}
''', 196, 3),
    ('V30-SHIFT', 'the ends: counts of 3 and 15 are shifted by a constant, 2 and 16 are not',
     '''unsigned a;
int b, c, d;
main()
{
	a = 1;
	b = 3;
	c = 5;
	d = 9;
	return ((a << 15) >> 15) + (b << 3) + (c << 2) + (d << 16);
}
''', 45, 3),
    ('V30-SHIFT', 'register variables shifted in place (=<<, =>>), and the value of one',
     '''main()
{
	register int r;
	register unsigned q;
	int x;

	r = 3;
	q = 60000;
	r <<= 4;
	q >>= 3;
	r >>= 3;
	x = (r <<= 3) + 1;
	return r + q + x;
}
''', 7597, 4),
    ('V30-SHIFT', 'a shift in memory is left: a file-scope int, a local, through a pointer',
     '''int g, v, *p;
main()
{
	int l;

	g = 5;
	l = 3;
	v = 1;
	p = &v;
	g <<= 3;
	l <<= 4;
	*p <<= 6;
	return g + l + v;
}
''', 152, 0),
    ('V30-SHIFT', 'branches on the flags of a shift by a constant',
     '''main()
{
	int a, n;

	a = 04000;
	n = 0;
	if (a << 4)
		n += 1;
	if ((a << 5) == 0)
		n += 2;
	if ((a >> 12) == 0)
		n += 4;
	if (a >> 3)
		n += 8;
	return n;
}
''', 15, 4),
    ('V30-SHIFT', 'a char widened and shifted, and a product by 8 under an index',
     '''int t[40];
char c;
main()
{
	int i;

	i = 2;
	t[8 * i] = 7;
	c = 100;
	return t[16] + (c >> 3) + (c << 3);
}
''', 819, 3),
    ('V30-IMUL', 'a byte, a word and a negative constant, an unsigned and a char',
     '''int g;
unsigned u;
char c;
main()
{
	int x;

	x = 7;
	g = 11;
	u = 9000;
	c = 5;
	return x * 30 + g * 300 + x * -5 + u * 7 + c * 3;
}
''', 954, 5),
    ('V30-IMUL', 'the ends of the byte: -128 and 127 are "*n.", -129 and 129 "#n."; 128 is a shift',
     '''int a, b, c, d, e;
main()
{
	a = 3;
	b = -2;
	c = 5;
	d = 1;
	e = 4;
	return a * 127 + b * -128 + c * -129 + d * 129 + e * 128;
}
''', 633, 4),
    ('V30-IMUL', 'products in place (=*): a register variable, a file-scope int, through a pointer',
     '''int g, v, *p, t[4];
main()
{
	register int r;
	int i;

	r = 3;
	g = 4;
	v = 5;
	p = &v;
	i = 2;
	t[2] = 6;
	r *= 6;
	g *= 18;
	*p *= 7;
	t[i] *= 300;
	return r + g + v + t[2];
}
''', 1925, 4),
    ('V30-IMUL', 'left: a product by a variable, by a register variable, of a long, by 16',
     '''long l;
main()
{
	register int r;
	int x, y;

	r = 3;
	x = 5;
	y = 7;
	l = 1000;
	l = l * 70;
	return x * y + x * r + (int)(l / 100) + x * 16;
}
''', 830, 0),
    ('V30-IMUL', 'a product tested and compared, and the size of a structure under an index',
     '''struct s {
	char n[28];
	int v;
} a[4], *p;
main()
{
	int i, n;

	i = 3;
	n = 0;
	p = &a[i];
	p->v = 40;
	if (i * 3)
		n += 1;
	if (i * 10 > 29)
		n += 2;
	if (!(n * 300))
		n += 4;
	while (i * 5 > 5)
		i--;
	return n + a[3].v + i * 1000;
}
''', 1043, 6),
    ('V30-IMUL', 'a branch over ten products by a word: 4 bytes each, which c2 must count',
     LONGIF, 18392, 10),
]


def env():
    return {k: v for k, v in os.environ.items() if k not in ('MUTOS_MODE', 'MUTOS_RULELOG')}


def compile_s(cc, d, src, opts, log=False):
    """(exit status, messages, the .s text, the rule log's lines)."""
    with open(os.path.join(d, 'p.c'), 'w') as f:
        f.write(src)
    s = os.path.join(d, 'p.s')
    lg = os.path.join(d, 'rulelog')
    for p in (s, lg):
        if os.path.exists(p):
            os.unlink(p)
    e = env()
    if log:
        e['MUTOS_RULELOG'] = lg
    r = subprocess.run([cc] + opts + ['-S', 'p.c'], cwd=d, capture_output=True, env=e)
    text = open(s).read() if os.path.exists(s) else ''
    lines = open(lg).read().splitlines() if os.path.exists(lg) else []
    return r.returncode, (r.stdout + r.stderr).decode('latin-1'), text, lines


def textsize(cc, d, opts):
    """The text size of the object "cc <opts> -c p.c" makes, and its
    messages; (None, messages) when it makes none."""
    o = os.path.join(d, 'p.o')
    if os.path.exists(o):
        os.unlink(o)
    r = subprocess.run([cc] + opts + ['-c', 'p.c'], cwd=d, capture_output=True, env=env())
    msgs = (r.stdout + r.stderr).decode('latin-1')
    if r.returncode != 0 or not os.path.exists(o):
        return None, msgs
    with open(o, 'rb') as f:
        hdr = f.read(16)
    return struct.unpack('<H', hdr[2:4])[0], msgs


def simulate(text, v30):
    """(main()'s value, the file-scope variables' bytes, the clocks), or
    the message x86sim.py stops with."""
    try:
        r = x86sim.run(text, v30=v30)
    except x86sim.SimError as e:
        return 'x86sim: %s' % e
    lo = x86sim.DATA0
    hi = max(list(r.data.values()) + [lo]) + 64
    data = tuple(sorted((a, v) for a, v in r._mem.items() if lo <= a < hi and v))
    return r.ret, data, r.clocks


def emulated(cc, d, opts):
    r = subprocess.run([cc] + opts + ['-o', 'p', 'p.c'], cwd=d, capture_output=True, env=env())
    if r.returncode != 0:
        return 'cc: exit status %d: %s' % (r.returncode, r.stderr.decode('latin-1')[-200:])
    r = subprocess.run([sys.executable, MUTOSEMU, '--cwd', d, os.path.join(d, 'p')],
                       capture_output=True)
    return r.returncode


def entries(log, rule, what, tool=None):
    return [ln for ln in log if ln.split()[:3] == [tool or TOOL[rule], rule, what]]


def kinds_of(text):
    """{mutos_as's word: the instructions of that kind in the .s}."""
    return {word: len(kind.findall(text)) for kind, word in KIND.values()}


def report(text):
    """The line mutos_as -1 reports for the .s."""
    n = kinds_of(text)
    total = sum(n.values())
    if not total:
        return 'as: -1: no 80186/V30 instructions'
    return 'as: -1: %d 80186/V30 instruction%s: %s' % (
        total, '' if total == 1 else 's',
        ', '.join('%s %d' % (w, n[w]) for w in ('push', 'shift', 'imul') if n[w]))


def triggers(cc, realbin):
    problems, lines = [], []
    for rule, name, src, want, count in CASES:
        for opts in OPTS[rule]:
            p, ln = trigger(cc, realbin, rule, name, src, want, count, opts)
            problems += p
            lines += ln
    return problems, lines


def trigger(cc, realbin, rule, name, src, want, count, opts):
    problems, lines = [], []
    tag = '%s %s%s' % (rule, name, ' (%s)' % ' '.join(opts) if opts else '')
    kind, word = KIND[rule]
    with tempfile.TemporaryDirectory(prefix='v30') as d:
        st0, m0, s0, log0 = compile_s(cc, d, src, opts, log=True)
        st1, m1, s1, log1 = compile_s(cc, d, src, opts + ['-1'], log=True)
        if st0 or st1:
            return ['%s: cc -S exit status %d, with -1 %d: %s'
                    % (tag, st0, st1, (m0 + m1).strip()[-200:])], lines
        r0, r1 = simulate(s0, False), simulate(s1, True)
        for r, how in ((r0, 'without -1'), (r1, 'with -1')):
            if isinstance(r, str) or r[0] != want:
                problems.append('%s: %s %r, C says %d'
                                % (tag, how, r if isinstance(r, str) else r[0], want))
        if not isinstance(r0, str) and not isinstance(r1, str) and r0[1] != r1[1]:
            problems.append('%s: the file-scope variables differ with -1' % tag)
        n0, n1 = len(kind.findall(s0)), len(kind.findall(s1))
        if n0:
            problems.append('%s: %d 80186/V30 instruction(s) without -1' % (tag, n0))
        if n1 != count:
            problems.append('%s: %d 80186/V30 instruction(s) with -1, pinned %d'
                            % (tag, n1, count))
        made = [ln for ln in entries(log1, rule, 'applied') if 'chunk=1' not in ln]
        if len(made) != count:
            problems.append('%s: the rule log has %d applied entries, pinned %d'
                            % (tag, len(made), count))
        if entries(log0, rule, 'applied'):
            problems.append('%s: an applied entry without -1' % tag)
        if len(entries(log0, rule, 'met')) != count:
            problems.append('%s: the rule log has %d met entries without -1, -1 '
                            'applies %d' % (tag, len(entries(log0, rule, 'met')), count))
        if TOOL[rule] == 'c2':
            # a rule of c2 leaves c1's code alone: what -1 changes there is
            # a rule's of c1 (a product or a shift in the trigger)
            c1s = compile_s(cc, d, src, ['-1'], log=True)
            if c1s[2] != compile_s(cc, d, src, [])[2] and \
                    not [ln for ln in c1s[3] if ln.split()[:1] == ['c1']
                         and ln.split()[2:3] == ['applied']]:
                problems.append('%s: -1 changes c1\'s code' % tag)
        if count:
            if not isinstance(simulate(s1, False), str):
                problems.append('%s: x86sim.py runs the -1 code as 8086 code' % tag)
            with open(os.path.join(d, 'q.s'), 'w') as f:
                f.write(s1)
            r = subprocess.run([AS, '-o', 'q.o', 'q.s'], cwd=d, capture_output=True)
            if r.returncode == 0:
                problems.append('%s: mutos_as takes the -1 code without -1' % tag)
        size0, _ = textsize(cc, d, opts)
        size1, msgs = textsize(cc, d, opts + ['-1'])
        # the report counts every kind (a trigger of one rule may meet
        # another: a product among the arguments of a call)
        rep = report(s1)
        if kinds_of(s1)[word] != count or rep not in msgs:
            problems.append('%s: cc -c -1 reports %r, not %r' % (tag, msgs.strip(), rep))
        line = '%s: %d either way, %d instruction(s), text %s -> %s bytes' % (
            tag, want, count, size0, size1)
        if realbin:
            for o in (opts, opts + ['-1']):
                got = emulated(cc, d, o)
                if got != want & 0xff:
                    problems.append('%s: cc %s, run under mutosemu.py: %r, C says %d'
                                    % (tag, ' '.join(o), got, want & 0xff))
            line += '; linked and emulated'
        lines.append(line)
    return problems, lines


def lengths(cc):
    """What c2 counts for the 80186/V30 forms when it makes a branch short
    (toofar(); V30-IMUL's half in c2): c1's -1 code of LONGIF through c2
    with -1 keeps the branch over the products long and assembles;
    through c2 without -1 - which counts as the original, 2 bytes for
    "imul ax,#n" - the branch becomes short, and mutos_as refuses it,
    since it does not reach. So the hook is needed, and a length counted
    too small ends in a message, not in wrong code."""
    problems = []
    with tempfile.TemporaryDirectory(prefix='v30') as d:
        st, m, s1, _ = compile_s(cc, d, LONGIF, ['-1'])
        if st:
            return ['lengths: cc -S -1 exit status %d: %s' % (st, m.strip()[-200:])]
        with open(os.path.join(d, 'c1.s'), 'w') as f:
            f.write(s1)
        for opt, want in (([], 'refused'), (['-1'], 'assembled')):
            out = 'c2%s.s' % ''.join(opt)
            r = subprocess.run([C2] + opt + ['c1.s', out], cwd=d, capture_output=True,
                               env=env())
            if r.returncode != 0:
                problems.append('lengths: c2 %s exit status %d' % (' '.join(opt), r.returncode))
                continue
            text = open(os.path.join(d, out)).read()
            short = bool(re.search(r'^je\tL\d+$', text, re.M))
            r = subprocess.run([AS, '-1', '-o', 'x.o', out], cwd=d, capture_output=True,
                               env=env())
            msgs = (r.stdout + r.stderr).decode('latin-1')
            if want == 'refused' and not (short and r.returncode != 0
                                          and 'short jump out of range' in msgs):
                problems.append('lengths: c2 without -1 on c1\'s -1 code: the branch over '
                                'the products is %s and mutos_as ends with status %d (%s) - '
                                'expected a short one, refused'
                                % ('short' if short else 'long', r.returncode,
                                   msgs.strip()[-80:]))
            if want == 'assembled' and (short or r.returncode != 0):
                problems.append('lengths: c2 -1 on c1\'s -1 code: the branch over the '
                                'products is %s and mutos_as ends with status %d (%s) - '
                                'expected a long one, assembled'
                                % ('short' if short else 'long', r.returncode,
                                   msgs.strip()[-80:]))
    return problems


# ---- programs of calls ------------------------------------------------------

HELPERS = '''int g0, g1, g2, g3, ga[4];
h2(a, b)
{
	return a - b;
}
h3(a, b, c)
{
	g0 = g0 + a;
	return a + (b ^ c);
}
hp(p, v)
int *p;
{
	*p = *p + v;
	return *p;
}
hr(n, k)
{
	if (n <= 0)
		return k;
	return hr(n - 1, k + 3);
}
'''
CONSTS = [0, 1, -1, 2, 5, 7, 100, 127, 128, -128, -129, 255, 256, 300, 1000, -1000,
          32767, -32767]
VARS = ['g0', 'g1', 'g2', 'g3', 'x', 'y', 'ga[1]', 'ga[3]']
ADDRS = ['&g0', '&g1', '&g2', '&g3', '&ga[0]', '&ga[2]', '&x']


class Calls:
    def __init__(self, rnd):
        self.rnd = rnd

    def const(self):
        r = self.rnd
        return str(r.choice(CONSTS) if r.random() < 0.7 else r.randint(-200, 200))

    def arg(self, depth):
        r = self.rnd.random()
        if r < 0.55:
            return self.const()
        if r < 0.8 or depth > 2:
            return self.rnd.choice(VARS)
        return self.call(depth + 1)

    def call(self, depth=0):
        k = self.rnd.random()
        if k < 0.35:
            return 'h2(%s, %s)' % (self.arg(depth), self.arg(depth))
        if k < 0.7:
            return 'h3(%s, %s, %s)' % (self.arg(depth), self.arg(depth), self.arg(depth))
        if k < 0.9:
            return 'hp(%s, %s)' % (self.rnd.choice(ADDRS), self.arg(depth))
        return 'hr(%d, %s)' % (self.rnd.randint(0, 4), self.arg(depth))

    def stmt(self, ind, depth=0):
        r = self.rnd.random()
        t = '\t' * ind
        if r < 0.5 or depth > 1:
            return '%s%s = %s;\n' % (t, self.rnd.choice(VARS), self.call())
        if r < 0.75:
            return '%sif (%s %s %s)\n%s%selse\n%s' % (
                t, self.call(), self.rnd.choice(['>', '<', '==', '!=', '<=', '>=']),
                self.const(), self.stmt(ind + 1, depth + 1), t, self.stmt(ind + 1, depth + 1))
        if r < 0.9:
            body = ''.join(self.stmt(ind + 1, depth + 1)
                           for _ in range(self.rnd.randint(1, 6)))
            return '%sfor (i%d = 0; i%d < %d; i%d++) {\n%s%s}\n' % (
                t, depth, depth, self.rnd.randint(1, 3), depth, body, t)
        return '%sy = y + %s + %s;\n' % (t, self.call(), self.call())

    def program(self):
        body = ''.join(self.stmt(1) for _ in range(self.rnd.randint(3, 14)))
        return HELPERS + 'main()\n{\n\tint x, y, i0, i1, i2;\n\n\tx = 3;\n\ty = -2;\n' + \
            body + '\treturn x + y + %s;\n}\n' % self.call()


# ---- programs of shifts -----------------------------------------------------

SCOUNTS = [0, 1, 2, 3, 3, 4, 4, 5, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 17]
SVARS = ['g0', 'g1', 'x', 'y', 'r', 'u0', 'q', 'ga[1]', 'ga[5]']
SLVALS = ['g0', 'g1', 'x', 'y', 'r', 'u0', 'q', 'ga[2]', 'ga[6]']


class Shifts:
    """Shifts by constants and by variables, of ints, unsigneds, a char
    and register variables (r, q), as values, assignments, conditions and
    under indices. No constant is shifted. No signed comparison of a shift with 0: the original
    branches on the undefined overflow flag there (C1-15), where
    x86sim.py stops."""

    def __init__(self, rnd):
        self.rnd = rnd

    def count(self):
        r = self.rnd
        if r.random() < 0.12:
            return '(%s & %d)' % (r.choice(['x', 'y', 'g1']), r.choice([3, 7, 15]))
        return str(r.choice(SCOUNTS))

    def atom(self):
        r = self.rnd
        k = r.random()
        if k < 0.6:
            return r.choice(SVARS)
        if k < 0.7:
            return 'c0'
        if k < 0.8:
            return 'ga[(%s) & 7]' % r.choice(['x', 'y', 'g0 >> 3', 'u0 >> 13', 'r << 4'])
        return str(r.choice([1, -1, 3, 8, 100, 255, -256, 4095, -4096, 32767]))

    def expr(self, depth=0):
        r = self.rnd
        k = r.random()
        if depth > 3 or k < 0.2:
            return self.atom()
        if k < 0.65:
            # no constant shifted by a constant: c1 folds it, and refuses
            # some ("C error: const")
            e = self.expr(depth + 1)
            if e.lstrip('-').isdigit():
                e = r.choice(SVARS)
            return '(%s %s %s)' % (e, r.choice(['<<', '>>']), self.count())
        if k < 0.75:
            return '(%s * %d)' % (self.expr(depth + 1), r.choice([8, 16, 32, 64, 6]))
        return '(%s %s %s)' % (self.expr(depth + 1), r.choice(['+', '-', '&', '|', '^']),
                               self.expr(depth + 1))

    def cond(self):
        r = self.rnd
        k = r.random()
        e = '(%s %s %s)' % (self.atom(), r.choice(['<<', '>>']), self.count())
        if k < 0.4:
            return e
        if k < 0.6:
            return '!' + e
        if k < 0.8:
            return '%s %s 0' % (e, r.choice(['==', '!=']))
        return '%s %s %s' % (self.expr(2), r.choice(['==', '!=', '<', '>']), self.atom())

    def stmt(self, ind, depth=0):
        r = self.rnd
        k = r.random()
        t = '\t' * ind
        if k < 0.4 or depth > 1:
            return '%s%s = %s;\n' % (t, r.choice(SLVALS), self.expr())
        if k < 0.6:
            return '%s%s %s %s;\n' % (t, r.choice(SLVALS), r.choice(['<<=', '>>=']),
                                      self.count())
        if k < 0.68:
            return '%s%s = (%s %s %s) + %s;\n' % (
                t, r.choice(SLVALS), r.choice(['r', 'q', 'x', 'g0']),
                r.choice(['<<=', '>>=']), self.count(), self.atom())
        if k < 0.85:
            return '%sif (%s)\n%s%selse\n%s' % (
                t, self.cond(), self.stmt(ind + 1, depth + 1), t,
                self.stmt(ind + 1, depth + 1))
        body = ''.join(self.stmt(ind + 1, depth + 1) for _ in range(r.randint(1, 5)))
        return '%sfor (i%d = 0; i%d < %d; i%d++) {\n%s%s}\n' % (
            t, depth, depth, r.randint(1, 3), depth, body, t)

    def program(self):
        r = self.rnd
        body = ''.join(self.stmt(1) for _ in range(r.randint(4, 16)))
        return ('int g0, g1, ga[8];\nunsigned u0;\nchar c0;\nmain()\n{\n'
                '\tregister int r;\n\tregister unsigned q;\n\tint x, y, i0, i1;\n\n'
                '\tg0 = %d;\n\tg1 = %d;\n\tu0 = %d;\n\tc0 = %d;\n\tx = %d;\n\ty = %d;\n'
                '\tr = %d;\n\tq = %d;\n\tfor (i0 = 0; i0 < 8; i0++)\n'
                '\t\tga[i0] = i0 * %d - %d;\n' % (
                    r.randint(-2000, 2000), r.randint(-30000, 30000), r.randint(0, 65535),
                    r.randint(-128, 127), r.randint(-100, 100), r.randint(-30000, 30000),
                    r.randint(-2000, 2000), r.randint(0, 65535), r.randint(1, 4000),
                    r.randint(0, 9000))
                + body + '\treturn x + y + r + q + %s;\n}\n' % self.expr(1))


# ---- programs of products ---------------------------------------------------

MCONSTS = [3, 3, 5, 6, 7, 10, 10, 12, 18, 30, 30, 68, 100, 127, 129, 142, 255, 300, 1000,
           -1, -3, -5, -127, -128, -129, -300, 2, 4, 16, 128, 256]
MVARS = ['g0', 'g1', 'x', 'y', 'r', 'u0', 'q', 'ga[1]', 'ga[5]', 'st[2].v']
MLVALS = ['g0', 'g1', 'x', 'y', 'r', 'u0', 'q', 'ga[2]', 'ga[6]']


class Products:
    """Products by constants of every kind (a byte, a word, negative,
    powers of two - which are shifts) and by variables, of ints,
    unsigneds, a char and register variables (r, q), as values, in place
    (=*, in memory too), as conditions and under the index of an array of
    structures - what V30-IMUL meets -, with branches around them. No
    product of two constants (c1 folds it)."""

    def __init__(self, rnd):
        self.rnd = rnd

    def factor(self):
        r = self.rnd
        if r.random() < 0.15:
            return r.choice(['x', 'y', 'g1', 'r'])
        return str(r.choice(MCONSTS) if r.random() < 0.8 else r.randint(-400, 400))

    def atom(self):
        r = self.rnd
        k = r.random()
        if k < 0.6:
            return r.choice(MVARS)
        if k < 0.7:
            return 'c0'
        if k < 0.82:
            return 'st[(%s) & 3].v' % r.choice(['x', 'y', 'g0', 'r * 3', 'q'])
        return str(r.choice([1, -1, 3, 8, 100, 255, -256, 4095, 32767]))

    def expr(self, depth=0):
        r = self.rnd
        k = r.random()
        if depth > 3 or k < 0.2:
            return self.atom()
        if k < 0.65:
            e = self.expr(depth + 1)
            if e.lstrip('-').isdigit():
                e = r.choice(MVARS)
            f = self.factor()
            return '(%s * %s)' % ((e, f) if r.random() < 0.8 else (f, e))
        if k < 0.72:
            return '(%s << %d)' % (r.choice(MVARS), r.choice([1, 3, 5]))
        return '(%s %s %s)' % (self.expr(depth + 1), r.choice(['+', '-', '&', '|', '^']),
                               self.expr(depth + 1))

    def cond(self):
        r = self.rnd
        k = r.random()
        e = '(%s * %s)' % (r.choice(MVARS), self.factor())
        if k < 0.3:
            return e
        if k < 0.45:
            return '!' + e
        return '%s %s %s' % (e, r.choice(['==', '!=', '<', '>', '<=', '>=']), self.atom())

    def stmt(self, ind, depth=0):
        r = self.rnd
        k = r.random()
        t = '\t' * ind
        if k < 0.4 or depth > 1:
            return '%s%s = %s;\n' % (t, r.choice(MLVALS), self.expr())
        if k < 0.6:
            return '%s%s *= %s;\n' % (t, r.choice(MLVALS), self.factor())
        if k < 0.66:
            return '%sst[(%s) & 3].v = %s;\n' % (t, r.choice(['x', 'g0', 'r']), self.expr(1))
        if k < 0.85:
            return '%sif (%s)\n%s%selse\n%s' % (
                t, self.cond(), self.stmt(ind + 1, depth + 1), t,
                self.stmt(ind + 1, depth + 1))
        body = ''.join(self.stmt(ind + 1, depth + 1) for _ in range(r.randint(1, 8)))
        if r.random() < 0.5:
            return '%sif (%s) {\n%s%s}\n' % (t, self.cond(), body, t)
        return '%sfor (i%d = 0; i%d < %d; i%d++) {\n%s%s}\n' % (
            t, depth, depth, r.randint(1, 3), depth, body, t)

    def program(self):
        r = self.rnd
        body = ''.join(self.stmt(1) for _ in range(r.randint(4, 16)))
        return ('int g0, g1, ga[8];\nunsigned u0;\nchar c0;\n'
                'struct s {\n\tchar n[%d];\n\tint v;\n} st[4];\nmain()\n{\n'
                '\tregister int r;\n\tregister unsigned q;\n\tint x, y, i0, i1;\n\n'
                '\tg0 = %d;\n\tg1 = %d;\n\tu0 = %d;\n\tc0 = %d;\n\tx = %d;\n\ty = %d;\n'
                '\tr = %d;\n\tq = %d;\n\tfor (i0 = 0; i0 < 8; i0++)\n'
                '\t\tga[i0] = i0 * %d - %d;\n\tfor (i0 = 0; i0 < 4; i0++)\n'
                '\t\tst[i0].v = i0 * %d + %d;\n' % (
                    r.choice([4, 10, 28, 66, 140, 298]),
                    r.randint(-2000, 2000), r.randint(-30000, 30000), r.randint(0, 65535),
                    r.randint(-128, 127), r.randint(-100, 100), r.randint(-30000, 30000),
                    r.randint(-2000, 2000), r.randint(0, 65535), r.randint(1, 4000),
                    r.randint(0, 9000), r.randint(1, 900), r.randint(-50, 50))
                + body + '\treturn x + y + r + q + %s;\n}\n' % self.expr(1))


def generated(cc, ncalls, nshifts, nmuls, nfuzz, seed, keep):
    problems = []
    t = {'fuzz': 0, 'fchanged': 0, 'refused': 0}
    kinds = [k for k, _ in KIND.values()]

    def save(name, src, s0, s1):
        if keep:
            d = os.path.join(keep, name)
            os.makedirs(d, exist_ok=True)
            for n, x in (('p.c', src), ('p0.s', s0), ('p1.s', s1)):
                with open(os.path.join(d, n), 'w') as f:
                    f.write(x)

    def programs(d, what, n, gen, optsets):
        """n generated programs, each compiled with every option set of
        optsets with and without -1 and executed; the tally."""
        c = {'n': 0, 'changed': 0, 'unrun': 0, 'made': 0, 'b0': 0, 'b1': 0, 'c0': 0,
             'c1': 0}
        for i in range(n):
            src = gen(random.Random('v30 %s %d %d' % (what, seed, i))).program()
            c['n'] += 1
            for opts in optsets:
                name = '%s.s%d.%d%s' % (what, seed, i, ''.join(opts))
                o = ' '.join(opts + ['-S'])
                st0, m0, s0, _ = compile_s(cc, d, src, opts)
                st1, m1, s1, _ = compile_s(cc, d, src, opts + ['-1'])
                if st0 or st1:
                    # refused the same way either way (a constant c1 folds
                    # and refuses, say): counted with those not run
                    if st0 == st1 and m0 == m1:
                        c['unrun'] += 1
                        continue
                    problems.append('%s: cc %s exit status %d, with -1 %d: %s'
                                    % (name, o, st0, st1, (m0 + m1).strip()[-160:]))
                    save(name, src, s0, s1)
                    continue
                r0, r1 = simulate(s0, False), simulate(s1, True)
                if isinstance(r0, str):
                    # nothing to compare with (with -1 c2 may use other
                    # registers and write forms x86sim.py does run)
                    c['unrun'] += 1
                    continue
                if isinstance(r1, str) or r0[:2] != r1[:2]:
                    problems.append('%s: without -1 main() is %d, with -1 %s' % (
                        name, r0[0], r1 if isinstance(r1, str) else
                        '%d%s' % (r1[0], '' if r0[1] == r1[1] else
                                  ', and the file-scope variables differ')))
                    save(name, src, s0, s1)
                    continue
                if s0 != s1:
                    c['changed'] += 1
                    c['made'] += sum(len(k.findall(s1)) for k in kinds)
                    b0, b1 = textsize(cc, d, opts)[0], textsize(cc, d, opts + ['-1'])[0]
                    if b0 is None or b1 is None:
                        problems.append('%s: cc %s -c makes no object' % (name, ' '.join(opts)))
                        continue
                    c['b0'] += b0
                    c['b1'] += b1
                    c['c0'] += r0[2][0]
                    c['c1'] += r1[2][0]
        if c['unrun'] * 20 > max(c['n'] * len(optsets), 1):
            problems.append('%d of the %d compilations of programs of %s do not run under '
                            'x86sim.py without -1' % (c['unrun'], c['n'] * len(optsets), what))
        return c

    with tempfile.TemporaryDirectory(prefix='v30') as d:
        t['calls'] = programs(d, 'calls', ncalls, Calls, [['-O']])
        t['shifts'] = programs(d, 'shifts', nshifts, Shifts, [[], ['-O']])
        t['products'] = programs(d, 'products', nmuls, Products, [[], ['-O']])
        for i in range(nfuzz):
            rnd = random.Random('v30 fuzz %d %d' % (seed, i))
            src, fenv, ret, g = fuzz_c.generate(rnd, rnd.random() < 0.8, rnd.random() < 0.3,
                                                rnd.random() < 0.5)
            counted = False
            for opts in ([['-O'], []] if i % 2 else [['-O']]):
                name = 'fuzz.s%d.%d%s' % (seed, i, ''.join(opts))
                st0, m0, s0, _ = compile_s(cc, d, src, opts)
                st1, m1, s1, _ = compile_s(cc, d, src, opts + ['-1'])
                if st0 != st1 or m0 != m1:
                    problems.append('%s: cc %s -S ends otherwise with -1'
                                    % (name, ' '.join(opts)))
                    save(name, src, s0, s1)
                    continue
                if st0:
                    t['refused'] += not counted
                    counted = True
                    continue
                t['fuzz'] += not counted
                counted = True
                if s0 == s1:
                    continue
                t['fchanged'] += 1
                j0 = fuzz_c.judge(s0, fenv, ret, g)
                j1 = fuzz_c.judge(s1, fenv, ret, g, v30=True)
                if j0 != j1:
                    problems.append('%s: without -1 %s, with -1 %s' % (
                        name, ' '.join(j0)[:120], ' '.join(j1)[:120]))
                    save(name, src, s0, s1)
    return problems, t


def active_v30():
    out = []
    with open(os.path.join(REPO, 'docs', 'RULES.md'), encoding='utf-8') as f:
        for line in f:
            cells = [c.strip().strip('`') for c in line.strip().strip('|').split('|')]
            if len(cells) >= 4 and cells[0].startswith('V30-') and cells[2] == '-1' \
                    and cells[3] == 'active':
                if cells[0] not in out:     # V30-IMUL: a row for c1 and one for c2
                    out.append(cells[0])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--cc', default=CC)
    ap.add_argument('--calls', type=int, default=300, metavar='N',
                    help='generated programs of calls (300)')
    ap.add_argument('--shifts', type=int, default=300, metavar='N',
                    help='generated programs of shifts (300)')
    ap.add_argument('--products', type=int, default=300, metavar='N',
                    help='generated programs of products (300)')
    ap.add_argument('--fuzz', type=int, default=300, metavar='N',
                    help='programs of fuzz_c.py (300)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--realbin', action='store_true',
                    help='also link the triggers and run them under mutosemu.py')
    ap.add_argument('--keep', metavar='DIR', help='where to save programs that differ')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    problems, lines = triggers(a.cc, a.realbin)
    problems += lengths(a.cc)
    rules = active_v30()
    for rule in rules:
        if not any(c[0] == rule and c[4] for c in CASES):
            problems.append('%s is active in docs/RULES.md but has no trigger here that '
                            'it changes' % rule)
    for rule in sorted({c[0] for c in CASES}):
        if rule not in rules:
            problems.append('%s has a trigger here but is no active V30 rule in '
                            'docs/RULES.md' % rule)
    p, t = generated(a.cc, a.calls, a.shifts, a.products, a.fuzz, a.seed, a.keep)
    problems += p
    if a.verbose or problems:
        for ln in lines:
            print('  ' + ln)
    for p in problems[:40]:
        print('check_v30: ' + p)
    if problems:
        print('check_v30: %d problem(s)' % len(problems))
        return 1
    gain = ''
    for what in ('calls', 'shifts', 'products'):
        c = t[what]
        if c['b0']:
            gain += '; in the %d compilations of programs of %s -1 changed: %d 80186/V30 ' \
                    'instructions, text %d -> %d bytes (%.1f%%), V30 clocks %d -> %d ' \
                    '(%.1f%%)' % (c['changed'], what, c['made'], c['b0'], c['b1'],
                                  100.0 * (c['b0'] - c['b1']) / c['b0'], c['c0'], c['c1'],
                                  100.0 * (c['c0'] - c['c1']) / max(c['c0'], 1))
    print('check_v30: %d trigger(s) of %d V30 rule(s) (%s): C\'s value with and without '
          '-1%s; c2 counts the forms\' lengths; %d programs of calls, %d of shifts and '
          '%d of products (seed %d; %d, %d and %d compilations '
          'not run by x86sim.py) and %d of fuzz_c.py (%d compilations changed by -1; %d '
          'refused): the same values with -1%s'
          % (len(CASES), len(rules), ', '.join(rules) or 'none',
             ', linked and emulated too' if a.realbin else '', t['calls']['n'],
             t['shifts']['n'], t['products']['n'], a.seed, t['calls']['unrun'],
             t['shifts']['unrun'], t['products']['unrun'],
             t['fuzz'], t['fchanged'], t['refused'], gain))
    return 0


if __name__ == '__main__':
    sys.exit(main())
