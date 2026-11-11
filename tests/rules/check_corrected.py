#!/usr/bin/env python3
"""check_corrected.py - the corrections of the corrected mode compute what
C says, and link as a link must end (docs/RULES.md; docs/TOOLCHAIN_PLAN.md,
section 7.2, "Semantics instead of bytes where no golden can exist").

tests/rules/check_accounting.py shows that every byte the corrected mode
changes belongs to a rule; this script shows that what the rules write is
right. Two parts:

  * Triggers: for each active correction of docs/RULES.md, small programs
    that meet the bug, compiled by mutos_cc as the original does (no
    switch), with -H, with -F and with MUTOS_MODE=corrected, c1's code
    ("cc -S") executed by tests/mutos_cc/fuzz/x86sim.py: main()'s value
    must be the original's as pinned here (a wrong value, the message
    x86sim.py stops with, or - "cc: ..." - the message with which c1
    refuses the program, as the port does where the original's result
    depends on state it does not model) without a switch and with -H,
    and C's with -F; -F and MUTOS_MODE=corrected must give the same code,
    and so must -H and no switch. A correction of libc.a's runtime that
    c1 re-enacts in its folding has a trigger whose constants c1 folds:
    with -F the value C gives, which is what the corrected copy of libc.a
    computes at run time. With --realbin the programs are also linked ("cc -o"
    and, through c2, "cc -O -o") against the MUTOS root's crt0.o and
    libc.a and run under tests/realbin/mutosemu.py (which needs the
    Python module unicorn): with -F the exit status must be C's value
    (modulo 256) with and without c2, without a switch the original's
    where one is pinned.
  * The triggers of ld's corrections: links run by mutos_ld without a
    switch, with -H, with -F and with MUTOS_MODE=corrected. Without a
    switch and with -H the link must end as the original's does, as
    pinned here (its exit status, whether the output is made executable,
    its messages), with -F and MUTOS_MODE=corrected as pinned for the
    corrected mode; the output's bytes are the same in all four. Every
    corrected run must also end as a link must: exit status 0 exactly
    when every message is a warning, the output executable exactly then,
    and then as long as its header says. With --realbin the emulated
    original ld runs each trigger too, and must end as mutos_ld does
    without a switch (tests/realbin/ldcmp.py's compare()).
  * A fuzz run: tests/mutos_cc/fuzz/fuzz_c.py --mode corrected on fixed
    seeds, once with arrays and once with longs - every program must
    compute what C says unless it fails through a bug of the original the
    corrected mode does not correct yet (fuzz_c.py tallies those by bug).

Every rule of docs/RULES.md that is active and corrects a bug of c1 or
ld must have a trigger here, one at least that the correction changes.

Usage:
    check_corrected.py [--cc PATH] [--realbin] [--fuzz N]

Exit status 1 if anything differs. Python 3; --realbin needs unicorn.
"""
import argparse
import os
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
sys.path.insert(0, HERE)
import check_accounting  # noqa: E402

CC = os.path.join(REPO, 'src', 'mutos_cc', 'mutos_cc')
LD = os.path.join(REPO, 'src', 'mutos_ld', 'mutos_ld')
MUTOSEMU = os.path.join(REPO, 'tests', 'realbin', 'mutosemu.py')

