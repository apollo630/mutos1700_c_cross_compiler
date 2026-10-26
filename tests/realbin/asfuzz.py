#!/usr/bin/env python3
"""asfuzz.py - differential fuzzing of mutos_as against the original as.

plan step 5 (docs/TOOLCHAIN_PLAN.md): mutos_as is not a port, so this is a
measurement - how far does it agree with the original on input that is not
in the corpus? Each case is a source file, assembled by the original (under
the emulator) and by mutos_as with ascmp.py's comparison and classes
(OBJ-DIFF, PORT-ACCEPTS, PORT-REJECTS, both-reject, obj-same, same); the
command line is the same normalized one for both ("as [-gC] -o x.o x.s",
the original cc's own call being -gC), so that what is measured is the
assembler and not the command line (ascmp.py cases measures that).

Every case that is a serious difference (OBJ-DIFF, PORT-ACCEPTS,
PORT-REJECTS) is reduced to a minimal source - lines taken out while the
class stays - and the reduced sources are clustered by their shape
(numbers and names made one), so that the report lists each kind of
difference once, with how often it was seen.

Generators:

    insn    one or a few instructions: every mnemonic of mutos_as and some
            the original has and it has not, with operands of every shape
            (registers, byte and word immediates, memory with displacement,
            base and index, symbols and expressions, jump targets)
    prog    whole programs: .text/.data/.bss, labels, .globl, .comm, .byte,
            .word, .asciz, .even, .blkb, ".=.+n", assignments, expressions,
            references to defined and undefined symbols
    corpus  lines of the real corpus (the kernel's and the compiler's
            assembler text) recombined: a mnemonic with operands of another
            line, numbers and registers changed
    cc      the real compiler's output for random C programs (c1fuzz.py's
            and fuzz_c.py's), through the ported chain; also after mutos_c2
    mix     all of them

--mutate P damages each case with probability P: lines deleted, doubled,
swapped or cut, odd bytes, NUL, CR, no last linefeed, long names and
numbers, deep expressions.

Usage:
    asfuzz.py [-n COUNT] [-s SEED] [-j JOBS] [--gen insn|prog|corpus|cc|mix]
              [--mutate P] [--opts C|plain|both] [--show N] [--minimize N]
              [--keep DIR] [--port PATH] [--port-args ARG]

    asfuzz.py --words [-j N]
        no fuzzing: every instruction word of the original (read from its
        binary) and of mutos_as, each with up to 30 operand shapes, as a
        table of classes - the exhaustive version of "insn"

A fixed seed gives the same cases every time. Exit status 0 (it measures);
with --strict 1 if the port gets a source the original accepts wrong
(OBJ-DIFF or PORT-REJECTS; not PORT-ACCEPTS, which is leniency); 1 if the
harness itself failed.

Requires the Python module "unicorn" (pip install unicorn).
"""
import argparse
import concurrent.futures
import glob
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'mutos_cc', 'fuzz'))
import ascmp          # noqa: E402

# A fuzz case that the original does not finish within this many instructions
# (a few seconds) counts as endless; the 3,000,000,000 of ascmp.py are for
# the biggest kernel source, and a reduction tries a case a hundred times.
ascmp.MAX_INSNS = 300_000_000
ascmp.WALL = 10
import c2fuzz         # noqa: E402

ROOT = ascmp.ROOT
FUZZ_INSNS = 400_000_000
SERIOUS = ascmp.SERIOUS

REG16 = ['ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp']
REG8 = ['al', 'ah', 'bl', 'bh', 'cl', 'ch', 'dl', 'dh']
SEG = ['es', 'cs', 'ss', 'ds']
BASE = ['bx', 'bp', 'si', 'di']
LOCALS = ['L%d' % i for i in range(1, 9)] + ['L10001', 'L10002', 'lab', '_loc']
EXTERNS = ['_ext', '_putc', 'cret', '_tab', 'csv', '_buf2']
NUMS = [0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 15, 16, 31, 100, 127, 128, 129, 255,
        256, 1000, 4096, 32767, 32768, 65535, -1, -2, -127, -128, -129,
        -32768, -32769]

# mnemonic -> operand signatures (a signature is a tuple of kinds)
ALU = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp']
SHIFT = ['sal', 'sar', 'shl', 'shr', 'rol', 'ror', 'rcl', 'rcr']
UNARY = ['inc', 'dec', 'neg', 'not', 'mul', 'div', 'idiv', 'imul']
JCC = ['beq', 'bne', 'blt', 'bge', 'ble', 'bgt', 'blo', 'bhi', 'blos', 'bhis',
       'bvs', 'bvc', 'bmi', 'bpl', 'je', 'jne', 'jz', 'jnz', 'jl', 'jg', 'jle',
       'jge', 'jb', 'ja', 'jbe', 'jae', 'jc', 'jnc', 'js', 'jns', 'jo', 'jno',
       'jp', 'jnp', 'jcxz', 'loop', 'loope', 'loopne']
NOARG = ['ret', 'nop', 'cwd', 'cbw', 'cld', 'std', 'cli', 'sti', 'clc', 'stc',
         'cmc', 'pushf', 'popf', 'sahf', 'lahf', 'stow', 'stob', 'lodw', 'lodb',
         'movs', 'movsb', 'cmps', 'cmpsb', 'scas', 'scasb', 'xlat', 'wait',
         'iret', 'reti', 'hlt', 'into', 'lock', 'aaa', 'aas', 'daa', 'das',
         'aam', 'aad', 'in', 'out', 'inw', 'outw', 'rep', 'repe', 'repne',
         'repz', 'repnz']
