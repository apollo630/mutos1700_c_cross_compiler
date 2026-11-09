#!/usr/bin/env python3
"""check_copy.py - the corrected copy of libc.a against the original (in
"make test"; docs/TOOLCHAIN_PLAN.md, section 7.3, step F3b).

  1. Every reconstructed source orig/<name>.s, assembled by "mutos_as -C",
     is the original member tests/mutos1700_libc/<name>.o byte for byte -
     the proof of the reconstruction.
  2. Every corrected source fixed/<name>.s has its reconstruction, and its
     object keeps the member's interface: the same global symbols defined
     (at any address) and no external symbol the original does not
     reference - so that objects the original compiler made, and the
     original cc and ld on the hardware, link with the copy as with the
     original (decision 14).
  3. The copy - src/libc_fixed/libc.a, as make builds it (fix_archive.py)
     - and the copy of the kernel's arith.a, src/libc_fixed/arith.a (from
     tests/mutos1700_arith/), each against its original archive: it
     has the original archive's members in the original's order; each
     is the original's byte for byte but the corrected ones (the objects
     of fixed/) and the table of contents, __.SYMDEF, which names the
     same symbols; every member's header has the original's date, owner,
     group and mode (the table's date is its own). With nothing in
     fixed/, the copy is the original archive.

Which lines of fixed/ differ from orig/, and that each difference names a
correction of docs/RULES.md's register of the copy, is
scripts/check_rules.py's part. What the corrections compute is
tests/libc_runtime/rtcheck.py's ("make check-runtime").

Usage:
    python3 src/libc_fixed/check_copy.py [COPY]
        COPY: default src/libc_fixed/libc.a. Exit status 1 on a
        difference. Needs a built tree (make).
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import fix_archive as F  # noqa: E402

ORIG = os.path.join(HERE, 'orig')
ARITH = os.path.join(HERE, '..', '..', 'tests', 'mutos1700_arith', 'arith.a.base64.txt')
FIXED = os.path.join(HERE, 'fixed')
PROBLEMS = []


def problem(msg):
    PROBLEMS.append(msg)
    print('  FAIL ' + msg)


def assemble(src):
    with tempfile.TemporaryDirectory() as d:
        obj = os.path.join(d, 'x.o')
        r = subprocess.run([F.MUTOS_AS, '-C', '-o', obj, src], capture_output=True, text=True)
        if r.returncode:
            problem('%s: mutos_as: %s' % (os.path.relpath(src), (r.stdout + r.stderr).strip()))
            return None
        with open(obj, 'rb') as f:
            return f.read()


def symbols(obj):
    """(the global symbols defined, the external symbols referenced) of
    an object."""
    t, d, bss, syms = struct.unpack_from('<4H', obj, 2)
    so = 16 + 2 * (t + d)
    defined, undefined = set(), set()
    for i in range(so, so + syms, 12):
        name = obj[i:i + 8].rstrip(b'\0').decode('latin-1')
        ty = obj[i + 8]
        if ty == 0x20:
            undefined.add(name)
        elif ty & 0x20:
            defined.add(name)
    return defined, undefined


def symdef_names(content):
    """The names of a __.SYMDEF: entries of 8 bytes of name and a long."""
    return sorted(content[i:i + 8].rstrip(b'\0').decode('latin-1')
                  for i in range(0, len(content) - 11, 12))


def check_archive(copy, original, fixed):
    """Check 3 for one copy: the original archive's members in its order,
    each the original's but the corrected ones and __.SYMDEF; every
    header the original's but the table's date."""
    label = os.path.relpath(copy)
    if not os.path.exists(copy):
        problem('%s does not exist - run "make"' % label)
        return
    with open(copy, 'rb') as f:
        data = f.read()
    got, want = F.members(data), F.members(original)
    done = []
    if [m[0] for m in got] != [m[0] for m in want]:
        problem('%s: its members are not the original\'s, in its order' % label)
        return
    for (name, date, off, c), (_, odate, ooff, oc) in zip(got, want):
        # owner, group and mode the original's; the date too, but the
        # table's (made anew by ranlib)
        if data[off + 18:off + 22] != original[ooff + 18:ooff + 22] or \
                (date != odate and name != F.SYMDEF):
            problem('%s: %s: its header (date, owner, group, mode) is not the '
                    'original\'s' % (label, name))
        if name == F.SYMDEF:
            if symdef_names(c) != symdef_names(oc):
                problem('%s: __.SYMDEF names other symbols than the original\'s' % label)
        elif name in fixed:
            done.append(name)
            if c != fixed[name]:
                problem('%s: %s: not the object of fixed/%s.s' % (label, name, name[:-2]))
        elif c != oc:
            problem('%s: %s: differs from the original, and nothing corrects it'
                    % (label, name))
    if not done and data != original:
        problem('%s: nothing is corrected, but it is not the original archive' % label)
    print('libc copy: %s - %d members, %d corrected (%s), the rest%s the original\'s'
          % (label, len(got), len(done), ', '.join(sorted(done)) or 'none',
             ' and the names of __.SYMDEF' if any(m[0] == F.SYMDEF for m in got) else ''))


def main(argv):
    copy = argv[0] if argv else os.path.join(HERE, 'libc.a')
    for tool in (F.MUTOS_AS,):
        if not os.access(tool, os.X_OK):
            sys.exit('check_copy.py: %s is not built - run "make" first' % tool)
    # 1. the reconstructions
    recon = sorted(n[:-2] for n in os.listdir(ORIG) if n.endswith('.s'))
    for name in recon:
        want = F.original_member(name + '.o')
        got = assemble(os.path.join(ORIG, name + '.s'))
        if want is None:
            problem('orig/%s.s: libc.a has no member %s.o' % (name, name))
        elif got is not None and got != want:
            problem('orig/%s.s: not the original %s.o byte for byte' % (name, name))
    print('libc copy: %d reconstructed members, each the original byte for byte'
          % len(recon) if not PROBLEMS else 'libc copy: the reconstructions differ')
    # 2. the corrected sources
    fixed = {}
    if os.path.isdir(FIXED):
        for n in sorted(os.listdir(FIXED)):
            if not n.endswith('.s'):
                continue
            name = n[:-2]
            if name not in recon:
                problem('fixed/%s: no reconstruction orig/%s' % (n, n))
                continue
            obj = assemble(os.path.join(FIXED, n))
            if obj is None:
                continue
            fixed[name + '.o'] = obj
            od, ou = symbols(F.original_member(name + '.o'))
            nd, nu = symbols(obj)
            if od != nd:
                problem('fixed/%s: defines %s, the original %s' % (n, sorted(nd), sorted(od)))
            if not nu <= ou:
                problem('fixed/%s: references %s, which the original does not'
                        % (n, sorted(nu - ou)))
    # 3. the copies
    check_archive(copy, F.original_libc(), fixed)
    if not argv:
        check_archive(os.path.join(HERE, 'arith.a'), F.read_archive(ARITH), fixed)
    if PROBLEMS:
        print('check_copy: %d problem(s)' % len(PROBLEMS))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
