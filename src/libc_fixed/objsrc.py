#!/usr/bin/env python3
"""objsrc.py - the first draft of a reconstructed source: a MUTOS 1700
object (a.out, 0407, with its relocation and symbol tables) written as a
mutos_as source that "mutos_as -C" assembles to the same object byte for
byte.

How the sources in orig/ were begun (docs/TOOLCHAIN_PLAN.md, section 7.3,
step F3b): each was drafted by this script from the real object in
tests/mutos1700_libc/ and then annotated by hand (comments only;
check_copy.py checks that each still gives its object). A draft is
mechanical:

  * the instructions as capstone decodes them, spelled as mutos_as reads
    them (the original assembler's syntax: "*" for a byte immediate, "#"
    for a word one, "disp(reg)" for a memory operand, a bare address for
    direct addressing, "b" after a mnemonic whose operand is a byte in
    memory, "j" for EB, "jmp" for E9, lodb/lodw/stob/stow/movs/movsb for
    the string instructions); a relocated field as its symbol and addend;
  * a relocated word where an instruction would begin as ".word";
  * the data as ".byte" runs and relocated ".word"s, before the text if
    the symbol table names its labels first, else after it (in two parts
    if need be);
  * the symbol table's order - the original's order of first mention - by
    ".globl" and ".comm" lines placed where the order needs them;
  * a jump target without a symbol of its own labelled L1, L2 ...: "-C"
    leaves such labels out of the symbol table, as the original cc's
    "as -gC" does (mutos_as's numeric labels "1:" are no way out: the
    original assembler does not have them).

Requires the Python module capstone (pip install capstone) and, for
--check, a built mutos_as.

Usage:
    objsrc.py OBJECT [-o SOURCE] [--offsets] [--check]
        writes the draft to SOURCE (default: standard output); --offsets
        ends each line of code with "|@t<offset>" (or "|@d" for data);
        --check assembles the draft with "mutos_as -C" and compares the
        result with OBJECT (exit status 1 if they differ).
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
    from capstone.x86 import X86_OP_REG, X86_OP_IMM, X86_OP_MEM
except ImportError:
    sys.exit('objsrc.py: needs the Python module "capstone" (pip install capstone)')

HERE = os.path.dirname(os.path.abspath(__file__))
MUTOS_AS = os.path.normpath(os.path.join(HERE, '..', 'mutos_as', 'mutos_as'))

R_TEXT, R_DATA, R_BSS, R_EXT = 2, 4, 6, 8
BYTEREGS = {'al', 'ah', 'bl', 'bh', 'cl', 'ch', 'dl', 'dh'}
JCC = {'je', 'jne', 'jl', 'jge', 'jle', 'jg', 'jb', 'jae', 'jbe', 'ja', 'js', 'jns', 'jo',
       'jno', 'jp', 'jnp'}
NOARG = {0x98: 'cbw', 0x99: 'cwd', 0xc3: 'ret', 0xfc: 'cld', 0xfd: 'std', 0x9c: 'pushf',
         0x9d: 'popf', 0x9f: 'lahf', 0x9e: 'sahf', 0xf5: 'cmc', 0xf8: 'clc', 0xf9: 'stc',
         0xac: 'lodb', 0xad: 'lodw', 0xaa: 'stob', 0xab: 'stow', 0xa5: 'movs', 0xa4: 'movsb',
         0x90: 'nop', 0xf4: 'hlt', 0xfa: 'cli', 0xfb: 'sti'}
STRING = {'lodsb': 'lodb', 'lodsw': 'lodw', 'stosb': 'stob', 'stosw': 'stow',
          'movsw': 'movs', 'movsb': 'movsb'}
# opcodes whose immediate is a byte ("*") and a word ("#")
BYTE_IMM = {0x80, 0x82, 0x83, 0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c, 0xa8, 0xc6, 0xf6} \
    | set(range(0xb0, 0xb8))
WORD_IMM = {0x81, 0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d, 0xa9, 0xc7, 0xf7} \
    | set(range(0xb8, 0xc0))


def load(path):
    with open(path, 'rb') as f:
        b = f.read()
    magic, t, d, bss, syms, entry, unused, flag = struct.unpack('<8H', b[:16])
    if magic != 0o407 or flag:
        raise ValueError('%s: not an object with relocation (magic %o, flag %d)'
                         % (path, magic, flag))
    so = 16 + 2 * (t + d)
    sym = []
    for i in range(so, so + syms, 12):
        sym.append((b[i:i + 8].rstrip(b'\0').decode('latin-1'), b[i + 8],
                    struct.unpack('<H', b[i + 10:i + 12])[0]))
    return dict(text=b[16:16 + t], data=b[16 + t:16 + t + d],
                trel=b[16 + t + d:16 + 2 * t + d], drel=b[16 + 2 * t + d:so], sym=sym)


def relocs(rel):
    """{field offset: (type, pc-relative, symbol index)} of a relocation
    table - a tag per word, bit 15 moving the field one byte up
    (man/mutos_aout.h.5)."""
    out = {}
    for o in range(0, len(rel), 2):
        tag = struct.unpack('<H', rel[o:o + 2])[0]
        if tag:
            out[o + (1 if tag & 0x8000 else 0)] = (tag & 0x0e, tag & 1, (tag & 0x7ff0) >> 4)
    return out


def num(v, hexa=False):
    if v < 0:
        return '-' + num(-v, hexa)
    return '/%x' % v if hexa or v > 255 else '%d.' % v


class Draft:
    def __init__(self, obj, offsets=False):
        self.o = obj
        self.offsets = offsets
        self.trel = relocs(obj['trel'])
        self.drel = relocs(obj['drel'])
        self.syms = obj['sym']
        self.kind = {n: (ty, v) for n, ty, v in self.syms}
        self.order = [n for n, ty, v in self.syms]
        self.tlab, self.dlab = {}, {}
        for n, ty, v in self.syms:
            if ty & 0x1f == 2:
                self.tlab.setdefault(v, []).append(n)
            elif ty & 0x1f == 3:
                self.dlab.setdefault(v, []).append(n)
        self.anon = set()

    # --- operands ---------------------------------------------------------
    def symexpr(self, field, value, rel):
        """A relocated field holding value, as an expression; the symbols
        it names."""
        typ, pcrel, idx = rel[field]
        if typ == R_EXT:
            n = self.syms[idx][0]
            if value >= 0x8000:
                value -= 0x10000
            if value == 0:
                return n, [n]
            if -256 < value < 256:
                return ('%s%+d.' % (n, value)), [n]
            return ('%s%s%s' % (n, '-' if value < 0 else '+', num(abs(value), True))), [n]
        labs = self.dlab if typ == R_DATA else self.tlab if typ == R_TEXT else {}
        best = max((a for a in labs if a <= value), default=None)
        if best is None:
            raise ValueError('no label at or below %d for a relocated field' % value)
        n = labs[best][0]
        return (n if value == best else '%s+%d.' % (n, value - best)), [n]

    def target(self, off, t):
        if t in self.tlab:
            labs = self.tlab[t]
            loc = [n for n in labs if not self.kind[n][0] & 0x20]
            n = (loc or labs)[0]
            return n, [n]
        self.anon.add(t)
        return 'ANON%d' % t, []

    def mem(self, ins, op, off):
        m = op.mem
        mentions = []
        disp = m.disp
        if ins.disp_size and (off + ins.disp_offset) in self.trel:
            d, mentions = self.symexpr(off + ins.disp_offset, disp & 0xffff, self.trel)
        elif m.base == 0 and m.index == 0:
            d = num(disp & 0xffff, True)
        elif disp == 0 and ins.disp_size == 0:
            d = ''
        else:
            d = num(disp)
        regs = [ins.reg_name(r) for r in (m.base, m.index) if r]
        return (d + '(' + ','.join(regs) + ')' if regs else d), mentions

    def imm(self, ins, op, off, marker):
        v = op.imm
        if ins.imm_size and (off + ins.imm_offset) in self.trel:
            e, m = self.symexpr(off + ins.imm_offset, v & 0xffff, self.trel)
            return marker + e, m
        v &= 0xff if ins.imm_size == 1 else 0xffff
        return marker + num(v, v > 9), []

    def insn(self, ins, off):
        b = ins.bytes
        op0 = b[0]
        mn = ins.mnemonic
        if len(b) == 1 and op0 in NOARG:
            return NOARG[op0], []
        if len(b) == 1 and op0 == 0xf3:
            return 'rep', []
        if mn in STRING:
            return STRING[mn], []
        ops = ins.operands
        if mn in JCC or op0 in (0xeb, 0xe9, 0xe8, 0xe2, 0xe3):
            name = {0xeb: 'j', 0xe9: 'jmp', 0xe8: 'call', 0xe2: 'loop', 0xe3: 'jcxz'}.get(op0, mn)
            if op0 in (0xe9, 0xe8) and (off + 1) in self.trel:
                addend = (struct.unpack('<H', b[1:3])[0] + off + 3) & 0xffff
                e, m = self.symexpr(off + 1, addend, self.trel)
                return '%s\t%s' % (name, e), m
            e, m = self.target(off, ops[0].imm & 0xffff)
            return '%s\t%s' % (name, e), m
        size = max((o.size for o in ops), default=2)
        marker = '*' if op0 in BYTE_IMM else '#' if op0 in WORD_IMM else ('*' if size == 1 else '#')
        txt, mentions = [], []
        for o in ops:
            if o.type == X86_OP_REG:
                txt.append(ins.reg_name(o.reg))
            elif o.type == X86_OP_MEM:
                e, m = self.mem(ins, o, off)
                txt.append(e)
                mentions += m
            elif o.type == X86_OP_IMM:
                if op0 in (0xd0, 0xd1):
                    txt.append('#1')
                else:
                    e, m = self.imm(ins, o, off, marker)
                    txt.append(e)
                    mentions += m
        if op0 in (0xd0, 0xd1) and len(txt) == 1:
            txt.append('#1')
        if op0 in (0x86, 0x87) and b[1] >= 0xc0:
            txt = [txt[1], txt[0]]           # mutos_as: the reg field first
        if mn == 'cdq':
            mn = 'cwd'
        if size == 1 and not any(t in BYTEREGS for t in txt) and mn != 'lea':
            mn += 'b'
        return '%s\t%s' % (mn, ','.join(txt)), mentions

    # --- segments ---------------------------------------------------------
    def text_lines(self):
        cs = Cs(CS_ARCH_X86, CS_MODE_16)
        cs.detail = True
        t = self.o['text']
        off = 0
        out = []
        while off < len(t):
            labs = list(self.tlab.get(off, []))
            if off in self.trel:            # a relocated word: data
                e, m = self.symexpr(off, struct.unpack('<H', t[off:off + 2])[0], self.trel)
                out.append((off, labs, '.word\t' + e, m))
                off += 2
                continue
            ins = next(cs.disasm(t[off:off + 15], off), None)
            if ins is None:
                out.append((off, labs, '.byte\t%s' % num(t[off]), []))
                off += 1
                continue
            s, m = self.insn(ins, off)
            out.append((off, labs, s, m))
            off += ins.size
        names = {a: 'L%d' % (i + 1) for i, a in enumerate(sorted(self.anon))}
        res = []
        for off, labs, s, m in out:
            s = re.sub(r'ANON(\d+)\b', lambda mm: names[int(mm.group(1))], s)
            res.append((off, labs + ([names[off]] if off in names else []), s, m))
        return res

    def data_lines(self):
        d = self.o['data']
        out = []
        off = 0
        while off < len(d):
            labs = list(self.dlab.get(off, []))
            if off in self.drel:
                e, m = self.symexpr(off, struct.unpack('<H', d[off:off + 2])[0], self.drel)
                out.append((off, labs, '.word\t' + e, m))
                off += 2
                continue
            end = off + 1
            while end < len(d) and end not in self.dlab and end not in self.drel and end - off < 12:
                end += 1
            out.append((off, labs, '.byte\t' + ','.join(num(x) for x in d[off:end]), []))
            off = end
        return out

    # --- the source -------------------------------------------------------
    def source(self):
        S = self.order
        tl = self.text_lines()
        dl = self.data_lines()
        first_text = min((S.index(n) for ns in self.tlab.values() for n in ns), default=10 ** 9)
        early, late, cur = [], [], None
        for line in dl:
            if line[1]:
                cur = early if min(S.index(n) for n in line[1]) < first_text else late
            (cur if cur is not None else early).append(line)
        body = []

        def seg(lines, name):
            body.append(('.' + name, []))
            for off, labs, s, m in lines:
                for lab in labs:
                    body.append((lab + ':', [lab] if lab in self.kind else []))
                body.append((s + ('\t|@%s%04x' % (name[0], off) if self.offsets else ''), m))
        if early:
            seg(early, 'data')
        seg(tl, 'text')
        if late:
            seg(late, 'data')
        out = []
        k = 0
        seen, declared = set(), set()

        def decl(s):
            ty, v = self.kind[s]
            if ty == 0x20:
                return '.comm\t%s,%d.' % (s, v) if v else '.globl\t%s' % s
            if ty & 0x20:
                declared.add(s)
                return '.globl\t%s' % s
            raise ValueError('the symbol order needs the local %s earlier than it can be '
                             'mentioned' % s)
        for line, mentions in body:
            pre = []
            for m in mentions:
                if m in seen:
                    continue
                j = S.index(m)
                while k < j:
                    if S[k] not in seen:
                        pre.append(decl(S[k]))
                        seen.add(S[k])
                    k += 1
                seen.add(m)
                k = max(k, j + 1)
                ty = self.kind[m][0]
                if ty & 0x20 and ty != 0x20 and m not in declared:
                    pre.append('.globl\t%s' % m)
                    declared.add(m)
            out += pre
            out.append(line)
        for s in S[k:]:
            if s not in seen:
                out.append(decl(s))
        for n, ty, v in self.syms:
            if ty & 0x20 and ty != 0x20 and n not in declared:
                out.append('.globl\t%s' % n)
        return '\n'.join(out) + '\n'


def assemble(src_path, out_path):
    r = subprocess.run([MUTOS_AS, '-C', '-o', out_path, src_path], capture_output=True, text=True)
    return r.returncode == 0, r.stdout + r.stderr


def main(argv):
    args = list(argv)
    out = None
    offsets = check = False
    obj = None
    while args:
        a = args.pop(0)
        if a == '-o':
            out = args.pop(0)
        elif a == '--offsets':
            offsets = True
        elif a == '--check':
            check = True
        elif obj is None and not a.startswith('-'):
            obj = a
        else:
            print(__doc__)
            return 2
    if obj is None:
        print(__doc__)
        return 2
    src = Draft(load(obj), offsets).source()
    if out:
        with open(out, 'w') as f:
            f.write(src)
    else:
        sys.stdout.write(src)
    if check:
        with tempfile.TemporaryDirectory() as d:
            s = os.path.join(d, 'x.s')
            o = os.path.join(d, 'x.o')
            with open(s, 'w') as f:
                f.write(src)
            ok, msg = assemble(s, o)
            if not ok:
                sys.stderr.write('objsrc.py: mutos_as: %s' % msg)
                return 1
            with open(o, 'rb') as f, open(obj, 'rb') as g:
                same = f.read() == g.read()
            sys.stderr.write('objsrc.py: %s: %s\n' % (obj, 'the same object' if same
                                                       else 'a DIFFERENT object'))
            return 0 if same else 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
