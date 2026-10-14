#!/usr/bin/env python3
"""c1tables.py - the real MUTOS 1700 c1's code tables, read from the binary.

The original c1 (../../mutos_bin/c1.golden.base64.txt) is v7's c1 ported
to the 8086: its code tables are v7's data structures (v7/cc/c1.h:
struct table {int tabop; struct optab *tabp;}, struct optab {char
tabdeg1, tabtyp1, tabdeg2, tabtyp2; char *tabstring;}), compiled by
v7's cvopt (v7/cc/cvopt.c) from a table source in v7's own notation, with
the instructions in MUTOS assembler syntax. This script reads them out
of the data segment and writes them back in v7 table.s notation (the
inverse of cvopt), together with the operator tables c1 uses with them
(opdope[], opntab[], instab[], branchtab[]).

Where the tables are (data-segment offsets, found from the code that
uses them): rcexpr() compares its table argument with cctab (3e66) and
efftab (3c9e) and passes regtab (334a) for RFORCE; sptab (40ca) is the
table comarg() pushes arguments with; opdope[] (0aa2) and opntab[] (0b9a)
are the arrays every function indexes by an op; instab (0d4c) is what
cexpr's 'M' macro hands prins(); branchtab (0e84) follows it.

Usage:
    c1tables.py                 print the reconstruction
    c1tables.py --check FILE    exit 1 unless FILE is exactly what this
                                script prints (keeps c1_tables.txt honest)
    c1tables.py --c             print the same tables as C source for the
                                c1 port (src/mutos_cc/c1port/c1tab.c):
                                the template strings byte for byte, 0200
                                bits included
    c1tables.py --check-c FILE  exit 1 unless FILE is exactly what --c
                                prints
    c1tables.py --rows          one line per distinct optab row (offset,
                                table, op, classes) - for check_real.py's
                                coverage report
"""
import base64
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
C1 = os.path.normpath(os.path.join(HERE, '..', '..', 'mutos_bin',
                                   'c1.golden.base64.txt'))

TABLES = [('regtab', 0x334a), ('efftab', 0x3c9e), ('cctab', 0x3e66),
          ('sptab', 0x40ca)]
OPDOPE, OPNTAB, NOPNAMES = 0x0aa2, 0x0b9a, 122
INSTAB, BRANCHTAB = 0x0d4c, 0x0e84

# cvopt's encodings (v7/cc/cvopt.c): degree classes and type flags
DEG = {4: 'z', 5: '1', 8: 'c', 9: 'r', 12: 'i', 16: 'a', 20: 'e', 63: 'n'}
TYP = {0: '', 1: 'w', 2: 'i', 3: 'b', 4: 'f', 5: 'd', 6: 's', 8: 'l', 9: 'u'}

# cvopt's output letters back to the table-source macros - only those the
# MUTOS cexpr() interprets (v7's O, E and F - "A", "C1", "C2" - are not
# among them and do not occur in its templates)
MACRO = {'A': 'A1', 'B': 'A2', 'C': 'B1', 'D': 'B2', 'L': 'BE', 'P': 'BF',
         'J': 'R1', 'M': 'I', '#': '#1', '"': '#2',
         # MUTOS-only letters, not produced by v7's cvopt: kept literal,
         # in angle brackets so they cannot be read as v7's R/S
         'U': '<U>', 'Q': '<Q>', 'R': '<R>', 'S': '<S>'}

