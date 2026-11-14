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
    (c2's code; with "-S" and "-S -1" for a rule of c1), executed by
    tests/mutos_cc/fuzz/x86sim.py. main()'s value is C's, pinned here,
    either way; with -1 the code has exactly the pinned number of
    80186/V30 instructions of the rule's kind and the rule log as many
    "applied" entries of the rule (those with chunk=1 apart: c2's entry
    for the function the rule was applied in); without -1 there is none, and the log
    has "met" where -1 applies the rule. The -1 code is no 8086 code:
    x86sim.py without v30 stops on it, and mutos_as without -1 refuses
    it. "cc -O -c -1" reports the pinned number ("as: -1: ...").
    With --realbin the triggers are also linked ("cc -O -o", with and
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
    run under x86sim.py at all; a generated program that does not is
    counted, and more than one in twenty fail the check.)
  * Programs of tests/mutos_cc/fuzz/fuzz_c.py (--fuzz N): expressions,
    arrays, longs - the same comparison, on fuzz_c.py's own judgement of
    the two codes (every variable at every statement against C's
    semantics): with -1 it must be what it is without.

It reports what -1 gained in the generated programs: the bytes of text
(from the objects of "cc -O -c") and the V30 clocks x86sim.py counted.

Every V30 rule that is active in docs/RULES.md must have a trigger here.

Usage:
    check_v30.py [--cc PATH] [--calls N] [--fuzz N] [--seed S] [--realbin]
                 [--keep DIR] [-v]

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
MUTOSEMU = os.path.join(REPO, 'tests', 'realbin', 'mutosemu.py')

# the instructions of a rule's kind in a .s, and the word mutos_as's
# report has for them ("as: -1: 3 80186/V30 instructions: push 3")
KIND = {
    'V30-PUSH': (re.compile(r'^(?:(?:L\d+|_\w+):)*push\t[*#][^(\n]*$', re.M), 'push'),
}
# the stage whose code a rule changes: c2's rules need -O
OPTS = {'V30-PUSH': ['-O']}

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


def entries(log, rule, what):
    return [ln for ln in log if ln.split()[:3] == ['c2', rule, what]
            or ln.split()[:3] == ['c1', rule, what]]


def triggers(cc, realbin):
    problems, lines = [], []
    for rule, name, src, want, count in CASES:
        tag = '%s %s' % (rule, name)
        kind, word = KIND[rule]
        opts = OPTS[rule]
        with tempfile.TemporaryDirectory(prefix='v30') as d:
            st0, m0, s0, log0 = compile_s(cc, d, src, opts, log=True)
            st1, m1, s1, log1 = compile_s(cc, d, src, opts + ['-1'], log=True)
            if st0 or st1:
                problems.append('%s: cc -S exit status %d, with -1 %d: %s'
                                % (tag, st0, st1, (m0 + m1).strip()[-200:]))
                continue
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
            if opts and compile_s(cc, d, src, ['-1'])[2] != compile_s(cc, d, src, [])[2]:
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
            rep = 'as: -1: %d 80186/V30 instructions: %s %d' % (count, word, count) \
                if count != 1 else 'as: -1: 1 80186/V30 instruction: %s 1' % word
            if not count:
                rep = 'as: -1: no 80186/V30 instructions'
            if rep not in msgs:
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


def generated(cc, ncalls, nfuzz, seed, keep):
    problems = []
    t = {'calls': 0, 'changed': 0, 'unrun': 0, 'fuzz': 0, 'fchanged': 0, 'refused': 0,
         'made': 0, 'b0': 0, 'b1': 0, 'c0': 0, 'c1': 0}
    kind = KIND['V30-PUSH'][0]

    def save(name, src, s0, s1):
        if keep:
            d = os.path.join(keep, name)
            os.makedirs(d, exist_ok=True)
            for n, x in (('p.c', src), ('p0.s', s0), ('p1.s', s1)):
                with open(os.path.join(d, n), 'w') as f:
                    f.write(x)

    with tempfile.TemporaryDirectory(prefix='v30') as d:
        for i in range(ncalls):
            name = 'calls.s%d.%d' % (seed, i)
            src = Calls(random.Random('v30 calls %d %d' % (seed, i))).program()
            st0, m0, s0, _ = compile_s(cc, d, src, ['-O'])
            st1, m1, s1, _ = compile_s(cc, d, src, ['-O', '-1'])
            if st0 or st1:
                problems.append('%s: cc -O -S exit status %d, with -1 %d: %s'
                                % (name, st0, st1, (m0 + m1).strip()[-160:]))
                save(name, src, s0, s1)
                continue
            t['calls'] += 1
            r0, r1 = simulate(s0, False), simulate(s1, True)
            if isinstance(r0, str):
                t['unrun'] += 1
                if not isinstance(r1, str):
                    problems.append('%s: runs with -1 only (%s)' % (name, r0))
                continue
            if isinstance(r1, str) or r0[:2] != r1[:2]:
                problems.append('%s: without -1 main() is %d, with -1 %s' % (
                    name, r0[0], r1 if isinstance(r1, str) else
                    '%d%s' % (r1[0], '' if r0[1] == r1[1] else
                              ', and the file-scope variables differ')))
                save(name, src, s0, s1)
                continue
            if s0 != s1:
                t['changed'] += 1
                t['made'] += len(kind.findall(s1))
                b0, b1 = textsize(cc, d, ['-O'])[0], textsize(cc, d, ['-O', '-1'])[0]
                if b0 is None or b1 is None:
                    problems.append('%s: cc -O -c makes no object' % name)
                    continue
                t['b0'] += b0
                t['b1'] += b1
                t['c0'] += r0[2][0]
                t['c1'] += r1[2][0]
        for i in range(nfuzz):
            name = 'fuzz.s%d.%d' % (seed, i)
            rnd = random.Random('v30 fuzz %d %d' % (seed, i))
            src, fenv, ret, g = fuzz_c.generate(rnd, rnd.random() < 0.8, rnd.random() < 0.3,
                                                rnd.random() < 0.5)
            st0, m0, s0, _ = compile_s(cc, d, src, ['-O'])
            st1, m1, s1, _ = compile_s(cc, d, src, ['-O', '-1'])
            if st0 != st1 or m0 != m1:
                problems.append('%s: cc -O -S ends otherwise with -1' % name)
                save(name, src, s0, s1)
                continue
            if st0:
                t['refused'] += 1
                continue
            t['fuzz'] += 1
            if s0 == s1:
                continue
            t['fchanged'] += 1
            j0 = fuzz_c.judge(s0, fenv, ret, g)
            j1 = fuzz_c.judge(s1, fenv, ret, g, v30=True)
            if j0 != j1:
                problems.append('%s: without -1 %s, with -1 %s' % (
                    name, ' '.join(j0)[:120], ' '.join(j1)[:120]))
                save(name, src, s0, s1)
    if t['unrun'] * 20 > max(t['calls'], 1):
        problems.append('%d of the %d programs of calls do not run under x86sim.py '
                        'without -1' % (t['unrun'], t['calls']))
    return problems, t


def active_v30():
    out = []
    with open(os.path.join(REPO, 'docs', 'RULES.md'), encoding='utf-8') as f:
        for line in f:
            cells = [c.strip().strip('`') for c in line.strip().strip('|').split('|')]
            if len(cells) >= 4 and cells[0].startswith('V30-') and cells[2] == '-1' \
                    and cells[3] == 'active':
                out.append(cells[0])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--cc', default=CC)
    ap.add_argument('--calls', type=int, default=300, metavar='N',
                    help='generated programs of calls (300)')
    ap.add_argument('--fuzz', type=int, default=300, metavar='N',
                    help='programs of fuzz_c.py (300)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--realbin', action='store_true',
                    help='also link the triggers and run them under mutosemu.py')
    ap.add_argument('--keep', metavar='DIR', help='where to save programs that differ')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    problems, lines = triggers(a.cc, a.realbin)
    rules = active_v30()
    for rule in rules:
        if not any(c[0] == rule and c[4] for c in CASES):
            problems.append('%s is active in docs/RULES.md but has no trigger here that '
                            'it changes' % rule)
    for rule in sorted({c[0] for c in CASES}):
        if rule not in rules:
            problems.append('%s has a trigger here but is no active V30 rule in '
                            'docs/RULES.md' % rule)
    p, t = generated(a.cc, a.calls, a.fuzz, a.seed, a.keep)
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
    if t['b0']:
        gain = '; in the %d programs of calls -1 changed: %d 80186/V30 instructions, text ' \
               '%d -> %d bytes (%.1f%%), V30 clocks %d -> %d (%.1f%%)' % (
                   t['changed'], t['made'], t['b0'], t['b1'],
                   100.0 * (t['b0'] - t['b1']) / t['b0'], t['c0'], t['c1'],
                   100.0 * (t['c0'] - t['c1']) / max(t['c0'], 1))
    print('check_v30: %d trigger(s) of %d V30 rule(s) (%s): C\'s value with and without '
          '-1%s; %d programs of calls (seed %d; %d not run by x86sim.py) and %d of '
          'fuzz_c.py (%d changed by -1; %d refused): the same values with -1%s'
          % (len(CASES), len(rules), ', '.join(rules) or 'none',
             ', linked and emulated too' if a.realbin else '', t['calls'], a.seed,
             t['unrun'], t['fuzz'], t['fchanged'], t['refused'], gain))
    return 0


if __name__ == '__main__':
    sys.exit(main())
