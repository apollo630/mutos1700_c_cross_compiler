#!/usr/bin/env python3
"""rtcheck.py - the runtime of libc.a that compiled code calls, measured.

Step F3 of docs/TOOLCHAIN_PLAN.md (section 7.3): the routines c1's code
tables call without the programmer naming them - fourteen members of
libc.a: aldiv.o, almul.o, alrem.o, ldiv.o, lmul.o, lrem.o (the long
helpers), chkstk.o, convert.o (itof, ftoi), lconvert.o (ltof, ftol),
dmath.o (dadd, dsub, dmul, ddiv), stacks.o (the floating-point stack),
stkmath.o, singles.o, doubles.o (the floating operations on it) - and
what they pull in (fperr.o's __ovfl and __div0, cret.o). Each entry
point is called under tests/realbin/mutosemu.py, on edge values and on
many random ones, and its result compared with Python's arithmetic:
C's semantics on 32-bit longs and 16-bit ints, exact rationals for the
floating routines, rounded to the nearest 56-bit MUTOS double (ties to
even; a result below the smallest double is 0, one above the largest
raises SIGFPE, as the routines are written to do).

The MUTOS double is eight bytes, words little-endian: byte 7 the
exponent (0 = zero, else value = 0.1mmm... * 2**(byte7 - 128)), bit 7
of byte 6 the sign, the other 55 bits the mantissa below its hidden
leading 1. A float is the double's high four bytes.

What runs: the members, from tests/mutos1700_libc/ (or, with --archive,
from another archive - the corrected copy of step F3b), crt0.o and a
two-instruction main linked by mutos_ld -i into a 0411 image; mutosemu's
Emu loads it, crt0 runs up to main, and every call then starts at the
entry point with the arguments set up as compiled code sets them up
and a HLT as its return address. A call that reaches __ovfl or __div0
(which would kill(getpid(), SIGFPE)) is stopped there: "SIGFPE"; an
8086 divide error (interrupt 0) is "divide error".

Every difference from the right result must be explained: each known
bug of the original has a model here that predicts the original's
exact wrong output (or signal) - L-01 to L-05 and L-08 to L-14, the
latter found by this step (docs/MUTOS_ORIGINAL_BUGS.md) - and a case counts for that
bug only if the original gives what the model predicts. A difference
no model predicts is UNEXPLAINED and the run exits 1. The cases come in
two sets per entry point, "ordinary" (values as programs have them:
moderate magnitudes, divisors of every length, ints and longs of every
size) and "edge" (the limits of each range, zeros of every form, the
smallest and largest doubles, cancellation), so that the report says
how often a bug meets ordinary values.

Requires Python 3, the unicorn module (pip install unicorn) and a built
mutos_as, mutos_ld (and mutos_ar for --archive): top-level "make". Not
part of "make test"; "make check-runtime" runs it.

Usage:
  rtcheck.py [options] [GROUP ...]
      GROUPs: long, float, conv, stack, cmp, chkstk, layout (default:
      all). Options: -n N (random cases per entry point and set,
      default 1000), --seed S (default 1), --examples K (examples
      printed per class, default 2), --archive A (take the members the
      archive A has from it - the corrected copy, or an archive of some
      of them such as the kernel's arith.a -, the rest from
      tests/mutos1700_libc/; A may be a .base64.txt of tests/, as
      tests/mutos1700_arith/arith.a.base64.txt), --fixed BUG,...
      (bugs that are corrected in that archive: their models are
      dropped, so the original's behaviour counts as UNEXPLAINED - for
      step F3b), --copy [C] (the corrected copy of step F3b,
      src/libc_fixed/libc.a - or C, such as src/libc_fixed/arith.a -
      with the corrections its register in docs/RULES.md names for the
      members it has: --archive and --fixed for it). Exit 1 if anything
      is UNEXPLAINED.
  rtcheck.py show [--archive A] ENTRY ARG ...
      One call, its result printed: "show itof 1001", "show ldiv
      245554906 -3131", "show ftol 1.5e9", "show fcmp 1e38 -1e38" (floating
      arguments are decimal texts, taken as the nearest double); with
      --archive, the members A has from A.
"""

import base64
import io
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True      # no __pycache__ in the test directories
sys.path.insert(0, os.path.join(ROOT, 'tests', 'realbin'))

try:
    from unicorn import UC_HOOK_CODE, UC_HOOK_INTR, UcError
    from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_EFLAGS,
                                   UC_X86_REG_ES, UC_X86_REG_SS)
    import mutosemu  # noqa: E402
except ImportError:
    sys.exit('rtcheck.py: needs the Python module "unicorn" (pip install unicorn)')

