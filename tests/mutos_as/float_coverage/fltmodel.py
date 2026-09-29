#!/usr/bin/env python3
"""fltmodel.py - reference model of the real MUTOS 1700 assembler's
.float/.double conversion, independent of src/mutos_as/fltconst.c
(Python's arbitrary-precision integers, written separately).

The model (see src/mutos_as/fltconst.h for the evidence): v7 libc
atof()'s algorithm on the 56-bit MUTOS double -

  * digits: fl = 10*fl + digit (a dmul, then a dadd) while fl < 2**56;
    a fraction digit lowers the decimal exponent; after that an integer
    digit raises it and a fraction digit is dropped;
  * flexp = 5**k (k = |decimal exponent|) by v7's repeated squaring;
  * fl /= flexp (ddiv) for a negative exponent, else fl *= flexp;
    ldexp(fl, exponent); negated for a leading '-';
  * nd - k < -39 (LOGHUGE) makes atof() give up: a nonzero value there
    is out of range, a zero's bytes are unknown;
  * a zero: the real arithmetic clears only the exponent byte of its
    accumulator, which still holds the previous result - flexp for
    k >= 1, so flexp's mantissa with exponent byte 0 on either path;
    for k = 0 whatever the digit loop left there, observed only for an
    unsigned .float with 18 digits (ff ff ff 00);
  * a .float stores the double's high four bytes.

Each of dmul (M), dadd (A) and ddiv (D) rounds to 56 significant bits
with a mode from: trunc, ne (nearest, ties even), na (nearest, ties
away) or away (away from zero on any remainder). dmul has one more
candidate, libc: the product libc.a's own dmath.o forms - its pmuld
takes the partial product a0*b2 from b1's word, so the product is
a*b + (a0*b1 - a0*b2) * 2**32 (a = the operand fmuld addresses, b = the
one on the floating-point stack; words of 16 bits from the bottom) -
rounded to nearest, ties even. The combinations that reproduce every
known-source real constant "survive"; a constant is determined only if
all survivors give the same stored bytes. Since fltmode.o.golden: A = ne,
D = ne or na (never different in atof(), whose one division cannot tie),
M = ne or libc (different only for products no real constant has needed
so far).

Usage:
  fltmodel.py survivors [DIR ...]
      Reads every <name>.s that has a <name>.o.golden in the given
      directories (default: this script's own directory), takes each
      .float/.double constant's real bytes from the golden's data
      segment, and prints the rounding-mode combinations consistent with
      all of them. Exit 1 if they differ from EXPECTED below - the set
      fltconst.c's MODES_MUL/MODES_ADD/MODES_DIV encode - if the model
      contradicts a golden outright, or if a golden holds a constant the
      model does not cover (a zero outside its known shape). Also the
      way to read a new probe's golden: "survivors . ../float_open".
  fltmodel.py check TOOL [N]
      Runs fixed edge cases plus N seeded random texts (default 2000)
      through TOOL (src/mutos_as/fltconst_test) and through the model
      restricted to EXPECTED; exit 1 on any difference.
"""

import itertools
import os
import random
import re
import struct
import subprocess
import sys
from fractions import Fraction

MODES = ('trunc', 'ne', 'na', 'away')
M_MODES = MODES + ('libc',)     # dmul only: libc.a dmath.o's product
SIG = 56
BIG = 1 << 56

# Must match fltconst.c's MODES_MUL (M), MODES_ADD (A) and MODES_DIV (D).
EXPECTED = set(itertools.product(('ne', 'libc'), ('ne',), ('ne', 'na')))

# The one zero with decimal exponent 0 seen (fltmode.o.golden, Z0).
ZERO_K0_DIGITS = 18
ZERO_K0_FLOAT = bytes.fromhex('ffffff00')


class Range(Exception):
    pass


def rnd(n, sticky, e, mode):
    """Round (n + f) * 2**e (0 < f < 1 iff sticky) to SIG bits -> (m, e)."""
    L = n.bit_length()
    if L <= SIG:
        assert not sticky
        return n << (SIG - L), e - (SIG - L)
    s = L - SIG
    keep, tail, half = n >> s, n & ((1 << s) - 1), 1 << (s - 1)
    if mode == 'trunc':
        up = False
    elif mode in ('ne', 'libc'):
        up = tail > half or (tail == half and (sticky or keep & 1))
    elif mode == 'na':
        up = tail >= half
    else:
        up = tail != 0 or sticky
    if up:
        keep += 1
        if keep == 1 << SIG:
            keep >>= 1
            s += 1
    return keep, e + s


