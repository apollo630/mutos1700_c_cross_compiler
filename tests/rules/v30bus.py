#!/usr/bin/env python3
"""v30bus.py - the timing programs of step V4's kit (v30kit.py) under a
model of the bus, beside the times measured on the owner's A7100.

The kit predicted the run times by the NEC manual's clocks alone, at the
A7100's original 4.9152 MHz (x86sim.py counts them). The machine they ran
on (2026-11-21, tests/rules/hwlog_2026-11-21.txt) has its NEC V30 at
15 MHz and its memory with 3 or 4 wait states (the owner): a bus cycle
takes 7 or 8 clocks instead of 4, and short instructions wait for the
queue rather than for the execution unit. This tool runs one turn of each
timing program (work(2) less work(1)) under tests/realbin/mutosemu.py,
instruction by instruction, and counts its clocks with

  - the execution unit (EU): the NEC V20/V30 user's manual's clocks for
    the uPD70116 (the figures x86sim.py uses), a data transfer taking a
    bus cycle of 4 + w clocks where the manual counts 4;
  - the bus interface unit (BIU): a queue of 6 bytes, a word fetched (a
    byte at an odd address) per bus cycle when two bytes are free, a data
    transfer of the EU before the next fetch; an instruction starts when
    all its bytes are in the queue;
  - a taken branch (a jump, a call, a return, a conditional branch taken)
    empties the queue, and fetching starts again at the target.

It prints, for each program as 8086 code (N.8) and as V30 code (N.v), the
seconds by the manual's clocks alone, by the model, and as measured (the
user time MUTOS's time printed), and the gains. A multiplication's clocks
depend on the data; the model gives the manual's lowest and highest.

Usage:
    v30bus.py [--mhz F] [--wait W] [--imul3 N]

--mhz: the processor clock in MHz (default 15). --wait: wait states of a
bus cycle (default 3). --imul3: clocks added to the manual's figures for
"imul r,r,imm" (28-34, 36-42 with a word), default 0 - with 12 the model
gives what bimul.v measured. Requires the Python modules unicorn and
capstone and a built tree ("make").
"""

import io
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'tests', 'realbin'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'mutos_cc', 'fuzz'))
sys.path.insert(0, HERE)

try:
    import mutosemu  # noqa: E402
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_CX
except ImportError:
    sys.exit('v30bus.py: needs the Python module "unicorn" (pip install unicorn)')
try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
except ImportError:
    sys.exit('v30bus.py: needs the Python module "capstone" (pip install capstone)')
import v30kit  # noqa: E402

CC = os.path.join(ROOT, 'mutos_root', 'bin', 'cc')
QUEUE = 6
WARM = 300          # instructions run before the turn, to fill the queue
# the turns of the kit that ran (v30kit.py, --seconds 40) and the user
# times of MUTOS's time on the A7100, 2026-11-21 (hwlog_2026-11-21.txt)
TURNS = {'bpush': 304, 'bshift': 878, 'bimul': 702}
MEASURED = {('bpush', '8'): 23.0, ('bpush', 'v'): 22.4,
            ('bshift', '8'): 21.1, ('bshift', 'v'): 18.0,
            ('bimul', '8'): 15.6, ('bimul', 'v'): 13.6}
REGS = set('ax bx cx dx si di sp bp al ah bl bh cl ch dl dh'.split())
BRANCHES = set('jo jno jb jae je jne jbe ja js jns jp jnp jl jge jle jg '
               'jcxz loop'.split())