# the 80186 set (mutos_as -1) and spellings the original has no table entry
# for or only mutos_as has
I186 = ['pusha', 'popa', 'leave', 'insb', 'insw', 'outsb', 'outsw']
ODD = ['movsw', 'inb', 'outb', 'bloss', 'lods', 'stos', 'esc', 'fld', 'fadd',
       'movw', 'addw', 'pushb', 'callf', 'retf', 'retn', 'jmpf', 'jmpi',
       'calli', 'foo', 'Mov', 'MOV', 'mov.b']


def pick(r, xs):
    return xs[r.randrange(len(xs))]


class Gen:
    """Random assembler text."""

    def __init__(self, r):
        self.r = r
        # strict: forms the original accepts (byte instructions on byte
        # registers, short jumps to labels of the file, plain numbers as
        # byte values); the other 30% is anything the generator knows
        self.strict = r.random() < 0.7
        self.base_labels = ['L1', 'L2', 'L3', 'lab', '_loc']
        self.labels = list(self.base_labels) if self.strict else []
        self.nlab = 100
        self.assigned = []      # names given a value by "name = ..."

    # ---- operands ----
    def num(self):
        r = self.r
        v = pick(r, NUMS) if r.random() < 0.8 else r.randrange(-70000, 70000)
        k = r.random()
        if k < 0.6:
            return '%d.' % v
        if k < 0.75:
            return '/%x' % (v & 0xffff)
        if k < 0.9:
            return '%o' % (v & 0xffff) if v >= 0 else '%d.' % v
        return '%d' % v if v >= 0 else '%d.' % v

    def sym(self):
        r = self.r
        k = r.random()
        if self.labels and k < 0.5:
            return pick(r, self.labels)
        if k < 0.8:
            return pick(r, EXTERNS)
        return pick(r, LOCALS)

    def expr(self):
        r = self.r
        k = r.random()
        if k < 0.35:
            return self.num()
        if k < 0.65:
            return self.sym()
        if k < 0.85:
            return '%s+%s' % (self.sym(), self.num())
        if k < 0.92:
            return '%s-%s' % (self.sym(), self.sym())
        if k < 0.96:
            return '%s+%s' % (self.num(), self.sym())
        return '.+%s' % self.num()

    def imm(self):
        r = self.r
        m = '*' if r.random() < 0.5 else '#'
        return m + self.expr()

    def mem(self):
        r = self.r
        k = r.random()
        b = pick(r, BASE)
        if k < 0.12:
            return '(%s)' % b
        if k < 0.30:
            return '*%s(%s)' % (self.num(), b)
        if k < 0.42:
            return '#%s(%s)' % (self.num(), b)
        if k < 0.58:
            return self.expr()
        if k < 0.70:
            return '%s(%s)' % (self.expr(), b)
        if k < 0.80:
            return '*%s(bx)(%s)' % (self.num(), pick(r, ['si', 'di']))
        if k < 0.86:
            return '(bx)(%s)' % pick(r, ['si', 'di'])
        if k < 0.92:
            return '*%s +%s(%s)' % (self.num(), self.num(), b)
        return '%s%s' % (pick(r, ['', '*', '#']), self.expr())

    def reg(self):
        r = self.r
        k = r.random()
        if k < 0.7:
            return pick(r, REG16)
        if k < 0.92:
            return pick(r, REG8)
        return pick(r, SEG)

    def target(self):
        r = self.r
        k = r.random()
        if k < 0.5:
            return self.sym()
        if k < 0.6:
            return '.%s%s' % (pick(r, '+-'), self.num())
        if k < 0.7:
            return '@' + pick(r, REG16)
        if k < 0.8:
            return '@%s(%s)' % (self.expr(), pick(r, BASE))
        if k < 0.88:
            return '@*%s(%s)' % (self.num(), pick(r, BASE))
        return self.expr()

    def operand(self):
        r = self.r
        k = r.random()
        if k < 0.35:
            return self.reg()
        if k < 0.65:
            return self.mem()
        if k < 0.9:
            return self.imm()
        return self.target()

    def bimm(self):
        """A byte immediate that is a plain number."""
        return '*%d.' % self.r.randrange(-128, 256)

    def sane2(self, byte=False):
        """A source and a destination of kinds the 8086 allows."""
        r = self.r
        regs = REG8 if byte else REG16
        k = r.random()
        if k < 0.30:
            return pick(r, regs), pick(r, regs)
        if k < 0.50:
            return pick(r, regs), self.mem()
        if k < 0.65:
            return self.mem(), pick(r, regs)
        if k < 0.88:
            return pick(r, regs), (self.bimm() if byte else self.imm())
        return self.mem(), (self.bimm() if byte else self.imm())

    # ---- instructions ----
    def jtarget(self):
        """A target of a short jump: a label of this file."""
        r = self.r
        if self.strict:
            return pick(r, self.base_labels)
        return self.target()

    def insn(self):
        r = self.r
        st = self.strict
        k = r.random()
        if k < 0.30:
            m = pick(r, ALU + [a + 'b' for a in ALU] + ['mov', 'movb', 'test',
                                                       'testb', 'xchg', 'xchgb'])
            byte = m.endswith('b') and m not in ('sub', 'sbb')
            if st:
                a, b = self.sane2(byte)
            else:
                a, b = self.sane2(r.random() < 0.5) if r.random() < 0.85 else \
                    (self.operand(), self.operand())
            return '%s\t%s,%s' % (m, a, b)
        if k < 0.38:
            m = pick(r, ['lea', 'lds', 'les'])
            return '%s\t%s,%s' % (m, pick(r, REG16), self.mem())
        if k < 0.48:
            m = pick(r, SHIFT + [s + 'b' for s in SHIFT])
            byte = m.endswith('b') and m not in ('sub', 'sbb')
            if st:
                dst = pick(r, REG8 if byte else REG16) if r.random() < 0.6 else self.mem()
                c = pick(r, ['cl', '*1', '*1.'])
            else:
                dst = pick(r, REG16) if r.random() < 0.6 else self.mem()
                c = pick(r, ['cl', '*1', '*1.', '#1', '1', '*2', '*3.', '#4.',
                             'cx', self.imm()])
            return '%s\t%s,%s' % (m, dst, c)
        if k < 0.60:
            m = pick(r, UNARY + [u + 'b' for u in UNARY])
            byte = m.endswith('b') and m not in ('sub', 'sbb')
            if st:
                return '%s\t%s' % (m, pick(r, REG8 if byte else REG16)
                                   if r.random() < 0.6 else self.mem())
            return '%s\t%s' % (m, self.operand() if r.random() < 0.3 else
                               (pick(r, REG16) if r.random() < 0.6 else self.mem()))
        if k < 0.68:
            m = pick(r, ['push', 'pop'])
            if st:
                return '%s\t%s' % (m, pick(r, REG16 + SEG) if r.random() < 0.7
                                   else self.mem())
            return '%s\t%s' % (m, self.operand())
        if k < 0.80:
            m = pick(r, ['jmp', 'call', 'j', 'br'] + JCC)
            if st and m in JCC:
                return '%s\t%s' % (m, self.jtarget())
            return '%s\t%s' % (m, self.target())
        if k < 0.88:
            m = pick(r, NOARG)
            if r.random() < 0.15 and not st:
                return '%s\t%s' % (m, self.operand())
            return m
        if k < 0.91:
            return 'seg\t%s' % pick(r, SEG + ['ax', 'foo'] * (not st))
        if k < 0.93:
            return 'int\t%s' % (self.bimm() if st or r.random() < 0.5 else self.imm())
        if k < 0.96:
            if r.random() < 0.6:
                return pick(r, I186)
            return pick(r, ['enter\t%s,%s' % (self.imm(), self.imm()),
                            'bound\t%s,%s' % (pick(r, REG16), self.mem()),
                            'imul\t%s,%s' % (pick(r, REG16), self.imm()),
                            'imul\t%s,%s,%s' % (pick(r, REG16), pick(r, REG16),
                                               self.imm()),
                            'push\t%s' % self.imm()])
        m = pick(r, ODD)
        return m if r.random() < 0.5 else '%s\t%s' % (m, self.operand())

    def line(self):
        """An instruction, maybe with a label before it."""
        r = self.r
        s = self.insn()
        if r.random() < 0.20:
            if self.strict:
                self.nlab += 1
                lab = 'L%d' % self.nlab
            else:
                lab = pick(r, LOCALS)
            self.labels.append(lab)
            return '%s:%s%s' % (lab, pick(r, ['', '\t', ' ']), s)
        return '\t' + s

    def header(self):
        """In strict mode the labels the jumps go to, defined."""
        if not self.strict:
            return ''
        return ''.join('%s:\n' % l for l in self.base_labels)

    # ---- programs ----
    def wexpr(self):
        """A word value; in strict mode a number, a symbol or symbol+number."""
        r = self.r
        if not self.strict:
            return self.expr()
        k = r.random()
        if k < 0.4:
            return self.num()
        if k < 0.7:
            return self.sym()
        return '%s+%d.' % (self.sym(), r.randint(0, 40))

    def data_line(self):
        r = self.r
        k = r.random()
        if k < 0.30:
            return '\t.word\t%s' % ', '.join(self.wexpr() for _ in range(r.randint(1, 4)))
        if k < 0.50:
            return '\t.byte\t%s' % ', '.join(
                (self.num() if self.strict or r.random() < 0.8 else self.expr())
                for _ in range(r.randint(1, 6)))
        if k < 0.60:
            return '\t.%s\t*%s*' % (pick(r, ['asciz', 'asciz', 'ascii']),
                                    ''.join(pick(r, 'abcdefg /\\n012') for _ in range(r.randint(0, 8))))
        if k < 0.68:
            return '\t.even'
        if k < 0.76:
            return '\t.=.+%s' % (('%d.' % r.randint(0, 30)) if self.strict else self.num())
        if k < 0.84:
            return '\t.%s\t%d.' % (pick(r, ['blkb', 'blkb', 'blkw']), r.randint(0, 12))
        if k < 0.90:
            return '\t.float\t%s' % pick(r, ['1.0', '0.5', '-2.25', '100', '3.14159',
                                              '1e3', '0', '1.5e-3'])
        if k < 0.94:
            return '\t.double\t%s' % pick(r, ['1.0', '0.5', '-2.25', '100', '2.718281828',
                                               '1e10', '0'])
        if k < 0.945:
            return pick(r, ['.list', '.nlist', '.targref', '.textref'] if self.strict
                        else ['.list', '.nlist', '.end', '.foo', '.evn', '.dlptr 1'])
        if k < 0.97:
            self.nlab += 1
            name = 'X%d' % self.nlab if self.strict else pick(r, LOCALS + ['X', 'Y'])
            if self.strict and r.random() < 0.5:
                # a value made of symbols already known (asfuzz found that
                # mutos_as took only plain constants here): a label or an
                # earlier assignment, plus or minus a number, a difference
                # of two, the location counter
                known = self.assigned + [l for l in self.labels if l.startswith('_d')]
                a = pick(r, known) if known else None
                kk = r.random()
                if a is None or kk < 0.15:
                    rhs = '.+%d.' % r.randint(0, 20)
                elif kk < 0.55:
                    rhs = '%s+%d.' % (a, r.randint(0, 20))
                elif kk < 0.75:
                    rhs = '%s*%d.' % (a, r.randint(1, 5))
                else:
                    rhs = '%s-%s' % (a, pick(r, known))
                self.assigned.append(name)
                return '%s = %s' % (name, rhs)
            if self.strict:
                self.assigned.append(name)
            return '%s = %s' % (name, self.num() if self.strict else self.expr())
        self.nlab += 1
        return '\t.comm\t%s, %s' % ('_c%d' % self.nlab if self.strict else
                                    pick(r, ['_c1', '_c2', 'Lc', '_buf']),
                                    ('%d.' % r.randint(1, 200)) if self.strict
                                    else self.num())

    def program(self):
        r = self.r
        out = []
        for _ in range(r.randint(0, 3)):
            out.append('.globl\t%s' % pick(r, EXTERNS + LOCALS + ['_main', '_d1']))
        segs = ['.text', '.data', '.bss']
        cur = '.text'
        out.append('.text')
        if self.strict:
            out.append(self.header().rstrip('\n'))
        for _ in range(r.randint(2, 40)):
            k = r.random()
            if k < 0.08:
                cur = pick(r, segs[:2] if r.random() < 0.9 else segs)
                out.append(cur)
            elif cur == '.text' or r.random() < 0.15:
                out.append(self.line())
            elif cur == '.bss':
                self.nlab += 1
                lab = ('b%d' % self.nlab) if self.strict else \
                    pick(r, ['_b1', '_b2', 'Lb', 'bsx'])
                out.append('%s:\t.%s\t%d.' % (lab, pick(r, ['blkb', 'blkb', 'blkw']),
                                               r.randint(1, 8)))
            else:
                if r.random() < 0.4:
                    self.nlab += 1
                    lab = ('_d%d' % self.nlab) if self.strict else \
                        pick(r, ['_d1', '_d2', 'Ld', 'dat'])
                    self.labels.append(lab)
                    out.append('%s:' % lab)
                out.append(self.data_line())
        return '\n'.join(out) + '\n'


