#!/usr/bin/env python3
"""v30time.py - the timing kit of step V4b: each 80186/V30 form of -1,
and the 8086 sequence it replaces, timed alone on the owner's A7100 (NEC
V30 at 15 MHz, memory with 3 or 4 wait states).

Step V4's timing programs (v30kit.py), under the bus model of v30bus.py,
came out as measured but for one: bimul.v, the loop of products as V30
code, took 13.6 s where the NEC manual's 28-34 clocks for "imul r,r,imm"
(36-42 with a word) give 10.8-12.2 s. This kit times the forms one by
one, so that the clocks of each are measured instead of fitted.

A measurement runs a block of copies of one unit - an instruction, or
the few instructions a rule writes or replaces - in a loop: once with
K1 = 8 copies, once with K2 = 72, each timed by times() in ticks of 1/20
s (MUTOS's HZ). The difference is 64 copies, the loop - its counter, its
jump, the queue filled again after the jump - cancelled out:

    clocks of a unit = (t2 - t1) / 20 * F / (iterations * 64)

with F the processor's clock (15 MHz; --mhz). The units:

  - for the clock and the bus: nop, "inc ax" (1 byte), "mov ax,bx" (2
    bytes), "mov ax,#n" (3), "push ax"/"pop ax", "sal ax,cl" by 15 and
    by 3, "sal ax,*15." and "sal ax,*3.", "aam", "mov ax,bx"/"mul cx".
    The short ones wait for the bus (a word fetched per bus cycle of 4 +
    w clocks), the long ones (a shift by 15: 22 clocks, aam: 15, mul:
    29-30) for the execution unit - they show the clock and w;
  - "imul ax,bx,*n." (the form of -1, a source other than its target,
    so that the data stay the same) for nine bytes n and eight values;
    "imul ax,bx,#n" for five words; "mov ax,bx" / "imul cx" with cx = n
    (the 8086's one-operand imul) for the same data; "mov ax,bx" / "imul
    ax,*n." - the form as c1 writes it, its source its target -, and
    "mov ax,bx" / "mov cx,*n." / "imul cx", the 8086 code it replaces,
    for eight values - their difference is what V30-IMUL gains per
    product;
  - the body of bimul's inner loop, as 8086 code and as V30 code (the
    registers renamed: s in BX, i in SI), i from 100 on.

Every routine returns AX; what the K1 and the K2 routine return is
computed here (16-bit arithmetic) and must be what vtime.v prints - on
the host under tests/realbin/mutosemu.py (times() is 0 there), with the
iterations divided by --scale, and on the machine, in full. The kit is
flat - no subdirectory: a copy of the kit onto the CF card left step V4's
expect/ empty. Its script, v30time.sh, compares the values with cmp and
copies the timings into the log.

Each measurement's iterations are set from the bus model's clocks (the
manual's figures, 3 wait states, 15 MHz) so that its K2 run takes about 9
seconds (5 for the clock and the bus); the whole run about 13 minutes.

Usage:
    v30time.py [--out DIR] [--tar FILE] [--check] [--scale N]
    v30time.py --analyze LOG [--mhz F] [--wait W]

--out: the kit's directory (default "v30time"; it must not exist or be
empty). --tar: also a tar file of the kit. --check: the kit built and
checked, none written (in "make check-realbin"). --scale: the divisor of
the iterations for the run under the emulator (default 500). --analyze:
the clocks of each unit from a log (or vtime.tim) the machine wrote,
beside the manual's and the bus model's. Exit status 0 if the values
hold, 1 otherwise. Requires the Python modules unicorn and capstone and a
built tree ("make").
"""

import io
import math
import os
import re
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
    sys.exit('v30time.py: needs the Python module "unicorn" (pip install unicorn)')
try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
except ImportError:
    sys.exit('v30time.py: needs the Python module "capstone" (pip install capstone)')
import v30bus  # noqa: E402
import v30kit  # noqa: E402

