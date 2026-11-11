#!/usr/bin/env python3
"""foldcheck.py - what c1 folds is what the runtime it links computes.

c1 folds constants with re-enactments of libc.a's routines
(src/mutos_cc/c1port/c1lib.c): a long constant's / and % with aldiv's
and alrem's code, an int constant converted to a floating one with
itod's, a floating constant converted to an int or a long with dtoi's
and ftol's. In the original mode they are the original routines, bugs
included (docs/MUTOS_ORIGINAL_BUGS.md, L-01, L-03, L-04, L-11); in the
corrected mode those of the corrected copy of libc.a that the corrected
mode links (step F3b; the rules L-01, L-03, L-04 and L-11 of c1,
docs/RULES.md). So a program's folded constants must be what the same
operations give at run time - with cc against the original libc.a, with
cc -F against the copy.

For each of four kinds - long / and % (ldiv, lrem at run time), int to
double (itof), double to int (ftoi), double to long (ftol) - this
writes a program whose initializers c1 folds and whose main() computes
the same from the operands at run time and writes both with write(),
compiles and links it with the port's cc in the MUTOS root, once without
a switch and once with -F, runs it under tests/realbin/mutosemu.py and
compares the two, value for value. The operands: the edges of each
routine's paths and bugs, and random ones (--seed, -n). Where the
original's c1 refuses a constant (dtoi from 2**15, ftol from 2**30: the
original's result depends on state the port does not model, or is
SIGFPE) the original mode's program leaves it out; the corrected mode's
has it. An int-to-double constant reaches the object as c1's 18-digit
text read back by the assembler's atof(): exactly in the corrected mode,
where the double is the int, but not always in the original mode - 777
folds to 777.0000119209289... (L-01), whose text the assembler reads
back one unit lower in the last place than itof's bytes - so that kind
is compared in the corrected mode only. A zero is compared as a value:
the assembler's zero keeps other bytes than itof's (fltconst.h, ZERO).

The conversion of a floating constant's text (atof(), L-05 - c1's and
as's rule of that name) is checked against the copy's own atof() by
tests/mutos_as/float_coverage/libcatof.py copy.

Usage:
    foldcheck.py [-n N] [--seed S] [--show K]

Exit status 0 if every folded value is the computed one, 1 otherwise.
Requires the Python module "unicorn" and a built tree ("make").
"""

import io
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'tests', 'realbin'))

try:
    import mutosemu  # noqa: E402
except ImportError:
    sys.exit('foldcheck.py: needs the Python module "unicorn" (pip install unicorn)')

CC = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_cc')
MUTOS_ROOT = os.path.join(ROOT, 'mutos_root')
MAX_INSNS = 50000000
M32 = 0xFFFFFFFF


def init(items):
    """An initializer list, a few items to a line."""
    items = list(items)
    return ',\n\t'.join(', '.join(items[i:i + 4]) for i in range(0, len(items), 4))


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def lcon(v):
    """A long constant c0 reads as it is: hexadecimal, its bits (no
    negation for c1 to fold)."""
    return '0x%xL' % (v & M32)


