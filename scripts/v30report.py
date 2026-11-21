#!/usr/bin/env python3
"""v30report.py - the 80186/V30 instructions of a build with -1, summed.

Under -1 mutos_as reports one line for each assembly (docs/RULES.md;
docs/TOOLCHAIN_PLAN.md, decision 16):

    as: -1: 15 80186/V30 instructions: push 4, shift 7, imul 4
    as: -1: no 80186/V30 instructions

This sums those lines of a build's log - the MUTOS kernel's, built with
the cross toolchain and "-1" in its CFLAGS (docs/KERNEL_BUILD_V30.md) -
by kind, and names the source of each when the log has the commands (the
kernel's makefile runs its recipes with "set -x": the line "+ cc ... x.c"
before the report):

    cd ~/sys_v30/conf
    $M/make CFLAGS="-c -O -K1024 -1" 2>&1 | tee ../build.log
    python3 scripts/v30report.py ../build.log

--forms (run in the directory the build ran in, the kernel's conf/, or
name it with --conf DIR) compiles every source of the log that was
compiled with -1 once more, with the same command but -S for -c, in a
scratch directory - the tree gets no file -, and checks the assembler
text it gives: that its 80186/V30 instructions, by kind, are as many as
the build's report of that source counted (mutos_as counts them from the
bytes, so an instruction the text check would not see shows), and that
each is one of the forms step V4's kit ran on the NEC V30
(tests/rules/v30kit.py, which takes PROBED from here): "push" of "*n." or
"#...", "sal"/"sar"/"shr" of AX, BX, CX, DX, SI or DI by 3 to 15, "imul
ax," with "*n." or "#n". Run it in the environment the build ran in
(MUTOS_MODE); MUTOS_RULELOG is left out.

Usage:
    v30report.py LOG [--files]
    v30report.py LOG --forms [--conf DIR]

--files lists every assembly with its numbers. Exit status 1 if the log
has no report at all (the build ran without -1), or with --forms if a
source cannot be compiled again, its count differs from the report's, or
an instruction is no probed form; 2 for a wrong command line. Python 3,
standard library only.
"""
import os
import re
import shlex
import subprocess
import sys
import tempfile

REPORT = re.compile(r'^as: -1: (?:no|(\d+)) 80186/V30 instructions?(?:: (.*))?$')
CC = re.compile(r'^\+ cc\s.*?(\S+\.[cs])\s*$')
CCLINE = re.compile(r'^\+ cc\s+(.*)$')
CD = re.compile(r'^\+ cd\s+(\S+)\s*$')

# the 80186/V30 instructions cc -S -1 writes, by their text: the probed
# forms are "push" of "*n." or "#...", a word shift of a probed register by
# 3 to 15, and "imul ax," with "*n." or "#n"
V30_LINE = re.compile(r'^(?:(?:L\d+|_\w+):)*(push\t[*#][^(\n]*|(?:sal|sar|shr|rol|ror|rcl|rcr)b?'
                      r'\t[^,\n]+,\*\d+\.|imulb?\t[^,\n]+,.*)$', re.M)
PROBED = re.compile(r'^(?:push\t(?:\*-?\d+\.|#[^(\n]+)|(?:sal|sar|shr)\t(?:ax|bx|cx|dx|si|di),'
                    r'\*(?:[3-9]|1[0-5])\.|imul\tax,[*#]-?\d+\.)$')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORDER = ('push', 'shift', 'imul')


def kind_of(insn):
    """The kind mutos_as's report gives an instruction V30_LINE found."""
    return 'push' if insn.startswith('push') else 'imul' if insn.startswith('imul') else 'shift'


def unprobed(text):
    """The 80186/V30 instructions of a .s that are no probed form."""
    return [m.group(1) for m in V30_LINE.finditer(text) if not PROBED.match(m.group(1))]


def forms_of(text):
    """{kind: n} of the 80186/V30 instructions of a .s, by their text."""
    out = {}
    for m in V30_LINE.finditer(text):
        k = kind_of(m.group(1))
        out[k] = out.get(k, 0) + 1
    return out


def kinds_of(report):
    """{kind: n} of a report line's match."""
    kinds = {}
    for part in (report.group(2) or '').split(', '):
        if part:
            w, n = part.split()
            kinds[w] = int(n)
    return kinds


def show(kinds):
    order = [w for w in ORDER if w in kinds] + sorted(w for w in kinds if w not in ORDER)
    return ', '.join('%s %d' % (w, kinds[w]) for w in order) or 'none'


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
                out.append((src, kinds_of(m)))
                src = '?'
    return out, ncc