CC, AS = v30kit.CC, v30kit.AS
K1, K2 = 8, 72
TICKS = 20              # MUTOS's HZ: times() counts in 1/20 s
MHZ, WAIT = 15.0, 3     # the owner's V30 (2026-11-21)
X8 = (0, 1, 0x7f, 999, 0x1234, 0x7fff, 0x8000, 0xffff)
N8 = (0, 1, 7, 10, 100, 127, -1, -7, -128)
NW = (300, 1000, 12345, -300, -32768)


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def u16(v):
    return v & 0xffff


def num(n):
    return '%d.' % n


def nname(n):
    """A number in a unit's name: m for minus."""
    return 'm%d' % -n if n < 0 else '%d' % n


def repeat(f):
    """A result after k copies of a unit that maps AX to AX."""
    def r(x, m, k):
        v = x
        for _ in range(k):
            v = f(v) & 0xffff
        return v
    return r


def aam(v):
    al = v & 0xff
    return (al // 10) << 8 | al % 10


def body(x, m, k):
    """bimul's inner loop, k times from s = 0, i = x: s at the end."""
    s, i = 0, x
    for _ in range(k):
        s = u16(s + i * 10)
        s ^= u16(i * 300)
        s = u16(s + (s16(s) >> 1) * 7)
        s ^= u16((u16(i * 10) + s) * 100)
        i = u16(i + 1)
    return s


BODY_V = ['mov\tax,si', 'imul\tax,*10.', 'add\tbx,ax', 'mov\tax,si', 'imul\tax,#300.',
          'xor\tbx,ax', 'mov\tax,bx', 'sar\tax,*1', 'imul\tax,*7.', 'add\tbx,ax',
          'mov\tax,si', 'imul\tax,*10.', 'add\tax,bx', 'imul\tax,*100.', 'xor\tbx,ax',
          'inc\tsi']
BODY_8 = ['mov\tax,si', 'mov\tcx,*10.', 'imul\tcx', 'add\tbx,ax', 'mov\tax,si',
          'mov\tcx,#300.', 'imul\tcx', 'xor\tbx,ax', 'mov\tax,bx', 'sar\tax,*1',
          'mov\tcx,*7.', 'imul\tcx', 'add\tbx,ax', 'mov\tax,si', 'mov\tcx,*10.',
          'imul\tcx', 'add\tax,bx', 'mov\tcx,*100.', 'imul\tcx', 'xor\tbx,ax',
          'inc\tsi']
RESET_AX = ['mov\tax,bx']
RESET_BODY = ['mov\tsi,*6.(bp)', 'sub\tbx,bx']


class Unit:
    """A unit: what is done once before the loop (pre), at the start of
    each turn (reset), the lines copied K times, CL for a shift by CL,
    the result after k copies, f(x, m, k), and the group it belongs to."""

    def __init__(self, name, group, lines, result, pre=(), reset=(), cl=None, post=()):
        self.name, self.group, self.lines = name, group, list(lines)
        self.result, self.pre, self.reset = result, list(pre), list(reset)
        self.cl, self.post = cl, list(post)


def units():
    us = [
        Unit('nop', 'bus', ['nop'], repeat(lambda v: v), reset=RESET_AX),
        Unit('inc', 'bus', ['inc\tax'], repeat(lambda v: v + 1), reset=RESET_AX),
        Unit('movrr', 'bus', ['mov\tax,bx'], lambda x, m, k: x),
        Unit('movri', 'bus', ['mov\tax,#4660.'], lambda x, m, k: 0x1234),
        Unit('pushpop', 'bus', ['push\tax', 'pop\tax'], repeat(lambda v: v), reset=RESET_AX),
        Unit('salcl15', 'clock', ['sal\tax,cl'], repeat(lambda v: v << 15),
             pre=['mov\tcx,*15.'], reset=RESET_AX, cl=15),
        Unit('salcl3', 'clock', ['sal\tax,cl'], repeat(lambda v: v << 3),
             pre=['mov\tcx,*3.'], reset=RESET_AX, cl=3),
        Unit('sali15', 'clock', ['sal\tax,*15.'], repeat(lambda v: v << 15), reset=RESET_AX),
        Unit('sali3', 'clock', ['sal\tax,*3.'], repeat(lambda v: v << 3), reset=RESET_AX),
        Unit('aam', 'clock', ['aam'], repeat(aam), reset=RESET_AX),
        Unit('mul', 'clock', ['mov\tax,bx', 'mul\tcx'], lambda x, m, k: u16(x * 10),
             pre=['mov\tcx,*10.']),
    ]
    for n in N8:
        us.append(Unit('i3b' + nname(n), 'imul', ['imul\tax,bx,*%s' % num(n)],
                       lambda x, m, k, n=n: u16(s16(x) * n)))
    for n in NW:
        us.append(Unit('i3w' + nname(n), 'imul', ['imul\tax,bx,#%s' % num(n)],
                       lambda x, m, k, n=n: u16(s16(x) * n)))
    us.append(Unit('i1', 'imul', ['mov\tax,bx', 'imul\tcx'],
                   lambda x, m, k: u16(s16(x) * s16(m)), pre=['mov\tcx,*8.(bp)']))
    us.append(Unit('i2b10', 'imul', ['mov\tax,bx', 'imul\tax,*10.'],
                   lambda x, m, k: u16(s16(x) * 10)))
    us.append(Unit('i2w300', 'imul', ['mov\tax,bx', 'imul\tax,#300.'],
                   lambda x, m, k: u16(s16(x) * 300)))
    us.append(Unit('i8b10', 'imul', ['mov\tax,bx', 'mov\tcx,*10.', 'imul\tcx'],
                   lambda x, m, k: u16(s16(x) * 10)))
    us.append(Unit('i8w300', 'imul', ['mov\tax,bx', 'mov\tcx,#300.', 'imul\tcx'],
                   lambda x, m, k: u16(s16(x) * 300)))
    us.append(Unit('body8', 'body', BODY_8, body, reset=RESET_BODY, post=['mov\tax,bx']))
    us.append(Unit('bodyv', 'body', BODY_V, body, reset=RESET_BODY, post=['mov\tax,bx']))
    for i, u in enumerate(us):
        u.index = i
    return us


def measurements():
    """(unit, x, m) in the order they run."""
    out = [(u, 0x1234, 0) for u in ('nop', 'inc', 'movrr', 'movri', 'pushpop', 'salcl15',
                                    'salcl3', 'sali15', 'sali3', 'aam', 'mul')]
    out += [('i3b10', x, 0) for x in X8]
    out += [('i3b' + nname(n), 999, 0) for n in N8 if n != 10]
    out += [('i3w300', x, 0) for x in X8]
    out += [('i3w' + nname(n), 999, 0) for n in NW if n != 300]
    out += [('i1', x, 10) for x in X8]
    out += [('i1', 999, n) for n in N8 + NW if n != 10]
    out += [('i2b10', x, 0) for x in X8]
    out += [('i2w300', 999, 0)]
    out += [('i8b10', x, 0) for x in X8]
    out += [('i8w300', 999, 0)]
    out += [('body8', 100, 0), ('bodyv', 100, 0)]
    return out


def routine(label, u, k):
    lines = ['.even', label + ':\tpush\tbp', '\tmov\tbp,sp', '\tpush\tsi', '\tpush\tdi',
             '\tmov\tdi,*4.(bp)', '\tmov\tbx,*6.(bp)']
    lines += ['\t' + ln for ln in u.pre]
    loop = 'L' + label[1:]
    first = u.reset + u.lines * k
    lines.append(loop + ':\t' + first[0])
    lines += ['\t' + ln for ln in first[1:]]
    lines += ['\tdec\tdi', '\tbne\t' + loop]
    lines += ['\t' + ln for ln in u.post]
    lines += ['\tpop\tdi', '\tpop\tsi', '\tpop\tbp', '\tret']
    return '\n'.join(lines) + '\n'


def label(u, k):
    """A routine's name - eight characters at most, as a symbol has."""
    return '_t%d%s' % (u.index, 'a' if k == K1 else 'b')


def modules(us):
    """The routines in modules of at most about 12K of text each (the
    original ld refuses an object of 32K or more, LD-07)."""
    mods, cur, size = [], [], 0
    for u in us:
        est = sum(4 for _ in u.lines) * (K1 + K2) + 60
        if cur and size + est > 12000:
            mods.append(cur)
            cur, size = [], 0
        cur.append(u)
        size += est
    mods.append(cur)
    out = {}
    for i, mus in enumerate(mods):
        t = ['| vt%d.s - made by tests/rules/v30time.py: timing routines, each a\n'
             '| loop of K copies of a unit. Assembled with mutos_as -1.\n' % i]
        t += ['.globl\t%s, %s\n' % (label(u, K1), label(u, K2)) for u in mus]
        t.append('.text\n')
        for u in mus:
            for k in (K1, K2):
                t.append(routine(label(u, k), u, k))
        out['vt%d.s' % i] = ''.join(t)
    return out


DRIVER = '''/* vtime.c - made by tests/rules/v30time.py: the driver of the timings.
 * Each row: the routine of K1 = %(k1)d copies, that of K2 = %(k2)d, x (BX),
 * m (the multiplier of i1), the iterations of a call and the calls. */
#include <stdio.h>
#include <sys/types.h>
#include <sys/times.h>
extern int %(externs)s;
struct te {
	int (*f1)();
	int (*f2)();
	int x;
	int m;
	unsigned n;
	int reps;
	char *nm;
} ttab[] = {
%(rows)s
	{ 0 }
};

main(argc, argv)
char **argv;
{
	register struct te *p;
	register int r;
	int a1, a2, scale, reps;
	unsigned n;
	long u0, u1, u2;
	struct tms t;

	scale = argc > 1 ? atoi(argv[1]) : 1;
	for (p = ttab; p->f1; p++) {
		n = p->n;
		reps = p->reps;
		if (scale > 1) {
			n = n / scale + 1;
			reps = 1;
		}
		times(&t);
		u0 = t.tms_utime;
		for (r = 0; r < reps; r++)
			a1 = (*p->f1)(n, p->x, p->m);
		times(&t);
		u1 = t.tms_utime;
		for (r = 0; r < reps; r++)
			a2 = (*p->f2)(n, p->x, p->m);
		times(&t);
		u2 = t.tms_utime;
		printf("%%s %%x %%x %%x %%x\\n", p->nm, p->x, p->m, a1, a2);
		fprintf(stderr, "%%s %%x %%x %%u %%d %%D %%D\\n", p->nm, p->x, p->m,
			n, reps, u1 - u0, u2 - u1);
	}
	return 0;
}
'''


def driver(us, plan):
    externs = ', '.join('%s()' % label(u, k)[1:] for u in us for k in (K1, K2))
    rows = []
    for (name, x, m), (n, reps) in zip(measurements(), plan):
        u = next(v for v in us if v.name == name)
        rows.append('\t{ %s, %s, 0%o, 0%o, %d, %d, "%s" },' % (
            label(u, K1)[1:], label(u, K2)[1:], u16(x), u16(m), n, reps, name))
    return DRIVER % {'k1': K1, 'k2': K2, 'externs': externs, 'rows': '\n'.join(rows)}


def expected(us):
    out = []
    for name, x, m in measurements():
        u = next(v for v in us if v.name == name)
        out.append('%s %x %x %x %x\n' % (name, u16(x), u16(m), u.result(x, m, K1),
                                         u.result(x, m, K2)))
    return ''.join(out)


# ---- the bus model's clocks of a unit ---------------------------------------

def decode(text, start):
    """The instructions of a routine from its start to its ret:
    [(offset, mnemonic, operands, size)]."""
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    out, off = [], start
    while True:
        i = next(md.disasm(text[off:off + 16], off))
        out.append((off, i.mnemonic, i.op_str, i.size))
        off += i.size
        if i.mnemonic == 'ret':
            return out


def data_cycles(mnem, ops):
    if mnem in ('push', 'pop'):
        return 1 + ('[' in ops)
    return 1 if '[' in ops else 0


def turn_clocks(text, start, cl, wait, high):
    """The bus model's clocks of a turn of the routine's loop (and the
    EU's alone), in the steady state: three turns less two."""
    ins = decode(text, start)
    jmp = [x for x in ins if x[1] == 'jmp'][-1]
    top = int(jmp[2], 0)
    turn = [x for x in ins if top <= x[0] <= jmp[0]]
    dec = {x[0]: (x[1], x[2], x[3]) for x in turn}
    seq1 = [[x[0], data_cycles(x[1], x[2]), cl if x[2].endswith(', cl') else None]
            for x in turn]
    got = []
    for bus in (True, False):
        c = [v30bus.clocks(dec, seq1 * r, wait, high, 0, bus) for r in (2, 3)]
        got.append(c[1] - c[0])
    return got


def predict(image, syms, us, wait):
    """{unit: (lowest, highest) clocks of a copy by the bus model, and by
    the EU alone; the clocks of a K2 turn (the average)}."""
    text = image[16:16 + int.from_bytes(image[2:4], 'little')]
    out = {}
    for u in us:
        per = {}
        for high in (False, True):
            t1 = turn_clocks(text, syms[label(u, K1)], u.cl, wait, high)
            t2 = turn_clocks(text, syms[label(u, K2)], u.cl, wait, high)
            per[high] = ([(b - a) / (K2 - K1) for a, b in zip(t1, t2)], t2[0])
        out[u.name] = dict(bus=(per[False][0][0], per[True][0][0]),
                           eu=(per[False][0][1], per[True][0][1]),
                           turn2=(per[False][1] + per[True][1]) / 2)
    return out


def plan_of(pred):
    """(iterations of a call, calls) of each measurement: its K2 run about
    9 seconds (5 for the clock and the bus) at 15 MHz by the bus model."""
    out = []
    for name, x, m in measurements():
        p = pred[name]
        secs = 5 if name in ('nop', 'inc', 'movrr', 'movri', 'pushpop', 'salcl15',
                             'salcl3', 'sali15', 'sali3', 'aam', 'mul') else 9
        total = max(1, round(secs * MHZ * 1e6 / p['turn2']))
        reps = math.ceil(total / 65535)
        out.append((round(total / reps), reps))
    return out


# ---- the kit ----------------------------------------------------------------

SCRIPT = ''': v30time.sh - step V4b of the cross toolchain: each 80186/V30 form
: of -1, and the 8086 code it replaces, timed alone. It needs the NEC
: V30. vtime.v prints its values to vtime.out, compared with vtime.exp,
: and its timings to vtime.tim, copied into the log. Nothing outside
: this directory is changed. Run it here with sh v30time.sh, see README.
: These comment lines are commands of their own: no redirection in them.
chmod 755 vtime.v
echo vtime.v
./vtime.v >vtime.out 2>vtime.tim
echo vtime.v: exit status $?
if cmp vtime.out vtime.exp
then
	echo values: as expected
else
	echo values: DIFFERENT
fi
echo timings:
cat vtime.tim
'''

README = '''The timing kit of step V4b of the MUTOS 1700 cross toolchain
=============================================================

Made by tests/rules/v30time.py. Each form the switch -1 writes, and the
8086 code it replaces, timed alone on the NEC V30, so that its clocks are
measured: step V4's loop of products was slower than the NEC manual's
clocks for "imul r,r,imm" allow.

vtime.v NEEDS THE NEC V30 (or an 80186). Nothing outside this directory
is changed. The kit has no subdirectory: copy all its files.

On MUTOS 1700, in this directory:

    sh v30time.sh >log 2>&1

It takes about %(minutes)d minutes, with nothing else running. Bring back the
log: it ends with "values: as expected" and the timings, a line for
each of the %(nmeas)d measurements:

    unit  x(hex)  m(hex)  iterations  calls  ticks(K1)  ticks(K2)

A measurement runs a loop of K1 = %(k1)d copies of a unit, then one of K2 = %(k2)d,
each timed by times() in ticks of 1/20 s; their difference gives the
clocks of one copy (tests/rules/v30time.py --analyze log).

The units: nop, inc, movrr, movri, pushpop (the bus); salcl15, salcl3,
sali15, sali3, aam, mul (the clock); i3bN "imul ax,bx,*N.", i3wN "imul
ax,bx,#N", i1 "mov ax,bx" / "imul cx" with cx = m, i2b10 / i2w300 "mov
ax,bx" / "imul ax,*10." (#300.) as c1 writes it, i8b10 / i8w300 "mov
ax,bx" / "mov cx,*10." (#300.) / "imul cx", the 8086 code it replaces;
body8, bodyv the body of step V4's bimul loop as 8086 and as V30 code.

The sources are vtime.c and vt*.s, for reading; the script does not use
them.
'''


def build(work, bad, scale):
    us = units()
    mods = modules(us)
    objs = []
    for name in sorted(mods):
        v30kit.write(os.path.join(work, name), mods[name])
        obj = name[:-2] + '.o'
        st, msgs = v30kit.run([AS, '-1', '-o', obj, name], work)
        if st or v30kit.counts(msgs) is None:
            bad.append('%s: as -1: exit %d, %r' % (name, st, msgs))
            return None
        objs.append(obj)
    # a first link with a plan of ones gives the routines' addresses; the
    # plan changes no address (the rows' numbers are data)
    plan = [(1, 1)] * len(measurements())
    image = None
    for _ in range(2):
        src = driver(us, plan)
        v30kit.write(os.path.join(work, 'vtime.c'), src)
        st, msgs = v30kit.run([CC, '-o', 'vtime.v', 'vtime.c'] + objs, work)
        if st or msgs:
            bad.append('vtime.v: cc: exit %d %s' % (st, msgs))
            return None
        image = v30kit.read(os.path.join(work, 'vtime.v'))
        syms = v30kit.symbols('vtime.v', work)
        pred = predict(image, syms, us, WAIT)
        new = plan_of(pred)
        if new == plan:
            break
        plan = new
    exp = expected(us)
    o, e = io.BytesIO(), io.BytesIO()
    try:
        emu = mutosemu.Emu(image, ['vtime.v', str(scale)], cwd=work, stdin=io.BytesIO(b''),
                           stdout=o, stderr=e, eof_limit=1000)
        st = emu.run(v30kit.MAX_INSNS)
    except RuntimeError as x:
        bad.append('vtime.v under the emulator: %s' % x)
        return None
    out = o.getvalue().decode('latin-1')
    if st != 0 or out != exp:
        bad.append('vtime.v under the emulator: exit %r; its values are %s the model\'s%s'
                   % (st, 'not' if out != exp else '', v30kit.first_diff(out, exp)))
    tim = e.getvalue().decode('latin-1').splitlines()
    if len(tim) != len(measurements()) or any(not ln.endswith(' 0 0') for ln in tim):
        bad.append('vtime.v under the emulator: its timings %r' % tim[:3])
    source = dict(mods)
    source['vtime.c'] = src
    secs = sum(n * r * (pred[name]['turn2'] * (1 + K1 / K2)) for (name, _, _), (n, r)
               in zip(measurements(), plan)) / (MHZ * 1e6)
    return dict(image=image, exp=exp, source=source, pred=pred, plan=plan, secs=secs,
                units=us)


def write_kit(out, kit):
    os.makedirs(out)
    with open(os.path.join(out, 'vtime.v'), 'wb') as f:
        f.write(kit['image'])
    os.chmod(os.path.join(out, 'vtime.v'), 0o755)
    v30kit.write(os.path.join(out, 'vtime.exp'), kit['exp'])
    v30kit.write(os.path.join(out, 'v30time.sh'), SCRIPT)
    for name, text in kit['source'].items():
        v30kit.write(os.path.join(out, name), text)
    v30kit.write(os.path.join(out, 'README'), README % {
        'minutes': round(kit['secs'] / 60), 'nmeas': len(measurements()), 'k1': K1, 'k2': K2})


def write_tar(out, tarname):
    """The kit's files under the directory's name, regular files only."""
    base = os.path.basename(os.path.normpath(out))
    with tarfile.open(tarname, 'w', format=tarfile.USTAR_FORMAT) as tar:
        for rel in sorted(os.listdir(out)):
            info = tar.gettarinfo(os.path.join(out, rel), arcname=base + '/' + rel)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o755 if rel.endswith('.v') else 0o644
            with open(os.path.join(out, rel), 'rb') as f:
                tar.addfile(info, f)


# ---- the analysis -----------------------------------------------------------

LINE = re.compile(r'^(\w+) ([0-9a-f]+) ([0-9a-f]+) (\d+) (\d+) (-?\d+) (-?\d+)$')


def rng(p):
    lo, hi = p
    return '%5.1f' % lo if abs(hi - lo) < 0.05 else '%4.1f-%-4.1f' % (lo, hi)


def analyze(path, mhz, wait):
    with open(path, encoding='latin-1') as f:
        rows = [LINE.match(ln.strip()) for ln in f]
    rows = [m for m in rows if m]
    if not rows:
        sys.exit('v30time.py: no timing lines in %s' % path)
    bad = []
    with tempfile.TemporaryDirectory(prefix='v30time.') as work:
        kit = build(work, bad, 500)
    if kit is None:
        sys.exit('v30time.py: the kit does not build: %s' % bad)
    pred = kit['pred']
    hz = mhz * 1e6
    got = {}
    print('v30time: clocks of a copy of each unit, at %g MHz (%d wait states in the '
          'model)' % (mhz, wait))
    print('    unit      x     m       measured (+-2 ticks)  manual (EU)  bus model')
    for m in rows:
        name, x, mm = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        n, reps, t1, t2 = (int(v) for v in m.groups()[3:])
        tick = hz / TICKS / (n * reps * (K2 - K1))
        c = (t2 - t1) * tick
        got.setdefault(name, []).append((x, mm, c))
        p = pred.get(name)
        print('    %-9s %04x  %04x  %7.2f (%.2f)       %-12s %s' % (
            name, x, mm, c, 2 * tick, rng(p['eu']) if p else '?',
            rng(p['bus']) if p else '?'))
    cal = got.get('salcl15')
    if cal:
        f = 22.0 / cal[0][2]
        print('    the clock from "sal ax,cl" by 15 (22 clocks by the manual): '
              '%.3f MHz' % (mhz * f))
    for a, b, what in (('i8b10', 'i2b10', 'a product by *10.'),
                       ('i8w300', 'i2w300', 'a product by #300.')):
        if a in got and b in got:
            ca = sum(c for _, _, c in got[a]) / len(got[a])
            cb = sum(c for _, _, c in got[b]) / len(got[b])
            print('    V30-IMUL on %s: %.1f clocks as 8086 code, %.1f as V30 code - '
                  '%.1f gained (%.0f%%)' % (what, ca, cb, ca - cb, 100 * (ca - cb) / ca))
    return 0


def main(argv):
    out, tarname, check, scale, log = 'v30time', None, False, 500, None
    mhz, wait = MHZ, WAIT
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--out' and args:
            out = args.pop(0)
        elif a == '--tar' and args:
            tarname = args.pop(0)
        elif a == '--check':
            check = True
        elif a == '--scale' and args:
            scale = int(args.pop(0))
        elif a == '--analyze' and args:
            log = args.pop(0)
        elif a == '--mhz' and args:
            mhz = float(args.pop(0))
        elif a == '--wait' and args:
            wait = int(args.pop(0))
        else:
            print(__doc__)
            return 2
    for tool in (CC, AS):
        if not os.access(tool, os.X_OK):
            sys.exit('v30time.py: %s is not built - run "make" first' % tool)
    if log:
        return analyze(log, mhz, wait)
    if not check and os.path.exists(out) and (not os.path.isdir(out) or os.listdir(out)):
        sys.exit('v30time.py: %s exists and is not an empty directory' % out)
    bad = []
    with tempfile.TemporaryDirectory(prefix='v30time.') as work:
        kit = build(work, bad, scale)
    if bad or kit is None:
        for b in bad:
            print('  WRONG ' + b)
        print('v30time: the kit does not hold - none written')
        return 1
    us = kit['units']
    print('v30time: %d units, %d measurements; under the emulator (iterations / %d) '
          'every value as computed' % (len(us), len(measurements()), scale))
    print('    unit      manual (EU)  bus model (%d wait states)' % WAIT)
    for u in us:
        p = kit['pred'][u.name]
        print('    %-9s %-12s %s' % (u.name, rng(p['eu']), rng(p['bus'])))
    print('v30time: the run takes about %.0f minutes at %g MHz by the bus model'
          % (kit['secs'] / 60, MHZ))
    if not check:
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        write_kit(out, kit)
        if tarname:
            write_tar(out, tarname)
        print('v30time: the kit is in %s%s' % (out, ' and %s' % tarname if tarname else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
