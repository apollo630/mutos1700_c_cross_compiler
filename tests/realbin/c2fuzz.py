#!/usr/bin/env python3
"""c2fuzz.py - differential fuzzing of mutos_c2 against the original c2.

The c2 port (src/mutos_cc/c2port/) must do what the original binary does
for any input, not only for the goldens. Each case is a file of assembly
text, run through the original c2 (emulated) and the port with
c2cmp.py's comparison - the output, the messages, the exit status; a
refusal of the port must have written a prefix of the original's output,
and where the original does not finish the port must have refused.
Generators:

    c1fuzz   random K&R programs (c1fuzz.py's generator) through the
             ported chain - mutos_cpp -P, mutos_c0, mutos_c1, the ports
             of the originals, identical to them -: the real compiler's
             output, as "cc -O" gives it to c2
    fuzz_c   the same with tests/mutos_cc/fuzz/fuzz_c.py's programs
    asm      assembly in c1's style made to reach every pass of c2:
             functions with register moves and renames, push and pop,
             "add si,bx" before "(si)", "mov r,*0.", "cmp r,*0.",
             "and ax,#255.", "or r,r", "sub r,r", "lea r,(si)", xchg,
             cwd, cbw, imul, idiv, shifts by cl, loop, calls, "|NREG n"
             (n 0..3), "|RTYP n", constant comparisons and branches of
             every kind near and far, jumps to jumps, unreachable code,
             loops, .data, .bss and .text in between, jump tables,
             several functions in one file (the arena reused)

--mutate P damages each case with probability P: lines deleted, doubled,
swapped, cut, long operands (copied over regs[], rt1[], rt2[]), long
first tokens (over tmpop[]), odd bytes, empty lines, no last linefeed,
odd label numbers, branches to undefined labels or to code, a line
longer than c2's line buffer. The command line is "c2 in out", sometimes
"c2 - in out" (the statistics) or "c2 <in >out".

Usage:
    c2fuzz.py [-n COUNT] [-s SEED] [-j JOBS] [--gen c1fuzz|fuzz_c|asm|mix]
              [--mutate P] [--keep DIR] [--show N] [--port PATH]

Exit status 1 if any case differs. A fixed seed gives the same cases
every time.

Requires the Python module "unicorn" (pip install unicorn).
"""
import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'mutos_cc', 'fuzz'))
import c2cmp          # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
CPP = os.path.join(ROOT, 'src', 'mutos_cpp', 'mutos_cpp')
C0 = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_c0')
C1 = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_c1')
FUZZ_INSNS = 150_000_000    # an endless original stops here
FUZZ_CPU = 10               # and the port (seconds)

REGS = ('ax', 'dx', 'si', 'di', 'bx', 'cx')
IDX = ('si', 'di', 'bx')
CONDS = ('beq', 'bne', 'ble', 'bge', 'blt', 'bgt', 'blo', 'bhi', 'blos', 'bhis')
SCONDS = ('je', 'jne', 'jle', 'jge', 'jl', 'jg', 'jb', 'ja', 'jbe', 'jae')
GLOBALS = ('_x', '_y', '_buf', '_tab', '_u', '_p')


# ---------------------------------------------------------------------
# c1's output from random programs
# ---------------------------------------------------------------------

def compile_c(src, wd):
    """The ported chain on a C program: its .s text, or None."""
    c = os.path.join(wd, 'p.c')
    with open(c, 'w') as f:
        f.write(src)
    i, t1, t2, s = (os.path.join(wd, n) for n in ('p.i', 't1', 't2', 'p.s'))
    for cmd in ([CPP, '-P', c, i], [C0, i, t1, t2], [C1, t1, t2, s]):
        r = subprocess.run(cmd, capture_output=True, timeout=60)
        if r.returncode not in (0, 1) or not os.path.exists(cmd[-1]):
            return None
    return open(s, 'rb').read()


def gen_c1fuzz(r, wd):
    import c1fuzz
    return compile_c(c1fuzz.Gen(r).program(), wd)


def gen_fuzz_c(r, wd):
    import fuzz_c
    return compile_c(fuzz_c.generate(r, True, r.random() < 0.5)[0], wd)


# ---------------------------------------------------------------------
# c1-style assembly
# ---------------------------------------------------------------------

