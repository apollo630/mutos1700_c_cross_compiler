#!/usr/bin/env python3
"""check_x86sim_v30.py - x86sim.py's 80186/V30 forms and its V30 clocks
(docs/TOOLCHAIN_PLAN.md, step F0; part of "make test").

The three forms the rules of -1 produce (section 7.4) - "push" of a
constant, a shift by a constant count, "imul" by a constant - run in
x86sim.py only with v30=True, with the NEC manual's flags: each case
below is a small program, its value and its clocks (V30, even addresses:
the manual's figures, summed by hand) pinned, and the flags the manual
leaves undefined must stop a branch that reads them. Without v30 each
form is an error. Every program is also assembled by mutos_as -1, so the
syntax x86sim.py takes is the one the assembler encodes.

Usage:
    check_x86sim_v30.py [--as PATH]

Exit status 1 on a difference. Python 3, standard library only.
"""
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import x86sim  # noqa: E402

AS = os.path.join(HERE, '..', '..', '..', 'src', 'mutos_as', 'mutos_as')

# main's prologue (push bp 8, mov bp,sp 2, push di 8, push si 8: 26) and
# "jmp cret" (12, then lea 4, three pops 24, ret 15: 55) frame each body.
PRO, EPI = 26, 55

# (name, body, value, clocks of the body (lowest, highest))
CASES = [
    ('push of a constant', 'push\t*-5.\npush\t#300.\npop\tax\npop\tbx\nadd\tax,bx\n',
     295, (7 + 8 + 8 + 8 + 2,) * 2),
    ('sal by 4', 'mov\tax,*3.\nsal\tax,*4.\n', 48, (4 + 11,) * 2),
    ('sar by 3', 'mov\tax,#-100.\nsar\tax,*3.\n', -13, (4 + 10,) * 2),
    ('shr by 12', 'mov\tax,#-1.\nshr\tax,*12.\n', 15, (4 + 19,) * 2),
    ('sal to memory by 3', 'mov\t*-6.(bp),*5.\nsal\t*-6.(bp),*3.\nmov\tax,*-6.(bp)\n',
     40, (11 + 22 + 11,) * 2),
    ('imul ax,*30.', 'mov\tax,*7.\nimul\tax,*30.\n', 210, (4 + 28, 4 + 34)),
    ('imul ax,cx,#200.', 'mov\tcx,#-300.\nimul\tax,cx,#200.\n', 5536, (4 + 36, 4 + 42)),
    ('imul from memory', 'mov\t*-6.(bp),*9.\nimul\tax,*-6.(bp),*-3.\n', -27,
     (11 + 34, 11 + 40)),
    ('imul leaves DX', 'mov\tdx,*9.\nmov\tax,*7.\nimul\tax,*30.\nmov\tax,dx\n', 9,
     (4 + 4 + 28 + 2, 4 + 4 + 34 + 2)),
    # CY = V = the product does not fit (300 * 200): "blo" taken
    ('imul sets CY', 'mov\tax,#300.\nimul\tax,#200.\nmov\tbx,*0.\nblo\tL1\n'
     'mov\tbx,*1.\nL1:mov\tax,bx\n', 0, (4 + 36 + 4 + 14 + 2, 4 + 42 + 4 + 14 + 2)),
    ('imul clears CY', 'mov\tax,*3.\nimul\tax,*4.\nmov\tbx,*0.\nblo\tL1\n'
     'mov\tbx,*1.\nL1:mov\tax,bx\n', 1, (4 + 28 + 4 + 4 + 4 + 2, 4 + 34 + 4 + 4 + 4 + 2)),
    # CY the last bit out: 0x8001 << 2 shifts out 0, then 0 ... the bit 14
    ('sal sets CY from the last bit', 'mov\tax,#/4001\nsal\tax,*2.\nmov\tbx,*0.\n'
     'blo\tL1\nmov\tbx,*1.\nL1:mov\tax,bx\n', 0, (4 + 9 + 4 + 14 + 2,) * 2),
]

# flags the manual leaves undefined: a branch on them stops the checker
UNDEFINED = [
    ('Z after imul', 'mov\tax,*3.\nimul\tax,*4.\nbeq\tL1\nL1:mov\tax,bx\n'),
    ('S after imul', 'mov\tax,*3.\nimul\tax,*4.\nbge\tL1\nL1:mov\tax,bx\n'),
    ('V after sal by 4', 'mov\tax,*3.\nsal\tax,*4.\nblt\tL1\nL1:mov\tax,bx\n'),
]


def program(body):
    return ('.globl\t_main\n.text\n_main:\npush\tbp\nmov\tbp,sp\npush\tdi\n'
            'push\tsi\n' + body + 'jmp\tcret\n')


def main():
    asm = AS
    if '--as' in sys.argv:
        asm = sys.argv[sys.argv.index('--as') + 1]
    bad = []
    with tempfile.TemporaryDirectory(prefix='x86v30') as tmp:
        for name, body, value, clocks in CASES:
            text = program(body)
            r = x86sim.run(text, v30=True)
            want = (clocks[0] + PRO + EPI, clocks[1] + PRO + EPI)
            if r.ret != value or r.clocks != want:
                bad.append('%s: value %s, clocks %s - expected %s, %s'
                           % (name, r.ret, r.clocks, value, want))
            try:
                x86sim.run(text)
                bad.append('%s: runs without v30' % name)
            except x86sim.SimError:
                pass
            src = os.path.join(tmp, 'p.s')
            with open(src, 'w') as f:
                f.write(text)
            p = subprocess.run([asm, '-1', '-o', os.path.join(tmp, 'p.o'), src],
                               capture_output=True, text=True)
            if p.returncode != 0:
                bad.append('%s: mutos_as -1 does not take it: %s'
                           % (name, p.stderr.strip()[-120:]))
        for name, body in UNDEFINED:
            try:
                x86sim.run(program(body), v30=True)
                bad.append('%s: a branch on it runs' % name)
            except x86sim.SimError:
                pass
    for b in bad:
        print('FAIL  ' + b)
    print('x86sim V30: %d forms with their values and clocks, %d undefined flags: %s'
          % (len(CASES), len(UNDEFINED), '%d failure(s)' % len(bad) if bad else 'all as pinned'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