# (rule, name, source, C's value of main(), the original's: a value, a
# piece of the message x86sim.py stops with, or "cc: " and a piece of the
# message c1 refuses the program with; for the emulated run of the linked
# program, the original's exit status - or a pair, without and with c2,
# where c2's code leaves other stale flags - or None, not pinned)
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
    ('L-01', 'd = 1001, double g = 777, f = 4097 (a float): ints c1 folds to doubles',
     '''double d, g = 777;
float f;
main()
{
	d = 1001;
	f = 4097;
	return (d == 1001.0) + (g == 777.0) * 2 + (f * 2 == 8194.0) * 4;
}
''', 7, 4, 4),
    ('L-03', 'int i = -32768.0, j = -32768.75, k = -32767.5: doubles c1 folds to ints',
     '''int i = -32768.0, j = -32768.75, k = -32767.5;
main()
{
	return (i == -32767 - 1) + (j == -32767 - 1) * 2 + (k == -32767) * 4;
}
''', 7, 'cc: dtoi of a magnitude of 2**15 or more', None),
    ('L-04', '99999L / 100000, 327680L / -3, 0x7fffffffL / 0177777, (-2147483647L - 1) % 32768 '
     'folded, in initializers and in statements',
     '''long a = 99999L / 100000, b = 327680L / -3;
main()
{
	long c, e;

	c = 0x7fffffffL / 0177777;
	e = (-2147483647L - 1) % 32768;
	return (a == 0) + (b == -109226L) * 2 + (c == -2147483647L) * 4 + (e == 0) * 8;
}
''', 15, 0, 0),
    ('L-05', 'double d = 4398046511105e4 (an exact double), p = 1.26765060022822940e+30 (2**100): '
     'texts c1 converts',
     '''double d = 4398046511105e4, p = 1.26765060022822940e+30;
main()
{
	double t;
	int i;

	t = 1.0;
	for (i = 0; i < 100; i++)
		t = t * 2.0;
	return (int)(d - 43980465111049000.) + (p == t) * 10000;
}
''', 11000, 984, 984 & 0xff),
    ('L-11', 'long l = 1.5e9, m = -2147483648.5, n = -1.25e9: doubles c1 folds to longs',
     '''long l = 1.5e9, m = -2147483648.5, n = -1.25e9;
main()
{
	return (l == 1500000000L) + (m == -2147483647L - 1) * 2 + (n == -1250000000L) * 4;
}
''', 7, 'cc: ftol overflows (SIGFPE)', None),
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
    if isinstance(want, str) and want.startswith('cc: '):
        return isinstance(got, str) and got.startswith('cc: ') and want[4:] in got
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
        # "cc: ...": the original's program is refused with that message
        refuse = want_orig[4:] if isinstance(want_orig, str) and \
            want_orig.startswith('cc: ') else None
        with tempfile.TemporaryDirectory(prefix='corr') as d:
            runs = {}
            for tag, opts, mode in (('none', [], None), ('-H', ['-H'], None),
                                    ('-F', ['-F'], None),
                                    ('MUTOS_MODE', [], 'corrected')):
                st, msgs, text = compile_s(cc, d, src, opts, mode)
                if st != 0 and refuse is not None and tag in ('none', '-H') \
                        and refuse in msgs:
                    text = 'cc: ' + msgs.strip()
                elif st != 0:
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
            got_orig = runs['none'] if runs['none'].startswith('cc: ') else \
                simulate(runs['none'])
            got_c = simulate(runs['-F'])
            if not matches(got_orig, want_orig):
                problems.append('%s %s: without a switch %r, pinned %r'
                                % (rule, name, got_orig, want_orig))
            if not matches(got_c, want_c):
                problems.append('%s %s: with -F %r, C says %r' % (rule, name, got_c, want_c))
            line = '%s %s: original %s, -F %s' % (
                rule, name, 'refused' if refuse is not None else got_orig, got_c)
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


# ---- ld's corrections ------------------------------------------------------
#
# A link ends right when its exit status is 0 exactly if every message is a
# warning ("... (warning)"), the output is made executable exactly then,
# and an executable is as long as its header says. Each trigger pins how
# the original's link ends - (exit status, executable, messages) - and how
# the corrected mode's does.

def ld_triggers_cases():
    """[(rule, name, argv, files, libt (the root's lib/libt.a, out of date,
    or None), the original's end, the corrected mode's end)]."""
    w = lambda *v: struct.pack('<%dH' % len(v), *v)
    mk = check_accounting.mkobj
    main_o = mk(text=w(0xe8, 0, 0x90c3), syms=[('_main', 0x22, 0), ('_foo', 0x20, 0)])
    foo_o = mk(text=w(0x90c3), data=w(1, 2), syms=[('_foo', 0x22, 0)])
    nr_o = mk(text=w(0x90c3), syms=[('_nr', 0x22, 0)], flag=1)
    nr2_o = mk(text=w(0x90c3), syms=[('_nr2', 0x22, 0)], flag=1)
    files = {'main.o': main_o, 'foo.o': foo_o,
             'p.a': check_accounting.mkar([('nr.o', nr_o)]),
             'q.a': check_accounting.mkar([('nr2.o', nr2_o)])}
    libt = check_accounting.mkranlib([('foo.o', foo_o)])
    nr = b'p.a(nr.o): No relocation bits\n'
    nr2 = b'q.a(nr2.o): No relocation bits\n'
    ood = b'/lib/libt.a(__.SYMDEF): out of date (warning)\n'
    return [
        ('LD-06', 'an error, then a warning', ['main.o', 'foo.o', 'p.a', '-lt'],
         files, libt, (0, True, b'ld:' + nr + ood), (1, False, b'ld:' + nr + ood)),
        ('LD-06', 'an error, then the warning twice',
         ['main.o', 'foo.o', 'p.a', '-lt', '-lt'], files, libt,
         (0, True, b'ld:' + nr + ood + b'ld:' + ood), (1, False, b'ld:' + nr + ood + ood)),
        ('LD-06', 'an error, a warning, an error', ['main.o', 'foo.o', 'p.a', '-lt', 'q.a'],
         files, libt, (1, False, b'ld:' + nr + ood + b'ld:' + nr2),
         (1, False, b'ld:' + nr + ood + nr2)),
        ('LD-06', 'a warning, then an error (as the original)',
         ['main.o', 'foo.o', '-lt', 'p.a'], files, libt,
         (1, False, b'ld:' + ood + b'ld:' + nr), (1, False, b'ld:' + ood + b'ld:' + nr)),
        ('LD-06', 'a warning alone (as the original)', ['main.o', 'foo.o', '-lt'],
         files, libt, (0, True, b'ld:' + ood), (0, True, b'ld:' + ood)),
    ] + ld_text_cases(files, nr)