def chk(v):
    if not -127 <= v[1] + SIG <= 127:
        raise Range()
    return v


def mul(a, b, mode):
    """a * b; a is the operand fmuld addresses (dmul's [si]), b the one
    on the floating-point stack ([di]) - atof()'s order."""
    p = a[0] * b[0]
    if mode == 'libc':
        a0, b1, b2 = a[0] & 0xffff, (b[0] >> 16) & 0xffff, (b[0] >> 32) & 0xffff
        p += (a0 * b1 - a0 * b2) << 32
    return chk(rnd(p, False, a[1] + b[1], mode))


def div(a, b, mode):
    q, r = divmod(a[0] << 64, b[0])
    return chk(rnd(q, r != 0, a[1] - b[1] - 64, mode))


def as_int(v):
    return v[0] << v[1] if v[1] >= 0 else v[0] >> -v[1]


def parse(kind, t):
    expl = 'e'
    if len(t) >= 2 and t[0] == '0' and t[1].lower() == kind:
        t = t[2:]
        expl = 'd' if kind == 'd' else 'e'
    m = re.fullmatch(r'([+-]?)(\d*)(?:\.(\d*))?(?:[%s%s]([+-]?\d+))?'
                     % (expl, expl.upper()), t)
    if not m or (m.group(2) == '' and (m.group(3) or '') == ''):
        return None
    digs = [(int(c), False) for c in m.group(2)] + \
           [(int(c), True) for c in (m.group(3) or '')]
    return m.group(1) == '-', digs, int(m.group(4)) if m.group(4) else 0


def image(neg, m, xbyte):
    b = bytearray((m & ((1 << 55) - 1)).to_bytes(7, 'little') + bytes([xbyte]))
    if neg:
        b[6] |= 0x80
    return bytes(b)


def convert(kind, text, M, A, D):
    """'SYNTAX' | 'ZERO' | 'RANGE' | the stored bytes (4 or 8)."""
    p = parse(kind, text)
    if p is None:
        return 'SYNTAX'
    neg, digs, eexp = p
    try:
        fl, exp10, nd = None, 0, 0
        for d, frac in digs:
            if fl is None or as_int(fl) < BIG:
                t = as_int(chk(rnd(as_int(fl) * 10, False, 0, M))) if fl else 0
                fl = chk(rnd(t + d, False, 0, A)) if t + d else None
                if frac:
                    exp10 -= 1
            elif not frac:
                exp10 += 1
            nd += 1
        exp10 += eexp
        k = abs(exp10)
        if exp10 < 0 and nd - k < -39:
            return 'ZERO' if fl is None else 'RANGE'
        flexp, exp5, kk = rnd(1, False, 0, 'trunc'), rnd(5, False, 0, 'trunc'), k
        while True:
            if kk & 1:
                flexp = mul(flexp, exp5, M)
            kk >>= 1
            if kk == 0:
                break
            exp5 = mul(exp5, exp5, M)
        if fl is None:
            if k == 0:
                if kind == 'f' and not neg and nd == ZERO_K0_DIGITS:
                    return ZERO_K0_FLOAT
                return 'ZERO'
            b = image(neg, flexp[0], 0)
            return b[4:] if kind == 'f' else b
        fl = div(fl, flexp, D) if exp10 < 0 else mul(fl, flexp, M)
        m, e = chk((fl[0], fl[1] + exp10))
    except Range:
        return 'RANGE'
    b = image(neg, m, e + SIG + 128)
    return b[4:] if kind == 'f' else b


def classify(kind, text, combos):
    """What fltconst_test prints for this text under these combos."""
    outs = {convert(kind, text, *c) for c in combos}
    for status in ('SYNTAX', 'ZERO', 'RANGE'):
        if status in outs:
            return status
    if len(outs) > 1:
        return 'ROUNDING'
    return 'OK ' + outs.pop().hex(' ')


# ---------------------------------------------------------------------
# Known-source real constants: every .float/.double in a golden's source

DATA_DIR = re.compile(r'^\s*(?:[A-Za-z_.~][\w.~]*:\s*)*(\.\w+)\s*(.*)$')


