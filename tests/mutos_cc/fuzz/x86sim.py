#!/usr/bin/env python3
"""x86sim.py - executes mutos_c1 output for a semantic check.

Runs the subset of mutos_as-syntax 8086 code that mutos_c1 emits - the
shape fuzz_c.py generates, a parameterless main(), plus calls of other
functions defined in the same file (with the shared "cret" epilogue and
the "chkstk" large-frame helper built in) and "movb"/"cbw" byte
operations - and reports the final state: main()'s return value (AX at
main's "jmp cret") and every local variable's word(s), located through
c1's own "| _name=-N." frame comments. It is a checker, not an
emulator: anything outside the subset (a libc or indirect call, a
static label operand, a branch on flags not set by a cmp or an "or
r,r", ...) stops it with exit status 2 and a message, never with a
guess.

Validated against real hardware-compiled output: every tests/mutos_cc
.s.golden it can execute (47 of the 62 - the rest call libc or runtime
helpers, or use statics, 'long' carries or a jump table) returns the
value its C source computes, among them 01_expr/06_compasgn (33),
03_ctrlflow/05_breakcont (12), 04_funcs/03_recfact (720), 05_arrptr/
02_array2d (138), 09_abiprobe/03_frame128 (3, through chkstk),
10_integ/02_bubsort (91) and 05_matmul (134), and even 06_struct/
07_bitfield (12) and 06_union (3), whose code mutos_c1 cannot produce
yet.

Usage:
    x86sim.py file.s        prints "ret=<n>", then "<name>=<off>" per
                            local and "w<off>=<n>" per frame word
As a module:
    r = x86sim.run(text)    -> Result (r.ret, r.locals, r.word(off))
"""
import re
import sys

M16 = 0xFFFF
SP0 = 0xF000          # initial stack pointer; the frame lives below it
STEP_LIMIT = 200000   # generated programs have no loops; goldens do

# Instructions that change the flags. A conditional branch is only
# accepted while the flags still come from the most recent cmp - or
# from "or r,r" (same register twice), mutos_c1's truth test of a
# register value: it leaves r unchanged, clears CF and OF and sets ZF/SF
# from r, exactly the flags of "cmp r,0".
FLAG_WRITERS = {"add", "sub", "adc", "sbb", "and", "or", "xor", "inc",
                "dec", "sal", "shl", "sar", "imul", "idiv", "neg"}
BRANCHES = {"blt", "ble", "bgt", "bge", "beq", "bne", "blos", "bhi"}


class SimError(Exception):
    """Code outside the supported subset (not a wrong-code verdict)."""


def s16(v):
    v &= M16
    return v - 0x10000 if v & 0x8000 else v


class Result:
    def __init__(self, ret, bp, locals_, mem, snapshots=()):
        self.ret = ret
        self.bp = bp
        self.locals = locals_       # name -> bp-relative offset
        self._mem = mem
        # (value written, Result of that moment) per write to the watched
        # local - see run()'s `watch`
        self.snapshots = list(snapshots)

    def word(self, off):
        """Signed word at bp+off."""
        a = (self.bp + off) & M16
        return s16(self._mem.get(a, 0) | (self._mem.get((a + 1) & M16, 0) << 8))


