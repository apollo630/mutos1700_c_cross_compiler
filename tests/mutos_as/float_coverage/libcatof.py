#!/usr/bin/env python3
"""libcatof.py - libc.a's own atof(), run on libc.a's own floating-point
runtime under an 8086 emulator: a second oracle for mutos_as's
.float/.double conversion, next to fltmodel.py, made of the real
toolchain's own machine code instead of a model of it.

What runs: the real objects from tests/mutos1700_libc/ (atof.o - v7's
algorithm, compiled by the real compiler - and the software runtime it
calls: stacks.o, stkmath.o, doubles.o, dmath.o, convert.o, plus ldexp.o,
cret.o, ctype_.o, fperr.o, cuexit.o, crt0.o) linked by mutos_ld into a
0407 image, and _atof(text) called directly under Unicorn's 8086 mode
(one 64K segment, a HLT as return address, no system calls). The result
is the double atof() leaves in fac; a .float is its high four bytes.

The real assembler converts exactly like this for every nonzero constant
seen so far (tests/mutos_as/float_coverage/'s goldens), including
libc.a's inexact product in dmul (its pmuld takes the partial product
a0*b2 from b1's word). Its zeros differ: libc.a's dmul/ddiv clear all of
fac for a zero operand, the real assembler's runtime clears only fac's
exponent byte. By default this tool patches dmath.o's "zero" routine to
do the same (mov byte fac+7,0 / ret) - "assembler zeros"; --libc runs
the unmodified runtime. Even so, one kind of zero is not reproduced: a
text whose digits are all 0 and whose decimal exponent is 0 (or which
atof() gives up on, LOGHUGE), where the result is what the digit loop
left in fac - -2**56 here, 00 00 00 00 ff ff ff in the real assembler
(see src/mutos_as/fltconst.h). The subcommands report those separately
and do not count them as differences.

Requires Python 3, the unicorn module (pip install unicorn) and a built
mutos_as and mutos_ld (top-level "make"). Not part of "make test"; run
it with "make check-libcatof".

Usage:
  libcatof.py atof [--libc] TEXT ...
      Prints the double (and its .float high half) atof() gives for each
      text - an atof() text: a "0f"/"0d" prefix is dropped and a d/D
      exponent read as e, as the assembler does.
  libcatof.py goldens [DIR ...]
      Every .float/.double constant in the goldens of the given
      directories (default: this script's own) against the emulation
      with assembler zeros. Exit 1 on a difference.
  libcatof.py check TOOL [N]
      Runs fixed edge cases plus N seeded random and targeted texts
      (default 3000) through TOOL (src/mutos_as/fltconst_test) and,
      for every one TOOL accepts, through the emulation; exit 1 if any
      accepted constant's bytes differ.
  libcatof.py ops [N]
      Checks the runtime's dmul, ddiv and dadd directly on N random
      operand pairs each (default 2000) against fltmodel.py's
      arithmetic: dmul against its "libc" product, ddiv and dadd against
      nearest-even. Exit 1 on a difference.
"""

import base64
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True      # no __pycache__ in the test directory
import fltmodel as F  # noqa: E402

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_DI,
                                   UC_X86_REG_ES, UC_X86_REG_FLAGS, UC_X86_REG_SI,
                                   UC_X86_REG_SP, UC_X86_REG_SS)
except ImportError:
    sys.exit('libcatof.py: needs the Python module "unicorn" (pip install unicorn)')

MUTOS_AS = os.path.join(ROOT, 'src', 'mutos_as', 'mutos_as')
MUTOS_LD = os.path.join(ROOT, 'src', 'mutos_ld', 'mutos_ld')
LIBC = os.path.join(ROOT, 'tests', 'mutos1700_libc')
CRT0 = os.path.join(ROOT, 'tests', 'mutos1700_crt0', 'crt0.o.base64.txt')

# Named explicitly: libc.a's __.SYMDEF is stale, so "-u _atof" alone does
# not pull the runtime in. libc.a follows for everything else.
MEMBERS = ('atof', 'ldexp', 'stacks', 'stkmath', 'doubles', 'dmath', 'convert',
           'cret', 'ctype_', 'fperr', 'cuexit')

SEG = 0x1000                # the image's segment (CS = DS = ES = SS)
BASE = SEG << 4
TEXT_AT, HLT_AT, SP_AT = 0xE000, 0xF000, 0xFF00
STEPS = 5000000             # instruction limit per call

