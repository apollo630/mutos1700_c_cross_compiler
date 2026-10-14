#!/usr/bin/env python3
"""c1trees.py - the real c1's expression trees against the c1 port's.

The port of c1 (src/mutos_cc/c1port/, docs/C1_PORT_PLAN.md) is checked
against the original binary at four depths. Depths 1 and 2 are the
expression trees getree() reads (before optim()) and the same trees after
optim() - what the original's options b and a print. Until the port
generates all of the code, those traces are mixed with code lines the port
cannot match yet, so this script takes the trees out of the emulated
memory instead: hooks on getree()'s call of optim() (text 335f: the tree
read, at [di]; text 3367: optim()'s result, in AX) walk each tree and
write it as the original's tree printer (tprint, text 3bdc) would. The
port writes its own trees in the same form into the file the environment
variable C1PORT_TREES names (its treedump()). Each tree is headed
"b <line>" (before optim) or "a <line>" (after).

Usage:
    c1trees.py [-j N] [--port PATH] [--depth 1|2|3|4] [--show N] [BASE...]
        BASE: .1/.2 pairs without the suffix (default: every golden
        pair in tests/mutos_cc, as <base>.1.golden/<base>.2.golden).
        --depth 1 compares the "b" trees, 2 the "a" trees as well;
        --depth 4 the whole output - the .s file, the messages and the
        exit status - with the emulated original's; --depth 3 the same
        with all of the original's traces on (options r, c, d, a, b).
        Prints one line per file that differs (and its first differing
        tree with --show), then a count. Exit status 1 on a difference.
    c1trees.py --dump BASE1 BASE2
        the original's trees for one .1/.2 pair, on stdout.

One number is compared as "#": a STRASG's degree ("= structure").
getree() never sets it, so the original prints whatever word its block
held before - getblk() reuses memory without clearing it
(docs/C1_PORT_PLAN.md 5.3), and the port clears it. (At depth 3 that
number is in the original's trace: 06_struct/04_stassign differs there,
and only there.)

A file the port refuses (exit status 2: a c1trap(), state it does not
model) is reported as refused, with the number of trees or lines that
were identical before it.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import glob
import io
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402
import c1tables       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
CC = os.path.join(ROOT, 'tests', 'mutos_cc')
PORT = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_c1')
HOOK_BEFORE, HOOK_AFTER = 0x335f, 0x3367
LINE = 0x4312
MAX_INSNS = 2_000_000_000

TYNAMES = ['int', 'char', 'float', 'double', 'struct', 'rstruct', 'long',
           'unsign']
XTNAMES = ['', 'ptr', 'func', 'array']
NAME, CON, LEAF, BINARY = 20, 21, 0o400, 0o1


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    return v - 0x100 if v & 0x80 else v


class Tprint:
    """tprint(tree, 0) of the original, over the emulated memory."""

    def __init__(self, emu):
        self.e = emu
        self.opdope = [c1tables.word(c1tables.OPDOPE + 2 * i) for i in range(124)]
        self.opntab = [c1tables.plain(c1tables.word(c1tables.OPNTAB + 2 * i))
                       for i in range(c1tables.NOPNAMES)]

    def __call__(self, p, d=0, out=None):
        if out is None:
            out = []
        if p == 0:
            return out
        e = self.e
        op, ty = s16(e.word(p)), e.word(p + 2)
        name = self.opntab[op] if 0 <= op < len(self.opntab) else '?'
        tn, xn = TYNAMES[ty & 7], XTNAMES[(ty & 0o30) >> 3]
        if op == NAME:
            line = '%s %s %s %o %d %d %d' % (
                name, tn, xn, s8(e.byte(p + 4)) & 0xffff, s8(e.byte(p + 5)),
                s16(e.word(p + 6)), s16(e.word(p + 8)))
        elif op == CON:
            line = '%s %s %s %u' % (name, tn, xn, e.word(p + 4))
        else:
            line = '%s %s %s %d' % (name, tn, xn, s16(e.word(p + 4)))
        out.append(line + '\n')
        if op < 0 or op > 121:
            out.append('unknown op %o\n' % (op & 0xffff))
            return out
        if self.opdope[op] & LEAF:
            return out
        out.append(' ' * d + '< ')
        self(e.word(p + 6), d + 1, out)
        if not self.opdope[op] & BINARY:
            return out
        out.append(' ' * d + '> ')
        self(e.word(p + 8), d + 1, out)
        return out


def original_trees(t1, t2, errout=None):
    """The original's trees for the pair t1/t2: (text, exit status); its
    messages are appended to the list errout if one is given."""
    with tempfile.TemporaryDirectory() as tmp:
        err = io.BytesIO()
        emu = mutosemu.Emu(mutosemu.load_image('c1'),
                           ['c1', os.path.abspath(t1), os.path.abspath(t2),
                            os.path.join(tmp, 'out.s')],
                           cwd=tmp, stdin=io.BytesIO(b''), stdout=io.BytesIO(),
                           stderr=err)
        tp = Tprint(emu)
        out = []

        def before(e):
            out.append('@@ b %d\n' % s16(e.word(LINE)))
            tp(e.word(e.reg('di')), 0, out)

        def after(e):
            out.append('@@ a %d\n' % s16(e.word(LINE)))
            tp(e.reg('ax'), 0, out)
        emu.hook_text(HOOK_BEFORE, before)
        emu.hook_text(HOOK_AFTER, after)
        st = emu.run(MAX_INSNS)
    if errout is not None:
        errout.append(err.getvalue().decode('latin1'))
    return ''.join(out), st


def port_trees(t1, t2, port):
    with tempfile.TemporaryDirectory() as tmp:
        dump = os.path.join(tmp, 'trees')
        env = dict(os.environ, C1PORT_TREES=dump)
        p = subprocess.run([port, t1, t2, os.path.join(tmp, 'out.s')],
                           env=env, capture_output=True, timeout=120)
        text = open(dump, encoding='latin1').read() if os.path.exists(dump) else ''
    return text, p.returncode, p.stderr.decode('latin1')


STRASG = re.compile(r'(?m)^([ <>]*= structure \S+ \S* ?)-?\d+$')


def invalid_goldens():
    """tests/mutos_cc/invalid_goldens.txt: {base: lines} - the goldens
    whose tail is the original's invalid output, which the port refuses
    after exactly <lines> lines of .s."""
    out = {}
    with open(os.path.join(CC, 'invalid_goldens.txt')) as f:
        for ln in f:
            w = ln.split()
            if len(w) == 2 and not ln.startswith('#'):
                out[os.path.join(CC, w[0])] = int(w[1])
    return out


def blocks(text, depth):
    """The trees as a list of (header, body), "a" trees only at depth 2.
    A header "@@ b <line>" or "@@ a <line>" may follow a tree's last line
    on the same line: tprint() writes "< " for a missing operand and
    nothing after it."""
    parts = re.split(r'@@ ([ab] -?\d+)\n', text)
    out = []
    for i in range(1, len(parts), 2):
        body = parts[i + 1]
        body = STRASG.sub(r'\1#', body)
        if parts[i][0] == 'b' or depth >= 2:
            out.append((parts[i], body))
    return out


def compare4(base, port, opts=(), invalid=None):
    """Depth 4 (and 3, with the original's trace options): the port's .s,
    messages and exit status against the emulated original's."""
    t1, t2 = pair(base)
    with tempfile.TemporaryDirectory() as tmp:
        st, _, err = mutosemu.run_tool('c1', [os.path.abspath(t1), os.path.abspath(t2),
                                              os.path.join(tmp, 'o.s')] + list(opts),
                                       cwd=tmp, max_insns=MAX_INSNS)
        orig = open(os.path.join(tmp, 'o.s'), 'rb').read()
        p = subprocess.run([port, t1, t2, os.path.join(tmp, 'p.s')] + list(opts),
                           capture_output=True, timeout=120)
        mine = open(os.path.join(tmp, 'p.s'), 'rb').read() \
            if os.path.exists(os.path.join(tmp, 'p.s')) else b''
    if opts:
        # the traces hold a STRASG's degree (see above)
        orig = STRASG.sub(r'\1#', orig.decode('latin1')).encode('latin1')
        mine = STRASG.sub(r'\1#', mine.decode('latin1')).encode('latin1')
    if p.returncode == 2:
        same = 0
        for x, y in zip(orig.split(b'\n'), mine.split(b'\n')[:-1]):
            if x != y:
                break
            same += 1
        why = p.stderr.decode('latin1').strip()[-300:]
        if invalid and base in invalid and (opts or same == invalid[base]) \
                and orig.startswith(mine):
            return base, 'refused', 'refused after %d identical lines, as ' \
                'invalid_goldens.txt specifies' % same
        return base, False, 'refused by the port after %d identical lines: %s' % (same, why)
    if orig != mine:
        ol, ml = orig.split(b'\n'), mine.split(b'\n')
        for i, (x, y) in enumerate(zip(ol, ml)):
            if x != y:
                return base, False, '.s line %d differs\n--- original\n%s\n--- port\n%s' % (
                    i + 1, b'\n'.join(ol[max(0, i - 3):i + 4]).decode('latin1'),
                    b'\n'.join(ml[max(0, i - 3):i + 4]).decode('latin1'))
        return base, False, '.s: %d lines, port %d' % (len(ol), len(ml))
    if err != p.stderr:
        return base, False, 'messages differ\n--- original\n%s--- port\n%s' % (
            err.decode('latin1'), p.stderr.decode('latin1'))
    if st != p.returncode:
        return base, False, 'exit status %d, port %d' % (st, p.returncode)
    return base, True, ''


def compare(job):
    base, depth, port, invalid = job
    if depth == 3:
        return compare4(base, port, ['rcdab'], invalid)
    if depth >= 4:
        return compare4(base, port, (), invalid)
    t1, t2 = pair(base)
    try:
        orig, _ = original_trees(t1, t2)
    except RuntimeError as e:
        return base, False, 'original: %s' % e
    mine, rc, err = port_trees(t1, t2, port)
    a, b = blocks(orig, depth), blocks(mine, depth)
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return base, False, 'tree %d (%s): differs\n--- original %s\n%s--- port %s\n%s' % (
                i, x[0], x[0], x[1], y[0], y[1])
    if rc == 2 and len(b) < len(a) and base in invalid:
        return base, 'refused', 'refused after %d identical trees, as ' \
            'invalid_goldens.txt specifies' % len(b)
    if rc == 2 and len(b) < len(a):
        return base, False, 'refused by the port after %d identical trees: %s' % (
            len(b), err.strip()[-300:])
    if len(a) != len(b):
        return base, False, '%d trees, port %d (exit %d) %s' % (
            len(a), len(b), rc, err.strip()[:300])
    return base, True, '%d trees' % len(a)


def pair(base):
    if os.path.exists(base + '.1.golden'):
        return base + '.1.golden', base + '.2.golden'
    return base + '.1', base + '.2'


def main(argv):
    jobs, port, depth, show = os.cpu_count() or 1, PORT, 1, 0
    bases = []
    if argv[:1] == ['--dump']:
        text, st = original_trees(argv[1], argv[2])
        sys.stdout.write(text)
        return st
    while argv:
        a = argv.pop(0)
        if a == '-j':
            jobs = int(argv.pop(0))
        elif a == '--port':
            port = argv.pop(0)
        elif a == '--depth':
            depth = int(argv.pop(0))
        elif a == '--show':
            show = int(argv.pop(0))
        else:
            bases.append(a)
    if not bases:
        bases = sorted(g[:-len('.1.golden')] for g in
                       glob.glob(os.path.join(CC, '**', '*.1.golden'), recursive=True))
    bad = refused = 0
    invalid = invalid_goldens()
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for base, ok, msg in ex.map(compare, [(b, depth, port, invalid) for b in bases]):
            if ok == 'refused':
                refused += 1
                print('%s: %s' % (os.path.relpath(base, CC), msg))
            elif not ok:
                bad += 1
                first = msg.split('\n', 1)
                print('%s: %s' % (os.path.relpath(base, CC), first[0]))
                if bad <= show and len(first) > 1:
                    print(first[1])
    print('c1 %s, depth %d: %d/%d identical%s' % (
        'output' if depth >= 3 else 'trees', depth, len(bases) - bad - refused,
        len(bases), ', %d refused on purpose' % refused if refused else ''))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