class Sim:
    REGS = ("ax", "bx", "cx", "dx", "si", "di", "sp", "bp")

    def __init__(self, text, watch=None):
        self.watch = watch
        self.snapshots = []
        self.regs = {r: 0 for r in self.REGS}
        self.regs["sp"] = SP0
        self.mem = {}
        self.cmp = None            # (a, b) of the last cmp, or None
        self.prog = []             # (mnemonic, [operands])
        self.labels = {}
        self.locals = {}
        for raw in text.splitlines():
            self._parse_line(raw)

    def _note_local(self, comment):
        m = re.match(r"\| _(\w+)=(-?\d+)\.$", comment)
        if m:
            self.locals[m.group(1)] = int(m.group(2))

    def _parse_line(self, line):
        # Labels glue onto whatever follows them ("L4:cmp ...",
        # "L8:L6:mov ...", "L2:| _a=-12.").
        while True:
            m = re.match(r"^(L\d+|_\w+):", line)
            if not m:
                break
            self.labels[m.group(1)] = len(self.prog)
            line = line[m.end():]
        if not line:
            return
        if line.startswith("|"):
            self._note_local(line)
            return
        if line.startswith("."):
            return                  # .globl/.text/.even/.data: no effect
        mnem, _, rest = line.partition("\t")
        ops = rest.split(",") if rest else []
        if mnem == "pop cx":        # c1's literal-space quirk
            mnem, ops = "pop", ["cx"]
        self.prog.append((mnem, ops))

    # -- operands -------------------------------------------------------
    def _rd(self, a):
        a &= M16
        return self.mem.get(a, 0) | (self.mem.get((a + 1) & M16, 0) << 8)

    def _wr(self, a, v):
        a &= M16
        self.mem[a] = v & 0xFF
        self.mem[(a + 1) & M16] = (v >> 8) & 0xFF

    def _ea(self, op):
        m = re.match(r"^(?:[*#](-?\d+)\.)?\((\w+)\)$", op)
        if not m or m.group(2) not in self.regs:
            return None
        disp = int(m.group(1)) if m.group(1) else 0
        return (self.regs[m.group(2)] + disp) & M16

    def get(self, op):
        if op in self.regs:
            return self.regs[op]
        if op == "cl":
            return self.regs["cx"] & 0xFF
        m = re.match(r"^[*#](-?\d+)\.?$", op)
        if m:
            return int(m.group(1)) & M16
        m = re.match(r"^[*#]/([0-9a-f]+)$", op)
        if m:
            return int(m.group(1), 16) & M16
        a = self._ea(op)
        if a is not None:
            return self._rd(a)
        raise SimError(f"operand '{op}' not supported")

    def put(self, op, v):
        if op in self.regs:
            self.regs[op] = v & M16
            return
        a = self._ea(op)
        if a is None:
            raise SimError(f"destination '{op}' not supported")
        self._wr(a, v & M16)
        if self.watch in self.locals and \
                a == (self.regs["bp"] + self.locals[self.watch]) & M16:
            self.snapshots.append((s16(v), Result(None, self.regs["bp"],
                                                  dict(self.locals), dict(self.mem))))

    # Byte operands ("movb"): mutos_as spells a byte register by its word
    # register's name - "movb ax,*-84.(bp)" loads AL, "movb *-6.(bp),dx"
    # stores DL - so a register operand means its LOW byte, and the high
    # byte of a register destination is left as it was (8086 MOV AL,..).
    BYTE_REGS = ("ax", "bx", "cx", "dx")

    def get_byte(self, op):
        if op in self.BYTE_REGS:
            return self.regs[op] & 0xFF
        if op in self.regs:
            raise SimError(f"byte operand '{op}' has no low byte")
        m = re.match(r"^[*#](-?\d+)\.?$", op)
        if m:
            return int(m.group(1)) & 0xFF
        a = self._ea(op)
        if a is not None:
            return self.mem.get(a, 0)
        raise SimError(f"byte operand '{op}' not supported")

    def put_byte(self, op, v):
        if op in self.BYTE_REGS:
            self.regs[op] = (self.regs[op] & 0xFF00) | (v & 0xFF)
            return
        a = self._ea(op)
        if a is None:
            raise SimError(f"byte destination '{op}' not supported")
        self.mem[a] = v & 0xFF
        if self.watch in self.locals and \
                a == (self.regs["bp"] + self.locals[self.watch]) & M16:
            self.snapshots.append((s16(self._rd(a)), Result(None, self.regs["bp"],
                                                            dict(self.locals), dict(self.mem))))

    # -- execution ------------------------------------------------------
    def run(self):
        if "_main" not in self.labels:
            raise SimError("no _main")
        pc = self.labels["_main"]
        depth = 0                   # calls in progress (see "call")
        for _ in range(STEP_LIMIT):
            if pc >= len(self.prog):
                raise SimError("ran off the end of the code")
            mnem, ops = self.prog[pc]
            pc += 1
            if mnem in FLAG_WRITERS:
                self.cmp = None
            if mnem == "jmp":
                if ops[0] == "cret" and depth > 0:
                    # cret (docs/MUTOS_C_ABI.md): lea sp,-4(bp) / pop si /
                    # pop di / pop bp / ret
                    self.regs["sp"] = (self.regs["bp"] - 4) & M16
                    for r in ("si", "di", "bp"):
                        self.regs[r] = self._rd(self.regs["sp"])
                        self.regs["sp"] = (self.regs["sp"] + 2) & M16
                    pc = self._rd(self.regs["sp"])
                    self.regs["sp"] = (self.regs["sp"] + 2) & M16
                    depth -= 1
                    self.cmp = None
                    continue
                if ops[0] == "cret":
                    return Result(s16(self.regs["ax"]), self.regs["bp"],
                                  dict(self.locals), self.mem, self.snapshots)
                if ops[0] not in self.labels:
                    raise SimError(f"jump target '{ops[0]}' not supported")
                pc = self.labels[ops[0]]
            elif mnem in BRANCHES:
                if self.cmp is None:
                    raise SimError(f"'{mnem}' on flags not set by a cmp "
                                   "or an 'or r,r'")
                a, b = self.cmp
                take = {"blt": s16(a) < s16(b), "ble": s16(a) <= s16(b),
                        "bgt": s16(a) > s16(b), "bge": s16(a) >= s16(b),
                        "beq": a == b, "bne": a != b,
                        "blos": a <= b, "bhi": a > b}[mnem]
                if take:
                    pc = self.labels[ops[0]]
            elif mnem == "cmp":
                self.cmp = (self.get(ops[0]), self.get(ops[1]))
            elif mnem == "mov":
                self.put(ops[0], self.get(ops[1]))
            elif mnem == "lea":
                a = self._ea(ops[1])
                if a is None:
                    raise SimError(f"lea operand '{ops[1]}' not supported")
                self.put(ops[0], a)
            elif mnem in ("add", "sub", "and", "or", "xor"):
                a, b = self.get(ops[0]), self.get(ops[1])
                self.put(ops[0], {"add": a + b, "sub": a - b, "and": a & b,
                                  "or": a | b, "xor": a ^ b}[mnem])
                if mnem == "or" and ops[0] == ops[1] and ops[0] in self.regs:
                    self.cmp = (a, 0)
            elif mnem == "inc":
                self.put(ops[0], self.get(ops[0]) + 1)
            elif mnem == "dec":
                self.put(ops[0], self.get(ops[0]) - 1)
            elif mnem == "not":
                self.put(ops[0], ~self.get(ops[0]))
            elif mnem in ("sal", "shl"):
                self.put(ops[0], self.get(ops[0]) << (self.get(ops[1]) & 0x1F))
            elif mnem == "sar":
                self.put(ops[0], s16(self.get(ops[0])) >> (self.get(ops[1]) & 0x1F))
            elif mnem == "imul":
                prod = s16(self.regs["ax"]) * s16(self.get(ops[0]))
                self.regs["ax"] = prod & M16
                self.regs["dx"] = (prod >> 16) & M16
            elif mnem == "idiv":
                d = s16(self.get(ops[0]))
                n = (self.regs["dx"] << 16) | self.regs["ax"]
                if n & 0x80000000:
                    n -= 1 << 32
                if d == 0:
                    raise SimError("division by zero")
                q = abs(n) // abs(d) * (1 if (n < 0) == (d < 0) else -1)
                if not -0x8000 <= q <= 0x7FFF:
                    raise SimError("division overflow")
                self.regs["ax"] = q & M16
                self.regs["dx"] = (n - q * d) & M16
            elif mnem == "call" and ops[0] == "chkstk":
                # The large-frame allocation helper (docs/MUTOS_C_ABI.md
                # sect. 1.9): sp -= ax, as "sub sp,ax" would, returning
                # with the return address left in ax.
                self.regs["sp"] = (self.regs["sp"] - self.regs["ax"]) & M16
                self.regs["ax"] = pc
            elif mnem == "call":
                # A call of a function in the same file only (a libc or
                # indirect call has no code here to run).
                if ops[0] not in self.labels:
                    raise SimError(f"call target '{ops[0]}' not supported")
                self.regs["sp"] = (self.regs["sp"] - 2) & M16
                self._wr(self.regs["sp"], pc)
                pc = self.labels[ops[0]]
                depth += 1
                self.cmp = None
            elif mnem == "movb":
                self.put_byte(ops[0], self.get_byte(ops[1]))
            elif mnem == "cbw":
                al = self.regs["ax"] & 0xFF
                self.regs["ax"] = (al | 0xFF00) if al & 0x80 else al
            elif mnem == "cwd":
                self.regs["dx"] = M16 if self.regs["ax"] & 0x8000 else 0
            elif mnem == "push":
                self.regs["sp"] = (self.regs["sp"] - 2) & M16
                self._wr(self.regs["sp"], self.get(ops[0]))
            elif mnem == "pop":
                v = self._rd(self.regs["sp"])
                self.regs["sp"] = (self.regs["sp"] + 2) & M16
                self.put(ops[0], v)
            else:
                raise SimError(f"mnemonic '{mnem}' not supported")
        raise SimError("step limit reached")


def run(text, watch=None):
    """Executes mutos_c1 output `text`; returns a Result. With `watch`
    (a local's name), every store to that local also records a snapshot
    of the whole frame - fuzz_c.py's per-statement markers."""
    return Sim(text, watch).run()


def main():
    if len(sys.argv) != 2:
        print(__doc__.split("Usage:")[1].split("As a module")[0], file=sys.stderr)
        return 2
    try:
        r = run(open(sys.argv[1]).read())
    except SimError as e:
        print(f"x86sim: {e}", file=sys.stderr)
        return 2
    print(f"ret={r.ret}")
    for name, off in sorted(r.locals.items(), key=lambda kv: kv[1]):
        print(f"{name}={off}")
    for off in range(-2, -1024, -2):
        a = (r.bp + off) & M16
        if a in r._mem or ((a + 1) & M16) in r._mem:
            print(f"w{off}={r.word(off)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:          # e.g. "x86sim.py f.s | head -1"
        sys.stderr.close()
        sys.exit(0)