# Offsets inside dmath.o's text, from its disassembly (checked below
# against the linked image, so a different libc.a is noticed).
DMUL, DDIV, DADD, ZERO, PMUL_B2 = 0x1ac, 0x1ed, 0x14, 0x1a1, 0x352


class Runtime:
    """The linked image and an emulator to call into it."""

    def __init__(self):
        for tool in (MUTOS_AS, MUTOS_LD):
            if not os.access(tool, os.X_OK):
                sys.exit('libcatof.py: %s is not built - run "make" first' % tool)
        work = tempfile.mkdtemp(prefix='libcatof.')
        try:
            self._link(work)
            with open(os.path.join(work, 'atof.out'), 'rb') as f:
                self._load(f.read())
        finally:
            shutil.rmtree(work)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x100000)

    @staticmethod
    def _link(work):
        def unb64(src, dst):
            with open(src, 'rb') as f, open(os.path.join(work, dst), 'wb') as g:
                g.write(base64.b64decode(f.read()))
        unb64(CRT0, 'crt0.o')
        for m in MEMBERS:
            unb64(os.path.join(LIBC, m + '.o.base64.txt'), m + '.o')
        unb64(os.path.join(LIBC, 'libc.a.base64.txt'), 'libc.a')
        with open(os.path.join(work, 'main.s'), 'w') as f:
            f.write('.globl\t_main\n.text\n_main:\nret\n')     # crt0.o needs one
        subprocess.run([MUTOS_AS, '-o', 'main.o', 'main.s'], cwd=work, check=True,
                       capture_output=True)
        r = subprocess.run([MUTOS_LD, '-o', 'atof.out', '-u', '_atof', 'crt0.o', 'main.o']
                           + [m + '.o' for m in MEMBERS] + ['libc.a'],
                           cwd=work, capture_output=True, text=True)
        if 'Undefined' in r.stdout + r.stderr or not os.path.exists(os.path.join(work, 'atof.out')):
            sys.exit('libcatof.py: linking failed:\n' + r.stdout + r.stderr)

    def _load(self, b):
        magic, t, d, bss, syms = struct.unpack('<5H', b[:10])
        if magic != 0o407:
            sys.exit('libcatof.py: unexpected a.out magic %o' % magic)
        self.image = bytearray(0x10000)
        self.image[:t + d] = b[16:16 + t + d]
        self.sym = {}
        for i in range(len(b) - syms, len(b), 12):
            name = b[i:i + 8].rstrip(b'\0').decode('latin-1')
            self.sym.setdefault(name, struct.unpack('<H', b[i + 10:i + 12])[0])
        dmath = self.sym['dmul'] - DMUL
        self.fac = self.sym['fac']
        self.zero = dmath + ZERO
        # dmath.o's zero routine: lea di,fac / sub ax,ax / stosw x4 / ret
        want = bytes([0x8d, 0x3e]) + struct.pack('<H', self.fac) + bytes.fromhex('2bc0abababab c3'.replace(' ', ''))
        if bytes(self.image[self.zero:self.zero + len(want)]) != want:
            sys.exit('libcatof.py: dmath.o\'s "zero" routine is not where expected')
        # pmuld's a0*b2 term: mov dx,2[di] (b1's word) where 4[di] was meant
        if bytes(self.image[dmath + PMUL_B2:dmath + PMUL_B2 + 3]) != bytes.fromhex('8b5502'):
            sys.exit('libcatof.py: dmath.o\'s pmuld is not the expected code')
        if self.sym.get('ddiv') != dmath + DDIV or self.sym.get('dadd') != dmath + DADD:
            sys.exit('libcatof.py: dmath.o\'s entry points are not where expected')
        # "assembler zeros": mov byte fac+7,0 / ret
        self.as_zero = bytes([0xc6, 0x06]) + struct.pack('<H', self.fac + 7) + bytes([0x00, 0xc3])

    def _prepare(self, as_zero):
        m = bytearray(self.image)
        if as_zero:
            m[self.zero:self.zero + len(self.as_zero)] = self.as_zero
        m[HLT_AT] = 0xF4
        self.uc.mem_write(BASE, bytes(m))
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            self.uc.reg_write(r, SEG)
        self.uc.reg_write(UC_X86_REG_FLAGS, 0x0002)

    def atof(self, text, as_zero=True):
        """The eight bytes of the double atof(text) returns (in fac)."""
        s = text.encode('ascii') + b'\0'
        if len(s) > HLT_AT - TEXT_AT:
            raise ValueError('text too long')
        self._prepare(as_zero)
        self.uc.mem_write(BASE + TEXT_AT, s)
        self.uc.mem_write(BASE + SP_AT - 4, struct.pack('<HH', HLT_AT, TEXT_AT))
        self.uc.reg_write(UC_X86_REG_SP, SP_AT - 4)
        self.uc.emu_start(BASE + self.sym['_atof'], BASE + HLT_AT, count=STEPS)
        ax = self.uc.reg_read(UC_X86_REG_AX)
        return bytes(self.uc.mem_read(BASE + ax, 8))

    def op(self, name, a, b):
        """dmath.o's dmul/ddiv/dadd on the double images a ([si]) and b
        ([di]); the eight bytes it leaves in fac."""
        self._prepare(False)
        self.uc.mem_write(BASE + 0xD000, a)
        self.uc.mem_write(BASE + 0xD010, b)
        self.uc.reg_write(UC_X86_REG_SI, 0xD000)
        self.uc.reg_write(UC_X86_REG_DI, 0xD010)
        self.uc.mem_write(BASE + SP_AT - 2, struct.pack('<H', HLT_AT))
        self.uc.reg_write(UC_X86_REG_SP, SP_AT - 2)
        self.uc.emu_start(BASE + self.sym[name], BASE + HLT_AT, count=STEPS)
        return bytes(self.uc.mem_read(BASE + self.fac, 8))