HEADER = """\
/ MUTOS 1700 c1 code tables, read from the original binary
/ (mutos_bin/c1.golden.base64.txt) by tests/realbin/c1tables.py - generated,
/ do not edit; "make check-realbin" checks it is up to date.
/
/ Notation: v7/cc/table.s (the inverse of v7/cc/cvopt.c).
/   %<left>,<right>: degree class z=0 1=1 c=constant r=register variable
/   a=addressable e=easy n=any, '*' = must be an indirection; type flag
/   w b f d l u (word, byte, float, double, long, unsigned).
/   F, S, H: compute the left operand, the right one, the whole tree
/   (modifiers: * its operand, S onto the stack, C condition codes only,
/   1 into R1, ! into this register, d into DX:AX - '!' and 'd' are MUTOS
/   additions). R, R1: the result register and the second one; R*: the
/   register used as a base (cexpr moves it to BX unless it is SI or DI).
/   A1, A2: address of the left/right operand; B1, B2: suffix by type
/   ('b' char, 's' float, 'd' double); I: instruction from instab (I' its
/   second form, e.g. inc/dec); #1, #2: offset of an indirection; T: sign
/   extension (cwd, cbw, sub dx,dx); V: adc/sbb for a long's high word;
/   X<n>: long relational. MUTOS-only: <U> pushes the compile-time model
/   of the floating-point stack ("Floating point stack overflow; simplify
/   expression"), <Q> pops it ("Floating point stack underflow"), <R>
/   pops it ("floating point stack underflow") and prints 'p' only while
/   rcexpr's nesting counter (data 000a) is 0; a literal "push"/"pop" in
/   a template counts nstack.
"""


def load_data():
    img = base64.b64decode(open(C1, 'rb').read())
    magic, tsize, dsize = struct.unpack_from('<3H', img, 0)
    assert magic == 0o411, 'c1 is expected to be a 0411 a.out'
    return img[16 + tsize:16 + tsize + dsize]


D = load_data()


def word(a):
    return struct.unpack_from('<H', D, a)[0]


def rawstr(a):
    end = D.index(b'\0', a)
    return D[a:end]


def plain(a):
    return rawstr(a).decode('latin1')


def opname(op):
    if 0 <= op < NOPNAMES and word(OPNTAB + 2 * op):
        return plain(word(OPNTAB + 2 * op))
    return 'op %d' % op


def table(addr):
    ents = []
    while word(addr):
        ents.append((word(addr), word(addr + 2)))
        addr += 4
    return ents


def optab(p):
    rows = []
    while D[p]:
        rows.append((p, D[p], D[p + 1], D[p + 2], D[p + 3], word(p + 4)))
        p += 6
    return rows


def cls(d, t):
    s = DEG.get(d & 0o77, '?%d' % (d & 0o77))
    s += TYP.get(t & 15, '?%d' % (t & 15))
    if t & 16:
        s += 'p'
    if d & 0o100:
        s += '*'
    return s


def mods(c):
    v = ord(c) - ord('A')
    return ''.join(m for bit, m in ((1, '*'), (2, 'S'), (4, 'C'), (8, '1'),
                                    (16, '2')) if v & bit)


def render(raw):
    """One template string back to table-source lines. cvopt drops each
    line's leading tab and the newline after F/S/H/T/X (its nlflg and
    snlflg); a 0200 bit stands for a tab inside the line."""
    out = '\t'
    i = 0

    def newline_if_more():
        nonlocal out
        if i < len(raw) and (raw[i] & 0x7f) != 0o12:
            out += '\n\t'
    while i < len(raw):
        c = raw[i]
        i += 1
        if c & 0x80:
            out += '\t'
            c &= 0x7f
        ch = chr(c)
        if ch in 'GKH' and i < len(raw):
            out += {'G': 'F', 'K': 'S', 'H': 'H'}[ch] + mods(chr(raw[i] & 0x7f))
            i += 1
            while i < len(raw) and chr(raw[i]) in '!d':
                out += chr(raw[i])
                i += 1
            newline_if_more()
        elif ch == 'I' and i < len(raw):
            out += 'R' + mods(chr(raw[i] & 0x7f))
            i += 1
        elif ch == 'X' and i < len(raw):
            out += 'X' + chr(raw[i])
            i += 1
            newline_if_more()
        elif ch == 'T':
            out += 'T'
            newline_if_more()
        elif ch == '\n':
            out += '\n\t' if i < len(raw) else '\n'
        else:
            out += MACRO.get(ch, ch)
    return [ln for ln in out.split('\n') if ln.strip()]


