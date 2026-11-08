#!/usr/bin/env python3
"""check_corrected.py - the corrections of the corrected mode compute what
C says (docs/RULES.md; docs/TOOLCHAIN_PLAN.md, section 7.2, "Semantics
instead of bytes where no golden can exist").

tests/rules/check_accounting.py shows that every byte the corrected mode
changes belongs to a rule; this script shows that what the rules write is
right. Two parts:

  * Triggers: for each active correction of docs/RULES.md, small programs
    that meet the bug, compiled by mutos_cc as the original does (no
    switch), with -H, with -F and with MUTOS_MODE=corrected, c1's code
    ("cc -S") executed by tests/mutos_cc/fuzz/x86sim.py: main()'s value
    must be the original's as pinned here (a wrong value, or the message
    x86sim.py stops with) without a switch and with -H, and C's with -F;
    -F and MUTOS_MODE=corrected must give the same code, and so must -H
    and no switch. With --realbin the programs are also linked ("cc -o"
    and, through c2, "cc -O -o") against the MUTOS root's crt0.o and
    libc.a and run under tests/realbin/mutosemu.py (which needs the
    Python module unicorn): with -F the exit status must be C's value
    (modulo 256) with and without c2, without a switch the original's
    where one is pinned.
  * A fuzz run: tests/mutos_cc/fuzz/fuzz_c.py --mode corrected on fixed
    seeds, once with arrays and once with longs - every program must
    compute what C says unless it fails through a bug of the original the
    corrected mode does not correct yet (fuzz_c.py tallies those by bug).

Every rule of docs/RULES.md that is active and corrects a bug of c1
must have a trigger here.

Usage:
    check_corrected.py [--cc PATH] [--realbin] [--fuzz N]

Exit status 1 if anything differs. Python 3; --realbin needs unicorn.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
FUZZ = os.path.join(REPO, 'tests', 'mutos_cc', 'fuzz')
sys.path.insert(0, FUZZ)
import x86sim  # noqa: E402

CC = os.path.join(REPO, 'src', 'mutos_cc', 'mutos_cc')
MUTOSEMU = os.path.join(REPO, 'tests', 'realbin', 'mutosemu.py')

# (rule, name, source, C's value of main(), the original's: a value, or
# a piece of the message x86sim.py stops with; for the emulated run of
# the linked program, the original's exit status - or a pair, without
# and with c2, where c2's code leaves other stale flags - or None, not
# pinned)
CASES = [
    ('C1-03', 'x = l++ and x = m-- on file-scope longs, y = a++ on a local',
     '''long l, m;
int x;
main()
{
	long a;
	int y;

	l = 65535;
	x = l++;
	m = 0;
	x = m--;
	a = 65535;
	y = a++;
	return (int)(l >> 16) * 100 + (int)(m >> 16) * 10 + (int)(a >> 16);
}
''', 91, 0, 0),
    ('C1-03', 'the kernel\'s shape (amx.c): f((int)t++) and x = (int)u-- on local longs',
     '''int seen;
put(x)
{
	seen = x;
}
main()
{
	long t, u;
	int i, x;

	t = 65534;
	for (i = 0; i < 3; i++)
		put((int)t++);
	u = 65536;
	x = (int)u--;
	return (int)(t >> 16) * 10 + (int)(u >> 16) + x;
}
''', 10, 1, 1),
    ('C1-06', 'l <<= i, m >>= i, g <<= i and *p <<= i with i 0 and 2: local, file-scope, '
     'through a pointer',
     '''long g;
main()
{
	long l, m, *p;
	int i, r;

	l = 300;
	m = -300;
	g = 7;
	i = 0;
	l <<= i;
	m >>= i;
	g <<= i;
	p = &m;
	*p <<= i;
	r = (int)l + (int)m + (int)g;
	i = 2;
	l <<= i;
	m >>= i;
	*p <<= i;
	g >>= i;
	return r * 10 + (int)(l - 1200) + (int)(m + 300) + (int)(g - 1);
}
''', 70, 'with a count of 0 past 6 bytes of stores', None),
    ('C1-06', 'a count computed first ("pop cx"), through a pointer too',
     '''int k[2];
main()
{
	long l, *p;
	int i, j;

	l = 65537;
	k[0] = 0;
	k[1] = 3;
	i = 0;
	j = 1;
	l <<= k[i] * j;
	p = &l;
	*p >>= k[i] + i;
	l <<= k[j] - 1;
	return (int)(l >> 16) * 10 + (int)l;
}
''', 44, 'with a count of 0 past 6 bytes of stores', None),
    ('C1-02', '(u = 7, x) * 0, (v = 5) & 0 and w++ * 0 * x',
     '''int u, v, w, x, s;
main()
{
	u = 1;
	v = 1;
	w = 1;
	x = 3;
	s = (u = 7, x) * 0;
	s = s + ((v = 5) & 0);
	s = s + (w++ * 0 * x);
	return s * 1000 + u * 100 + v * 10 + w;
}
''', 752, 111, 111),
    ('C1-02', 'in a condition, as an argument and in a sum: (u = 2) & 0, f() * 0, u++ * 0 + 1',
     '''int u, n, c;
f()
{
	c++;
	return 5;
}
put(x)
{
	n = n + x;
}
main()
{
	u = 0;
	n = 0;
	c = 0;
	if ((u = 2) & 0)
		n = 100;
	put(f() * 0);
	put(u++ * 0 + 1);
	n = n + (c ? 0 : 1000);
	return n + u * 10 + c * 100;
}
''', 131, 1001, 1001 & 0xff),
    ('C1-02', 'into a char, an unsigned and a long, as an index, in a pointer sum, ?:, '
     'switch and an argument',
     '''int u, a[3], r;
char c;
unsigned x;
long l;
g(v)
{
	return v + 1;
}
main()
{
	int *p;

	c = (u = 3) * 0;
	r = u;
	x = (u = 2) & 0;
	r = r * 10 + u;
	a[(u = 1) * 0] = 5;
	r = r * 10 + u + a[0];
	p = a + (u = 1) * 0;
	r = r + *p + u;
	l = (u = 2) * 0;
	r = r + u + (int)l;
	r = r + ((u = 3) * 0 ? 100 : u);
	switch ((u = 2) & 0) {
	case 0:
		r = r + u;
	}
	r = r + g((u = 7) * 0) + u;
	return r;
}
''', 347, 11, 11),
    ('C1-01', 'if (~a || n++), z = (!y >= 0), if (x << k), if (x >> k): stale flags '
     'that are known',
     '''int a, n, y, z, x, k, r;
main()
{
	a = -1;
	n = 0;
	y = -5;
	x = 1;
	k = 0;
	r = 0;
	if (y == 0)
		r = 5;
	if (~a || n++)
		r = r + 100;
	z = (!y >= 0);
	if (k != 0)
		r = 7;
	if (x << k)
		r = r + 10;
	if (k != 0)
		r = 7;
	if (x >> k)
		r = r + 20;
	return r + n * 1000 + z * 200;
}
''', 1230, 100, (100, 44)),
    ('C1-01', '~(a + b), (x << k) && a, (x >> k) ? :, ~f(): the value in ax',
     '''int a, b, r, k, x;
f()
{
	return -1;
}
main()
{
	a = 3;
	b = -4;
	k = 0;
	x = 5;
	r = 0;
	if (a == 0)
		r = 1;
	if (~(a + b))
		r = r + 20;
	if (k != 0)
		r = 7;
	if ((x << k) && a)
		r = r + 40;
	if (k != 0)
		r = 7;
	r = r + ((x >> k) ? 100 : 200);
	if (~f())
		r = r + 10;
	return r;
}
''', 140, 'on flags the checker does not know', 230),
]


def compile_s(cc, d, src, opts, mode=None):
    """c1's code for src ("cc -S" in d): (exit status, messages, .s text)."""
    env = {k: v for k, v in os.environ.items() if k not in ('MUTOS_MODE', 'MUTOS_RULELOG')}
    if mode:
        env['MUTOS_MODE'] = mode
    with open(os.path.join(d, 'p.c'), 'w') as f:
        f.write(src)
    s = os.path.join(d, 'p.s')
    if os.path.exists(s):
        os.unlink(s)
    r = subprocess.run([cc] + opts + ['-S', 'p.c'], cwd=d, capture_output=True,
                       env=env)
    text = open(s).read() if os.path.exists(s) else ''
    return r.returncode, (r.stdout + r.stderr).decode('latin-1'), text


