#!/usr/bin/env python3
"""check_accounting.py - every difference the corrected mode or -1 makes
must belong to a rule that says so (docs/RULES.md, "The accounting";
docs/TOOLCHAIN_PLAN.md, section 7.2).

mutos_cc compiles each source once as the original does (no switch, no
MUTOS_MODE) and once with each of -H, -F, -1 and -F -1, the rule log on
(MUTOS_RULELOG, src/h/mutos_mode.h), in four stages - "cc -S" (c1's .s),
"cc -O -S" (c2's), "cc -O -c" (the object, through c2 and as) and, for a
program, "cc -o" (linked by ld) - and "cc -c" with --full. Against the run
without a switch:

  * a .s of "cc -S" may differ only in lines that lie in the out= range of
    an "applied" entry of c1 in that run's log; its messages and exit
    status only when the log has an applied entry of c0 or c1;
  * "cc -O -S", the objects and the linked program may differ only where
    the stage before them differed, or where an applied entry of c2, as or
    ld (the stage's own tool) says so - c2's by out= ranges as c1's;
  * -H changes nothing and logs nothing "applied", and the "met" entries
    of every run are those of the run without a switch (diagnostics do not
    depend on the mode until a rule is active);
  * with -1 every assembly reports exactly one line ("as: -1: ...") on
    the standard error, which names 80186/V30 instructions only if a V30
    rule was applied; "cc -S -1" reports nothing.

The sources: the construct corpus (tests/mutos_cc/00_smoke .. 10_integ),
the floating-point probes (fltprobe/), the real kernel sources of the
tree (tests/mutos_cc/11_kernel/, tests/mutos_cpp/c/ with its h/) and
programs of tests/mutos_cc/fuzz/fuzz_c.py's generator, half of them with
longs (--fuzz N, --seed). Then links that meet ld's rules (ld_cases()),
which the corpus never does: without a switch each meets its rule; with
-F or MUTOS_MODE=corrected the link may end otherwise (exit status,
messages, the output's mode) only with an "applied" entry of ld, and its
output's bytes are the same.

--report prints, from the runs without a switch, which rules the code met
(the diagnostic hooks of step F0): per rule, how often and where.

Usage:
    check_accounting.py [--cc PATH] [--fuzz N] [--seed S] [-j JOBS]
                        [--full] [--report] [--keep DIR]

Exit status 1 on an unaccounted difference. Python 3, standard library.
"""
import argparse
import concurrent.futures
import difflib
import glob
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tests', 'mutos_cc', 'fuzz'))
import fuzz_c  # noqa: E402

CC = os.path.join(REPO, 'src', 'mutos_cc', 'mutos_cc')
LD = os.path.join(REPO, 'src', 'mutos_ld', 'mutos_ld')
CONFIGS = [('none', []), ('H', ['-H']), ('F', ['-F']), ('1', ['-1']),
           ('F1', ['-F', '-1'])]
REPORT_RE = re.compile(r'^as: -1: ')
TOOLS = ('c0', 'c1', 'c2', 'as', 'ld')
STAGE = {'S': 'cc -S', 'OS': 'cc -O -S', 'C': 'cc -c', 'OC': 'cc -O -c', 'L': 'cc -o'}
PREV = {'S': None, 'OS': 'S', 'C': 'S', 'OC': 'OS', 'L': 'OC'}
TOOL = {'S': 'c1', 'OS': 'c2', 'C': 'as', 'OC': 'as', 'L': 'ld'}
NO186 = 'as: -1: no 80186/V30 instructions'


class Unit:
    """One source in its own scratch directory: name (for messages), files
    {relative path: bytes}, the source's relative path, whether it is a
    program (a link stage)."""

    def __init__(self, name, files, src, program):
        self.name, self.files, self.src, self.program = name, files, src, program