def atof_text(kind, text):
    """The text as the assembler hands it to atof(): no 0f/0d prefix, a
    d/D exponent (after 0d) read as e."""
    if len(text) >= 2 and text[0] == '0' and text[1].lower() == kind:
        text = text[2:]
        if kind == 'd':
            text = text.replace('d', 'e').replace('D', 'e')
    return text


def not_emulated(kind, text):
    """True for the one kind of text the emulation cannot reproduce: every
    digit 0 and a decimal exponent of 0, or LOGHUGE - the result is the
    digit loop's leftover in fac, which differs between the runtimes."""
    p = F.parse(kind, text)
    if p is None:
        return False
    neg, digs, eexp = p
    if any(d for d, _ in digs):
        return False
    exp10 = eexp - sum(1 for _, frac in digs if frac)
    return exp10 == 0 or (exp10 < 0 and len(digs) + exp10 < -39)


def stored(kind, b):
    return b[4:] if kind == 'f' else b


def cmd_atof(args):
    as_zero = True
    if args and args[0] == '--libc':
        as_zero, args = False, args[1:]
    if not args:
        print(__doc__)
        return 2
    rt = Runtime()
    for t in args:
        b = rt.atof(atof_text('d', t), as_zero)
        print('%-36s double %s   float %s' % (t, b.hex(' '), b[4:].hex(' ')))
    return 0


def cmd_goldens(dirs):
    rt = Runtime()
    consts = F.confirmed(dirs or [HERE])
    same = skipped = bad = 0
    for kind, text, real, where in consts:
        got = stored(kind, rt.atof(atof_text(kind, text)))
        name = '.%s %s' % ('float' if kind == 'f' else 'double', text)
        if got == real:
            same += 1
        elif not_emulated(kind, text):
            skipped += 1
            print('  not emulated: %-40s real %s, emulated %s (%s) - digit-loop leftover'
                  % (name, real.hex(' '), got.hex(' '), where))
        else:
            bad += 1
            print('  DIFFERENT:    %-40s real %s, emulated %s (%s)' % (name, real.hex(' '), got.hex(' '), where))
    print('%d real constants: %d byte-identical to libc.a\'s atof with assembler zeros, %d not emulated, '
          '%d different' % (len(consts), same, skipped, bad))
    return 1 if bad else 0