class Asm:
    def __init__(self, r):
        self.r = r
        self.lab = r.randint(1, 30)
        self.out = []

    def pick(self, s):
        return self.r.choice(s)

    def chance(self, p):
        return self.r.random() < p

    def newlab(self):
        self.lab += 1
        return self.lab

    def const(self):
        r = self.r.random()
        if r < 0.3:
            return '*%d.' % self.r.choice((0, 0, 1, 2, 3, 4, 8, -1, -6, 127))
        if r < 0.5:
            return '#%d.' % self.r.choice((0, 1, 192, 255, 256, 1000, -2, 32767))
        if r < 0.6:
            return '*%d.+%d' % (self.r.randint(0, 9), self.r.randint(0, 3))
        return '*%d.' % self.r.randint(-130, 300)

    def mem(self):
        r = self.r.random()
        if r < 0.35:
            return '*%d.(bp)' % self.r.choice((4, 6, 8, -6, -8, -10, -12))
        if r < 0.5:
            return '(%s)' % self.pick(IDX)
        if r < 0.65:
            return '*%d.(%s)' % (self.r.choice((0, 2, 4, 6)), self.pick(IDX))
        if r < 0.75:
            return '%s(%s)' % (self.pick(GLOBALS), self.pick(IDX))
        if r < 0.8:
            return '#%d.(bp)' % self.r.choice((-200, 300))
        return self.pick(GLOBALS) + ('' if self.chance(0.7) else
                                     '+%d.' % self.r.randint(1, 8))

    def opnd(self, regp=0.5, constp=0.2):
        r = self.r.random()
        if r < regp:
            return self.pick(REGS) if self.chance(0.95) else 'bp'
        if r < regp + constp:
            return self.const()
        return self.mem()

    def emit(self, s):
        self.out.append(s)

    def insn(self, labels):
        r = self.r.random()
        R = self.pick(REGS)
        if r < 0.16:
            # moves: register, memory, constants
            k = self.r.random()
            if k < 0.3:
                self.emit('mov\t%s,%s' % (R, self.pick(REGS)))
            elif k < 0.5:
                self.emit('mov\t%s,%s' % (R, self.opnd(0.2, 0.4)))
            elif k < 0.7:
                self.emit('mov\t%s,%s' % (self.mem(), self.opnd(0.6, 0.4)))
            elif k < 0.8:
                self.emit('mov\t%s,*0.' % R)
            elif k < 0.9:
                self.emit('movb\t%s,%s' % (R, self.opnd(0.3, 0.3)))
            else:
                self.emit('mov\t%s,%s' % (self.opnd(0.7, 0.0), self.opnd(0.6, 0.3)))
        elif r < 0.30:
            op = self.pick(('add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb'))
            k = self.r.random()
            if k < 0.12:
                self.emit('add\t%s,%s' % (self.pick(('si', 'di')),
                                         self.pick(('bx', 'bp', 'si'))))
            elif k < 0.2:
                self.emit('add\tbx,%s' % self.pick(('si', 'di')))
            elif k < 0.28:
                self.emit('%s\t%s,%s' % (self.pick(('sub', 'or', 'xor')), R, R))
            elif k < 0.34:
                self.emit('and\t%s,#255.' % self.pick(('ax', 'dx', 'bx')))
            elif k < 0.4:
                self.emit('%s\tsp,%s' % (self.pick(('add', 'sub')),
                                         self.pick(('*2.', '*4.', '#300.'))))
            else:
                self.emit('%s\t%s,%s' % (op, self.opnd(0.6, 0.0),
                                         self.opnd(0.4, 0.4)))
        elif r < 0.40:
            k = self.r.random()
            if k < 0.3:
                self.emit('cmp\t%s,%s' % (R, self.const()))
            elif k < 0.45:
                self.emit('cmp\t%s,*0.' % R)
            elif k < 0.6:
                self.emit('test\t%s,%s' % (R, R if self.chance(0.7) else self.opnd()))
            elif k < 0.7:
                self.emit('cmp\t%s,%s' % (self.const(), self.const()))
            else:
                self.emit('cmp\t%s,%s' % (self.opnd(), self.opnd()))
            self.branch(labels, cond=True)
        elif r < 0.50:
            k = self.r.random()
            if k < 0.4:
                self.emit('push\t%s' % self.opnd(0.6, 0.0))
            elif k < 0.55:
                self.emit('push\t%s' % self.opnd(0.3, 0.6))
            elif k < 0.8:
                self.emit('pop\t%s' % R)
            else:
                self.emit('mov\t%s,%s' % (R, self.opnd(0.4, 0.3)))
                self.emit('push\t%s' % R)
        elif r < 0.56:
            k = self.r.random()
            if k < 0.5:
                self.emit('call\t%s' % self.pick(('_g', '_h', 'lmul', 'ldiv',
                                                  '@si', '@8.+_cdevsw(bx)')))
            else:
                self.emit('call\t_g')
                self.emit('add\tsp,*%d.' % self.r.choice((2, 4, 6)))
        elif r < 0.62:
            self.emit('%s\t%s' % (self.pick(('inc', 'dec', 'neg', 'not')),
                                  self.opnd(0.6, 0.0)))
        elif r < 0.67:
            k = self.r.random()
            if k < 0.3:
                self.emit(self.pick(('cwd', 'cbw')))
            elif k < 0.6:
                self.emit('%s\t%s' % (self.pick(('imul', 'idiv', 'div')),
                                      self.opnd(0.5, 0.0)))
            else:
                self.emit('%s\t%s,%s' % (self.pick(('sal', 'sar', 'shr', 'shl')),
                                         self.opnd(0.7, 0.0),
                                         self.pick(('1', 'cl', '1', '*1.'))))
        elif r < 0.71:
            k = self.r.random()
            if k < 0.08:
                # byte instructions: only ax dx bx cx have a byte half
                self.emit('%sb\t%s,%s' % (self.pick(('add', 'and', 'or', 'sub',
                                                      'xor', 'cmp', 'test')),
                                          self.opnd(0.5, 0.1), self.opnd(0.5, 0.3)))
            elif k < 0.14:
                self.emit('%sb\t%s' % (self.pick(('inc', 'dec', 'push', 'neg')),
                                       self.opnd(0.5, 0.0)))
            elif k < 0.2:
                # indexopt(): "add si,bx" then the addresses through si
                ri = self.pick(('si', 'di'))
                self.emit('add\t%s,%s' % (ri, self.pick(('bx', 'bp'))))
                for _ in range(self.r.randint(0, 3)):
                    self.emit('mov\t%s,*%d.(%s)' % (self.pick(('ax', 'dx', 'cx')),
                                                   self.r.choice((0, 2, 4)), ri))
                if self.chance(0.6):
                    self.emit('mov\t%s,*%d.(%s)' % (ri, self.r.choice((0, 2)), ri))
            elif k < 0.25:
                self.emit('xchg\t%s,%s' % (self.opnd(0.6, 0.0), self.opnd(0.6, 0.0)))
            elif k < 0.28:
                self.emit(self.pick(('mov\tsp,bp', 'lea\tsp,*-4.(bp)',
                                     'mov\tbp,sp')))
            elif k < 0.32:
                self.emit('%s\t%s,%s' % (self.pick(('sal', 'sar', 'shr')),
                                         self.pick(('*4.(si)', '(bx)', '*2.(di)')),
                                         self.pick(('1', 'cl'))))
            elif k < 0.5:
                self.emit('lea\t%s,%s' % (R, self.pick(('(si)', '(di)', '(bx)',
                                                         '(bp)', '*4.(bp)', '_x'))))
            elif k < 0.6:
                self.emit('lea\t%s,(si)' % self.mem())
            else:
                self.emit('xchg\t%s,%s' % (R, self.pick(('ax', 'dx', 'bx'))))
        elif r < 0.75:
            self.emit('%s' % self.pick(('| comment', '|\t_i=-6.', 'seg\tes',
                                        '|NREG\t2', '.even', 'ret')))
        elif r < 0.83:
            self.branch(labels, cond=False)
        elif r < 0.91:
            labels.append(self.newlab())
            self.emit('L%d:' % labels[-1] + ('' if self.chance(0.5) else
                                             self.inline()))
        elif r < 0.94:
            self.emit('loop\tL%d' % self.pick(labels or [self.lab]))
        elif r < 0.97:
            # a little data
            self.emit('.data')
            l = self.newlab()
            self.emit('L%d:%s' % (l, self.pick(('.byte 1,2,3', '_x', 'L%d' % l,
                                                 '.even'))))
            self.emit(self.pick(('.text', '.text', '.bss', '.data')))
        else:
            # a jump table
            l1, l2 = self.newlab(), self.newlab()
            labels += [l1, l2]
            self.emit('L%d:L%d' % (self.newlab(), l1))
            self.emit('L%d' % l2)

    def inline(self):
        return self.pick(('mov\tax,bx', 'push\tsi', '| _i=4.', 'inc\tdi',
                          'j\tL%d' % self.lab))

    def branch(self, labels, cond):
        if self.chance(0.6) and labels:
            t = self.pick(labels)
        else:
            t = self.lab + self.r.randint(1, 6)
        k = self.r.random()
        if cond:
            op = self.pick(CONDS) if k < 0.8 else self.pick(SCONDS)
        else:
            op = self.pick(('jmp', 'jmp', 'br', 'j'))
        if self.chance(0.05):
            self.emit('%s\t%s' % (op, self.pick(('cret', '_f', '@si'))))
        else:
            self.emit('%s\tL%d' % (op, t))

    def function(self, name):
        n = self.r.randint(3, 3 if self.chance(0.1) else
                           self.pick((20, 40, 80, 160)))
        self.emit('.globl\t%s' % name)
        self.emit('.text')
        self.emit('%s:' % name)
        self.emit('push\tbp')
        self.emit('mov\tbp,sp')
        self.emit('push\tdi')
        self.emit('push\tsi')
        self.emit('|NREG\t%d' % self.pick((3, 3, 3, 2, 1, 0)))
        labels = []
        if self.chance(0.6):
            l = self.newlab()
            self.emit('jmp\tL%d' % l)
            back = self.newlab()
            labels.append(back)
            self.emit('L%d:' % back)
        else:
            l = None
        for _ in range(n):
            self.insn(labels)
        end = self.newlab()
        # the targets that were invented: give most of them a place
        for t in range(end - 8, end + 8):
            if t not in labels and self.chance(0.3):
                self.emit('L%d:' % t)
                self.insn(labels)
        self.emit('L%d:|RTYP\t%d' % (end, self.pick((0, 0, 6, 1, 2))))
        self.emit(self.pick(('jmp\tcret', 'jmp\tcret', 'br\tcret', 'ret')))
        if l is not None:
            self.emit('L%d:sub\tsp,*%d.' % (l, self.r.choice((2, 4, 20))))
            self.emit('jmp\tL%d' % back)

    def program(self):
        for i in range(self.r.randint(1, 4)):
            if self.chance(0.3):
                self.emit(self.pick(('.data', '.bss', '.text')))
                self.emit('.comm\t_v%d,2' % i)
            self.function('_f%d' % i)
        if self.chance(0.7):
            self.emit('.data')
        return ('\n'.join(self.out) + '\n').encode('latin1')