def corpus_units():
    units = []
    base = os.path.join(REPO, 'tests', 'mutos_cc')
    for d in sorted(glob.glob(os.path.join(base, '[0-9][0-9]_*')) +
                    [os.path.join(base, 'fltprobe')]):
        cat = os.path.basename(d)
        hdrs = {os.path.basename(h): open(h, 'rb').read()
                for h in glob.glob(os.path.join(d, '*.h'))}
        for c in sorted(glob.glob(os.path.join(d, '*.c'))):
            files = dict(hdrs)
            files[os.path.basename(c)] = open(c, 'rb').read()
            units.append(Unit('%s/%s' % (cat, os.path.basename(c)), files,
                              os.path.basename(c), cat != '11_kernel'))
    cpp = os.path.join(REPO, 'tests', 'mutos_cpp')
    hdrs = {}
    for h in glob.glob(os.path.join(cpp, 'h', '**', '*.h'), recursive=True):
        hdrs[os.path.relpath(h, cpp)] = open(h, 'rb').read()
    for c in sorted(glob.glob(os.path.join(cpp, 'c', '*.c'))):
        files = dict(hdrs)
        rel = os.path.relpath(c, cpp)
        files[rel] = open(c, 'rb').read()
        units.append(Unit('mutos_cpp/' + rel, files, rel, False))
    return units


def fuzz_units(n, seed):
    units = []
    for i in range(n):
        rnd = random.Random('accounting %d %d' % (seed, i))
        src = fuzz_c.generate(rnd, rnd.random() < 0.8, rnd.random() < 0.3,
                              rnd.random() < 0.5)[0]
        units.append(Unit('fuzz/s%d.%d.c' % (seed, i), {'p.c': src.encode()},
                          'p.c', True))
    return units


def readlog(path):
    """[(tool, rule, what, {key: value})] of a rule log."""
    out = []
    try:
        with open(path, encoding='latin-1') as f:
            for line in f:
                parts = line.split()
                if len(parts) < 3:
                    continue
                keys = {}
                for p in parts[3:]:
                    if '=' in p:
                        k, v = p.split('=', 1)
                        keys[k] = v
                out.append((parts[0], parts[1], parts[2], keys))
    except OSError:
        pass
    return out