def all_rows():
    """(row offset, table, op, d1, t1, d2, t2) for every distinct row,
    with the first op that uses its list."""
    seen = set()
    rows = []
    for name, addr in TABLES:
        for op, p in table(addr):
            for r in optab(p):
                if r[0] not in seen:
                    seen.add(r[0])
                    rows.append((r[0], name, op) + r[1:5])
    return rows


def reconstruct():
    out = [HEADER]
    nrows = len(all_rows())
    strings = {r[5] for name, addr in TABLES for op, p in table(addr)
               for r in optab(p)}
    out.append('/ %d distinct rows, %d template strings; data offsets:'
               % (nrows, len(strings)))
    out.append('/ ' + ', '.join('%s %04x' % t for t in TABLES))
    out.append('')
    for name, addr in TABLES:
        out.append('/ ======== %s (%04x)' % (name, addr))
        first = {}
        for op, p in table(addr):
            if p in first:
                out.append('/ op %d %s: the list of op %d'
                           % (op, opname(op), first[p]))
                continue
            first[p] = op
            out.append('/ op %d %s (list %04x)' % (op, opname(op), p))
            for off, d1, t1, d2, t2, sp in optab(p):
                out.append('%%%s,%s\t\t/ row %04x' % (cls(d1, t1), cls(d2, t2), off))
                out.extend(render(rawstr(sp)))
            out.append('')
    out.append('/ ======== operators: op, opntab[] name, opdope[] (octal)')
    for op in range(NOPNAMES):
        out.append('/ %3d %-16s %06o' % (op, opname(op), word(OPDOPE + 2 * op)))
    out.append('')
    out.append('/ ======== instab (%04x): op, first and second form' % INSTAB)
    a = INSTAB
    while word(a):
        out.append('/ %3d %-14s %-6s %s' % (word(a), opname(word(a)),
                                          plain(word(a + 2)), plain(word(a + 4))))
        a += 6
    out.append('')
    out.append('/ ======== branchtab (%04x): op, branch, inverse' % BRANCHTAB)
    a = BRANCHTAB
    while word(a):
        out.append('/ %3d %-14s %-6s %s' % (word(a), opname(word(a)),
                                          plain(word(a + 2)), plain(word(a + 4))))
        a += 6
    return '\n'.join(out) + '\n'


MAPREL, NOTREL, REGNAME, NOPDOPE, LRTAB = 0x000c, 0x0016, 0x0020, 124, 0x0536

C_HEADER = """\
/*
 * c1tab.c - the real MUTOS 1700 c1's tables, read from the original binary
 * (mutos_bin/c1.golden.base64.txt) by tests/realbin/c1tables.py --c.
 * GENERATED - do not edit; "make check-realbin" checks it is up to date.
 *
 * The code tables (regtab, efftab, cctab, sptab) are v7's struct table
 * lists of struct optab rows, as v7's cvopt compiled them from a table
 * source in v7's notation with MUTOS assembler text - readable in
 * tests/realbin/c1_tables.txt. Every template string is the binary's,
 * byte for byte (a 0200 bit asks cexpr() for a tab). Each list and
 * string is named by its data-segment offset in the binary.
 */
#include "c1.h"
"""


def cstring(raw):
    out = '"'
    for c in raw:
        if c == 0o11:
            out += '\\t'
        elif c == 0o12:
            out += '\\n'
        elif c in (0x22, 0x5c, 0x3f):
            out += '\\' + chr(c)
        elif 0x20 <= c < 0x7f:
            out += chr(c)
        else:
            out += '\\%03o' % c
    return out + '"'