def simulate(text):
    """main()'s value under x86sim.py, or the message it stops with."""
    try:
        return x86sim.run(text).ret
    except x86sim.SimError as e:
        return 'x86sim: %s' % e


def matches(got, want):
    if isinstance(want, str):
        return isinstance(got, str) and want in got
    return got == want


def emulated(cc, d, src, opts):
    """The exit status of src linked by "cc <opts> -o p" and run under
    mutosemu.py, or a message."""
    with open(os.path.join(d, 'p.c'), 'w') as f:
        f.write(src)
    env = {k: v for k, v in os.environ.items() if k not in ('MUTOS_MODE', 'MUTOS_RULELOG')}
    r = subprocess.run([cc] + opts + ['-o', 'p', 'p.c'], cwd=d, capture_output=True,
                       env=env)
    if r.returncode != 0:
        return 'cc: exit status %d: %s' % (r.returncode, r.stderr.decode('latin-1')[-200:])
    r = subprocess.run([sys.executable, MUTOSEMU, '--cwd', d, os.path.join(d, 'p')],
                       capture_output=True)
    return r.returncode


def triggers(cc, realbin):
    problems, lines = [], []
    for rule, name, src, want_c, want_orig, want_emu in CASES:
        with tempfile.TemporaryDirectory(prefix='corr') as d:
            runs = {}
            for tag, opts, mode in (('none', [], None), ('-H', ['-H'], None),
                                    ('-F', ['-F'], None),
                                    ('MUTOS_MODE', [], 'corrected')):
                st, msgs, text = compile_s(cc, d, src, opts, mode)
                if st != 0:
                    problems.append('%s %s (%s): cc -S exit status %d: %s'
                                    % (rule, name, tag, st, msgs.strip()[-200:]))
                    text = None
                runs[tag] = text
            if None in runs.values():
                continue
            if runs['-H'] != runs['none']:
                problems.append('%s %s: -H changes the code' % (rule, name))
            if runs['MUTOS_MODE'] != runs['-F']:
                problems.append('%s %s: MUTOS_MODE=corrected and -F differ' % (rule, name))
            if runs['-F'] == runs['none']:
                problems.append('%s %s: -F does not change the code' % (rule, name))
            got_orig, got_c = simulate(runs['none']), simulate(runs['-F'])
            if not matches(got_orig, want_orig):
                problems.append('%s %s: without a switch %r, pinned %r'
                                % (rule, name, got_orig, want_orig))
            if not matches(got_c, want_c):
                problems.append('%s %s: with -F %r, C says %r' % (rule, name, got_c, want_c))
            line = '%s %s: original %s, -F %s' % (rule, name, got_orig, got_c)
            if realbin:
                for opts in (['-F'], ['-F', '-O']):
                    got = emulated(cc, d, src, opts)
                    if got != want_c & 0xff:
                        problems.append('%s %s: cc %s, run under mutosemu.py: %r, C says %d'
                                        % (rule, name, ' '.join(opts), got, want_c & 0xff))
                if want_emu is not None:
                    pins = want_emu if isinstance(want_emu, tuple) else (want_emu, want_emu)
                    for opts, pin in zip(([], ['-O']), pins):
                        got = emulated(cc, d, src, opts)
                        if got != pin:
                            problems.append('%s %s: cc %s, run under mutosemu.py: %r, '
                                            'pinned %r' % (rule, name, ' '.join(opts) or '(no switch)',
                                                           got, pin))
                line += '; linked and emulated, with and without c2'
            lines.append(line)
    return problems, lines


