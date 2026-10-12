#!/usr/bin/env python3
"""check_real.py - the real MUTOS 1700 cpp, c0, c1 and as, emulated,
against the project's goldens.

The goldens were made by these very binaries on real hardware; running
them here under mutosemu.py must give the goldens back, byte for byte.
That makes them an oracle that needs no hardware: for any .c (cpp), .i
(c0), .1/.2 pair (c1) or .s (as) - a golden, a fuzz program, a new probe
- the real toolchain's answer is one emulator run away.

Usage:
    check_real.py goldens [-j N]
        cpp on every tests/mutos_cc .c with an .i.golden and on
        tests/mutos_cpp/c (with run_goldens.sh's defines); c0 on every
        .i.golden against its .1/.2 goldens; c1 on every .1/.2 golden
        pair against its .s.golden - its messages against c1_errors.txt
        and the fltprobe round logs, its exit status against "nonzero
        iff it printed messages"; as on every tests/mutos_as .s with an
        .o.golden (AS_KNOWN lists the goldens the real as is not expected
        to reproduce, and why). Exit status 1 on any other difference.
    check_real.py coverage [-j N] [--unused]
        which rows of c1's code tables (see c1tables.py) match() returns
        over the c1 runs above - how much of the tables the goldens have
        seen. --unused lists the rows never matched.

Requires the Python module "unicorn" (pip install unicorn).
"""
import collections
import concurrent.futures
import functools
import glob
import io
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402
import c1tables       # noqa: E402

TESTS = os.path.normpath(os.path.join(HERE, '..'))
CC = os.path.join(TESTS, 'mutos_cc')
CPP_KERNEL = os.path.join(TESTS, 'mutos_cpp', 'c')
CPP_KERNEL_DEFINES = ['-DM7100', '-DASK', '-DIFSS', '-DV24', '-DV30IDE']
AS_DIRS = ['kernel_nonopt', 'kernel_opt', 'libc_recon', 'float_coverage']
# Goldens the real as does not reproduce, by design of the golden:
AS_KNOWN = {
    'kernel_opt/mch_insw_outsw':
        'mch.s with two ".byte /6d", ".byte /6f" replaced by "insw", '
        '"outsw"; its .o.golden is the real mch.o. The real as does not '
        'know those 80186 mnemonics: it assembles each as an undefined '
        'external word (2 more text bytes, 2 more symbols).',
}
MATCH_RETURN = 0x0492   # c1's match(): "mov ax,dx" - DX is the optab row or 0
MAX_INSNS = 2_000_000_000


def rel(path, base=TESTS):
    return os.path.relpath(path, base)


def golden_bases(top, suffix, need=()):
    out = []
    for g in sorted(glob.glob(os.path.join(top, '**', '*' + suffix), recursive=True)):
        b = g[:-len(suffix)]
        if all(os.path.exists(b + n) for n in need):
            out.append(b)
    return out


def same(path_a, path_b):
    with open(path_a, 'rb') as a, open(path_b, 'rb') as b:
        return a.read() == b.read()


# -- one run each ---------------------------------------------------------
def run_cpp(job):
    base, defines = job
    st, out, err = mutosemu.run_tool(
        'cpp', ['-P'] + defines + [os.path.basename(base) + '.c'],
        cwd=os.path.dirname(base), max_insns=MAX_INSNS)
    with open(base + '.i.golden', 'rb') as f:
        ok = st == 0 and out == f.read()
    return base, ok, 'exit %d %s' % (st, err.decode('latin1').strip())


def run_c0(base):
    with tempfile.TemporaryDirectory() as tmp:
        t1, t2 = os.path.join(tmp, 't1'), os.path.join(tmp, 't2')
        st, out, err = mutosemu.run_tool('c0', [base + '.i.golden', t1, t2],
                                         cwd=tmp, max_insns=MAX_INSNS)
        ok = (st == 0 and same(t1, base + '.1.golden')
              and same(t2, base + '.2.golden'))
    return base, ok, 'exit %d %s' % (st, err.decode('latin1').strip())


def run_c1(base, coverage=False):
    rows = set()
    with tempfile.TemporaryDirectory() as tmp:
        out_s = os.path.join(tmp, 'out.s')
        err = io.BytesIO()
        emu = mutosemu.Emu(mutosemu.load_image('c1'),
                           ['c1', base + '.1.golden', base + '.2.golden', out_s],
                           cwd=tmp, stdin=io.BytesIO(b''), stdout=io.BytesIO(),
                           stderr=err)
        if coverage:
            emu.hook_text(MATCH_RETURN,
                          lambda e: rows.add(e.reg('dx')) if e.reg('dx') else None)
        st = emu.run(MAX_INSNS)
        ok = same(out_s, base + '.s.golden')
    return base, st, ok, err.getvalue().decode('latin1').splitlines(), rows


def run_as(base):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.basename(base) + '.s'
        shutil.copy(base + '.s', os.path.join(tmp, src))
        st, out, err = mutosemu.run_tool('as', ['-o', 'x.o', src], cwd=tmp,
                                         max_insns=MAX_INSNS)
        obj = os.path.join(tmp, 'x.o')
        ok = st == 0 and os.path.exists(obj) and same(obj, base + '.o.golden')
    return base, ok, 'exit %d %s' % (st, (out + err).decode('latin1').strip())