def c_source():
    out = [C_HEADER]
    out.append('char maprel[] = {%s};' % ', '.join(str(D[MAPREL + i]) for i in range(10)))
    out.append('char notrel[] = {%s};' % ', '.join(str(D[NOTREL + i]) for i in range(10)))
    out.append('char *regname[8] = {%s};' % ', '.join(
        cstring(rawstr(word(REGNAME + 2 * i))) for i in range(8)))
    out.append('')
    out.append('/* xlongrel()\'s branches for a long relation (v7\'s lrtab) */')
    out.append('char lrtab[2][3][6] = {')
    for z in range(2):
        out.append('\t{' + ', '.join('{%s}' % ', '.join(str(D[LRTAB + z * 18 + r * 6 + i])
                                                       for i in range(6))
                                     for r in range(3)) + '},')
    out.append('};')
    out.append('')
    out.append('int16_t opdope[%d] = {' % NOPDOPE)
    for op in range(NOPDOPE):
        out.append('\t%07o,\t/* %3d %s */' % (word(OPDOPE + 2 * op), op,
                                             opname(op) if op < NOPNAMES else '(table lookup only)'))
    out.append('};')
    out.append('')
    out.append('char *opntab[%d] = {' % NOPNAMES)
    for op in range(NOPNAMES):
        out.append('\t%s,\t/* %3d */' % (cstring(rawstr(word(OPNTAB + 2 * op))), op))
    out.append('};')
    out.append('')
    for name, addr in (('instab', INSTAB), ('branchtab', BRANCHTAB)):
        out.append('struct instab %s[] = {' % name)
        a = addr
        while word(a):
            out.append('\t{%3d, %s, %s},' % (word(a), cstring(rawstr(word(a + 2))),
                                            cstring(rawstr(word(a + 4)))))
            a += 6
        out.append('\t{0, 0, 0}')
        out.append('};')
        out.append('')
    strings = sorted({r[5] for n, addr in TABLES for op, p in table(addr)
                      for r in optab(p)})
    out.append('/* template strings */')
    for sp in strings:
        out.append('static char s%04x[] = %s;' % (sp, cstring(rawstr(sp))))
    out.append('')
    done = set()
    for n, addr in TABLES:
        for op, p in table(addr):
            if p in done:
                continue
            done.add(p)
            out.append('static struct optab l%04x[] = {' % p)
            for off, d1, t1, d2, t2, sp in optab(p):
                out.append('\t{0%o, %d, 0%o, %d, s%04x},\t/* %%%s,%s */'
                           % (d1, t1, d2, t2, sp, cls(d1, t1), cls(d2, t2)))
            out.append('\t{0}')
            out.append('};')
    out.append('')
    for n, addr in TABLES:
        out.append('struct table %s[] = {' % n)
        for op, p in table(addr):
            out.append('\t{%3d, l%04x},\t/* %s */' % (op, p, opname(op)))
        out.append('\t{0, 0}')
        out.append('};')
        out.append('')
    return '\n'.join(out)


def main(argv):
    if argv[:1] == ['--c']:
        sys.stdout.write(c_source())
        return 0
    if argv[:1] == ['--check-c']:
        with open(argv[1]) as f:
            if f.read() != c_source():
                print('%s is not what c1tables.py --c reads from the binary - '
                      'regenerate it' % argv[1])
                return 1
        print('c1 tables: %s up to date' % os.path.basename(argv[1]))
        return 0
    if argv[:1] == ['--rows']:
        for off, name, op, d1, t1, d2, t2 in all_rows():
            print('%04x %-6s %3d %-14s %%%s,%s' % (off, name, op, opname(op),
                                                cls(d1, t1), cls(d2, t2)))
        return 0
    text = reconstruct()
    if argv[:1] == ['--check']:
        with open(argv[1]) as f:
            if f.read() != text:
                print('%s is not what c1tables.py reads from the binary - '
                      'regenerate it' % argv[1])
                return 1
        print('c1 tables: %s up to date' % os.path.basename(argv[1]))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