def active_c1_corrections():
    """The rules of docs/RULES.md's register that are active corrections of c1."""
    out = []
    with open(os.path.join(REPO, 'docs', 'RULES.md'), encoding='utf-8') as f:
        for line in f:
            cells = [c.strip().strip('`') for c in line.strip().strip('|').split('|')]
            if len(cells) >= 4 and re.fullmatch(r'C1-\d+', cells[0]) and \
                    cells[1] == 'c1' and cells[2] == '-F' and cells[3] == 'active':
                out.append(cells[0])
    return out


def fuzz(n):
    problems, lines = [], []
    for extra in ([], ['--longs']):
        r = subprocess.run([sys.executable, os.path.join(FUZZ, 'fuzz_c.py'), '-n', str(n),
                            '-s', '1', '--mode', 'corrected'] + extra,
                           capture_output=True, text=True)
        out = [ln for ln in r.stdout.splitlines() if ln.strip()]
        lines += out[:3]
        if r.returncode != 0:
            problems.append('fuzz_c.py --mode corrected%s: exit status %d'
                            % (' ' + ' '.join(extra) if extra else '', r.returncode))
            lines += out[3:]
    return problems, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--cc', default=CC)
    ap.add_argument('--realbin', action='store_true',
                    help='also link the triggers and run them under mutosemu.py')
    ap.add_argument('--fuzz', type=int, default=1000, metavar='N',
                    help='programs per fuzz run (0: none)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    problems, lines = triggers(args.cc, args.realbin)
    rules = active_c1_corrections()
    for rule in rules:
        if not any(c[0] == rule for c in CASES):
            problems.append('%s is active in docs/RULES.md but has no trigger here' % rule)
    for c in CASES:
        if c[0] not in rules:
            problems.append('%s has a trigger here but is no active correction of c1 in '
                            'docs/RULES.md' % c[0])
    if args.fuzz:
        p, ln = fuzz(args.fuzz)
        problems += p
        lines += ln
    if args.verbose or problems:
        for ln in lines:
            print('  ' + ln)
    for p in problems:
        print('check_corrected: ' + p)
    if problems:
        return 1
    print('check_corrected: %d trigger(s) of %d correction(s) (%s): the original\'s values '
          'without a switch, C\'s with -F%s; fuzz_c.py --mode corrected: %s'
          % (len(CASES), len(rules), ', '.join(rules),
             ', linked and emulated with and without c2' if args.realbin else '',
             '%d + %d programs, every failure a bug not corrected yet' % (args.fuzz, args.fuzz)
             if args.fuzz else 'not run'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