def constants_of(src, golden):
    """[(kind, text, real bytes)] for every .float/.double in src's .data."""
    with open(golden, 'rb') as f:
        obj = f.read()
    tsize, dsize = struct.unpack('<HH', obj[2:6])
    data = obj[16 + tsize:16 + tsize + dsize]
    out, seg, off = [], '.text', 0
    for raw in open(src):
        line = raw.split('|', 1)[0].strip()
        if not line:
            continue
        m = DATA_DIR.match(line)
        if not m:
            if seg == '.data':
                raise SystemExit('%s: cannot size data line: %s' % (src, line))
            continue
        d, rest = m.group(1), m.group(2)
        if d in ('.text', '.data', '.bss'):
            seg = d
            continue
        if seg != '.data':
            continue
        ops = [o.strip() for o in rest.split(',')] if rest.strip() else []
        if d in ('.float', '.double'):
            n = 4 if d == '.float' else 8
            for o in ops:
                out.append((d[1], o, data[off:off + n], '%s data+%d' % (os.path.basename(golden), off)))
                off += n
        elif d == '.word':
            off += 2 * len(ops)
        elif d == '.byte':
            off += len(ops)
        elif d == '.even':
            off += off & 1
        elif d != '.globl':
            raise SystemExit('%s: cannot size directive %s in .data' % (src, d))
    return out


def confirmed(dirs):
    out = []
    for d in dirs:
        for name in sorted(os.listdir(d)):
            if name.endswith('.s') and os.path.exists(os.path.join(d, name[:-2] + '.o.golden')):
                out += constants_of(os.path.join(d, name), os.path.join(d, name[:-2] + '.o.golden'))
    return out


def cmd_survivors(dirs):
    consts = confirmed(dirs)
    allc = list(itertools.product(M_MODES, MODES, MODES))
    modelled, unmodelled = [], []
    for k, t, b, where in consts:
        outs = {convert(k, t, *c) for c in allc}
        (modelled if any(isinstance(o, bytes) for o in outs) else unmodelled).append((k, t, b, where))
    surv = [c for c in allc if all(convert(k, t, *c) == b for k, t, b, _ in modelled)]
    print('%d known-source real constants (%s)' % (len(consts), ', '.join(sorted({w.split()[0] for *_, w in consts}))))
    rc = 0
    for k, t, b, where in modelled:
        outs = {convert(k, t, *c) for c in allc}
        if b not in outs:
            print('  MODEL CONTRADICTED: .%s %s is %s in %s; the model gives %s'
                  % ('float' if k == 'f' else 'double', t, b.hex(' '), where,
                     sorted(o.hex(' ') if isinstance(o, bytes) else o for o in outs)))
    for k, t, b, where in unmodelled:
        print('  NOT MODELLED: .%s %s is %s in %s - extend fltconst.c and this model from it'
              % ('float' if k == 'f' else 'double', t, b.hex(' '), where))
        rc = 1
    print('%d of %d rounding-mode combinations (M = dmul, A = dadd, D = ddiv) reproduce all modelled ones'
          % (len(surv), len(allc)))
    for axis, name in ((0, 'M'), (1, 'A'), (2, 'D')):
        print('  %s: %s' % (name, ' '.join(m for m in M_MODES if any(c[axis] == m for c in surv))))
    if len(surv) <= 8:
        for c in surv:
            print('    M=%s A=%s D=%s' % c)
    if not surv:
        print('FAIL: no combination fits - the conversion model itself is wrong')
        return 1
    if set(surv) != EXPECTED:
        print('FAIL: this differs from the set fltconst.c uses (MODES_MUL/MODES_ADD/MODES_DIV) and '
              'fltmodel.py\'s EXPECTED - update both, then re-run "check"')
        return 1
    if rc:
        print('FAIL: constants above are outside the model')
        return 1
    print('ok: matches fltconst.c\'s MODES_MUL/MODES_ADD/MODES_DIV')
    return 0


# ---------------------------------------------------------------------
# check: fltconst_test against the model

