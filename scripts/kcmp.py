#!/usr/bin/env python3
"""kcmp.py - compare the objects of a MUTOS kernel build with reference objects.

Step 6 of docs/KERNEL_BUILD.md: after the MUTOS kernel's own conf/Makefile
has run through mutos_make (the 7100 configuration, "TYP = M7100"), every
object it made is compared with the real-hardware object of
tests/mutos_as/kernel_opt/ (made with exactly that configuration), or with
the same file of another build.

Usage:
    kcmp.py CONF REFDIR            every object of the 7100 build against
                                   REFDIR/<name>.o.golden (the repo's
                                   tests/mutos_as/kernel_opt)
    kcmp.py -t CONF REFCONF        every object and the kernel "mutos" against
                                   the same files of another build (REFCONF is
                                   that tree's conf/ directory)
    kcmp.py A B                    two objects or two kernels

CONF is the kernel's conf/ directory after "make". An object counts as the same
when its header (but the symbol table's size), its text and data and its
relocation (an external relocation by its symbol's name) are the reference's.
The symbol tables are compared apart: the repo's goldens were assembled
without -C, so they carry the local L symbols that cc's "as -gC" leaves out.

Exit status: 0 all the same, 1 a difference, 2 a usage error.
"""
import os
import struct
import sys

# object of the build (relative to conf/) -> name of the reference
OBJECTS = [
    ('symtab_M7100.o', 'symtab'), ('c_M7100.o', 'c'),
    ('../mdep/mch_M7100.o', 'mch'), ('../mdep/gcpc_NOBIOS.o', 'gcpc'),
    ('../mdep/genio_M7100.o', 'genio'),
] + [('../mdep/%s.o' % n, n) for n in (
    'debug', 'main', 'trap', 'machdep', 'sbreak', 'physio', 'text',
    'dosent', 'load86', 'msdos', 'kdebug')] + [
    ('../io/intr_AC.o', 'intr'), ('../io/cons_M7100.o', 'cons_NOBIOS'),
] + [('../io/%s.o' % n, n) for n in (
    'lp_AC', 'bus', 'amx', 'k5170', 'ifss', 'v24', 'ifsp', 'v30ide',
    'mem', 'sys', 'tty', 'delay', 'dsort', 'ramfl', 'dlock')] + [
    ('../cfg/clp_AC.o', 'clp'),
] + [('../cfg/%s.o' % n, n) for n in (
    'c5170', 'casp', 'camx', 'cram', 'ccons')] + [
    ('../sys/%s.o' % n, n) for n in (
    'acct', 'alloc', 'bio', 'clocal', 'clock', 'fio', 'iget', 'malloc',
    'nami', 'pipe', 'prf', 'prim', 'rdwri', 'sig', 'slp', 'subr', 'sys1',
    'sys2', 'sys3', 'sys4', 'sysent', 'sysx')]


def aout(b):
    """header words, text+data, relocation, symbols (12 bytes each)."""
    if len(b) < 16:
        raise ValueError('shorter than an a.out header')
    h = struct.unpack_from('<8H', b, 0)
    t, d, syms = h[1], h[2], h[4]
    norel = h[7] & 1
    body = b[16:16 + t + d]
    reloc = b'' if norel else b[16 + t + d:16 + 2 * (t + d)]
    off = 16 + (t + d) * (1 if norel else 2)
    st = [b[off + i:off + i + 12] for i in range(0, syms, 12)]
    return h, body, reloc, st


def named(rel, st):
    """relocation words, an external one by its symbol's name (bits 4-14)."""
    out = []
    for i in range(0, len(rel), 2):
        r = struct.unpack_from('<H', rel, i)[0]
        if r & 0x0e == 0x08:
            k = (r >> 4) & 0x7ff
            out.append((r & 0x800f, st[k][:8] if k < len(st) else k))
        else:
            out.append(r)
    return out


def local_l(s):
    typ = struct.unpack_from('<H', s, 8)[0]
    return s[:1] == b'L' and (typ & 0x20) == 0 and (typ & 0x1f) != 0


def compare(new, ref):
    """'same' or a short reason."""
    h, body, reloc, syms = aout(new)
    gh, gbody, greloc, gsyms = aout(ref)
    if h[:4] != gh[:4] or h[5:] != gh[5:]:
        return 'header: text %d data %d bss %d, reference %d %d %d' % (
            h[1], h[2], h[3], gh[1], gh[2], gh[3])
    if body != gbody:
        n = next(i for i in range(len(body)) if body[i] != gbody[i])
        seg = 'text' if n < h[1] else 'data'
        off = n if n < h[1] else n - h[1]
        return 'text/data differ (first at %s 0x%04x)' % (seg, off)
    if named(reloc, syms) != named(greloc, gsyms):
        return 'relocation differs'
    if syms != gsyms and syms != [s for s in gsyms if not local_l(s)]:
        return 'same code, symbol table differs'
    return 'same'


def rd(p):
    with open(p, 'rb') as f:
        return f.read()


def main(argv):
    tree = len(argv) == 4 and argv[1] == '-t'
    if tree:
        argv = argv[:1] + argv[2:]
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    a, b = argv[1], argv[2]
    if not tree and os.path.isfile(a) and os.path.isfile(b):
        r = compare(rd(a), rd(b))
        print(r)
        return 0 if r == 'same' else 1
    if not (os.path.isdir(a) and os.path.isdir(b)):
        sys.stderr.write(__doc__)
        return 2
    counts = {}
    bad = 0
    for obj, name in OBJECTS + ([('mutos', 'mutos')] if tree else []):
        p = os.path.join(a, obj)
        ref = None
        cands = [obj] if tree else [name + '.o.golden']
        for cand in cands:
            if os.path.isfile(os.path.join(b, cand)):
                ref = os.path.join(b, cand)
                break
        if not os.path.isfile(p):
            r = 'not built'
        elif ref is None:
            r = 'no reference'
        else:
            try:
                r = compare(rd(p), rd(ref))
            except (ValueError, struct.error, IndexError) as e:
                r = 'unreadable: %s' % e
        key = r if r in ('same', 'not built', 'no reference') else 'DIFFERENT'
        counts[key] = counts.get(key, 0) + 1
        if key == 'DIFFERENT':
            bad += 1
        print('%-24s %-14s %s' % (obj, name, r))
    print()
    print(', '.join('%s: %d' % (k, counts[k]) for k in sorted(counts)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
