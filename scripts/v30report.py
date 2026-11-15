#!/usr/bin/env python3
"""v30report.py - the 80186/V30 instructions of a build with -1, summed.

Under -1 mutos_as reports one line for each assembly (docs/RULES.md;
docs/TOOLCHAIN_PLAN.md, decision 16):

    as: -1: 15 80186/V30 instructions: push 4, shift 7, imul 4
    as: -1: no 80186/V30 instructions

This sums those lines of a build's log - the MUTOS kernel's, built with
the cross toolchain and "-1" in its CFLAGS (docs/KERNEL_BUILD.md, step
8) - by kind, and names the source of each when the log has the
commands (the kernel's makefile runs its recipes with "set -x": the
line "+ cc ... x.c" before the report):

    cd ~/sys_cross/conf
    $M/make CFLAGS="-c -O -K1024 -1" 2>&1 | tee ../build.log
    python3 scripts/v30report.py ../build.log

Usage:
    v30report.py LOG [--files]

--files lists every assembly with its numbers. Exit status 1 if the log
has no report at all (the build ran without -1). Python 3, standard
library only.
"""
import re
import sys

REPORT = re.compile(r'^as: -1: (?:no|(\d+)) 80186/V30 instructions?(?:: (.*))?$')
CC = re.compile(r'^\+ cc\s.*?(\S+\.[cs])\s*$')


def read(path):
    """[(source or '?', {kind: n})] for each report in the log, and the
    number of "+ cc" lines."""
    out, src, ncc = [], '?', 0
    with open(path, encoding='latin-1') as f:
        for ln in f:
            ln = ln.rstrip('\n')
            m = CC.match(ln)
            if m:
                src, ncc = m.group(1), ncc + 1
                continue
            m = REPORT.match(ln)
            if m:
                kinds = {}
                for part in (m.group(2) or '').split(', '):
                    if part:
                        w, n = part.split()
                        kinds[w] = int(n)
                out.append((src, kinds))
                src = '?'
    return out, ncc


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        print(__doc__)
        return 2
    reports, ncc = read(args[0])
    if not reports:
        print('v30report: no "as: -1:" line in %s - a build without -1' % args[0])
        return 1
    total = {}
    for _src, kinds in reports:
        for w, n in kinds.items():
            total[w] = total.get(w, 0) + n
    order = [w for w in ('push', 'shift', 'imul') if w in total] + \
        sorted(w for w in total if w not in ('push', 'shift', 'imul'))
    if '--files' in sys.argv[1:]:
        for src, kinds in reports:
            print('%-16s %s' % (src, ', '.join('%s %d' % (w, kinds[w]) for w in order
                                                 if w in kinds) or 'none'))
    with_any = sum(1 for _s, k in reports if k)
    print('v30report: %d assemblies with -1%s, %d of them with 80186/V30 instructions'
          % (len(reports), ' (of %d cc commands in the log)' % ncc if ncc else '', with_any))
    print('v30report: %d 80186/V30 instructions: %s'
          % (sum(total.values()), ', '.join('%s %d' % (w, total[w]) for w in order) or 'none'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