def gen_asm(r, wd):
    return Asm(r).program()


# ---------------------------------------------------------------------
# Damage
# ---------------------------------------------------------------------

def long_text(r, n):
    return ''.join(r.choice('abcxyz_0123456789*()#.+,') for _ in range(n))


def mutate(r, data):
    lines = data.decode('latin1').split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    out = []
    for ln in lines:
        k = r.random()
        if k < 0.05:
            continue
        if k < 0.09:
            out += [ln, ln]
            continue
        if k < 0.12 and out:
            out.insert(len(out) - 1, ln)
            continue
        if k < 0.15:
            out.append(ln[:r.randint(0, len(ln))])
            continue
        if k < 0.18 and '\t' in ln:
            # a long operand: over regs[], conloc .. ophash, rt1/rt2
            op, rest = ln.split('\t', 1)
            # mostly over regs[] and into conloc .. ccloc, rt1[]/rt2[]
            # into libc.a's BSS; rarely far enough to write over ophash[]
            # (then the original follows text as pointers past its break)
            n = r.choice((31, 32, 33, 40, 49, 50, 51, 60, 70, 90)) \
                if r.random() < 0.9 else r.choice((200, 400, 480))
            if ',' in rest and r.random() < 0.5:
                a, b = rest.split(',', 1)
                out.append('%s\t%s,%s' % (op, a, b + long_text(r, n)) if r.random() < 0.5
                           else '%s\t%s,%s' % (op, a + long_text(r, n), b))
            else:
                out.append('%s\t%s' % (op, rest + long_text(r, n)))
            continue
        if k < 0.19:
            # a long first token: over tmpop[] into labhash[]
            out.append(long_text(r, r.choice((31, 32, 33, 40, 60, 100, 300)))
                       + '\tax,bx')
            continue
        if k < 0.24:
            out.append(ln + ''.join(chr(r.randint(0x80, 0xff))
                                    for _ in range(r.randint(1, 3))))
            continue
        if k < 0.26:
            out.append('')
            continue
        if k < 0.28:
            out.append(r.choice(('beq\tL0', 'jmp\tL99999', 'jmp\tL40000',
                                 'br\tL65535', 'beq\t_foo', 'j\tcret',
                                 'L0:', 'L40000:', 'Lx1:', 'jmp\tL7x',
                                 'loop\tL1', 'L%d' % r.randint(0, 99),
                                 '|NREG', '|NREG\t-1', '|RTYP\t6', 'test\tax',
                                 'lea\t_x,(si)', 'mov\t*5.,ax', 'inc\t*0.',
                                 'push', 'pop', 'call', '.globl', 'jmp')))
            continue
        out.append(ln)
    text = '\n'.join(out)
    if r.random() < 0.02:
        text += '\n' + long_text(r, 520)
    if r.random() > 0.1:
        text += '\n'
    return text.encode('latin1')