EDGES = [
    'f 0.00000000000000000e+00', 'f -0.00000000000000000e+00', 'f 0.0', 'f 0.00', 'f .0',
    'f 0', 'f 0.', 'f 0e5', 'f 0.0e+05', 'f 0.00000000000000000e+17', 'f 0.000000000000000000000000',
    'f 0.0000000000000000000000000', 'd 0.00000000000000000e+00', 'd -0.0', 'd 0d0.0d0',
    'f 0f0.00000000000000000e+00', 'f 0.10000000000000000e+00', 'f 0.1', 'd 0.1', 'd .03',
    'd 3.00000000000000000e-02', 'd 1.00000000023283064365386962890625', 'f 2.50000000000000000e+00',
    'f 3.50000000000000000e+00', 'f 7.20575940379279360e+16', 'f 72057594037927936',
    'f 72057594037927937', 'f 1152921504606846976', 'd 1152921504606846976',
    'f 2.93572534179687500e+03', 'f 5.96046447753906250e-08', 'f 9.99999940395355225e-01',
    'f 1.00000000000000000e-38', 'f 1e-38', 'f 3.4e38', 'f 1.7e38', 'f 1e-50', 'd 1e-50',
    'f -1.5e3', 'd 0D1.5D+3', 'd 0d1.5e3', 'f 0d1', 'f 1e', 'f e5', 'f 1.5.2', 'f -', 'f .',
    'd 144115188075855871', 'd 72057594037927935',
    'f 0.00000000000000000e+17', 'f 000000000000000000', 'f 00000000000000000.0e1',
    'f +0.00000000000000000e+17', 'f -0.00000000000000000e+17', 'd 0.00000000000000000e+17',
    'f 0.0000000000000000e+16', 'f 0.000000000000000000e+18', 'd 0.0e+05', 'f -0e1', 'd 0e54',
    'd 0e55', 'd 0.0000000000000000000000000', 'f -0.0000000000000000000000000000000000000000',
    'd 0e-40', 'f 0e-38', 'f 0.0e-39', 'd 1e-39', 'd 1e-40', 'f 1.00000000000000000e-38',
    'f 1.26765060022822940e+30', 'd 1.23456789012345678e+25', 'd 0.00000000000000000000e-34',
    'd 0.00000000000000000000e-29', 'f 1.00000000000000000e+21',
]


def random_texts(n, seed=1):
    rng = random.Random(seed)

    def exact_dec(v):
        k = v.denominator.bit_length() - 1
        return str(v.numerator * 5 ** k), k

    out = []
    for _ in range(n):
        kind = rng.choice('fd')
        r = rng.random()
        if r < 0.45:        # the compiler's "%.17e" of an exact value
            sb = rng.randint(1, 24 if kind == 'f' else 56)
            v = Fraction(rng.getrandbits(sb) | (1 << (sb - 1)) | 1) * Fraction(2) ** (rng.randint(-60, 60) - sb)
            s, k = exact_dec(v)
            e10 = len(s) - 1 - k
            dg = (s + '0' * 18)[:18]
            t = '%s.%se%s%02d' % (dg[0], dg[1:], '+' if e10 >= 0 else '-', abs(e10))
        elif r < 0.65:      # short decimals, exponents across the range
            t = '%d.%de%+d' % (rng.randint(0, 999), rng.randint(0, 99999), rng.randint(-45, 40))
        elif r < 0.8:       # integers around atof's 2**56 accumulation limit
            t = str(rng.randint(2 ** 50, 2 ** 66))
        elif r < 0.9:       # zeros
            t = '0.%se%+d' % ('0' * rng.randint(0, 30), rng.randint(-5, 5))
        else:               # long exact expansions
            sb = rng.randint(1, 24)
            v = Fraction(rng.getrandbits(sb) | 1) * Fraction(2) ** -rng.randint(1, 60)
            s, k = exact_dec(v)
            s = s.rjust(k + 1, '0')
            t = s[:-k] + '.' + s[-k:]
        out.append('%s %s%s' % (kind, rng.choice(['', '', '-', '+']), t))
    return out


def cmd_check(tool, n):
    texts = EDGES + random_texts(n)
    res = subprocess.run([tool], input='\n'.join(texts) + '\n', capture_output=True, text=True)
    if res.returncode != 0:
        print('FAIL: %s exited %d: %s' % (tool, res.returncode, res.stderr.strip()))
        return 1
    got = res.stdout.splitlines()
    bad = 0
    for line, g in zip(texts, got):
        want = classify(line[0], line[2:], sorted(EXPECTED))
        if g != want:
            bad += 1
            if bad <= 10:
                print('  MISMATCH  %-45s fltconst.c: %-30s model: %s' % (line, g, want))
    if len(got) != len(texts):
        print('FAIL: %d lines in, %d out' % (len(texts), len(got)))
        return 1
    print('%d texts (%d edge cases, %d random): %d differences between fltconst.c and the model'
          % (len(texts), len(EDGES), n, bad))
    return 1 if bad else 0


def main(argv):
    if len(argv) >= 2 and argv[1] == 'survivors':
        dirs = argv[2:] or [os.path.dirname(os.path.abspath(__file__))]
        return cmd_survivors(dirs)
    if len(argv) in (3, 4) and argv[1] == 'check':
        return cmd_check(argv[2], int(argv[3]) if len(argv) == 4 else 2000)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