SHIFTS = ('shl', 'sal', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr')


def kind(op):
    op = op.strip()
    if op in REGS:
        return 'r'
    return 'm' if '[' in op else 'i'


def eu_clocks(mnem, ops, size, cl, taken, high, imul3):
    """The manual's clocks of an instruction (capstone's spelling), as
    x86sim.v30_clocks() has them; high: a multiplication's highest."""
    k = tuple(kind(o) for o in ops.split(',')) if ops else ()
    if mnem == 'mov':
        return {('r', 'r'): 2, ('r', 'i'): 4, ('m', 'r'): 9, ('r', 'm'): 11,
                ('m', 'i'): 11}[k]
    if mnem in ('add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb'):
        return {('r', 'r'): 2, ('r', 'i'): 4, ('r', 'm'): 11, ('m', 'r'): 16,
                ('m', 'i'): 18}[k]
    if mnem == 'cmp':
        return {('r', 'r'): 2, ('r', 'i'): 4, ('r', 'm'): 11, ('m', 'r'): 11,
                ('m', 'i'): 13}[k]
    if mnem == 'test':
        return {('r', 'r'): 2, ('r', 'i'): 4, ('r', 'm'): 10, ('m', 'r'): 10,
                ('m', 'i'): 11}[k]
    if mnem in ('inc', 'dec', 'not', 'neg'):
        return {('r',): 2, ('m',): 16}[k]
    if mnem == 'lea':
        return 4
    if mnem == 'push':
        return {('r',): 8, ('m',): 18}.get(k) or (7 if size == 2 else 8)
    if mnem == 'pop':
        return {('r',): 8, ('m',): 17}[k]
    if mnem == 'call':
        return 16
    if mnem == 'ret':
        return 20 if ops else 15
    if mnem == 'jmp':
        return 12
    if mnem in BRANCHES:
        return 14 if taken else 4
    if mnem == 'imul':
        if len(k) == 1:
            return (47 if high else 41) if k == ('r',) else (53 if high else 47)
        # "imul r,r,*n." is 3 bytes, "imul r,r,#n" 4; a source in memory,
        # which -1 never writes, is counted as with a word
        byte = k[1] == 'r' and size == 3
        lo = (28 if byte else 36) if k[1] == 'r' else 42
        return lo + (6 if high else 0) + imul3
    if mnem in SHIFTS:
        mem = k[0] == 'm'
        count = ops.split(',')[1].strip()
        if count == '1':
            return 16 if mem else 2
        n = cl if count == 'cl' else int(count, 0)
        return (19 if mem else 7) + n
    if mnem in ('cbw', 'cwde'):
        return 2
    if mnem in ('cwd', 'cdq'):
        return 5
    return 8            # not in a turn of the timing programs


def trace(image):
    """[(text offset, data bus cycles, CL for a shift by CL)] of every
    instruction the image runs, and {offset: (mnemonic, operands, size)}."""
    out = io.BytesIO()
    emu = mutosemu.Emu(image, ['a.out'], cwd=ROOT, stdin=io.BytesIO(b''),
                       stdout=out, stderr=io.BytesIO(), eof_limit=1000,
                       now=500000000)
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    cbase, dbase, text = emu.cbase, emu.dbase, emu.text
    dec, seq = {}, []

    def code(uc, addr, size, _):
        off = addr - cbase
        ins = dec.get(off)
        if ins is None:
            i = next(md.disasm(text[off:off + size], off))
            ins = dec[off] = (i.mnemonic, i.op_str, size)
        cl = None
        if ins[0] in SHIFTS and ins[1].endswith(', cl'):
            cl = uc.reg_read(UC_X86_REG_CX) & 0xFF
        seq.append([off, 0, cl])

    def mem(uc, access, addr, size, value, _):
        if seq:     # a word at an odd address takes two bus cycles
            seq[-1][1] += 1 if size == 1 or (addr - dbase) % 2 == 0 else 2

    emu.uc.hook_add(UC_HOOK_CODE, code)
    emu.uc.hook_add(UC_HOOK_MEM_READ, mem)
    emu.uc.hook_add(UC_HOOK_MEM_WRITE, mem)
    if emu.run(v30kit.MAX_INSNS) != 0:
        raise RuntimeError('the image did not exit with 0')
    return out.getvalue(), dec, seq


def one_turn(work, name, tag):
    """The instructions of the program's last turn, with WARM before it:
    the trace of work(2) where it leaves work(1)'s."""
    runs = []
    for rep in (1, 2):
        d = os.path.join(work, '%s.%s.%d' % (name, tag, rep))
        os.makedirs(d)
        with open(os.path.join(d, 'p.c'), 'w') as f:
            f.write(v30kit.BENCH[name] + v30kit.BENCH_MAIN % rep)
        st = subprocess.run([CC, '-O'] + (['-1'] if tag == 'v' else []) +
                            ['-o', 'p', 'p.c'], cwd=d, capture_output=True)
        if st.returncode:
            sys.exit('v30bus.py: cc: %s' % st.stderr.decode('latin-1'))
        with open(os.path.join(d, 'p'), 'rb') as f:
            runs.append(trace(f.read()))
    (out1, _, s1), (out2, dec, s2) = runs
    if out1 != out2:
        sys.exit('v30bus.py: %s.%s prints %r and %r' % (name, tag, out1, out2))
    p = 0
    while s1[p][0] == s2[p][0]:
        p += 1
    extra = len(s2) - len(s1)
    if [x[0] for x in s2[p + extra:]] != [x[0] for x in s1[p:]]:
        sys.exit('v30bus.py: %s.%s: no turn found in the trace' % (name, tag))
    return dec, s2[p - WARM:p], s2[p - WARM:p + extra]


def clocks(dec, seq, wait, high, imul3, bus=True):
    """The clocks of the instructions seq; bus=False: the EU's alone."""
    n = len(seq)
    info = []
    for i, (off, data, cl) in enumerate(seq):
        mnem, ops, size = dec[off]
        nxt = seq[i + 1][0] if i + 1 < n else off + size
        taken = nxt != off + size
        info.append((size, data, eu_clocks(mnem, ops, size, cl, taken, high, imul3),
                     taken, nxt))
    if not bus:
        return sum(x[2] for x in info)
    cycle = 4 + wait
    t, queue, faddr = 0, 0, seq[0][0]
    bus_left, bus_kind, fetch_n, fetch_ok = 0, None, 0, True
    want_data, data_busy = False, False
    i, phase, data_left, eu_left = 0, 0, 0, 0
    taken, target = False, 0
    while i < n:
        if bus_left == 0:                       # the BIU takes the bus
            if want_data:
                bus_kind, bus_left, want_data, data_busy = 'd', cycle, False, True
            else:
                fetch_n = 1 if faddr & 1 else 2
                if queue + fetch_n <= QUEUE:
                    bus_kind, bus_left, fetch_ok = 'f', cycle, True
                else:
                    bus_kind = None
        if phase == 0:                          # the EU starts an instruction
            size, data, eu, taken, target = info[i]
            if queue >= min(size, QUEUE):
                queue -= min(size, QUEUE)
                data_left, eu_left = data, max(1, eu - 4 * data)
                phase = 1 if data else 2
        if phase == 1:                          # its data transfers
            if data_left == 0:
                phase = 2
            elif not data_busy and not want_data:
                want_data = True
        if phase == 2:                          # the rest of its clocks
            eu_left -= 1
            if eu_left <= 0:
                if taken:
                    queue, faddr = 0, target
                    if bus_kind == 'f':
                        fetch_ok = False
                i, phase = i + 1, 0
        t += 1
        if bus_left:
            bus_left -= 1
            if bus_left == 0:
                if bus_kind == 'f' and fetch_ok:
                    queue += fetch_n
                    faddr += fetch_n
                elif bus_kind == 'd':
                    data_busy = False
                    data_left -= 1
                bus_kind = None
    return t


def main(argv):
    mhz, wait, imul3 = 15.0, 3, 0
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--mhz' and args:
            mhz = float(args.pop(0))
        elif a == '--wait' and args:
            wait = int(args.pop(0))
        elif a == '--imul3' and args:
            imul3 = int(args.pop(0))
        else:
            print(__doc__)
            return 2
    if not os.access(CC, os.X_OK):
        sys.exit('v30bus.py: %s is not built - run "make" first' % CC)
    hz = mhz * 1e6
    print('v30bus: one turn of each timing program, at %g MHz, %d wait states%s'
          % (mhz, wait, ', imul r,r,imm +%d clocks' % imul3 if imul3 else ''))
    print('    program  manual\'s clocks   bus model        measured')
    got = {}
    with tempfile.TemporaryDirectory(prefix='v30bus.') as work:
        for name in ('bpush', 'bshift', 'bimul'):
            for tag in ('8', 'v'):
                dec, warm, full = one_turn(work, name, tag)
                row = []
                for bus in (False, True):
                    lo, hi = [(clocks(dec, full, wait, h, imul3, bus) -
                               clocks(dec, warm, wait, h, imul3, bus))
                              * TURNS[name] / hz for h in (False, True)]
                    row.append((lo, hi))
                got[(name, tag)] = row[1]
                print('    %-8s %-16s %-16s %4.1f s' % (
                    name + '.' + tag,
                    *['%4.1f s' % lo if lo == hi else '%4.1f-%4.1f s' % (lo, hi)
                      for lo, hi in row], MEASURED[(name, tag)]))
    print('    gain     bus model  measured')
    for name in ('bpush', 'bshift', 'bimul'):
        a, b = got[(name, '8')], got[(name, 'v')]
        print('    %-8s %5.1f%%     %5.1f%%' % (
            name, 100 * (1 - (b[0] + b[1]) / (a[0] + a[1])),
            100 * (1 - MEASURED[(name, 'v')] / MEASURED[(name, '8')])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