# ---- the corpus ------------------------------------------------------

_POOL = None


def corpus_pool():
    """[(label?, mnemonic, [operands])] of the real sources, and the plain
    lines. Built once per process."""
    global _POOL
    if _POOL is not None:
        return _POOL
    files = []
    at = os.path.join(ROOT, 'tests', 'mutos_as')
    for d in ('kernel_opt', 'kernel_nonopt', 'libc_recon'):
        files += sorted(glob.glob(os.path.join(at, d, '*.s')))
    files += sorted(glob.glob(os.path.join(ROOT, 'tests', 'mutos_cc', '**',
                                           '*.s.golden'), recursive=True))[::3]
    lines, insns = [], {}
    for f in files:
        for ln in open(f, 'rb').read().decode('latin-1').split('\n'):
            s = ln.rstrip()
            if not s or len(s) > 100:
                continue
            lines.append(s)
            body = s.split('|')[0].strip() if '.asciz' not in s else s.strip()
            body = re.sub(r'^[A-Za-z_.0-9$]+:\s*', '', body)
            m = re.match(r'([a-z][a-z0-9]*)\s+(.*)$', body)
            if m and ',' in m.group(2) and '.asciz' not in body:
                ops = [o.strip() for o in re.split(r',(?![^()]*\))', m.group(2))]
                insns.setdefault(m.group(1), []).append(ops)
    _POOL = (lines, insns)
    return _POOL