# -- the expected c1 messages -------------------------------------------
def expected_messages():
    """name -> (messages, source) from c1_errors.txt and the round logs."""
    exp = {}
    with open(os.path.join(CC, 'c1_errors.txt')) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            name, msg = line.rstrip('\n').split(' ', 1)
            exp.setdefault(name, ([], 'c1_errors.txt'))[0].append(msg)
    for log in sorted(glob.glob(os.path.join(CC, 'fltprobe', 'round*.log'))):
        cur = None
        msgs = collections.OrderedDict()
        with open(log, errors='replace') as f:
            for line in f:
                m = re.match(r'cc -S (\S+)\.c$', line.strip())
                if m:
                    cur = 'fltprobe/' + m.group(1)
                    msgs[cur] = []
                elif cur and re.match(r'\d+: ', line):
                    msgs[cur].append(line.rstrip('\n'))
                elif line.startswith('/lib/cpp'):
                    cur = None
        for name, m in msgs.items():
            if not m:
                continue
            if name not in exp:
                exp[name] = (m, os.path.basename(log))
            elif exp[name][0] != m:
                exp[name] = (exp[name][0], 'c1_errors.txt (which %s contradicts)'
                             % os.path.basename(log))
    return exp


# -- commands ---------------------------------------------------------------
def report(title, results):
    bad = [r for r in results if not r[1]]
    print('%s: %d/%d' % (title, len(results) - len(bad), len(results)))
    for base, ok, why in bad:
        print('  DIFF %s (%s)' % (rel(base), why))
    return len(bad)


def cmd_goldens(jobs):
    cpp_jobs = ([(b, []) for b in golden_bases(CC, '.i.golden')]
                + [(b, CPP_KERNEL_DEFINES) for b in golden_bases(CPP_KERNEL, '.i.golden')])
    c0_jobs = golden_bases(CC, '.i.golden', ('.1.golden', '.2.golden'))
    c1_jobs = golden_bases(CC, '.1.golden', ('.2.golden', '.s.golden'))
    as_jobs = [b for d in AS_DIRS
               for b in golden_bases(os.path.join(TESTS, 'mutos_as', d), '.o.golden', ('.s',))]
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as ex:
        cpp = list(ex.map(run_cpp, cpp_jobs))
        c0 = list(ex.map(run_c0, c0_jobs))
        c1 = list(ex.map(run_c1, c1_jobs))
        asm = list(ex.map(run_as, as_jobs))
    bad = report('real cpp (emulated), .c against .i.golden', cpp)
    bad += report('real c0 (emulated), .i.golden against .1/.2 goldens', c0)

    exp = expected_messages()
    c1_res = []
    with_msgs = []
    for base, st, ok, msgs, rows in c1:
        name = rel(base, CC)
        want, source = exp.get(name, ([], None))
        problems = []
        if not ok:
            problems.append('.s differs from the golden')
        if msgs != want:
            problems.append('%d messages, %d expected (%s)'
                            % (len(msgs), len(want), source or 'none listed'))
        if st != (1 if msgs else 0):
            problems.append('exit status %d' % st)
        c1_res.append((base, not problems, '; '.join(problems)))
        if msgs and not problems:
            with_msgs.append('%s %d (%s)' % (os.path.basename(base), len(msgs), source))
    bad += report('real c1 (emulated), .1/.2 goldens against .s.golden', c1_res)
    if with_msgs:
        print('  with the real messages and exit status 1: ' + ', '.join(with_msgs))

    as_res, known = [], []
    for base, ok, why in asm:
        key = rel(base, os.path.join(TESTS, 'mutos_as'))
        if key in AS_KNOWN:
            known.append((key, ok))
        else:
            as_res.append((base, ok, why))
    bad += report('real as (emulated), .s against .o.golden', as_res)
    for key, ok in known:
        print('  %s: %s - %s' % (key, 'reproduced (unexpectedly)' if ok
                                 else 'differs, as expected', AS_KNOWN[key]))
        if ok:
            bad += 1
    return 1 if bad else 0


def cmd_coverage(jobs, unused):
    c1_jobs = golden_bases(CC, '.1.golden', ('.2.golden', '.s.golden'))
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as ex:
        res = list(ex.map(functools.partial(run_c1, coverage=True), c1_jobs))
    used = set()
    for base, st, ok, msgs, rows in res:
        used |= rows
    rows = c1tables.all_rows()
    per = collections.Counter(r[1] for r in rows)
    seen = collections.Counter(r[1] for r in rows if r[0] in used)
    print('c1 code-table rows matched over %d golden runs: %d of %d'
          % (len(res), sum(seen.values()), len(rows)))
    print('  ' + ', '.join('%s %d of %d' % (n, seen[n], per[n])
                           for n, _ in c1tables.TABLES))
    if unused:
        for off, name, op, d1, t1, d2, t2 in rows:
            if off not in used:
                print('unused row %04x %-6s %3d %-14s %%%s,%s'
                      % (off, name, op, c1tables.opname(op),
                         c1tables.cls(d1, t1), c1tables.cls(d2, t2)))
    return 0


def main(argv):
    jobs = os.cpu_count() or 1
    if '-j' in argv:
        i = argv.index('-j')
        jobs = int(argv[i + 1])
        del argv[i:i + 2]
    if argv[:1] == ['goldens']:
        return cmd_goldens(jobs)
    if argv[:1] == ['coverage']:
        return cmd_coverage(jobs, '--unused' in argv)
    sys.exit(__doc__.split('Usage:')[1].split('Requires')[0])


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