def ld_text_cases(files, nr):
    """The triggers of LD-08 and LD-09: texts at 64K."""
    def text(n, name):
        return check_accounting.mkobj(text=b'\x90' * n, syms=[(name, 0x22, 0)])

    big = {'big.o': text(65530, '_big'), 'b28.o': text(65528, '_b28'),
           'a.o': text(30000, '_a'), 'b.o': text(30000, '_b'), 'c.o': text(5530, '_c'),
           'c68.o': text(5468, '_c68'), 'c66.o': text(5466, '_c66'), 'p.a': files['p.a']}
    ovf = b'text overflow\n'
    return [
        ('LD-08', 'a text of 65,530 bytes', ['big.o'], big, None,
         (0, True, b''), (1, False, b'ld:' + ovf)),
        ('LD-08', 'texts of 30,000, 30,000 and 5,530 bytes', ['a.o', 'b.o', 'c.o'], big, None,
         (0, True, b''), (1, False, b'ld:' + ovf)),
        ('LD-08', 'a text of 65,530 bytes with -i', ['-i', 'big.o'], big, None,
         (0, True, b''), (1, False, b'ld:' + ovf)),
        ('LD-08', 'a text of 65,530 bytes after an error', ['big.o', 'p.a'], big, None,
         (1, False, b'ld:' + nr), (1, False, b'ld:' + nr + ovf)),
        ('LD-08', 'a text of 65,528 bytes (as the original)', ['b28.o'], big, None,
         (0, True, b''), (0, True, b'')),
        ('LD-09', 'a text of 65,528 bytes with -i', ['-i', 'b28.o'], big, None,
         (0, True, b''), (1, False, b'ld:' + ovf)),
        ('LD-09', 'texts of 30,000, 30,000 and 5,468 bytes with -n', ['-n', 'a.o', 'b.o', 'c68.o'],
         big, None, (0, True, b''), (1, False, b'ld:' + ovf)),
        ('LD-09', 'texts of 30,000, 30,000 and 5,466 bytes with -i (as the original)',
         ['-i', 'a.o', 'b.o', 'c66.o'], big, None, (0, True, b''), (0, True, b'')),
    ]


def ld_end(res):
    """(exit status, executable, messages) of check_accounting.ld_run()'s
    result."""
    st, out, err, data, mode = res
    return st, mode is not None and bool(mode & 0o111), out + err


def ld_ends_right(res):
    """Why the link does not end as a link must, or None."""
    st, out, err, data, mode = res
    lines = [ln for ln in out.split(b'\n') if ln]
    warnings_only = all(ln.endswith(b'(warning)') for ln in lines)
    executable = mode is not None and bool(mode & 0o111)
    if (st == 0) != warnings_only:
        return 'exit status %d with %s' % (st, 'warnings only' if warnings_only
                                           else 'an error message')
    if executable != (st == 0):
        return 'exit status %d, the output %sexecutable' % (st, '' if executable else 'not ')
    if st == 0 and data is not None and len(data) >= 16:
        magic, t, d, b, sz, e, u, rel = struct.unpack_from('<8H', data)
        want = 16 + t + d + (0 if rel & 1 else t + d) + sz
        if magic in (0o407, 0o410, 0o411) and len(data) != want:
            return 'an executable of %d bytes, its header says %d' % (len(data), want)
    return None


def ld_emulated(argv, files, libt):
    """The emulated original ld against mutos_ld without a switch, on the
    trigger (ldcmp.py's compare()): its kind ('same' or another)."""
    sys.path.insert(0, os.path.join(REPO, 'tests', 'realbin'))
    import ldcmp
    with tempfile.TemporaryDirectory(prefix='corrld') as tmp:
        src = os.path.join(tmp, 'src')
        os.makedirs(src)
        for n, data in files.items():
            with open(os.path.join(src, n), 'wb') as f:
                f.write(data)
        extra = [] if libt is None else \
            [('lib/libt.a', libt, check_accounting.LIBC_SYMDEF_TIME + 9)]
        root = ldcmp.make_root(os.path.join(tmp, 'root'), extra)
        old = os.environ.pop('MUTOS_MODE', None)
        try:
            kind, detail = ldcmp.compare(argv, src, root, LD)
        finally:
            if old is not None:
                os.environ['MUTOS_MODE'] = old
        return kind if kind == 'same' else '%s: %s' % (kind, detail)