def compiles(path):
    """[(directory, arguments, {kind: n} or None)] for each "+ cc" line of
    the log with -1: the directory its recipe changed to (relative to
    conf/; every recipe of the kernel's makefile that passes $(CFLAGS)
    begins with "cd"), cc's arguments, the report of the assembly after
    it (None if there is none)."""
    out, cwd, last = [], '.', None
    with open(path, encoding='latin-1') as f:
        for ln in f:
            ln = ln.rstrip('\n')
            m = CD.match(ln)
            if m:
                cwd = m.group(1)
                continue
            m = CCLINE.match(ln)
            if m:
                args = shlex.split(m.group(1))
                last = [cwd, args, None] if '-1' in args else None
                if last:
                    out.append(last)
                continue
            m = REPORT.match(ln)
            if m and last is not None and last[2] is None:
                last[2] = kinds_of(m)
    return out


def cc_path():
    root = os.environ.get('MUTOS_ROOT') or os.path.join(ROOT, 'mutos_root')
    return os.path.join(root, 'bin', 'cc')


def forms(log, conf):
    """The check of --forms; the exit status."""
    builds = compiles(log)
    if not builds:
        print('v30report: no "+ cc" line with -1 in %s - a build without -1, or without set -x'
              % log)
        return 1
    cc = cc_path()
    env = dict(os.environ)
    env.pop('MUTOS_RULELOG', None)
    bad, total, n = [], {}, 0
    with tempfile.TemporaryDirectory(prefix='v30forms') as tmp:
        for i, (cwd, args, report) in enumerate(builds):
            srcs = [k for k, a in enumerate(args) if a.endswith('.c')]
            if len(srcs) != 1:
                bad.append('%s: not one C source on the line' % ' '.join(args))
                continue
            rel = args[srcs[0]]
            src = os.path.normpath(os.path.join(conf, cwd, rel))
            name = os.path.relpath(src, conf)
            if not os.path.isfile(src):
                bad.append('%s: no such source (the recipe\'s directory %s)' % (rel, cwd))
                continue
            if report is None:
                bad.append('%s: no report of as after it in the log' % name)
                continue
            argv = ['-S' if a == '-c' else a for a in args]
            argv[srcs[0]] = src
            d = os.path.join(tmp, str(i))
            os.mkdir(d)
            r = subprocess.run([cc] + argv, cwd=d, env=env, capture_output=True)
            s = os.path.join(d, os.path.basename(src)[:-2] + '.s')
            if r.returncode or not os.path.isfile(s):
                bad.append('%s: cc -S exit status %d: %s' % (name, r.returncode,
                           (r.stdout + r.stderr).decode('latin-1').strip()[:200]))
                continue
            with open(s, encoding='latin-1') as f:
                text = f.read()
            got = forms_of(text)
            if got != report:
                bad.append('%s: the text has %s, the build\'s report %s'
                           % (name, show(got), show(report)))
            for insn in unprobed(text):
                bad.append('%s: no probed form: %s' % (name, insn.replace('\t', ' ')))
            for k, v in got.items():
                total[k] = total.get(k, 0) + v
            n += 1
    for b in bad:
        print('v30report: ' + b)
    print('v30report: %d of %d sources with -1 compiled again with -S: %d 80186/V30 '
          'instructions (%s)%s' % (n, len(builds), sum(total.values()), show(total),
                                   ', each a probed form and as many as the build\'s report'
                                   if not bad else ''))
    return 1 if bad else 0


def main():
    argv = sys.argv[1:]
    conf = '.'
    if '--conf' in argv:
        k = argv.index('--conf')
        if k + 1 >= len(argv):
            print(__doc__)
            return 2
        conf = argv[k + 1]
        del argv[k:k + 2]
    args = [a for a in argv if not a.startswith('--')]
    opts = [a for a in argv if a.startswith('--')]
    if len(args) != 1 or [o for o in opts if o not in ('--files', '--forms')]:
        print(__doc__)
        return 2
    if '--forms' in opts:
        return forms(args[0], os.path.abspath(conf))
    reports, ncc = read(args[0])
    if not reports:
        print('v30report: no "as: -1:" line in %s - a build without -1' % args[0])
        return 1
    total = {}
    for _src, kinds in reports:
        for w, n in kinds.items():
            total[w] = total.get(w, 0) + n
    if '--files' in opts:
        order = [w for w in ORDER if w in total] + sorted(w for w in total if w not in ORDER)
        for src, kinds in reports:
            print('%-16s %s' % (src, ', '.join('%s %d' % (w, kinds[w]) for w in order
                                                 if w in kinds) or 'none'))
    with_any = sum(1 for _s, k in reports if k)
    print('v30report: %d assemblies with -1%s, %d of them with 80186/V30 instructions'
          % (len(reports), ' (of %d cc commands in the log)' % ncc if ncc else '', with_any))
    print('v30report: %d 80186/V30 instructions: %s' % (sum(total.values()), show(total)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