def gen_corpus(r, wd):
    """Lines of the real sources recombined: a line as it is, an instruction
    with operands taken from other lines of the same mnemonic and operand
    count, or a line with numbers / registers / markers changed. Labels the
    jumps of such lines need are defined at random places."""
    lines, insns = corpus_pool()
    out = ['.text']
    seen = set()
    for _ in range(r.randint(3, 40)):
        k = r.random()
        if k < 0.35:
            ln = pick(r, lines)
        elif k < 0.8:
            m = pick(r, sorted(insns))
            tmpl = pick(r, insns[m])
            same = [o for o in insns[m] if len(o) == len(tmpl)]
            ln = '\t%s\t%s' % (m, ','.join(pick(r, same)[i] for i in range(len(tmpl))))
        else:
            ln = pick(r, lines)
            if r.random() < 0.5:
                ln = re.sub(r'\d+', lambda mo: str(r.choice(
                    [0, 1, 2, 7, 127, 128, 255, int(mo.group(0)) + 1])), ln)
            if r.random() < 0.5:
                ln = re.sub(r'\b(ax|bx|cx|dx|si|di|bp)\b',
                            lambda mo: pick(r, REG16), ln)
            if r.random() < 0.3:
                ln = ln.replace('*', '#', 1) if r.random() < 0.5 else ln.replace('#', '*', 1)
        mo = re.match(r'([A-Za-z_.0-9$]+):', ln)
        if mo:
            if mo.group(1) in seen:
                continue
            seen.add(mo.group(1))
        out.append(ln)
    # the labels the lines jump to
    body = '\n'.join(out)
    defined = set(re.findall(r'^([A-Za-z_.0-9$]+):', body, re.M))
    wanted = set(re.findall(r'\b(L\d+)\b', body)) - defined
    for lab in sorted(wanted):
        out.insert(r.randint(1, len(out)), lab + ':')
    return '\n'.join(out) + '\n'