# ---------------------------------------------------------------------

GENS = {'c1fuzz': gen_c1fuzz, 'fuzz_c': gen_fuzz_c, 'asm': gen_asm}


def make_case(seed, idx, gen, mut, wd):
    r = random.Random('%d.%d' % (seed, idx))
    g = gen if gen != 'mix' else r.choice(('c1fuzz', 'asm', 'asm', 'fuzz_c'))
    data = GENS[g](r, wd)
    if data is None:
        return None, g
    if mut and r.random() < mut:
        data = mutate(r, data)
    k = r.random()
    mode, flags = ('file', ()) if k < 0.8 else \
        ('file', ('-',)) if k < 0.9 else ('stdio', ())
    return (data, mode, flags), g


def run_one(job):
    idx, seed, gen, mut, port, keep = job
    with tempfile.TemporaryDirectory() as wd:
        case, g = make_case(seed, idx, gen, mut, wd)
        if case is None:
            return idx, g, 'skipped', 'the chain did not compile the program'
        data, mode, flags = case
        open(os.path.join(wd, 'in.s'), 'wb').write(data)
        kind, detail, _ = c2cmp.compare('in.s', wd, port, mode, flags,
                                        max_insns=FUZZ_INSNS, cpu=FUZZ_CPU)
        if keep and kind not in ('same', 'refused'):
            os.makedirs(keep, exist_ok=True)
            shutil.copy(os.path.join(wd, 'in.s'),
                        os.path.join(keep, 'case%d_%s.s' % (idx, kind)))
    return idx, g, kind, detail + ('' if mode == 'file' and not flags else
                                   ' [%s %s]' % (mode, ' '.join(flags)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('-n', '--count', type=int, default=200)
    ap.add_argument('-s', '--seed', type=int, default=1)
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 1)
    ap.add_argument('--gen', choices=('c1fuzz', 'fuzz_c', 'asm', 'mix'),
                    default='mix')
    ap.add_argument('--mutate', type=float, default=0.0)
    ap.add_argument('--keep', help='directory to save the cases that differ in')
    ap.add_argument('--show', type=int, default=0)
    ap.add_argument('--port', default=c2cmp.PORT)
    args = ap.parse_args()
    keep = os.path.abspath(args.keep) if args.keep else None
    jobs = [(i, args.seed, args.gen, args.mutate, os.path.abspath(args.port), keep)
            for i in range(args.count)]
    counts = {}
    bad = []
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as ex:
        for idx, g, kind, detail in ex.map(run_one, jobs):
            counts[kind] = counts.get(kind, 0) + 1
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('case %d (%s): %s: %s' % (idx, g, kind, detail))
            elif kind not in ('same',) and args.show:
                print('case %d (%s): %s: %s' % (idx, g, kind, detail))
    for b in bad[:args.show or 20]:
        print(b)
    print('c2fuzz: %d cases (%s%s), seed %d: %s' % (
        args.count, args.gen, ', mutated %.2f' % args.mutate if args.mutate else '',
        args.seed, ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
