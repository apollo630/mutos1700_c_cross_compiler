#!/usr/bin/env python3
"""c1dis.py - annotated 8086 disassembly of the real MUTOS 1700 c1.

Reads the binary from ../../mutos_bin/c1.golden.base64.txt, names its
functions and data from c1map.txt, names the libc.a functions linked in
by matching every libc.a object (../mutos1700_libc/libc.a.base64.txt)
against the text, relocated words as wildcards, and prints a listing
with call targets, data names and the strings an immediate points at.

Usage:
    c1dis.py NAME               one function (a name from c1map.txt)
    c1dis.py START END          a text range (hex offsets)
    c1dis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the c1 port (see docs/C1_PORT_PLAN.md); it is not part of any
test.
"""
import base64
import os
import re
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))


def _aout(path):
    img = base64.b64decode(open(path, 'rb').read())
    magic, tsize, dsize = struct.unpack_from('<3H', img, 0)
    return img[16:16 + tsize], img[16 + tsize:16 + tsize + dsize]


TEXT, DATA = _aout(os.path.join(ROOT, 'mutos_bin', 'c1.golden.base64.txt'))
MAP = 'c1map.txt'


def use(binary):
    """Switch to another binary of mutos_bin/ and its map, <binary>map.txt
    (c0dis.py uses this for c0)."""
    global TEXT, DATA, MAP
    TEXT, DATA = _aout(os.path.join(ROOT, 'mutos_bin', binary + '.golden.base64.txt'))
    MAP = binary + 'map.txt'


def read_map():
    funcs, data = {}, {}
    with open(os.path.join(HERE, MAP)) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            parts = line.split(None, 5)
            if parts[0] == 'T':
                funcs[int(parts[1], 16)] = (parts[2], parts[3],
                                            parts[5].strip() if len(parts) > 5 else '',
                                            parts[4])
            elif parts[0] == 'D':
                data[int(parts[1], 16)] = parts[2]
    return funcs, data


def _ar_members(img):
    p = 2
    while p + 26 <= len(img):
        name = img[p:p + 14].split(b'\0')[0].decode('latin1')
        hi, lo = struct.unpack_from('<HH', img, p + 22)
        size = (hi << 16) | lo
        yield name, img[p + 26:p + 26 + size]
        p += 26 + size + (size & 1)


def libc_names():
    """Text offset -> libc.a symbol, for every object found in the text."""
    lib = base64.b64decode(open(os.path.join(
        ROOT, 'tests', 'mutos1700_libc', 'libc.a.base64.txt'), 'rb').read())
    found = {}
    for name, obj in _ar_members(lib):
        if not name.endswith('.o'):
            continue
        h = struct.unpack_from('<8H', obj, 0)
        tsize, dsize, ssize, flag = h[1], h[2], h[4], h[7]
        if tsize < 4 or flag:
            continue
        st, sd = (tsize + 1) & ~1, (dsize + 1) & ~1
        text = obj[16:16 + tsize]
        rel = obj[16 + st + sd:16 + 2 * st + sd]
        wild = set()
        for off in range(0, len(rel) - 1, 2):
            r = struct.unpack_from('<H', rel, off)[0]
            if r:
                t = off + (1 if r & 0x8000 else 0)
                wild.update((t, t + 1))
        pat = b''.join(b'.' if i in wild else re.escape(text[i:i + 1])
                       for i in range(tsize))
        starts = [m.start() for m in re.finditer(pat, TEXT, re.S)]
        if len(starts) != 1:
            continue
        symoff = 16 + 2 * (st + sd)
        for s in range(symoff, symoff + ssize, 12):
            sname = obj[s:s + 8].split(b'\0')[0].decode('latin1')
            styp, sval = obj[s + 8], struct.unpack_from('<H', obj, s + 10)[0]
            if styp & 0x1f == 2:
                found.setdefault(starts[0] + sval, sname)
    return found


def cstr(a):
    # only the start of a string: the byte before it ends another one
    if not 0 < a < len(DATA) or DATA[a - 1] != 0:
        return None
    end = DATA.find(b'\0', a)
    s = DATA[a:end]
    if len(s) < 2 or any(not (32 <= (c & 0x7f) < 127 or (c & 0x7f) in (9, 10))
                         for c in s):
        return None
    return ''.join(('~' if c & 0x80 else '') +
                   {10: '\\n', 9: '\\t'}.get(c & 0x7f, chr(c & 0x7f))
                   for c in s)[:60]


def disassemble(start, end, names, dnames):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    out = []
    for i in md.disasm(TEXT[start:end], start):
        if i.address in names:
            out.append('%s:' % names[i.address])
        notes = []
        if i.mnemonic in ('call', 'jmp') and i.operands \
                and i.operands[0].type == X86_OP_IMM:
            t = i.operands[0].imm & 0xffff
            if t in names:
                notes.append('-> ' + names[t])
        for op in i.operands:
            if op.type == X86_OP_IMM and not i.mnemonic.startswith(('j', 'call', 'loop')):
                # small immediates are numbers (ops, types, sizes), not addresses
                v = op.imm & 0xffff
                if v >= 0x0a and v in dnames:
                    notes.append('&' + dnames[v])
                elif v >= 0x60 and cstr(v):
                    notes.append('"%s"' % cstr(v))
            elif op.type == X86_OP_MEM:
                v = op.mem.disp & 0xffff
                absolute = op.mem.base == 0 and op.mem.index == 0
                # a small displacement off a register is a structure field
                if v in dnames and (absolute or v >= 0x100):
                    notes.append(('[%s]' if absolute else 'base %s') % dnames[v])
        out.append('  %04x: %-7s%s%s' % (i.address, i.mnemonic, i.op_str,
                                        ('    ; ' + ', '.join(notes)) if notes else ''))
    return '\n'.join(out)


def main(argv):
    funcs, dnames = read_map()
    # a libc.a object can match by chance inside the program's own code
    # (c0's getcc ends in the bytes of mount.o); libc.a is linked last
    names = {a: n for a, n in libc_names().items() if a > max(funcs)}
    names.update({a: v[0] for a, v in funcs.items()})
    starts = sorted(funcs) + [min(a for a in names if a not in funcs and a > max(funcs))]
    if argv[:1] == ['--functions']:
        for a, b in zip(starts, starts[1:]):
            n, status, note, port = funcs[a]
            print('%04x %5d %-10s %-7s %-7s %s' % (a, b - a, n, status, port, note))
        return 0
    if len(argv) == 1:
        by_name = {v[0]: a for a, v in funcs.items()}
        if argv[0] not in by_name:
            sys.exit('%s: no function %s in %s' % (os.path.basename(sys.argv[0]), argv[0], MAP))
        a = by_name[argv[0]]
        print(disassemble(a, starts[starts.index(a) + 1], names, dnames))
        return 0
    if len(argv) == 2:
        print(disassemble(int(argv[0], 16), int(argv[1], 16), names, dnames))
        return 0
    sys.exit(__doc__.split('Usage:')[1].split('Requires')[0])


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