def compiler_texts(n, seed):
    """"%.17e" texts of exact floats and doubles over the whole exponent
    range, and zeros of every shape."""
    rng = random.Random(seed)
    out = []
    for _ in range(n):
        kind = rng.choice('fd')
        sb = 24 if kind == 'f' else 56
        v = Fraction(rng.getrandbits(sb) | 1 << (sb - 1)) * Fraction(2) ** (rng.randint(-126, 126) - sb)
        k = max(0, v.denominator.bit_length() - 1)
        s = str(v.numerator * 5 ** k)
        e10 = len(s) - 1 - k
        s = (s + '0' * 18)[:18]
        out.append('%s %s%s.%se%s%02d' % (kind, rng.choice(['', '-']), s[0], s[1:],
                                         '+' if e10 >= 0 else '-', abs(e10)))
    for _ in range(n // 2):
        out.append('%s %s0.%se%+d' % (rng.choice('fd'), rng.choice(['', '-', '+']),
                                      '0' * rng.randint(0, 40), rng.randint(-70, 30)))
    for _ in range(n // 4):     # LOGHUGE: nd - k < -39
        nd = rng.randint(1, 20)
        out.append('%s %d%se-%d' % (rng.choice('fd'), rng.randint(1, 9), ''.join(rng.choice('0123456789') for _ in range(nd - 1)),
                                    nd + rng.randint(40, 60)))
    return out


def cmd_check(tool, n):
    texts = F.EDGES + F.random_texts(n) + compiler_texts(n, 2)
    res = subprocess.run([tool], input='\n'.join(texts) + '\n', capture_output=True, text=True)
    if res.returncode != 0:
        print('FAIL: %s exited %d: %s' % (tool, res.returncode, res.stderr.strip()))
        return 1
    rt = Runtime()
    counts, bad = {}, 0
    for line, got in zip(texts, res.stdout.splitlines()):
        kind, text = line[0], line[2:]
        if not got.startswith('OK '):
            counts[got] = counts.get(got, 0) + 1
            continue
        if not_emulated(kind, text):
            counts['OK, not emulated'] = counts.get('OK, not emulated', 0) + 1
            continue
        emu = stored(kind, rt.atof(atof_text(kind, text))).hex(' ')
        counts['OK, compared'] = counts.get('OK, compared', 0) + 1
        if emu != got[3:]:
            bad += 1
            if bad <= 10:
                print('  DIFFERENT %-45s fltconst.c %-24s emulated %s' % (line, got[3:], emu))
    print('%d texts (%s): %d accepted constants differ from libc.a\'s atof'
          % (len(texts), ', '.join('%s %d' % kv for kv in sorted(counts.items())), bad))
    return 1 if bad else 0


def cmd_ops(n):
    rt = Runtime()
    rng = random.Random(5)
    bad = {'dmul': 0, 'ddiv': 0, 'dadd': 0}

    def mant():
        m = rng.getrandbits(55) | 1 << 55
        r = rng.random()
        if r < 0.25:                        # long runs of zeros at the bottom
            m &= ~((1 << rng.randint(0, 55)) - 1)
            m |= 1 << 55
        elif r < 0.35:                      # near all-ones
            m = (1 << 56) - 1 - rng.getrandbits(rng.randint(0, 16))
        return m

    for _ in range(n):
        a, b = mant(), mant()
        xa, xb = rng.randint(-40, 40), rng.randint(-40, 40)
        A, B = F.image(False, a, xa + 128), F.image(False, b, xb + 128)
        da, db = (a, xa - 56), (b, xb - 56)
        for name, want in (('dmul', F.mul(da, db, 'libc')),
                           ('ddiv', F.div(da, db, 'ne')),
                           ('dadd', None)):
            if name == 'dadd':
                lo = min(da[1], db[1])
                want = F.chk(F.rnd((a << (da[1] - lo)) + (b << (db[1] - lo)), False, lo, 'ne'))
            if rt.op(name, A, B) != F.image(False, want[0], want[1] + 56 + 128):
                bad[name] += 1
                if bad[name] <= 3:
                    print('  %s differs: a=%s b=%s' % (name, A.hex(' '), B.hex(' ')))
    print('%d operand pairs: dmul vs fltmodel\'s "libc" product %d, ddiv vs nearest-even %d, '
          'dadd vs nearest-even %d differences' % (n, bad['dmul'], bad['ddiv'], bad['dadd']))
    return 1 if any(bad.values()) else 0


def main(argv):
    if len(argv) >= 2 and argv[1] == 'atof':
        return cmd_atof(argv[2:])
    if len(argv) >= 2 and argv[1] == 'goldens':
        return cmd_goldens(argv[2:])
    if len(argv) in (3, 4) and argv[1] == 'check':
        return cmd_check(argv[2], int(argv[3]) if len(argv) == 4 else 3000)
    if len(argv) in (2, 3) and argv[1] == 'ops':
        return cmd_ops(int(argv[2]) if len(argv) == 3 else 2000)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