MUTOS_AS = os.path.join(ROOT, 'src', 'mutos_as', 'mutos_as')
MUTOS_LD = os.path.join(ROOT, 'src', 'mutos_ld', 'mutos_ld')
MUTOS_AR = os.path.join(ROOT, 'src', 'mutos_ar', 'mutos_ar')
LIBC = os.path.join(ROOT, 'tests', 'mutos1700_libc')
COPY = os.path.join(ROOT, 'src', 'libc_fixed', 'libc.a')      # step F3b's copy
RULES = os.path.join(ROOT, 'docs', 'RULES.md')
CRT0 = os.path.join(ROOT, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt')

# The fourteen members, then what they pull in, named explicitly
# (libc.a's __.SYMDEF is stale); libc.a follows for the rest (atof.o and
# fltpr.o reach ecvt.o, ldexp.o, ctype_.o ... through stacks.o's
# fltused).
RUNTIME = ('aldiv', 'almul', 'alrem', 'ldiv', 'lmul', 'lrem', 'chkstk', 'convert',
           'lconvert', 'dmath', 'stacks', 'stkmath', 'singles', 'doubles')
PULLED = ('fperr', 'cret', 'atof', 'fltpr')

SEGREGS = (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS)
STEPS = 400000              # instruction limit per call
GUARD = 0x5A5A              # what the registers hold at a call
SCRATCH = 0xD000            # operands in memory (the data segment)
SIGFPE, DIVERR = 'SIGFPE', 'divide error'
UNDEFINED = 'undefined in C'    # a "right" outcome: anything goes


# ---------------------------------------------------------------------------
# Numbers

M32 = 0xFFFFFFFF


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def hi(v):
    return (v >> 16) & 0xFFFF


def lo(v):
    return v & 0xFFFF


def c_div(a, b):
    """C's quotient: truncated toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def c_rem(a, b):
    return a - c_div(a, b) * b


def long_bytes(v):
    """A long in memory: the high word first (docs/MUTOS_C_ABI.md 1.6)."""
    return struct.pack('<HH', hi(v), lo(v))


def long_of(b):
    h, l = struct.unpack('<HH', b)
    return s32(h << 16 | l)


ZERO = bytes(8)
OVF = 'OVF'
HIDDEN = 1 << 55


def value(b):
    """The rational value of a double image (exponent byte 0: zero)."""
    if b[7] == 0:
        return Fraction(0)
    m = (int.from_bytes(b[:7], 'little') & (HIDDEN - 1)) | HIDDEN
    v = Fraction(m) * Fraction(2) ** (b[7] - 184)
    return -v if b[6] & 0x80 else v


def mantissa(b):
    return (int.from_bytes(b[:7], 'little') & (HIDDEN - 1)) | HIDDEN


def image(neg, m, e):
    """The double image of mantissa m (2**55 <= m < 2**56) and exponent byte e."""
    b = bytearray((m & (HIDDEN - 1)).to_bytes(7, 'little') + bytes([e]))
    if neg:
        b[6] |= 0x80
    return bytes(b)


def rounded(v):
    """v rounded to the nearest double (ties to even): its image, ZERO if
    it is below the smallest double, OVF if above the largest."""
    if v == 0:
        return ZERO
    neg = v < 0
    n, d = abs(v.numerator), v.denominator
    s = 56 - (n.bit_length() - d.bit_length())
    while True:
        nn, dd = (n << s, d) if s >= 0 else (n, d << -s)
        if nn >= dd << 56:
            s -= 1
        elif nn < dd << 55:
            s += 1
        else:
            break
    q, r = divmod(nn, dd)
    if 2 * r > dd or (2 * r == dd and q & 1):
        q += 1
    e = 184 - s
    if q == 1 << 56:
        q >>= 1
        e += 1
    if e > 255:
        return OVF
    if e < 1:
        return ZERO
    return image(neg, q, e)


def dbl_text(t):
    """A decimal text as the nearest double image."""
    r = rounded(Fraction(t))
    if r == OVF:
        raise ValueError('%s is beyond the largest double' % t)
    return r


def show_dbl(b):
    if b is None or isinstance(b, str):
        return str(b)
    v = value(b)
    return '%s (%.17g)' % (b.hex(' '), float(v)) if abs(v) < 2 ** 1000 else b.hex(' ')


# ---------------------------------------------------------------------------
# The linked runtime under the emulator

class Runtime:
    from_archive = []           # the members the last link took from --archive

    def __init__(self, archive=None, pad=0):
        for tool in (MUTOS_AS, MUTOS_LD) + ((MUTOS_AR,) if archive else ()):
            if not os.access(tool, os.X_OK):
                sys.exit('rtcheck.py: %s is not built - run "make" first' % tool)
        self.img = self._link(archive, pad)
        magic, t, d, bss, syms = struct.unpack('<5H', self.img[:10])
        self.sym = {}
        for i in range(len(self.img) - syms, len(self.img), 12):
            name = self.img[i:i + 8].rstrip(b'\0').decode('latin-1')
            self.sym.setdefault(name, struct.unpack('<H', self.img[i + 10:i + 12])[0])
        self.stop = self.sym['_rtstop']
        self.fpstk = self.sym['fpstk']
        self.fac = self.sym['fac']
        self.hooks = []
        self._start()
        self.sp0 = (self.emu.reg('sp') - 0x40) & ~1

    def _start(self):
        """A fresh process, crt0 run up to main. Again after an interrupt
        (a divide error): Unicorn keeps the exception pending, and the
        next start would be a double fault."""
        self.out = io.BytesIO()
        self.emu = mutosemu.Emu(self.img, ['rtcheck'], stdout=self.out, stderr=self.out)
        uc = self.emu.uc
        uc.emu_start(self.emu.cbase, self.emu.cbase + self.sym['_main'], count=100000)
        if self.emu.reg('ip') != self.sym['_main']:
            sys.exit('rtcheck.py: crt0 did not reach main')
        self.segs = {r: uc.reg_read(r) for r in SEGREGS}
        self.raised = None
        self.dirty = False
        for name in ('__ovfl', '__div0'):
            a = self.emu.cbase + self.sym[name]
            uc.hook_add(UC_HOOK_CODE, self._signal, name, a, a)
        uc.hook_add(UC_HOOK_INTR, self._interrupt)
        for off, fn in self.hooks:
            uc.hook_add(UC_HOOK_CODE, fn, None, self.emu.cbase + off, self.emu.cbase + off)

    def hook(self, off, fn):
        """fn(uc, address, size, None) before the instruction at text
        offset off - kept across a restart."""
        self.hooks.append((off, fn))
        a = self.emu.cbase + off
        self.emu.uc.hook_add(UC_HOOK_CODE, fn, None, a, a)

    @staticmethod
    def _link(archive, pad):
        work = tempfile.mkdtemp(prefix='rtcheck.')
        try:
            def unb64(src, dst):
                with open(src, 'rb') as f, open(os.path.join(work, dst), 'wb') as g:
                    g.write(base64.b64decode(f.read()))
            unb64(CRT0, 'crt0.o')
            members = RUNTIME + PULLED
            # the members the archive has come from it, the rest (and
            # libc.a for everything else) from the original - so that
            # an archive of the long helpers alone, such as the kernel's
            # arith.a, can be measured too
            have = ()
            if archive:
                if archive.endswith('.base64.txt'):     # as tests/ keeps them
                    unb64(archive, 'given.a')
                else:
                    shutil.copy(archive, os.path.join(work, 'given.a'))
                r = subprocess.run([MUTOS_AR, 't', 'given.a'], cwd=work,
                                   capture_output=True, text=True)
                if r.returncode:
                    sys.exit('rtcheck.py: %s: %s' % (archive, r.stdout + r.stderr))
                have = [m for m in members if m + '.o' in r.stdout.split()]
                if not have:
                    sys.exit('rtcheck.py: %s has none of the runtime\'s members' % archive)
                r = subprocess.run([MUTOS_AR, 'x', 'given.a'] + [m + '.o' for m in have],
                                   cwd=work, capture_output=True, text=True)
                if r.returncode:
                    sys.exit('rtcheck.py: %s: %s' % (archive, r.stdout + r.stderr))
            for m in members:
                if m not in have:
                    unb64(os.path.join(LIBC, m + '.o.base64.txt'), m + '.o')
            unb64(os.path.join(LIBC, 'libc.a.base64.txt'), 'libc.a')
            Runtime.from_archive = list(have)
            # main: a HLT where crt0 calls it and one as every call's
            # return address; pad bytes of data, in objects of 16K each
            # (ld cannot have one of 32K, LD-07), move the BSS and the
            # commons (fpstk) up
            objs = ['main.o']
            src = '.globl\t_main\n.globl\t_rtstop\n.text\n_main:\nhlt\n_rtstop:\nhlt\n'
            while True:
                with open(os.path.join(work, objs[-1][:-1] + 's'), 'w') as f:
                    f.write(src)
                subprocess.run([MUTOS_AS, '-o', objs[-1], objs[-1][:-1] + 's'], cwd=work,
                               check=True, capture_output=True)
                if not pad:
                    break
                chunk = min(pad, 16384)
                pad -= chunk
                objs.append('pad%d.o' % len(objs))
                src = '.data\n.=.+%d.\n' % chunk
            r = subprocess.run([MUTOS_LD, '-i', '-o', 'rt.out', 'crt0.o'] + objs
                               + [m + '.o' for m in members] + ['libc.a'],
                               cwd=work, capture_output=True, text=True)
            out = os.path.join(work, 'rt.out')
            if 'Undefined' in r.stdout + r.stderr or not os.path.exists(out):
                sys.exit('rtcheck.py: linking failed:\n' + r.stdout + r.stderr)
            with open(out, 'rb') as f:
                return f.read()
        finally:
            shutil.rmtree(work)

    def _signal(self, uc, addr, size, name):
        self.raised = SIGFPE
        uc.emu_stop()

    def _interrupt(self, uc, intno, user):
        self.raised = DIVERR if intno == 0 else 'interrupt %d' % intno
        self.dirty = True
        uc.emu_stop()

    # memory of the data segment
    def word(self, off):
        return self.emu.word(off)

    def mem(self, off, n):
        return bytes(self.emu.uc.mem_read(self.emu.dbase + (off & 0xFFFF), n))

    def put(self, off, b):
        self.emu.uc.mem_write(self.emu.dbase + (off & 0xFFFF), bytes(b))

    def put_word(self, off, v):
        self.put(off, struct.pack('<H', v & 0xFFFF))

    def call(self, entry, regs=None, args=(), sp=None):
        """Call entry with these registers (the rest GUARD) and these words
        on the stack (args[0] at the lowest address, next to the return
        address). The registers after it, or None: self.raised says why."""
        e = self.emu
        for r, v in self.segs.items():
            e.uc.reg_write(r, v)
        sp = (self.sp0 if sp is None else sp) - 2 * len(args) - 2
        e.uc.mem_write(e.dbase + sp, struct.pack('<%dH' % (len(args) + 1), self.stop,
                                                *[a & 0xFFFF for a in args]))
        for r in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp'):
            e.set_reg(r, GUARD)
        for r, v in (regs or {}).items():
            e.set_reg(r, v)
        e.set_reg('sp', sp)
        e.uc.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        self.raised = None
        try:
            e.uc.emu_start(e.cbase + self.sym[entry], e.cbase + self.stop, count=STEPS)
        except UcError as err:
            self.raised = self.raised or 'fault (%s)' % err
            self.dirty = True
        if self.dirty:
            raised = self.raised
            self._start()       # before the next call's operands are written
            self.raised = raised
        if self.raised:
            return None
        if e.reg('ip') != self.stop:
            self.raised = 'no return within %d instructions' % STEPS
            return None
        out = {r: e.reg(r) for r in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp')}
        out['flags'] = e.uc.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF
        out['sp_in'] = sp
        return out

    # the floating-point stack: fpsp addresses the top slot, fpstk - 8
    # when it is empty
    def fp_set(self, slots):
        for i, b in enumerate(slots):
            self.put(self.fpstk + 8 * i, b)
        self.put_word(self.sym['fpsp'], self.fpstk + 8 * (len(slots) - 1))

    def fp_get(self):
        """The slots up to the top, or None if fpsp is not a slot."""
        sp = self.word(self.sym['fpsp'])
        n = (sp - self.fpstk) // 8 + 1
        if (sp - self.fpstk) % 8 or not 0 <= n <= 8:
            return None
        return [self.mem(self.fpstk + 8 * i, 8) for i in range(n)]


def saved(r, bx=False):
    """The registers a routine must preserve: SI, DI, BP (cret), and BX for
    the long helpers (their private convention, docs/MUTOS_C_ABI.md 1.8);
    SP back above the return address; the direction flag clear."""
    bad = [x for x in ('si', 'di', 'bp') + (('bx',) if bx else ()) if r[x] != GUARD]
    if r['sp'] != r['sp_in'] + 2:
        bad.append('sp')
    if r['flags'] & 0x0400:
        bad.append('DF')
    return bad


# ---------------------------------------------------------------------------
# The bookkeeping

class Tally:
    """Per entry point and case set: how many cases were right, how many
    each bug explains, how many nothing explains."""

    def __init__(self, examples):
        self.examples = examples
        self.counts = {}
        self.shown = {}
        self.unexplained = 0

    def add(self, entry, cset, got, right, model, bug, describe):
        """got: what the original did; right: the right outcome (or a
        predicate of the outcome); model: what the bug model predicts
        (None: no model applies); bug: its name."""
        ok = right(got) if callable(right) else got == right
        if right is UNDEFINED:
            cls = UNDEFINED
        elif ok:
            cls = 'right'
        elif model is not None and got == model:
            cls = bug
        else:
            cls = 'UNEXPLAINED'
            self.unexplained += 1
        key = (entry, cset)
        c = self.counts.setdefault(key, {})
        c[cls] = c.get(cls, 0) + 1
        if cls not in ('right', UNDEFINED):
            sh = self.shown.setdefault((entry, cls), [])
            if len(sh) < (self.examples if cls != 'UNEXPLAINED' else 10):
                sh.append(describe())
        return cls

    def report(self, title):
        print('== %s' % title)
        entries = []
        for (entry, cset) in self.counts:
            if entry not in entries:
                entries.append(entry)
        for entry in entries:
            parts = []
            for cset in ('ordinary', 'edge'):
                c = self.counts.get((entry, cset))
                if not c:
                    continue
                n = sum(c.values())
                rest = ', '.join('%s %d' % (k, v) for k, v in sorted(c.items()) if k != 'right')
                parts.append('%s %d: right %d%s' % (cset, n, c.get('right', 0),
                                                    ', ' + rest if rest else ''))
            print('  %-8s %s' % (entry, '; '.join(parts)))
            for (e, cls), shown in sorted(self.shown.items()):
                if e == entry:
                    for s in shown:
                        print('             %s: %s' % (cls, s))


# ---------------------------------------------------------------------------
# The long helpers: lmul/ldiv/lrem(a, b) -> DX:AX; almul/aldiv/alrem(&a, b)
# -> a and DX:AX (docs/MUTOS_C_ABI.md 1.8)

def neg32(h, l):
    v = (-(h << 16 | l)) & M32
    return v >> 16, v & 0xFFFF


def model_div(a, b):
    """aldiv/ldiv as the original computes (L-04): the short path when the
    divisor's high word is its low word's sign "extension" with -1 negated
    to 1 (two divs by the low word), else sixteen shift-and-subtract steps
    with signed high-word compares and the quotient's high word cleared.
    Returns (dx, ax) or DIVERR."""
    h, l = hi(a), lo(a)
    bh, bl = hi(b), lo(b)
    dx = 1 if bl & 0x8000 else 0
    if dx == bh:
        if bl == 0:
            return DIVERR
        sign = dx
        if h & 0x8000:
            h, l = neg32(h, l)
            sign = ~sign & 0xFFFF
        q, r = divmod(h, bl)
        l = ((r << 16) | l) // bl
        h = q
    else:
        sign = 0
        if h & 0x8000:
            sign = 0xFFFF
            h, l = neg32(h, l)
        ax, dx = 0, bh
        if dx & 0x8000:
            dx, bl = neg32(dx, bl)
            sign = ~sign & 0xFFFF
        for _ in range(16):
            ax = ((ax << 1) | (h >> 15)) & 0xFFFF
            h = ((h << 1) | (l >> 15)) & 0xFFFF
            l = (l << 1) & 0xFFFF
            if s16(dx) > s16(ax) or (s16(dx) == s16(ax) and bl > h):
                continue
            borrow = 1 if h < bl else 0
            h = (h - bl) & 0xFFFF
            ax = (ax - dx - borrow) & 0xFFFF
            l = (l + 1) & 0xFFFF
        h = 0
    if sign & 0x8000:
        h, l = neg32(h, l)
    return h, l


def model_rem(a, b, through_pointer):
    """alrem/lrem as the original computes: exact for a divisor of one word
    (-32768..32767; the remainder sign-extended from 16 bits), else the
    sixteen steps of model_div (signed high-word compares). Returns
    (stored, (dx, ax)) - alrem's DX:AX on the long way is not the result
    but (|b| >> 16, the result's high word) - or DIVERR."""
    h, l = hi(a), lo(a)
    bh, bl = hi(b), lo(b)
    dx = 0xFFFF if bl & 0x8000 else 0
    ax = (-bl) & 0xFFFF if dx else bl
    if dx == bh:
        if ax == 0:
            return DIVERR
        if h & 0x8000:
            h, l = neg32(h, l)
        r = h % ax
        r = ((r << 16) | l) % ax
        if a < 0:
            r = (-r) & 0xFFFF
        v = s16(r)
        return v, (hi(v), lo(v))
    if h & 0x8000:
        h, l = neg32(h, l)
    ax, dx = 0, bh
    if dx & 0x8000:
        dx, bl = neg32(dx, bl)
    for _ in range(16):
        ax = ((ax << 1) | (h >> 15)) & 0xFFFF
        h = ((h << 1) | (l >> 15)) & 0xFFFF
        l = (l << 1) & 0xFFFF
        if s16(dx) > s16(ax) or (s16(dx) == s16(ax) and bl > h):
            continue
        borrow = 1 if h < bl else 0
        h = (h - bl) & 0xFFFF
        ax = (ax - borrow - dx) & 0xFFFF
    if a < 0:
        ax, h = neg32(ax, h)
    v = s32(ax << 16 | h)
    return v, ((dx, ax) if through_pointer else (hi(v), lo(v)))


def long_cases(rng, n, cset, divide):
    """Pairs (a, b). Ordinary: dividends of every size, divisors of every
    length and sign. Edge: the limits of int and long, around 2**16 and
    2**31, the short path's set 0x18000..0x1ffff."""
    out = []
    if cset == 'ordinary':
        for _ in range(n):
            a = rng.getrandbits(rng.randint(1, 31)) * rng.choice((1, -1))
            b = rng.getrandbits(rng.randint(1, 31 if rng.random() < 0.7 else 16)) * rng.choice((1, -1))
            if divide and b == 0:
                b = 7
            out.append((s32(a), s32(b)))
        return out
    special = [0, 1, -1, 2, -2, 255, 256, 32767, 32768, -32768, -32769, 65535, 65536,
               -65535, -65536, 65537, 0x18000, 0x1FFFF, 0x20000, 0x3FFFFFFF, 0x40000000,
               -0x40000000, 0x7FFFFFFF, -0x7FFFFFFF, -0x80000000]
    for x in special:
        for y in special:
            out.append((x, y))
    for _ in range(n):
        x = rng.choice(special) + rng.randint(-3, 3)
        y = rng.choice(special) + rng.randint(-3, 3)
        if rng.random() < 0.5:
            x = s32(rng.getrandbits(32))
        out.append((s32(x), s32(y)))
    return out


def check_long(rt, tally, rng, n, fixed):
    p = SCRATCH
    for entry in ('lmul', 'almul', 'ldiv', 'aldiv', 'lrem', 'alrem'):
        op = entry[-3:]
        pointer = entry.startswith('a')
        for cset in ('ordinary', 'edge'):
            for a, b in long_cases(rng, n, cset, op != 'mul'):
                if pointer:
                    rt.put(p, long_bytes(a))
                    r = rt.call(entry, regs={'bx': GUARD}, args=(p, hi(b), lo(b)))
                else:
                    r = rt.call(entry, args=(hi(a), lo(a), hi(b), lo(b)))
                if r is None:
                    got = rt.raised
                else:
                    stored = long_of(rt.mem(p, 4)) if pointer else None
                    got = (stored, (r['dx'], r['ax']), tuple(saved(r, bx=True)))
                # the right outcome
                if op == 'mul':
                    v = s32(a * b)
                elif b == 0:
                    v = None
                elif op == 'div':
                    v = None if (a, b) == (-2 ** 31, -1) else s32(c_div(a, b))
                else:
                    v = c_rem(a, b)
                if op != 'mul' and b == 0:
                    right = DIVERR
                elif v is None:
                    right = UNDEFINED                   # -2**31 / -1
                else:
                    right = (v if pointer else None, (hi(v), lo(v)), ())
                # the model
                model, bug = None, None
                if op == 'div' and 'L-04' not in fixed:
                    m = model_div(a, b)
                    model = m if m == DIVERR else ((s32(m[0] << 16 | m[1]) if pointer else None), m, ())
                    bug = 'L-04'
                elif op == 'rem':
                    # with L-08 corrected, alrem leaves what it stores in DX:AX
                    m = model_rem(a, b, pointer and 'L-08' not in fixed)
                    if m != DIVERR:
                        stored, dxax = m
                        wrong_value = v is not None and stored != v
                        wrong_dxax = v is not None and dxax != (hi(v), lo(v))
                        if wrong_value and 'L-04' not in fixed:
                            model, bug = (stored if pointer else None, dxax, ()), 'L-04'
                        elif not wrong_value and wrong_dxax and 'L-08' not in fixed:
                            model, bug = (stored if pointer else None, dxax, ()), 'L-08'
                tally.add(entry, cset, got, right, model, bug,
                          lambda: '%d, %d: %s (right %s)' % (
                              a, b, got if isinstance(got, str) else
                              '%s, DX:AX %d%s' % (got[0], s32(got[1][0] << 16 | got[1][1]),
                                                  ' %s not kept' % ','.join(got[2]) if got[2] else ''),
                              'divide error' if right == DIVERR else v))


# ---------------------------------------------------------------------------
# The floating-point core: dmath.o's dadd, dsub, dmul, ddiv ([si] op [di]
# into fac) and the operations on the stack built on it

def model_dmul(a, b):
    """dmul's product (L-05): pmuld takes the partial product a0*b2 from
    b1's word, a = [si], b = [di]; the sum rounded as the others."""
    va, vb = value(a), value(b)
    if va == 0 or vb == 0:
        return ZERO
    ma, mb = mantissa(a), mantissa(b)
    a0, b1, b2 = ma & 0xFFFF, (mb >> 16) & 0xFFFF, (mb >> 32) & 0xFFFF
    p = ma * mb + ((a0 * b1 - a0 * b2) << 32)
    v = Fraction(p) * Fraction(2) ** (a[7] + b[7] - 368)
    return rounded(-v if (va < 0) != (vb < 0) else v)


def negated(b):
    t = bytearray(b)
    t[6] ^= 0x80
    return bytes(t)


def model_dadd(a, b, subtract):
    """dadd/dsub ([si] + [di], [si] - [di]) as the original computes:
    rounded right (ties to even), but an operand whose exponent is 57 or
    more below the other's is dropped - the other is copied as it is
    (L-14); a zero operand gives the other one's bytes."""
    if subtract:
        b = negated(b)
    if a[7] == 0:
        return outcome(b)
    if b[7] == 0:
        return outcome(a)
    if abs(a[7] - b[7]) > 56:
        return outcome(a if a[7] > b[7] else b)
    return rounded(value(a) + value(b))


def model_core(core, x, y):
    """The core routine's result for [si] = x, [di] = y, as the original
    computes it, and the bug that makes it differ (if it does)."""
    if core == 'dadd':
        return model_dadd(x, y, False), 'L-14'
    if core == 'dsub':
        return model_dadd(x, y, True), 'L-14'
    if core == 'dmul':
        return model_dmul(x, y), 'L-05'
    if value(y) == 0:
        return SIGFPE, None
    return rounded(value(x) / value(y)), None


def dbl_random(rng, lo_e=1, hi_e=255):
    """A double image: a random mantissa (sometimes with long runs of
    zeros or ones at the bottom), an exponent byte in [lo_e, hi_e], a
    random sign."""
    k = rng.random()
    m = rng.getrandbits(55)
    if k < 0.15:
        m &= ~((1 << rng.randint(0, 55)) - 1)
    elif k < 0.25:
        m = (1 << 55) - 1 - rng.getrandbits(rng.randint(0, 20))
    return image(rng.random() < 0.5, m | HIDDEN, rng.randint(lo_e, hi_e))


def zero_forms(rng):
    """Zeros as the runtime makes them: all bytes 0; the sign bit set
    (fneg of 0); old bytes below an exponent byte of 0 (ltof(0), L-02)."""
    junk = bytes(rng.getrandbits(8) for _ in range(6))
    return [ZERO, bytes(6) + b'\x80\x00', junk + b'\x00\x00', junk + b'\x80\x00']


def flt_pairs(rng, n, cset):
    out = []
    if cset == 'ordinary':
        # magnitudes as programs have them: 2**-40 .. 2**40
        for _ in range(n):
            out.append((dbl_random(rng, 88, 168), dbl_random(rng, 88, 168)))
        return out
    for _ in range(n):
        r = rng.random()
        if r < 0.3:
            a, b = dbl_random(rng), dbl_random(rng)
        elif r < 0.55:      # nearly equal: cancellation, either sign
            a = dbl_random(rng, 2, 254)
            m = (mantissa(a) + rng.randint(-2000, 2000)) & (HIDDEN - 1)
            b = image(bool(a[6] & 0x80) != (rng.random() < 0.5), m | HIDDEN,
                      a[7] + rng.choice((0, 0, 0, 1, -1)))
        elif r < 0.62:      # a power of two and an operand 55 to 59 binary orders below
            e = rng.randint(62, 250)
            eb = e - rng.randint(55, 59)
            a, b = image(rng.random() < 0.5, HIDDEN, e), dbl_random(rng, eb, eb)
            a, b = (a, b) if rng.random() < 0.5 else (b, a)
        elif r < 0.85:      # the ends of the range
            a = dbl_random(rng, *rng.choice(((1, 4), (252, 255), (1, 255), (120, 136))))
            b = dbl_random(rng, *rng.choice(((1, 4), (252, 255), (1, 255), (120, 136))))
        else:               # zeros of every form
            z = rng.choice(zero_forms(rng))
            x = dbl_random(rng)
            a, b = (z, x) if rng.random() < 0.5 else (x, z)
            if rng.random() < 0.2:
                b = rng.choice(zero_forms(rng))
        out.append((a, b))
    return out


def outcome(b):
    """A floating result as compared: the value's image, with every zero
    the same (only the exponent byte says zero)."""
    if b is None or isinstance(b, str):
        return b
    return ZERO if b[7] == 0 else bytes(b)


def check_core(rt, tally, rng, n, fixed):
    A, B = SCRATCH, SCRATCH + 0x10
    ops = (('dadd', lambda x, y: x + y), ('dsub', lambda x, y: x - y),
           ('dmul', lambda x, y: x * y), ('ddiv', lambda x, y: x / y))
    for entry, f in ops:
        for cset in ('ordinary', 'edge'):
            for a, b in flt_pairs(rng, n, cset):
                rt.put(A, a)
                rt.put(B, b)
                r = rt.call(entry, regs={'si': A, 'di': B})
                got = rt.raised if r is None else outcome(rt.mem(rt.fac, 8))
                if entry == 'ddiv' and value(b) == 0:
                    right = SIGFPE
                else:
                    right = rounded(f(value(a), value(b)))
                    right = SIGFPE if right == OVF else right
                model, bug = model_core(entry, a, b)
                if bug in fixed:
                    model, bug = None, None
                model = SIGFPE if model == OVF else model
                tally.add(entry, cset, got, right, model, bug,
                          lambda: '%s %s %s: %s (right %s)' % (
                              show_dbl(a), entry, show_dbl(b), show_dbl(got), show_dbl(right)))


# The operations on the floating-point stack: (entry, how many slots it
# takes, the memory operand's kind - 'd' a double, 's' a float, None -,
# the operation on (the stack operands, the memory operand), the core
# routine it calls and what that routine gets as [si] and [di]).
STACK_OPS = (
    ('fadd', 2, None, lambda s, m: s[0] + s[1], 'dadd', lambda s, m: (s[1], s[0])),
    ('fsub', 2, None, lambda s, m: s[0] - s[1], 'dsub', lambda s, m: (s[0], s[1])),
    ('fmul', 2, None, lambda s, m: s[0] * s[1], 'dmul', lambda s, m: (s[1], s[0])),
    ('fdiv', 2, None, lambda s, m: s[0] / s[1], 'ddiv', lambda s, m: (s[0], s[1])),
    ('faddd', 1, 'd', lambda s, m: s[0] + m, 'dadd', lambda s, m: (m, s[0])),
    ('fsubd', 1, 'd', lambda s, m: s[0] - m, 'dsub', lambda s, m: (s[0], m)),
    ('fsubrd', 1, 'd', lambda s, m: m - s[0], 'dsub', lambda s, m: (m, s[0])),
    ('fmuld', 1, 'd', lambda s, m: s[0] * m, 'dmul', lambda s, m: (m, s[0])),
    ('fdivd', 1, 'd', lambda s, m: s[0] / m, 'ddiv', lambda s, m: (s[0], m)),
    ('fdivrd', 1, 'd', lambda s, m: m / s[0], 'ddiv', lambda s, m: (m, s[0])),
    ('fadds', 1, 's', lambda s, m: s[0] + m, 'dadd', lambda s, m: (s[0], m)),
    ('fsubs', 1, 's', lambda s, m: s[0] - m, 'dsub', lambda s, m: (s[0], m)),
    ('fsubrs', 1, 's', lambda s, m: m - s[0], 'dsub', lambda s, m: (m, s[0])),
    ('fmuls', 1, 's', lambda s, m: s[0] * m, 'dmul', lambda s, m: (s[0], m)),
    ('fdivs', 1, 's', lambda s, m: s[0] / m, 'ddiv', lambda s, m: (s[0], m)),
    ('fdivrs', 1, 's', lambda s, m: m / s[0], 'ddiv', lambda s, m: (m, s[0])),
)


def check_stackops(rt, tally, rng, n, fixed):
    """Each operation with one more slot below its operands (which must
    stay), the memory operand at SCRATCH (a float followed by eight other
    bytes), the result in the right slot and fpsp right."""
    M = SCRATCH
    for entry, take, kind, f, core, operands in STACK_OPS:
        for cset in ('ordinary', 'edge'):
            for a, b in flt_pairs(rng, n, cset):
                below = dbl_random(rng)
                after = dbl_random(rng)             # the bytes behind a float operand
                if kind == 's':
                    mem = bytes(4) + b[4:]          # the float, as ftod makes it a double
                    rt.put(M, b[4:] + after)
                elif kind == 'd':
                    mem = b
                    rt.put(M, b)
                else:
                    mem = None
                stack = [below, a] if take == 1 else [below, a, b]
                rt.fp_set(stack)
                r = rt.call(entry, regs={'ax': M})
                if r is None:
                    got = rt.raised
                else:
                    slots = rt.fp_get()
                    if slots is None or len(slots) != 2 or slots[0] != below:
                        got = ('stack', None if slots is None else len(slots))
                    else:
                        got = (outcome(slots[1]), tuple(saved(r)))
                ops = [a, b] if take == 2 else [a]
                v = None
                try:
                    v = rounded(f([value(x) for x in ops], None if mem is None else value(mem)))
                except ZeroDivisionError:
                    pass
                right = SIGFPE if v is None or v == OVF else (v, ())
                x, y = operands(ops, mem)
                if entry == 'fsubrs':
                    # SI is left behind the float (ftod's two movsw): what
                    # dsub takes for [si] is the eight bytes after it
                    x, bug9 = after, 'L-09'
                m, bug = model_core(core, x, y)
                if entry == 'fsubrs':
                    if bug in fixed:        # dsub itself corrected (L-14): right on x, y
                        m = rounded(value(x) - value(y))
                    bug = bug9
                model = None if bug in fixed or bug is None else (SIGFPE if m in (OVF, SIGFPE) else (m, ()))
                tally.add(entry, cset, got, right, model, bug,
                          lambda: '%s %s %s: %s (right %s)' % (
                              show_dbl(a), entry, show_dbl(b if mem is None else mem),
                              show_dbl(got[0]) if isinstance(got, tuple) else got,
                              show_dbl(right[0]) if isinstance(right, tuple) else right))


# ---------------------------------------------------------------------------
# The stack itself: fldd, flds, fdup push; fstd, fsts store the top;
# fstdp, fstsp store and pop; fneg negates the top. A store of a float
# keeps the double's high four bytes (truncates) - one of the two
# neighbours C allows, by design.

def check_stack(rt, tally, rng, n, fixed):
    M = SCRATCH
    for entry in ('fldd', 'flds', 'fdup', 'fstd', 'fsts', 'fstdp', 'fstsp', 'fneg'):
        for cset in ('ordinary', 'edge'):
            for i in range(n):
                # pushes: up to 7 slots before (edge: 8, a full stack);
                # the others need a top (edge: the pops also an empty
                # stack - fneg, fstd and fsts do not look)
                pushes = entry in ('fldd', 'flds', 'fdup')
                k = rng.randint(1, 7) if pushes else rng.randint(1, 8)
                if cset == 'edge' and rng.random() < 0.3:
                    k = 8 if pushes else rng.choice((0, 8)) if entry.endswith('p') else 8
                slots = [dbl_random(rng) if cset == 'ordinary' or rng.random() < 0.7
                         else rng.choice(zero_forms(rng)) for _ in range(k)]
                x = dbl_random(rng)
                rt.put(M, x + bytes(rng.getrandbits(8) for _ in range(8)))
                old = rt.mem(M, 16)
                rt.fp_set(slots)
                r = rt.call(entry, regs={'ax': M})
                if r is None:
                    got = rt.raised
                else:
                    slots_after = rt.fp_get()
                    got = (tuple(slots_after) if slots_after is not None else 'bad fpsp',
                           rt.mem(M, 16), tuple(saved(r)))
                if pushes and k == 8 or not pushes and k == 0:
                    right = SIGFPE
                else:
                    new, mem = list(slots), old
                    if entry == 'fldd':
                        new.append(x)
                    elif entry == 'flds':
                        new.append(bytes(4) + x[:4])
                    elif entry == 'fdup':
                        new.append(slots[-1])
                    elif entry == 'fneg':
                        t = bytearray(slots[-1])
                        t[6] ^= 0x80
                        new[-1] = bytes(t)
                    else:
                        part = slots[-1] if entry in ('fstd', 'fstdp') else slots[-1][4:]
                        mem = part + old[len(part):]
                        if entry.endswith('p'):
                            new.pop()
                    right = (tuple(new), mem, ())
                tally.add(entry, cset, got, right, None, None,
                          lambda: '%d slots: %s' % (k, got if isinstance(got, str) else 'result differs'))


# ---------------------------------------------------------------------------
# Comparisons: fcmp pops two and sets the flags as for "second - top"
# (SF, ZF; AH = lahf for c1's sahf); ftest pops one, the flags of its value.

def flags_of(r):
    """(ZF, SF) of the flags and of AH, as c1's branches read them."""
    f, ah = r['flags'], r['ax'] >> 8
    return (bool(f & 0x40), bool(f & 0x80), bool(ah & 0x40), bool(ah & 0x80))


def check_cmp(rt, tally, rng, n, fixed):
    for entry in ('fcmp', 'ftest'):
        for cset in ('ordinary', 'edge'):
            pairs = flt_pairs(rng, n, cset)
            if cset == 'edge':     # equal values, and the largest of either sign
                pairs += [(a, a) for a, _ in pairs[:n // 10]]
                pairs += [(dbl_random(rng, 250, 255), dbl_random(rng, 250, 255)) for _ in range(n // 5)]
            for a, b in pairs:
                below = dbl_random(rng)
                rt.fp_set([below, a, b] if entry == 'fcmp' else [below, a])
                r = rt.call(entry)
                if r is None:
                    got = rt.raised
                else:
                    slots = rt.fp_get()
                    got = (flags_of(r), slots == [below], tuple(saved(r)))
                va = value(a)
                vb = value(b) if entry == 'fcmp' else Fraction(0)
                eq, lt = va == vb, va < vb
                right = ((eq, lt, eq, lt), True, ())
                model, bug = None, None
                if entry == 'fcmp' and 'L-10' not in fixed:
                    d = rounded(va - vb)
                    if d == OVF:
                        model = SIGFPE
                    else:
                        z, s = d[7] == 0, d[7] != 0 and bool(d[6] & 0x80)
                        model = ((z, s, z, s), True, ())
                    bug = 'L-10'
                tally.add(entry, cset, got, right, model, bug,
                          lambda: '%s cmp %s: %s (right %s)' % (
                              show_dbl(a), show_dbl(b), describe_flags(got), describe_flags(right)))


def describe_flags(g):
    if isinstance(g, str):
        return g
    zf, sf = g[0][0], g[0][1]
    return 'equal' if zf else 'less' if sf else 'greater'


# ---------------------------------------------------------------------------
# Conversions: itof (AX -> a new top), ftoi (the top, popped -> AX), ltof
# (a long on the stack -> a new top), ftol (the top, popped -> DX:AX).

def int_cases(rng, n, cset):
    if cset == 'ordinary':
        return [s16(rng.getrandbits(rng.randint(1, 15)) * rng.choice((1, -1))) for _ in range(n)]
    out = [0, 1, -1, 127, 128, 255, 256, 257, 1001, 32767, -32767, -32768, 0x7F00, 0x4001]
    return out + [s16(rng.getrandbits(16)) for _ in range(n)]


def model_itof(i):
    """itof as the original computes (L-01): the right image, but AX still
    holds the low mantissa byte in its high half when it fills bytes 0..3."""
    b = rounded(Fraction(i))
    return b if i == 0 else bytes([0, b[5], 0, b[5]]) + b[4:]


def dbl_cases(rng, n, cset, top):
    """Doubles to convert to an integer of top bits: ordinary ones inside
    its range; edge ones around every power of two up to 2**top and
    beyond, halves, and zeros."""
    out = []
    if cset == 'ordinary':
        for _ in range(n):
            e = rng.randint(129, 128 + top - 1)
            out.append(dbl_random(rng, e, e))
        return out
    for _ in range(n):
        r = rng.random()
        if r < 0.6:
            k = rng.randint(0, top + 2)
            v = Fraction(2) ** k + Fraction(rng.randint(-4, 4), rng.choice((1, 2, 4, 256)))
            out.append(rounded(v * rng.choice((1, -1))))
        elif r < 0.8:
            out.append(dbl_random(rng, 100, 128 + top + 3))
        else:
            out.append(rng.choice(zero_forms(rng)))
    return out


def check_conv(rt, tally, rng, n, fixed):
    # itof
    for cset in ('ordinary', 'edge'):
        for i in int_cases(rng, n, cset):
            below = dbl_random(rng)
            rt.fp_set([below])
            r = rt.call('itof', regs={'ax': i & 0xFFFF})
            slots = None if r is None else rt.fp_get()
            got = rt.raised if r is None else (
                tuple(slots) if slots else 'bad fpsp', tuple(saved(r)))
            right = ((below, rounded(Fraction(i))), ())
            model = None if 'L-01' in fixed else ((below, model_itof(i)), ())
            tally.add('itof', cset, got, right, model, 'L-01',
                      lambda: '%d: %s (right %s)' % (i, show_dbl(got[0][-1]) if isinstance(got, tuple)
                                                     else got, show_dbl(right[0][-1])))
    # ltof: a long on the stack (high word next to the return address)
    for cset in ('ordinary', 'edge'):
        cases = [p for p, _ in long_cases(rng, n, cset, False)]
        for v in cases:
            below = dbl_random(rng)
            junk = bytes(rng.getrandbits(8) for _ in range(8))
            rt.fp_set([below, junk])
            rt.put_word(rt.sym['fpsp'], rt.fpstk)          # one slot, junk above it
            r = rt.call('ltof', args=(hi(v), lo(v)))
            slots = None if r is None else rt.fp_get()
            got = rt.raised if r is None else (tuple(slots) if slots else 'bad fpsp',
                                               r['sp'] - r['sp_in'], tuple(saved(r)))
            right = ((below, rounded(Fraction(v))), 2, ())
            model = None
            if v == 0 and 'L-02' not in fixed:
                model = ((below, junk[:6] + bytes(2)), 2, ())
            tally.add('ltof', cset, got, right, model, 'L-02',
                      lambda: '%d: %s (right %s)' % (v, show_dbl(got[0][-1]) if isinstance(got, tuple)
                                                     else got, show_dbl(right[0][-1])))
    # ftoi, ftol; ftoi's 2**15 <= |x| < 2**16 reads the word at 8000
    for entry, top in (('ftoi', 16), ('ftol', 32)):
        for cset in ('ordinary', 'edge'):
            for x in dbl_cases(rng, n, cset, top):
                below = dbl_random(rng)
                w8000 = rng.choice((0, 0x8000, (x[6] << 8) | x[5]))
                rt.put_word(0x8000, w8000)
                rt.fp_set([below, x])
                r = rt.call(entry)
                if r is None:
                    got = rt.raised
                else:
                    res = s16(r['ax']) if top == 16 else s32(r['dx'] << 16 | r['ax'])
                    got = (res, rt.fp_get() == [below], tuple(saved(r)))
                v = value(x)
                t = int(v)                      # truncated toward zero
                inside = -(1 << (top - 1)) <= t < (1 << (top - 1))
                right = (t, True, ()) if inside else SIGFPE
                model, bug = None, None
                e = x[7] - 128
                if entry == 'ftoi' and e == 16 and 'L-03' not in fixed:
                    bx = (x[6] << 8) | x[5]
                    model = (s16(bx), True, ()) if bx == w8000 and not x[4] & 0x80 else SIGFPE
                    bug = 'L-03'
                if entry == 'ftol' and e >= 31 and 'L-11' not in fixed:
                    model, bug = SIGFPE, 'L-11'
                tally.add(entry, cset, got, right, model, bug,
                          lambda: '%s: %s (right %s)' % (
                              show_dbl(x), got if isinstance(got, str) else got[0],
                              right if isinstance(right, str) else right[0]))


# ---------------------------------------------------------------------------
# chkstk: AX the frame size; the stack pointer moved down by it, or the
# stack grown first (the far call through Isgadr - mutosemu grants every
# request) when the frame would go below Stkbot.

def check_chkstk(rt, tally, rng, n, fixed):
    grows = []
    rt.hook(mutosemu.STKGROW_OFF, lambda uc, a, s, u: grows.append(rt.emu.reg('bx')))
    stkbot = rt.sym['Stkbot']
    for cset in ('ordinary', 'edge'):
        for _ in range(n):
            if cset == 'ordinary':
                sp = rng.randrange(0xC000, 0xFF00, 2)
                size = rng.randrange(100, 4000, 2)
                bot = rng.choice((sp - rng.randint(0, 8000), 0x1000, 0xFFFF)) & 0xFFFE
            else:
                # a stack pointer low in the segment, frames up to c1's
                # largest (32766), some larger than the room below
                sp = rng.randrange(0x0100, 0x8000, 2)
                size = rng.choice((rng.randrange(100, 32767, 2), sp + rng.randrange(0, 64, 2),
                                   max(100, sp - rng.randrange(0, 64, 2))))
                bot = rng.choice((rng.randrange(0, 0x100, 2), 0xFFFF, sp - size))
            bot &= 0xFFFF
            rt.put_word(stkbot, bot)
            del grows[:]
            r = rt.call('chkstk', regs={'ax': size}, sp=sp + 2)
            if r is None:
                got = rt.raised
            else:
                got = (r['sp'], len(grows) > 0, r['si'] == GUARD and r['di'] == GUARD
                       and r['bp'] == GUARD)
            want = sp - size                    # sp at the call: sp (the return address)
            if want >= bot and want >= 0:
                right = (want, False, True)
            elif want >= 0:
                right = (want, True, True)
            else:
                # no room in the segment at all: a request to grow (the
                # kernel refuses it, chkstk then kills with SIGSEG) - never
                # a stack pointer wrapped round the segment
                right = (lambda g: not isinstance(g, str) and g[1])
            model, bug = None, None
            if want < 0 and 'L-13' not in fixed:
                w = want & 0xFFFF
                model = (w, False, True) if w >= bot else None
                bug = 'L-13'
            tally.add('chkstk', cset, got, right, model, bug,
                      lambda: 'sp %04x, frame %d, Stkbot %04x: %s' % (
                          sp, size, bot, got if isinstance(got, str) else
                          'sp %04x%s' % (got[0], ', stack grown' if got[1] else '')))


# ---------------------------------------------------------------------------
# Where the floating-point stack lies: incfpsp and decfpsp compare fpsp
# with its ends signed (jge, jl). Linked with BSS before it so that fpstk
# lies at, below and above 0x8000: eight pushes and eight pops must go
# through, a ninth push and a pop of an empty stack must raise SIGFPE.

def stack_walk(rt):
    """Eight pushes one by one, a ninth; eight pops of a full stack, a
    ninth: where SIGFPE came (it must come at the ninth of each, and only
    there)."""
    out = []
    rt.fp_set([])
    rt.put(SCRATCH, rounded(Fraction(1)))
    for i in range(9):
        if rt.call('fldd', regs={'ax': SCRATCH}) is None:
            out.append('push %d: %s' % (i + 1, rt.raised))
            break
    rt.fp_set([rounded(Fraction(i + 1)) for i in range(8)])
    for i in range(9):
        if rt.call('fstdp', regs={'ax': SCRATCH}) is None:
            out.append('pop %d: %s' % (i + 1, rt.raised))
            break
    return ', '.join(out) or 'no SIGFPE'


def model_walk(fpstk):
    """The same, with incfpsp's and decfpsp's signed compares (jge, jl)."""
    out = []
    sp = fpstk - 8
    for i in range(9):
        sp += 8
        if s16(sp) >= s16(fpstk + 0x40):
            out.append('push %d: %s' % (i + 1, SIGFPE))
            break
    sp = fpstk + 0x38
    for i in range(9):
        sp -= 8
        if s16(sp) < s16(fpstk - 8):
            out.append('pop %d: %s' % (i + 1, SIGFPE))
            break
    return ', '.join(out) or 'no SIGFPE'


def check_layout(rt0, tally, rng, n, fixed, archive):
    base = rt0.fpstk
    targets = [0x7E00, 0x7F80, 0x7FB8, 0x7FC0, 0x7FC8, 0x7FD0, 0x7FE8, 0x7FF0, 0x7FF8,
               0x8000, 0x8008, 0x8010, 0x8040, 0x8048, 0x8100, 0x9000]
    for t in targets:
        rt = Runtime(archive, pad=t - base)
        got = stack_walk(rt)
        model = None if 'L-12' in fixed else model_walk(rt.fpstk)
        tally.add('fpstk', 'edge', got, 'push 9: SIGFPE, pop 9: SIGFPE', model, 'L-12',
                  lambda: 'fpstk at %04x: %s' % (rt.fpstk, got))


# ---------------------------------------------------------------------------

GROUPS = ('long', 'float', 'conv', 'stack', 'cmp', 'chkstk', 'layout')


def copy_corrections():
    """The bugs the corrected copy of libc.a corrects, and in which
    members: {bug: {member name without .o}}, from the register of
    docs/RULES.md, "The corrected copy of libc.a"."""
    out, inside = {}, False
    with open(RULES, encoding='utf-8') as f:
        for line in f:
            if re.match(r'\|\s*Correction\s*\|\s*Member\s*\|', line):
                inside = True
            elif inside and not line.startswith('|'):
                break
            elif inside:
                m = re.match(r'\|\s*(L-\d\d)\s*\|\s*`?(\w+)\.o`?\s*\|', line)
                if m:
                    out.setdefault(m.group(1), set()).add(m.group(2))
    return out


def main(argv):
    n, seed, examples, archive, fixed = 1000, 1, 2, None, set()
    groups = []
    args = list(argv)
    if args and args[0] == 'show':
        return show(args[1:])
    while args:
        a = args.pop(0)
        if a == '-n':
            n = int(args.pop(0))
        elif a == '--seed':
            seed = int(args.pop(0))
        elif a == '--examples':
            examples = int(args.pop(0))
        elif a == '--archive':
            archive = os.path.abspath(args.pop(0))
        elif a == '--fixed':
            fixed = set(args.pop(0).split(','))
        elif a == '--copy':
            archive = COPY
            if args and args[0].endswith('.a'):     # another copy: src/libc_fixed/arith.a
                archive = os.path.abspath(args.pop(0))
            if not os.path.isfile(archive):
                sys.exit('rtcheck.py: %s is not built - run "make" first' % archive)
            fixed = None        # the corrections of the members it has: below
        elif a in GROUPS:
            groups.append(a)
        elif a == 'all':
            groups.extend(GROUPS)
        else:
            print(__doc__)
            return 2
    groups = groups or list(GROUPS)
    rng = random.Random(seed)
    rt = Runtime(archive)
    if fixed is None:
        fixed = {bug for bug, ms in copy_corrections().items()
                 if ms & set(Runtime.from_archive)}
    tally = Tally(examples)
    for g in GROUPS:
        if g not in groups:
            continue
        if g == 'long':
            check_long(rt, tally, rng, n, fixed)
        elif g == 'float':
            check_core(rt, tally, rng, n, fixed)
            check_stackops(rt, tally, rng, max(n // 4, 50), fixed)
        elif g == 'conv':
            check_conv(rt, tally, rng, n, fixed)
        elif g == 'stack':
            check_stack(rt, tally, rng, max(n // 4, 50), fixed)
        elif g == 'cmp':
            check_cmp(rt, tally, rng, n, fixed)
        elif g == 'chkstk':
            check_chkstk(rt, tally, rng, n, fixed)
        elif g == 'layout':
            check_layout(rt, tally, rng, n, fixed, archive)
    tally.report('libc.a runtime%s, seed %d, %d random cases per entry point and set'
                 % (' (%s from %s%s)' % (' '.join(Runtime.from_archive), archive,
                                        ', corrected: ' + ','.join(sorted(fixed)) if fixed else '')
                    if archive else '', seed, n))
    if tally.unexplained:
        print('FAIL: %d results no model explains' % tally.unexplained)
        return 1
    print('every result right or explained by a known bug of the original')
    return 0


def show(args):
    if not args:
        print(__doc__)
        return 2
    archive = None
    if args[0] == '--archive' and len(args) > 2:
        archive, args = os.path.abspath(args[1]), args[2:]
    rt = Runtime(archive)
    entry, rest = args[0], args[1:]
    if entry in ('lmul', 'ldiv', 'lrem', 'almul', 'aldiv', 'alrem'):
        a, b = (s32(int(x, 0)) for x in rest)
        if entry.startswith('a'):
            rt.put(SCRATCH, long_bytes(a))
            r = rt.call(entry, args=(SCRATCH, hi(b), lo(b)))
        else:
            r = rt.call(entry, args=(hi(a), lo(a), hi(b), lo(b)))
        if r is None:
            print('%s(%d, %d): %s' % (entry, a, b, rt.raised))
        else:
            print('%s(%d, %d): %sDX:AX %d' % (
                entry, a, b, 'stored %d, ' % long_of(rt.mem(SCRATCH, 4)) if entry.startswith('a')
                else '', s32(r['dx'] << 16 | r['ax'])))
    elif entry == 'itof':
        i = s16(int(rest[0], 0))
        rt.fp_set([])
        rt.call('itof', regs={'ax': i & 0xFFFF})
        print('itof(%d): %s' % (i, show_dbl(rt.fp_get()[-1])))
    elif entry == 'ltof':
        v = s32(int(rest[0], 0))
        rt.fp_set([])
        rt.call('ltof', args=(hi(v), lo(v)))
        print('ltof(%d): %s' % (v, show_dbl(rt.fp_get()[-1])))
    elif entry in ('ftoi', 'ftol'):
        x = dbl_text(rest[0])
        rt.fp_set([x])
        r = rt.call(entry)
        print('%s(%s): %s' % (entry, show_dbl(x), rt.raised if r is None else
                              s16(r['ax']) if entry == 'ftoi' else s32(r['dx'] << 16 | r['ax'])))
    elif entry in ('dadd', 'dsub', 'dmul', 'ddiv'):
        a, b = dbl_text(rest[0]), dbl_text(rest[1])
        rt.put(SCRATCH, a)
        rt.put(SCRATCH + 16, b)
        r = rt.call(entry, regs={'si': SCRATCH, 'di': SCRATCH + 16})
        print('%s(%s, %s): %s' % (entry, show_dbl(a), show_dbl(b),
                                  rt.raised if r is None else show_dbl(rt.mem(rt.fac, 8))))
    elif entry == 'fcmp':
        a, b = dbl_text(rest[0]), dbl_text(rest[1])
        rt.fp_set([a, b])
        r = rt.call('fcmp')
        print('fcmp(%s, %s): %s' % (show_dbl(a), show_dbl(b),
                                    rt.raised if r is None else describe_flags((flags_of(r),))))
    else:
        print('rtcheck.py: show knows the long helpers, itof, ltof, ftoi, ftol, dadd, dsub, '
              'dmul, ddiv and fcmp')
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