def long_cases(rng, n):
    a_edge = [0, 1, -1, 2, 7, 32767, 32768, -32768, -32769, 65535, 65536, -65536,
              99999, 327680, 1000000, -1000000, 0x7fffffff, -0x80000000, -0x7fffffff,
              0x12345678, -0x12345678]
    b_edge = [1, -1, 2, -2, 3, -3, 7, 32767, 32768, -32767, -32768, -32769, 65535,
              65536, -65535, -65536, 65537, 70000, 98303, 98304, 100000, -100000,
              131071, 131072, 0x7fffffff, -0x7fffffff, -0x80000000, 0x10000000]
    out = [(a, b) for a in a_edge for b in b_edge]
    rng.shuffle(out)
    out = out[:n // 2]

    def any_long():
        bits = rng.randint(1, 32)
        return s32(rng.getrandbits(bits) * rng.choice((1, -1)))

    while len(out) < n:
        b = any_long()
        if b:
            out.append((any_long(), b))
    return out


def prog_long(cases):
    n = len(cases)
    a = init(lcon(x) for x, _ in cases)
    b = init(lcon(y) for _, y in cases)
    q = init('%s / %s' % (lcon(x), lcon(y)) for x, y in cases)
    r = init('%s %% %s' % (lcon(x), lcon(y)) for x, y in cases)
    return '''long a[%d] = { %s };
long b[%d] = { %s };
long q[%d] = { %s };
long r[%d] = { %s };
long x[%d], y[%d];
main()
{
	int i;

	for (i = 0; i < %d; i++) {
		x[i] = a[i] / b[i];
		y[i] = a[i] %% b[i];
	}
	write(1, q, sizeof q);
	write(1, x, sizeof x);
	write(1, r, sizeof r);
	write(1, y, sizeof y);
	return 0;
}
''' % (n, a, n, b, n, q, n, r, n, n, n)


def int_cases(rng, n):
    edge = [0, 1, -1, 255, 256, 257, -257, 777, 1001, -3682, 4097, 32767, -32767, -32768,
            16384, -16384, 16385, 12345, -12345]
    out = list(edge)
    while len(out) < n:
        bits = rng.randint(1, 15)
        out.append(rng.getrandbits(bits) * rng.choice((1, -1)))
    return out[:n]


def icon(v):
    return '(-32767 - 1)' if v == -32768 else str(v)


def prog_itof(cases):
    n = len(cases)
    vals = init(icon(v) for v in cases)
    return '''double d[%d] = { %s };
int v[%d] = { %s };
double e[%d];
main()
{
	int i;

	for (i = 0; i < %d; i++)
		e[i] = v[i];
	write(1, d, sizeof d);
	write(1, e, sizeof e);
	return 0;
}
''' % (n, vals, n, vals, n, n)


def dtext(num, den):
    """The decimal text of num/den (den a power of two up to 8, so it
    ends): what c1's atof() reads exactly."""
    neg = num < 0
    num = abs(num)
    whole, rest = divmod(num, den)
    digits = ''
    while rest:
        rest *= 10
        digits += str(rest // den)
        rest %= den
    return '%s%d.%s' % ('-' if neg else '', whole, digits or '0')


def flt_cases(rng, n, top, corrected):
    """(text, truncated value) of doubles for ftoi (top 2**15) or ftol
    (top 2**31): below top in magnitude, and - in the corrected mode -
    -top to -top-0.99...; the original's c1 refuses from top/2 for ftol
    (L-11) and from top for ftoi (L-03)."""
    lim = top if (corrected or top == 1 << 15) else top // 2
    out = []
    edge = [0, 1, -1, 3, lim - 1, -(lim - 1), lim // 2, -(lim // 2), 12345, -12345]
    if corrected and top == 1 << 31:
        edge += [1 << 30, -(1 << 30), (1 << 30) + 1, 1500000000, -1500000000,
                 0x7fffffff, -0x7fffffff]
    for w in edge:
        for f in (0, 1, 3, 7):
            num = w * 8 + (f if w >= 0 else -f)
            if abs(num) < lim * 8:
                out.append(num)
    if corrected:
        out += [-top * 8 - f for f in (0, 1, 4, 7)]
    while len(out) < n:
        w = rng.getrandbits(rng.randint(1, (lim - 1).bit_length())) % lim
        num = w * 8 + rng.randint(0, 7)
        out.append(num * rng.choice((1, -1)))
    return [(dtext(num, 8), -(-num // 8) if num < 0 else num // 8) for num in out]


def prog_ftox(cases, kind):
    n = len(cases)
    texts = init(t for t, _ in cases)
    return '''%s k[%d] = { %s };
double d[%d] = { %s };
%s m[%d];
main()
{
	int i;

	for (i = 0; i < %d; i++)
		m[i] = d[i];
	write(1, k, sizeof k);
	write(1, m, sizeof m);
	return 0;
}
''' % (kind, n, texts, n, texts, kind, n, n)


def build_run(work, src, flag):
    """Compile, link and run src with cc [flag]: its standard output, or
    a message."""
    with open(os.path.join(work, 'p.c'), 'w') as f:
        f.write(src)
    env = dict(os.environ, MUTOS_ROOT=MUTOS_ROOT)
    env.pop('MUTOS_MODE', None)
    env.pop('MUTOS_RULELOG', None)
    out = os.path.join(work, 'p')
    if os.path.exists(out):
        os.unlink(out)
    r = subprocess.run([CC] + flag + ['-o', 'p', 'p.c'], cwd=work, env=env,
                       capture_output=True)
    if r.returncode != 0 or not os.path.exists(out):
        return None, 'cc %s: %s' % (' '.join(flag), (r.stdout + r.stderr).decode('latin-1')
                                   .strip()[-300:])
    with open(out, 'rb') as f:
        image = f.read()
    o, e = io.BytesIO(), io.BytesIO()
    try:
        emu = mutosemu.Emu(image, ['p'], cwd=work, stdin=io.BytesIO(b''), stdout=o,
                           stderr=e, eof_limit=1000)
        st = emu.run(MAX_INSNS)
    except RuntimeError as x:
        return None, 'stopped: %s' % str(x).split(' at ')[0]
    if st != 0:
        return None, 'exit status %d' % st
    return o.getvalue(), None


def longs(b):
    """The longs of a MUTOS long array (high word first)."""
    return [s32(h << 16 | l) for h, l in struct.iter_unpack('<HH', b)]


def ints(b):
    return [v for (v,) in struct.iter_unpack('<h', b)]


def dbls(b):
    """The doubles of an array, a zero (exponent byte 0) as eight 0 bytes."""
    return [b[i:i + 8] if b[i + 7] else bytes(8) for i in range(0, len(b), 8)]


def compare(label, operands, folded, computed, show):
    bad = [(o, f, c) for o, f, c in zip(operands, folded, computed) if f != c]
    for o, f, c in bad[:show]:
        fmt = (lambda v: v.hex(' ')) if isinstance(f, bytes) else str
        print('  DIFFERENT %s %s: folded %s, at run time %s' % (label, o, fmt(f), fmt(c)))
    return len(bad)


def main(argv):
    n, seed, show = 300, 1, 5
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '-n':
            n = int(args.pop(0))
        elif a == '--seed':
            seed = int(args.pop(0))
        elif a == '--show':
            show = int(args.pop(0))
        else:
            print(__doc__)
            return 2
    if not os.access(CC, os.X_OK) or not os.path.isdir(MUTOS_ROOT):
        sys.exit('foldcheck.py: %s or %s is missing - run "make" first' % (CC, MUTOS_ROOT))
    total_bad, lines = 0, []
    with tempfile.TemporaryDirectory(prefix='foldcheck.') as work:
        for mode, flag in (('original', []), ('corrected', ['-F'])):
            rng = random.Random('foldcheck %d %s' % (seed, mode))
            corrected = mode == 'corrected'
            counts = []
            # long / and %
            cases = long_cases(rng, n)
            out, why = build_run(work, prog_long(cases), flag)
            if out is None:
                print('  %s, long / and %%: %s' % (mode, why))
                total_bad += 1
            else:
                k = 4 * len(cases)
                q, x, r, y = (longs(out[i * k:(i + 1) * k]) for i in range(4))
                total_bad += compare('%s %s /' % (mode, 'aldiv/ldiv'), cases, q, x, show)
                total_bad += compare('%s %s %%' % (mode, 'alrem/lrem'), cases, r, y, show)
                counts.append('%d long / and %%' % len(cases))
            # int to double (see above: the corrected mode only)
            cases = int_cases(rng, n)
            out, why = build_run(work, prog_itof(cases), flag) if corrected else (None, None)
            if out is None and why is None:
                pass
            elif out is None:
                print('  %s, int to double: %s' % (mode, why))
                total_bad += 1
            else:
                k = 8 * len(cases)
                total_bad += compare('%s itod/itof' % mode, cases, dbls(out[:k]),
                                     dbls(out[k:2 * k]), show)
                counts.append('%d int to double' % len(cases))
            # double to int, double to long
            for kind, top, conv, unpack, size in (('int', 1 << 15, 'dtoi/ftoi', ints, 2),
                                                  ('long', 1 << 31, 'ftol', longs, 4)):
                cases = flt_cases(rng, n, top, corrected)
                out, why = build_run(work, prog_ftox(cases, kind), flag)
                if out is None:
                    print('  %s, double to %s: %s' % (mode, kind, why))
                    total_bad += 1
                    continue
                k = size * len(cases)
                folded, computed = unpack(out[:k]), unpack(out[k:2 * k])
                total_bad += compare('%s %s' % (mode, conv), [t for t, _ in cases],
                                     folded, computed, show)
                # and both C's truncation
                total_bad += compare('%s %s (C)' % (mode, conv), [t for t, _ in cases],
                                     folded, [v for _, v in cases], show)
                counts.append('%d double to %s' % (len(cases), kind))
            lines.append('%s mode (cc%s): %s' % (mode, ' -F' if flag else '', ', '.join(counts)))
    for ln in lines:
        print('foldcheck: ' + ln)
    print('foldcheck: %s' % ('every folded value the one computed at run time' if not total_bad
                             else '%d difference(s)' % total_bad))
    return 1 if total_bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