def read(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def run_stage(cc, tmp, unit, stage, opts, env):
    """One cc run: {'st', 'msgs', 'out' (bytes or None), 'log'}."""
    src = unit.src
    d = os.path.join(tmp, os.path.dirname(src))
    base = os.path.basename(src)[:-2]
    args = {'S': ['-S'], 'OS': ['-O', '-S'], 'C': ['-c'], 'OC': ['-O', '-c'],
            'L': ['-o', base]}[stage]
    outname = {'S': base + '.s', 'OS': base + '.s', 'C': base + '.o',
               'OC': base + '.o', 'L': base}[stage]
    log = os.path.join(tmp, 'rulelog')
    if os.path.exists(log):
        os.unlink(log)
    e = dict(env)
    e['MUTOS_RULELOG'] = log
    r = subprocess.run([cc] + opts + args + [os.path.basename(src)], cwd=d,
                       capture_output=True, env=e)
    msgs = [ln for ln in (r.stdout + r.stderr).decode('latin-1').split('\n') if ln]
    out = read(os.path.join(d, outname))
    for n in (outname, base + '.o'):
        p = os.path.join(d, n)
        if os.path.exists(p):
            os.unlink(p)
    return {'st': r.returncode, 'msgs': msgs, 'out': out, 'log': readlog(log)}


def hunks(a, b):
    """The byte ranges of b that differ from a, line by line: [(start, end)]
    (start == end for lines of a missing in b)."""
    al = a.splitlines(True)
    bl = b.splitlines(True)
    offs = [0]
    for ln in bl:
        offs.append(offs[-1] + len(ln))
    out = []
    sm = difflib.SequenceMatcher(None, al, bl, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag != 'equal':
            out.append((offs[j1], offs[j2]))
    return out


def ranges(log, tool):
    out = []
    for t, rule, what, keys in log:
        if t == tool and what == 'applied' and 'out' in keys:
            m = re.fullmatch(r'(-?\d+)-(-?\d+)', keys['out'])
            if m:
                out.append((int(m.group(1)), int(m.group(2))))
    return out


def covered(h, rs):
    s, e = h
    return any(a <= s and e <= b for a, b in rs)


def applied(log, tools):
    return [x for x in log if x[2] == 'applied' and x[0] in tools]


def one(job):
    cc, unit, full, keep, base_only = job
    problems, met = [], []
    stages = ['S', 'OS', 'OC'] + (['C'] if full else []) + \
        (['L'] if unit.program else [])
    configs = CONFIGS
    if base_only:
        stages = ['S'] + (['L'] if unit.program else [])
        configs = CONFIGS[:1]
    env = {k: v for k, v in os.environ.items()
           if k not in ('MUTOS_MODE', 'MUTOS_RULELOG')}
    with tempfile.TemporaryDirectory(prefix='acct') as tmp:
        for rel, data in unit.files.items():
            p = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(data)
        res = {}
        for cname, opts in configs:
            for st in stages:
                res[cname, st] = run_stage(cc, tmp, unit, st, opts, env)
        base = {st: res['none', st] for st in stages}
        for x in base['S']['log']:
            if x[2] == 'met':
                met.append((x[1], unit.name, x[3].get('line', '?'), x[3].get('op')))
        for x in base.get('L', {'log': []})['log']:
            if x[2] == 'met' and x[0] == 'ld':
                met.append((x[1], unit.name, '-', None))
        for st in stages:
            if applied(base[st]['log'], TOOLS):
                problems.append('%s %s: an applied rule without a switch'
                                % (STAGE[st], unit.name))
        for cname, opts in configs[1:]:
            v1 = '-1' in opts
            state = {}	# stage -> 'same', 'changed' (accounted for) or 'bad'
            for st in stages:
                b, v = base[st], res[cname, st]
                tag = '%s %s %s' % (' '.join(opts), STAGE[st], unit.name)
                bmet = sorted((x[0], x[1], tuple(sorted(x[3].items())))
                              for x in b['log'] if x[2] == 'met')
                vmet = sorted((x[0], x[1], tuple(sorted(x[3].items())))
                              for x in v['log'] if x[2] == 'met')
                if cname == 'H' and v['log'] != b['log']:
                    problems.append('%s: -H logs otherwise' % tag)
                elif not applied(v['log'], TOOLS) and bmet != vmet:
                    problems.append('%s: the diagnostics differ' % tag)
                msgs = v['msgs']
                if v1:
                    reports = [m for m in msgs if REPORT_RE.match(m)]
                    msgs = [m for m in msgs if not REPORT_RE.match(m)]
                    want = 0 if st in ('S', 'OS') else (1 if v['out'] is not None else None)
                    if want is not None and len(reports) != want:
                        problems.append('%s: %d report line(s) of -1, not %d'
                                        % (tag, len(reports), want))
                    if any(r != NO186 for r in reports) and \
                            not [x for x in v['log'] if x[2] == 'applied'
                                 and x[1].startswith('V30-')]:
                        problems.append('%s: -1 reports %r without a V30 rule'
                                        % (tag, reports))
                if (b['st'], b['msgs'], b['out']) == (v['st'], msgs, v['out']):
                    state[st] = 'same'
                    continue
                if cname == 'H':
                    problems.append('%s: -H changes the output' % tag)
                    state[st] = 'bad'
                    continue
                prev = PREV[st]
                if prev is not None and state.get(prev) in ('changed', 'bad'):
                    state[st] = state[prev]	# a consequence of the stage before
                    continue
                tools = ('c0', 'c1') if st == 'S' else (TOOL[st],)
                if not applied(v['log'], tools):
                    problems.append('%s: the %s differ%s with no applied rule of %s'
                                    % (tag, 'output' if b['out'] != v['out'] else
                                       'messages or exit status',
                                       's' if b['out'] != v['out'] else '',
                                       '/'.join(tools)))
                    state[st] = 'bad'
                    continue
                if TOOL[st] in ('c1', 'c2') and b['out'] is not None \
                        and v['out'] is not None:
                    rs = ranges(v['log'], TOOL[st])
                    bad = [h for h in hunks(b['out'], v['out']) if not covered(h, rs)]
                    if bad:
                        problems.append('%s: bytes %d-%d differ outside the applied '
                                        'rules\' ranges' % (tag, bad[0][0], bad[0][1]))
                        state[st] = 'bad'
                        continue
                state[st] = 'changed'
        if problems and keep:
            d = os.path.join(keep, unit.name.replace('/', '_'))
            shutil.copytree(tmp, d, dirs_exist_ok=True)
    return unit.name, problems, met


# ---- ld: the cases of its rules -------------------------------------------
#
# The corpus links never reach LD-06, LD-08 or LD-09; these cases do (those
# of tests/realbin/ldcmp.py of those names, built here without the emulator).
# ld's rules change what a link ends with - its exit status, its messages,
# whether the output is made executable - never the bytes it writes.

LIBC_SYMDEF_TIME = 602282477      # libc.a's __.SYMDEF time (ldcmp.py)


def _pdp(v):
    v &= 0xffffffff
    return struct.pack('<HH', v >> 16, v & 0xffff)


def mkobj(text=b'', data=b'', syms=(), flag=0):
    """An 0407 object; flag 1: no relocation bits."""
    st = b''.join(n.encode()[:8].ljust(8, b'\0') + bytes([ty, 0]) +
                  struct.pack('<H', val) for n, ty, val in syms)
    rel = b'' if flag else b'\0' * (len(text) + len(data))
    return struct.pack('<8H', 0o407, len(text), len(data), 0, len(st), 0, 0,
                       flag) + text + data + rel + st


def mkar(members, mtime=566554203):
    out = bytearray(struct.pack('<H', 0o177545))
    for name, data in members:
        out += name.encode()[:14].ljust(14, b'\0') + _pdp(mtime) + \
            bytes([0, 1]) + struct.pack('<H', 0o100664) + _pdp(len(data)) + \
            data + (b'\0' if len(data) & 1 else b'')
    return bytes(out)


def mkranlib(members):
    """__.SYMDEF (each external a member defines, with its header's offset)
    and the members."""
    tab_size = 0
    for _ in range(2):
        entries = []
        pos = 2 + 26 + tab_size
        for name, data in members:
            t, d, s = struct.unpack_from('<3H', data, 2)[0], \
                struct.unpack_from('<H', data, 4)[0], struct.unpack_from('<H', data, 8)[0]
            off = 16 + 2 * (t + d)
            for i in range(off, off + s, 12):
                if data[i + 8] & 0x20 and data[i + 8] & 0x1f in (1, 2, 3, 4):
                    entries.append(data[i:i + 8] + _pdp(pos))
            pos += 26 + len(data) + (len(data) & 1)
        tab_size = 12 * len(entries)
    return mkar([('__.SYMDEF', b''.join(entries))], LIBC_SYMDEF_TIME) + mkar(members)[2:]


def ld_run(ld, files, libt, argv, mode=None):
    """ld <argv> in a scratch directory with the files {name: bytes}, the
    MUTOS root's lib/libt.a libt (out of date) if not None, MUTOS_MODE
    mode: ((exit status, stdout, stderr, a.out's bytes, a.out's mode),
    the rule log)."""
    with tempfile.TemporaryDirectory(prefix='acctld') as tmp:
        root = os.path.join(tmp, 'root')
        os.makedirs(os.path.join(root, 'lib'))
        os.makedirs(os.path.join(root, 'tmp'))
        if libt is not None:
            p = os.path.join(root, 'lib', 'libt.a')
            with open(p, 'wb') as f:
                f.write(libt)
            os.utime(p, (LIBC_SYMDEF_TIME + 9, LIBC_SYMDEF_TIME + 9))
        for n, data in files.items():
            with open(os.path.join(tmp, n), 'wb') as f:
                f.write(data)
        env = {k: v for k, v in os.environ.items() if k != 'MUTOS_MODE'}
        env['MUTOS_ROOT'] = root
        env['MUTOS_RULELOG'] = os.path.join(tmp, 'log')
        if mode:
            env['MUTOS_MODE'] = mode
        r = subprocess.run([ld] + argv, cwd=tmp, capture_output=True, env=env)
        out = os.path.join(tmp, 'a.out')
        st = os.stat(out).st_mode & 0o777 if os.path.exists(out) else None
        return ((r.returncode, r.stdout, r.stderr, read(out), st),
                readlog(os.path.join(tmp, 'log')))


def ld_cases(ld):
    """(problems, met) of the cases, in each mode: without a switch the
    case meets its rule; -H is the run without a switch, its log too; -F
    (first or last), and MUTOS_MODE=corrected, agree with each other and
    differ from the run without a switch only with an applied entry of ld
    in their log, and then not in the bytes of the output."""
    w = lambda *v: struct.pack('<%dH' % len(v), *v)
    main_o = mkobj(text=w(0xe8, 0, 0x90c3), syms=[('_main', 0x22, 0), ('_foo', 0x20, 0)])
    foo_o = mkobj(text=w(0x90c3), data=w(1, 2), syms=[('_foo', 0x22, 0)])
    nr_o = mkobj(text=w(0x90c3), syms=[('_nr', 0x22, 0)], flag=1)
    cases = [
        ('LD-06', ['main.o', 'foo.o', 'p.a', '-lt'],
         {'main.o': main_o, 'foo.o': foo_o, 'p.a': mkar([('nr.o', nr_o)])},
         mkranlib([('foo.o', foo_o)])),
        ('LD-08', ['big.o'], {'big.o': mkobj(text=b'\x90' * 65530,
                                             syms=[('_big', 0x22, 0)])}, None),
        ('LD-09', ['-i', 'big.o'], {'big.o': mkobj(text=b'\x90' * 65528,
                                                   syms=[('_big', 0x22, 0)])}, None),
    ]
    problems, met = [], []
    for rule, args, files, libt in cases:
        results = []
        for cname, opts, mode in (('none', args, None), ('-H', ['-H'] + args, None),
                                  ('-F', ['-F'] + args, None),
                                  ('-F last', args[:1] + ['-F'] + args[1:], None),
                                  ('MUTOS_MODE', args, 'corrected')):
            results.append((cname,) + ld_run(ld, files, libt, opts, mode))
        tag = 'ld %s' % ' '.join(args)
        base = results[0]
        if not [x for x in base[2] if x[1] == rule and x[2] == 'met']:
            problems.append('%s: the case does not meet %s' % (tag, rule))
        else:
            met.append((rule, 'ld case', '-', None))
        if applied(base[2], ('ld',)):
            problems.append('%s: an applied rule without a switch' % tag)
        if results[1][1:] != base[1:]:
            problems.append('%s -H: the link or the log differs' % tag)
        corr = results[2]
        for cname, res, log in results[3:]:
            if (res, log) != corr[1:]:
                problems.append('%s %s: not what -F does' % (tag, cname))
        res, log = corr[1:]
        if res != base[1] and not applied(log, ('ld',)):
            problems.append('%s -F: the link differs with no applied rule' % tag)
        if res[3] != base[1][3]:
            problems.append('%s -F: the output\'s bytes differ' % tag)
    return problems, met


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--cc', default=CC)
    ap.add_argument('--fuzz', type=int, default=40, help='fuzz_c.py programs (40)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('-j', type=int, default=os.cpu_count() or 2)
    ap.add_argument('--full', action='store_true', help='also "cc -c"')
    ap.add_argument('--report', action='store_true',
                    help='which rules the code met, and where')
    ap.add_argument('--keep', help='copy the scratch directories of problems here')
    ap.add_argument('--only', help='only units whose name contains this')
    ap.add_argument('--ld', default=LD)
    ap.add_argument('--base-only', action='store_true',
                    help='only the runs without a switch, "cc -S" and "cc -o" '
                    '(with --report: the diagnostics of many programs, fast)')
    a = ap.parse_args()
    units = corpus_units() + fuzz_units(a.fuzz, a.seed)
    if a.only:
        units = [u for u in units if a.only in u.name]
    jobs = [(a.cc, u, a.full, a.keep, a.base_only) for u in units]
    problems, met = [], []
    if not a.only:
        problems, met = ld_cases(a.ld)
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for name, p, m in ex.map(one, jobs):
            problems += p
            met += m
    nstage = sum((1 if a.base_only else 4 if a.full else 3) + (1 if u.program else 0)
                 for u in units)
    for p in problems[:40]:
        print('FAIL  ' + p)
    if a.report or problems:
        rules = {}
        for rule, name, line, op in met:
            rules.setdefault(rule, []).append((name, line, op))
        print('rules met without a switch (the original\'s path taken):')
        if not rules:
            print('  none')
        for rule in sorted(rules):
            where = rules[rule]
            files = sorted({n for n, _, _ in where})
            ops = {}
            for _, _, op in where:
                if op is not None:
                    ops[op] = ops.get(op, 0) + 1
            print('  %-6s %4d time(s) in %3d source(s)%s: %s' % (
                rule, len(where), len(files),
                ' (%s)' % ', '.join('op %s %d' % (o, ops[o]) for o in sorted(ops))
                if ops else '',
                ', '.join('%s:%s' % (n, l) for n, l, _ in where[:12]) +
                (' ...' if len(where) > 12 else '')))
    print('check_accounting: %d sources (%d of fuzz_c.py, seed %d), %d stages x %d '
          'configurations, and ld\'s cases: %s'
          % (len(units), a.fuzz, a.seed, nstage, 1 if a.base_only else len(CONFIGS),
                                  '%d unaccounted difference(s)' % len(problems)
                                  if problems else 'every difference accounted for'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