# ---- the compiler's output ------------------------------------------

def _c2(s_bytes, wd):
    """The text through mutos_c2 (the optimizer), or None."""
    i, o = os.path.join(wd, 'o_in.s'), os.path.join(wd, 'o_out.s')
    with open(i, 'wb') as f:
        f.write(s_bytes)
    r = subprocess.run([os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_c2'), i, o],
                       capture_output=True, timeout=60)
    if r.returncode != 0 or not os.path.exists(o):
        return None
    return open(o, 'rb').read()


def gen_cc(r, wd):
    """The real compiler's text for a random C program (c2fuzz's two
    generators), a third of it after the optimizer."""
    if r.random() < 0.5:
        data = c2fuzz.gen_c1fuzz(r, wd)
    else:
        data = c2fuzz.gen_fuzz_c(r, wd)
    if data is not None and r.random() < 0.33:
        data = _c2(data, wd) or data
    return None if data is None else data.decode('latin-1')


def gen_insn(r, wd):
    g = Gen(r)
    return '.text\n' + g.header() + '\n'.join(
        g.line() for _ in range(r.randint(1, 6))) + '\n'


def gen_prog(r, wd):
    return Gen(r).program()


# ---- damage ----------------------------------------------------------

def mutate(r, text):
    lines = text.split('\n')
    for _ in range(r.randint(1, 3)):
        k = r.random()
        i = r.randrange(len(lines)) if lines else 0
        if not lines:
            break
        if k < 0.15:
            del lines[i]
        elif k < 0.25:
            lines.insert(i, lines[i])
        elif k < 0.35 and len(lines) > 1:
            j = r.randrange(len(lines))
            lines[i], lines[j] = lines[j], lines[i]
        elif k < 0.50:
            lines[i] = lines[i][:r.randrange(len(lines[i]) + 1)]
        elif k < 0.60:
            ln = lines[i]
            p = r.randrange(len(ln) + 1)
            lines[i] = ln[:p] + chr(pick(r, [0, 1, 7, 9, 13, 26, 127, 128, 200, 255])) + ln[p:]
        elif k < 0.68:
            lines[i] = lines[i] + 'x' * pick(r, [7, 8, 9, 40, 200])
        elif k < 0.76:
            lines[i] = lines[i] + ' ' + '9' * pick(r, [5, 6, 20, 70])
        elif k < 0.84:
            lines[i] = lines[i].replace(',', pick(r, [',,', ' , ', ',\t', '']), 1)
        elif k < 0.90:
            lines[i] = '(' * r.randint(1, 20) + lines[i] + ')' * r.randint(0, 20)
        elif k < 0.95:
            lines[i] = lines[i].replace('\t', ' ') + '\r'
        else:
            lines[i] = '+'.join(['1'] * r.randint(5, 60))
    text = '\n'.join(lines)
    if r.random() < 0.15:
        text = text.rstrip('\n')
    return text


GENS = {'insn': gen_insn, 'prog': gen_prog, 'corpus': gen_corpus, 'cc': gen_cc}


# ---- running ---------------------------------------------------------

def run_case(text, args, port, port_args, keep=None):
    with tempfile.TemporaryDirectory(prefix='asfuzz') as wd:
        with open(os.path.join(wd, 'x.s'), 'wb') as f:
            f.write(text.encode('latin-1', 'replace')
                    if isinstance(text, str) else text)
        r = ascmp.compare(args, wd, port, port_args, keep=keep)
    return r


def kind_of(text, args, port, port_args):
    r = run_case(text, args, port, port_args)
    return ascmp.classify(r)[0], r


def minimize(text, args, port, port_args, kind, budget=120):
    """Lines taken out while the class stays the same."""
    # A source without its last linefeed (a mutation makes them) stays
    # without one: the original refuses such a line, and a reduced case
    # that got its linefeed back would not show the difference any more.
    tail = '\n' if text.endswith('\n') else ''
    lines = (text[:-1] if tail else text).split('\n')
    n = 2
    tries = 0
    while len(lines) > 1 and tries < budget:
        size = max(1, len(lines) // n)
        shrunk = False
        i = 0
        while i < len(lines) and tries < budget:
            cand = lines[:i] + lines[i + size:]
            if cand and any(c.strip() for c in cand):
                tries += 1
                k, _ = kind_of('\n'.join(cand) + tail, args, port, port_args)
                if k == kind:
                    lines = cand
                    shrunk = True
                    continue
            i += size
        if not shrunk:
            if size == 1:
                break
            n = min(len(lines), n * 2)
    return '\n'.join(lines) + tail


_REGS_RE = re.compile(r'\b(?:%s)\b' % '|'.join(REG16 + REG8 + SEG))


def norm_msg(b, keep_quoted=False):
    """The first line of a message with line numbers, registers and numbers
    made one - the part of a cluster key that says what was complained of.
    keep_quoted: the words in quotes stay (the port names the instruction
    it does not know; the original's messages have none)."""
    s = b.decode('latin-1').split('\n', 1)[0]
    s = re.sub(r'\bline \d+', 'line N', s)
    if not keep_quoted:
        s = re.sub(r"'[^']*'", "'M'", s)
    s = _REGS_RE.sub('R', s)
    s = re.sub(r'\d+', 'N', s)
    return s.strip()


def mnemonics(text):
    """The instruction words of a source (not directives, not labels)."""
    out = set()
    for ln in text.split('\n'):
        ln = ln.split('|')[0]
        while True:
            m = re.match(r'\s*[A-Za-z_.0-9$]+:(.*)$', ln)
            if not m:
                break
            ln = m.group(1)
        m = re.match(r'\s*([A-Za-z][A-Za-z0-9]*)\b', ln)
        if m:
            out.add(m.group(1))
    return out


def signature(small, args, port, port_args):
    """(cluster key, mnemonics) of a reduced case. A difference in what the
    two tools say (PORT-ACCEPTS / PORT-REJECTS) is keyed by the messages,
    a difference in the object by the instruction words."""
    r = run_case(small, args, port, port_args)
    kind, flags = ascmp.classify(r)
    mn = mnemonics(small)
    if kind == 'OBJ-DIFF':
        return (kind, tuple(sorted(mn))), mn, r
    return (kind, norm_msg(r.oerr or r.oout),
            norm_msg(r.perr or r.pout, True)), mn, r


# ---- the instruction words, one by one ------------------------------

WORD_SHAPES = ['', 'ax', 'al', 'cl', 'es', '*4.(bx)', '(bx)', '_ext', 'L1',
               '@bx', '#5.', '*5.',
               'ax,bx', 'al,bl', 'ax,*4.(bx)', '*4.(bx),ax', 'al,*4.(bx)',
               '*4.(bx),al', 'ax,#5.', 'ax,*5.', 'al,*5.', '*4.(bx),#5.',
               '*4.(bx),*5.', 'ax,cl', 'ax,*1', '*4.(bx),*1', 'ax,_ext',
               '_ext,ax', 'ax,es', 'ds,ax']
WORD_SHAPES_FIRST = ['', 'ax', 'ax,bx']       # for every word
WORD_SHAPES_8087 = ['', '*4.(bx)', 'st']      # the 8087 words (f...)


def original_words():
    """The instruction words of the original's table, read from its binary
    (the names from aaa to xorb, in the data segment)."""
    path = os.path.join(ROOT, 'mutos', 'mutos_bin', 'as.golden.base64.txt')
    import base64
    d = base64.b64decode(open(path).read())
    i = d.index(b'aaa\0')
    j = d.index(b'xorb\0') + 5
    return [w.decode() for w in re.findall(rb'[a-z][a-z0-9]*', d[i:j])]


DIRECTIVE_SHAPES = ['', '5', '2+3', '*abc*', '"y.s"', '_ext', '5,6', '= 3']
# names the manual documents and the original's binary does not have
DIRECTIVES_NOT_THERE = ['.set', '.space', '.int', '.long', '.lcomm', '.align',
                        '.global', '.foo']


def original_directives():
    """The directive names of the original, read from its binary (the
    strings from .text to .textref, next to each other in the data)."""
    path = os.path.join(ROOT, 'mutos', 'mutos_bin', 'as.golden.base64.txt')
    import base64
    d = base64.b64decode(open(path).read())
    i = d.index(b'.text\0')
    j = d.index(b'.textref\0') + 8
    return [w.decode() for w in re.findall(rb'\.[a-z]+', d[i:j])]


def directive_one(job):
    name, shape, args, port, port_args = job
    text = '.text\n\t%s\t%s\n\tnop\n' % (name, shape)
    r = run_case(text, args, port, port_args)
    return name, shape, ascmp.classify(r)[0], ascmp.detail(r)


def word_one(job):
    word, shape, args, port, port_args = job
    text = '.text\nL1:\n\t%s\t%s\n' % (word, shape)
    r = run_case(text, args, port, port_args)
    return word, shape, ascmp.classify(r)[0], ascmp.detail(r)


def words_main(a, port):
    """Every instruction word of the original (and every one mutos_as has)
    with operand shapes: the class of each, as a table."""
    orig = original_words()
    src = open(os.path.join(ROOT, 'src', 'mutos_as', 'encode.c')).read()
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)       # not the words in comments
    pw = set(re.findall(r'"([a-z][a-z0-9]{1,8})"', src))
    pw -= set(REG16 + REG8 + SEG + ['irregular', 'positive', 'value'])
    pw.add('j')                 # one letter: the pattern above wants two
    port_only = sorted(pw - set(orig))
    unknown = ['foo', 'esc', 'movw', 'pushb', 'retf', 'MOV']
    known = orig + port_only
    args = ['-o', 'x.o', 'x.s']
    jobs = []
    for w in known + unknown:
        shapes = list(WORD_SHAPES_FIRST)
        if w in unknown:
            pass
        elif w.startswith('f') and w in orig and w not in ('fnop',) and len(w) > 2 \
                and w not in ('lds',):
            shapes = WORD_SHAPES_8087
        else:
            shapes = WORD_SHAPES
        for sh in shapes:
            jobs.append((w, sh, args, port, a.port_args))
    res = {}
    with concurrent.futures.ProcessPoolExecutor(a.jobs) as ex:
        for w, sh, kind, detail in ex.map(word_one, jobs, chunksize=4):
            res[(w, sh)] = (kind, detail)
    mark = {'same': '.', 'obj-same': '.', 'both-reject': 'r', 'OBJ-DIFF': 'D',
            'PORT-REJECTS': 'X', 'PORT-ACCEPTS': 'A'}
    print('. same object   r both refuse   D other object   X port refuses '
          '(original accepts)   A port accepts (original refuses)')
    print('word      in:orig port | shapes: ' + ' | '.join(
        '"%s"' % s for s in WORD_SHAPES))
    tot = {}
    for w in known + unknown:
        cells = []
        for sh in WORD_SHAPES:
            if (w, sh) in res:
                k = res[(w, sh)][0]
                cells.append(mark[k])
                tot[k] = tot.get(k, 0) + 1
            else:
                cells.append(' ')
        print('%-8s %-4s %-5s | %s' % (
            w, 'yes' if w in orig else 'no', 'yes' if w in pw else 'no',
            ' '.join(cells)))
    print('words: %d of the original, %d only of mutos_as, %d unknown to both'
          % (len(orig), len(port_only), len(unknown)))
    # the directives
    dirs = original_directives()
    djobs = [(n, sh, args, port, a.port_args) for n in dirs + DIRECTIVES_NOT_THERE
             for sh in DIRECTIVE_SHAPES]
    dres = {}
    with concurrent.futures.ProcessPoolExecutor(a.jobs) as ex:
        for n, sh, kind, detail in ex.map(directive_one, djobs, chunksize=4):
            dres[(n, sh)] = (kind, detail)
    print()
    print('directive   in:orig | shapes: ' + ' | '.join(
        '"%s"' % s for s in DIRECTIVE_SHAPES))
    for n in dirs + DIRECTIVES_NOT_THERE:
        print('%-11s %-7s | %s' % (n, 'yes' if n in dirs else 'no', ' '.join(
            mark[dres[(n, sh)][0]] for sh in DIRECTIVE_SHAPES)))
    for k in ('OBJ-DIFF', 'PORT-REJECTS'):
        for (n, sh), (kind, detail) in sorted(dres.items()):
            if kind == k:
                print('-- directive %s %s "%s": %s' % (k, n, sh, detail[:120]))
    print()
    print('cells: ' + ', '.join('%d %s' % (tot[k], k) for k in ascmp.KINDS
                                if k in tot))
    for k in ('OBJ-DIFF', 'PORT-REJECTS', 'PORT-ACCEPTS'):
        n = 0
        print('-- %s (first shapes per word)' % k)
        seen = set()
        for (w, sh), (kind, detail) in sorted(res.items()):
            if kind == k and w not in seen:
                seen.add(w)
                print('  %-8s "%s": %s' % (w, sh, detail[:140]))
    return 0


def reduce_one(job):
    """A serious case reduced to a minimal source, with its cluster key."""
    idx, g, kind, detail, text, args, port, port_args = job
    small = minimize(text, args, port, port_args, kind)
    key, mn, _ = signature(small, args, port, port_args)
    return idx, g, detail, small, args, key, mn


def one(job):
    idx, seed, gen, mut, optmode, port, port_args, keep = job
    r = random.Random('%d.%d' % (seed, idx))
    g = gen if gen != 'mix' else pick(r, ['insn', 'insn', 'prog', 'prog',
                                          'corpus', 'cc'])
    with tempfile.TemporaryDirectory(prefix='asfuzzgen') as wd:
        try:
            text = GENS[g](r, wd)
        except Exception as e:      # a generator's own failure: skipped
            return idx, g, 'skipped', str(e), None, None
    if text is None:
        return idx, g, 'skipped', 'the chain did not compile the program', None, None
    if mut and r.random() < mut:
        text = mutate(r, text)
    if optmode == 'both':
        args = pick(r, [['-gC', '-o', 'x.o', 'x.s'], ['-o', 'x.o', 'x.s']])
    elif optmode == 'C':
        args = ['-gC', '-o', 'x.o', 'x.s']
    else:
        args = ['-o', 'x.o', 'x.s']
    res = run_case(text, args, port, port_args, keep)
    kind, flags = ascmp.classify(res)
    return idx, g, kind, ascmp.detail(res), text, args


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('-n', '--count', type=int, default=300)
    ap.add_argument('-s', '--seed', type=int, default=1)
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 1)
    ap.add_argument('--gen', choices=('insn', 'prog', 'corpus', 'cc', 'mix'),
                    default='mix')
    ap.add_argument('--mutate', type=float, default=0.0)
    ap.add_argument('--opts', choices=('C', 'plain', 'both'), default='both')
    ap.add_argument('--show', type=int, default=15,
                    help='clusters to print (default 15)')
    ap.add_argument('--minimize', type=int, default=60,
                    help='serious cases to reduce (default 60)')
    ap.add_argument('--keep', help='directory to save serious cases in')
    ap.add_argument('--strict', action='store_true',
                    help='exit status 1 if the port gets a source the '
                    'original accepts wrong (OBJ-DIFF, PORT-REJECTS); for '
                    'a generator whose input it must get right: cc')
    ap.add_argument('--words', action='store_true',
                    help='instead of fuzzing: every instruction word with '
                    'operand shapes, as a table')
    ap.add_argument('--port', default=ascmp.PORT)
    ap.add_argument('--port-args', action='append', default=[])
    a = ap.parse_args()
    keep = os.path.abspath(a.keep) if a.keep else None
    port = os.path.abspath(a.port)
    if a.words:
        return words_main(a, port)
    jobs = [(i, a.seed, a.gen, a.mutate, a.opts, port, a.port_args, keep)
            for i in range(a.count)]
    counts = {}
    flagc = {}
    serious = []
    with concurrent.futures.ProcessPoolExecutor(a.jobs) as ex:
        for idx, g, kind, detail, text, args in ex.map(one, jobs, chunksize=4):
            counts[kind] = counts.get(kind, 0) + 1
            if kind in SERIOUS:
                serious.append((idx, g, kind, detail, text, args))
    clusters = {}
    n = 0
    # the cases that make a program come out wrong or fail (OBJ-DIFF,
    # PORT-REJECTS) are reduced first: with hundreds of PORT-ACCEPTS in a
    # run the first --minimize cases would be all of that kind (found
    # 2026-10-25, when a .bss difference was among the unreduced)
    prio = {'OBJ-DIFF': 0, 'PORT-REJECTS': 1, 'PORT-ACCEPTS': 2}
    serious.sort(key=lambda t: prio.get(t[2], 3))
    work = [(idx, g, kind, detail, text, args, port, a.port_args)
            for idx, g, kind, detail, text, args in serious[:a.minimize]]
    with concurrent.futures.ProcessPoolExecutor(a.jobs) as ex:
        for idx, g, detail, small, cargs, key, mn in ex.map(reduce_one, work):
            c = clusters.setdefault(key, {'n': 0, 'small': small, 'mn': set(),
                                          'idx': idx, 'g': g, 'detail': detail,
                                          'args': cargs})
            c['n'] += 1
            c['mn'] |= mn
            if len(small) < len(c['small']):
                c['small'] = small
            n += 1
    rest = len(serious) - n
    order = {k: i for i, k in enumerate(['OBJ-DIFF', 'PORT-REJECTS', 'PORT-ACCEPTS'])}
    ordered = sorted(clusters.items(),
                     key=lambda kv: (order.get(kv[0][0], len(order)), -kv[1]['n']))
    for key, c in ordered[:a.show]:
        if key[0] == 'OBJ-DIFF':
            what = 'object differs; words: %s' % ' '.join(key[1])
        else:
            what = 'original: "%s" / port: "%s"' % (key[1] or '(accepts)',
                                                  key[2] or '(accepts)')
        print('%s x%d  %s' % (key[0], c['n'], what[:200]))
        print('    e.g. case %d (%s), %s; words seen: %s' % (
            c['idx'], c['g'], ' '.join(a for a in c['args'] if a != 'x.s'),
            ' '.join(sorted(c['mn']))[:100]))
        for ln in c['small'].strip('\n').split('\n')[:8]:
            print('    | ' + ln.replace('\t', '    '))
    if len(ordered) > a.show:
        print('... and %d more clusters' % (len(ordered) - a.show))
    oacc = sum(counts.get(k, 0) for k in ('same', 'obj-same', 'OBJ-DIFF', 'PORT-REJECTS'))
    orej = sum(counts.get(k, 0) for k in ('both-reject', 'PORT-ACCEPTS'))
    print('asfuzz: %d cases (%s%s, -o %s), seed %d: %s' % (
        a.count, a.gen, ', mutated %.2f' % a.mutate if a.mutate else '',
        a.opts, a.seed,
        ', '.join('%d %s' % (counts[k], k) for k in ascmp.KINDS + ['skipped']
                  if k in counts)))
    print('  original accepted %d: %d the same object, %d other object, %d '
          'refused by the port' % (oacc, counts.get('same', 0) + counts.get('obj-same', 0),
                                   counts.get('OBJ-DIFF', 0), counts.get('PORT-REJECTS', 0)))
    print('  original refused %d: %d refused by the port too, %d accepted by '
          'the port' % (orej, counts.get('both-reject', 0), counts.get('PORT-ACCEPTS', 0)))
    print('  %d serious in %d clusters%s' % (
        len(serious), len(clusters),
        ' (%d more not reduced)' % rest if rest > 0 else ''))
    wrong = counts.get('OBJ-DIFF', 0) + counts.get('PORT-REJECTS', 0)
    if a.strict and wrong:
        print('asfuzz: --strict: %d sources the original accepts, the port '
              'gets wrong' % wrong)
    return 1 if a.strict and wrong else 0


if __name__ == '__main__':
    sys.exit(main())