def ld_triggers(ld, realbin):
    problems, lines = [], []
    for rule, name, argv, files, libt, want_orig, want_corr in ld_triggers_cases():
        runs, logs = {}, {}
        for tag, opts, mode in (('none', [], None), ('-H', ['-H'], None),
                                ('-F', ['-F'], None), ('MUTOS_MODE', [], 'corrected')):
            runs[tag], logs[tag] = check_accounting.ld_run(ld, files, libt, opts + argv, mode)
        what = '%s %s (ld %s)' % (rule, name, ' '.join(argv))
        # the rule log: a trigger the correction changes meets the rule
        # without a switch and applies it with -F; the others do neither
        changes = want_orig != want_corr
        for tag, kind in (('none', 'met'), ('-F', 'applied')):
            has = any(x[0] == 'ld' and x[1] == rule and x[2] == kind for x in logs[tag])
            if has != changes:
                problems.append('%s, %s: the rule log %s "%s %s"'
                                % (what, tag, 'lacks' if changes else 'has', rule, kind))
        for tag in ('none', '-H'):
            if ld_end(runs[tag]) != want_orig:
                problems.append('%s, %s: %r, the original\'s pinned %r'
                                % (what, tag, ld_end(runs[tag]), want_orig))
        for tag in ('-F', 'MUTOS_MODE'):
            if ld_end(runs[tag]) != want_corr:
                problems.append('%s, %s: %r, pinned %r' % (what, tag, ld_end(runs[tag]), want_corr))
            why = ld_ends_right(runs[tag])
            if why:
                problems.append('%s, %s: %s' % (what, tag, why))
        if len({r[3] for r in runs.values()}) != 1:
            problems.append('%s: the output\'s bytes depend on the mode' % what)
        line = '%s %s: original %r, -F %r' % (rule, name, want_orig[:2], want_corr[:2])
        if realbin:
            kind = ld_emulated(argv, files, libt)
            if kind != 'same':
                problems.append('%s: the emulated original against mutos_ld: %s' % (what, kind))
            line += '; the emulated original the same'
        lines.append(line)
    return problems, lines


def active_corrections(tool):
    """The rules of docs/RULES.md's register that are active corrections of
    tool (c1, ld)."""
    out = []
    with open(os.path.join(REPO, 'docs', 'RULES.md'), encoding='utf-8') as f:
        for line in f:
            cells = [c.strip().strip('`') for c in line.strip().strip('|').split('|')]
            if len(cells) >= 4 and re.fullmatch(r'[A-Z0-9]+-\d+', cells[0]) and \
                    cells[1] == tool and cells[2] == '-F' and cells[3] == 'active':
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
    ap.add_argument('--ld', default=LD)
    ap.add_argument('--realbin', action='store_true',
                    help='also link the triggers and run them under mutosemu.py, '
                    'and run ld\'s triggers through the emulated original ld')
    ap.add_argument('--fuzz', type=int, default=1000, metavar='N',
                    help='programs per fuzz run (0: none)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    problems, lines = triggers(args.cc, args.realbin)
    p, ln = ld_triggers(os.path.abspath(args.ld), args.realbin)
    problems += p
    lines += ln
    rules = active_corrections('c1')
    for rule in rules:
        if not any(c[0] == rule for c in CASES):
            problems.append('%s is active in docs/RULES.md but has no trigger here' % rule)
    for c in CASES:
        if c[0] not in rules:
            problems.append('%s has a trigger here but is no active correction of c1 in '
                            'docs/RULES.md' % c[0])
    ld_rules = active_corrections('ld')
    ld_cases = ld_triggers_cases()
    for rule in ld_rules:
        if not any(c[0] == rule and c[5] != c[6] for c in ld_cases):
            problems.append('%s is active in docs/RULES.md but has no trigger here that '
                            'it changes' % rule)
    for rule in sorted({c[0] for c in ld_cases}):
        if rule not in ld_rules:
            problems.append('%s has a trigger here but is no active correction of ld in '
                            'docs/RULES.md' % rule)
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
    print('check_corrected: %d trigger(s) of %d correction(s) of c1 (%s): the original\'s '
          'values without a switch, C\'s with -F%s; %d trigger(s) of %d of ld (%s): the original\'s '
          'end without a switch, a link\'s with -F%s; fuzz_c.py --mode corrected: %s'
          % (len(CASES), len(rules), ', '.join(rules),
             ', linked and emulated with and without c2' if args.realbin else '',
             len(ld_cases), len(ld_rules), ', '.join(ld_rules),
             ', the emulated original\'s the same' if args.realbin else '',
             '%d + %d programs, every failure a bug not corrected yet' % (args.fuzz, args.fuzz)
             if args.fuzz else 'not run'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
