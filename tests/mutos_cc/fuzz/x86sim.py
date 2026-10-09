#!/usr/bin/env python3
"""x86sim.py - executes mutos_c1 output for a semantic check.

Runs the subset of mutos_as-syntax 8086 code that mutos_c1 emits - the
shape fuzz_c.py generates, a parameterless main(), plus calls of other
functions defined in the same file (with the shared "cret" epilogue and
the "chkstk" large-frame helper built in), "movb"/"cbw"/"cmpb"/"orb"
byte operations, variables at a fixed address (a local static's "L4:.blkb
2.", a file-scope "static" one's "_hidden:.blkb 2.", a common block's
".comm _counter,2" - each zero-initialized, referenced as "L4",
"_counter" or, for its address, "#_counter") and initialized bytes (a
char array's "_text:.byte /74,..." or a string literal's "L4:.byte ...",
its elements addressed "_text(bx)" or "#_text(bx)") - and reports the
final state: main()'s return value (AX at main's "jmp cret") and every
local variable's word(s), located through c1's own "| _name=-N." frame
comments. Floating-point code runs against a model of libc.a's software
floating-point runtime (the calls mutos_c1 emits - "flds"/"fldd",
"fstsp"/"fstdp", "fsts"/"fstd", "fadds"..."fdivd", stkmath.o's
"fadd"..."fdiv", "fneg", "fdup" and "fcmp" (read by "sahf" and a signed
branch), "itof", "ftoi", "ftol", "ltof" - on a stack of host floats, see
FP_RUNTIME; popping an empty stack is an error, as the real compiler's
own code for an assignment passed as a floating argument does it -
fltprobe/p21_fltexp),
dmath.o's "fac" (a double function's result) and ".float" data, in
MUTOS's own floating format (see mbf_encode()), and the 'long' helpers
"lmul", "ldiv", "lrem", "almul", "aldiv" and "alrem" (see LONG_RUNTIME),
a long shifted a
bit at a time ("sal si,*1" / "rcl di,*1", "sar di,*1" / "rcr si,*1"),
also CX times ("loop .-4" back over the pair) or by a variable count
("or cx,cx" / "jz .+8" past the pair and the loop when CX is 0), and
"neg". It is a checker, not an emulator: anything outside the subset
(a libc or indirect call, a branch on flags not set by a cmp, a cmpb, an
"and"/"or"/"xor" or an "orb r,r", ...) stops it with exit status 2 and a
message, never with a guess.

Validated against real hardware-compiled output: every tests/mutos_cc
.s.golden it can execute (56 of the 62 - the rest call libc, or use a
jump table or a function's address) returns the value its C source
computes, among them 02_long/02_muldiv (24), 08_float/01_floatbas
(7) and 02_dblconv (3), 01_expr/06_compasgn
(33), 04_funcs/05_staticvar (3), 07_scope/01_globstat (3) and
03_externdef (12),
03_ctrlflow/05_breakcont (12), 04_funcs/03_recfact (720), 05_arrptr/
02_array2d (138), 09_abiprobe/03_frame128 (3, through chkstk),
10_integ/01_wordcount (55 - "cmpb _text(bx),*10.", "orb dx,dx"),
02_bubsort (91) and 05_matmul (134), and all nine of 06_struct - among
them 05_nestst (40), 06_union (3) and 07_bitfield (12: "and *-6.(bp),
#/ff0f", "sar si,cl").

Usage:
    x86sim.py file.s        prints "ret=<n>", then "<name>=<off>" per
                            local, "<sym>@<addr>=<n>" per fixed-address
                            variable and "w<off>=<n>" per frame word
As a module:
    r = x86sim.run(text)    -> Result (r.ret, r.locals, r.word(off))
"""
import re
import sys

M16 = 0xFFFF
SP0 = 0xF000          # initial stack pointer; the frame lives below it
DATA0 = 0x1000        # where fixed-address variables (.blkb, .comm) start
STEP_LIMIT = 200000   # generated programs have no loops; goldens do

# Instructions that change the flags. A conditional branch is only
# accepted while the flags still come from the most recent cmp - or
# from a logical operation, "and"/"or"/"xor", which clears CF and OF and
# sets ZF/SF from its result, exactly the flags of "cmp <result>,0"
# ("or r,r" - same register twice - is mutos_c1's truth test of a
# register value; "and dx,*12." / "beq" an AND tested directly) - or from
# the byte forms, "cmpb" and "orb dx,dx" (a char loaded into DL and
# tested).
FLAG_WRITERS = {"add", "sub", "adc", "sbb", "and", "or", "xor", "inc",
                "dec", "sal", "shl", "sar", "imul", "idiv", "neg", "orb",
                "rcl", "rcr"}
BRANCHES = {"blt", "ble", "bgt", "bge", "beq", "bne", "blos", "bhi", "blo",
            "bhis"}


class SimError(Exception):
    """Code outside the supported subset (not a wrong-code verdict)."""


# MUTOS 1700's floating format, as the real toolchain's own data shows it
# (tests/mutos1700_libc: atof.o's 2**56 is the bytes 00 00 00 b9, ecvt.o's
# 10.0 is 00 00 20 84, 1.0 is 00 00 00 81) and the runtime handles it
# (stacks.o's flds widens a float by zero-filling the double's LOW four
# bytes, convert.o's itof builds the exponent in the top byte): the value
# 0.1mmm... (binary) * 2**(e - 128), the excess-128 exponent e in the
# highest byte, the sign in the top bit of the byte below it, then the
# mantissa bits after its leading 1 (not stored) - 23 of them in a 4-byte
# float, 55 in an 8-byte double; e == 0 is zero. A float stored from the
# runtime's stack keeps the double's high four bytes (stacks.o's fstsp),
# i.e. its mantissa is truncated, not rounded.
def mbf_encode(v, nbytes):
    if v == 0:
        return [0] * nbytes
    import math
    f, e = math.frexp(abs(v))           # abs(v) = f * 2**e, f in [0.5, 1)
    if not 1 <= e + 128 <= 255:
        raise SimError(f"floating value {v!r} out of range")
    mbits = 8 * nbytes - 9              # stored mantissa bits
    m = int(f * (1 << (mbits + 1)))     # truncated, leading 1 included
    word = (m & ((1 << mbits) - 1)) | ((1 << mbits) if v < 0 else 0) \
        | ((e + 128) << (mbits + 1))
    return [(word >> (8 * i)) & 0xFF for i in range(nbytes)]


def mbf_decode(bs):
    nbytes = len(bs)
    word = sum(b << (8 * i) for i, b in enumerate(bs))
    mbits = 8 * nbytes - 9
    e = word >> (mbits + 1)
    if e == 0:
        return 0.0
    m = (word & ((1 << mbits) - 1)) | (1 << mbits)
    v = m / float(1 << (mbits + 1)) * 2.0 ** (e - 128)
    return -v if word & (1 << mbits) else v


# The software floating-point runtime's entry points mutos_c1 calls: each
# takes a memory operand's ADDRESS in AX (or an int in AX) and works on a
# stack of doubles - here host floats (53-bit, not the runtime's 56-bit
# mantissa: the values checked are exact either way). name -> (kind, size)
FP_RUNTIME = {
    "flds": ("load", 4), "fldd": ("load", 8),
    "fstsp": ("store", 4), "fstdp": ("store", 8),
    "fsts": ("keep", 4), "fstd": ("keep", 8),     # store, no pop (stacks.o)
    "fadds": ("+", 4), "faddd": ("+", 8), "fsubs": ("-", 4), "fsubd": ("-", 8),
    "fmuls": ("*", 4), "fmuld": ("*", 8), "fdivs": ("/", 4), "fdivd": ("/", 8),
    # stkmath.o: the top two entries - second op top - into one
    "fadd": ("s+", 0), "fsub": ("s-", 0), "fmul": ("s*", 0), "fdiv": ("s/", 0),
    "fneg": ("neg", 0),                            # the top, sign flipped
    "fdup": ("dup", 0),                            # the top, pushed again
    "fcmp": ("cmp", 0),                            # pops both, AH <- flags
    "itof": ("itof", 0), "ftoi": ("ftoi", 0), "ftol": ("ftol", 0),
    # lconvert.o: the long on the machine stack, high word on top
    "ltof": ("ltof", 0),
}


# libc.a's 'long' helpers mutos_c1 calls (lmul.o, ldiv.o, lrem.o, almul.o,
# aldiv.o, alrem.o): their operands on the machine stack - each long pushed
# low word first, the right operand first, so at the call [sp] is the left
# operand's high word - and the result in DX:AX; "almul", "aldiv" and
# "alrem" (a long variable *=, /=, %= a long) take the target's address on
# top and the right operand below it and compute into the target in place.
# C semantics: products wrap at 32 bits, quotients truncate toward zero.
LONG_RUNTIME = {"lmul", "ldiv", "lrem", "almul", "aldiv", "alrem"}


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def fp_arith(op, a, b):
    """a <op> b for one of "+-*/" - evaluated lazily (a table of all four
    results would divide by a zero right operand of "*")."""
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    return a / b


def s16(v):
    v &= M16
    return v - 0x10000 if v & 0x8000 else v


def sx8(v):
    """A byte sign-extended to a 16-bit word (as CBW does)."""
    v &= 0xFF
    return (v | 0xFF00) if v & 0x80 else v


class Result:
    def __init__(self, ret, bp, locals_, mem, snapshots=(), data=None):
        self.ret = ret
        self.bp = bp
        self.locals = locals_       # name -> bp-relative offset
        self.data = data or {}      # fixed-address variable -> address
        self._mem = mem
        # (value written, Result of that moment) per write to the watched
        # local - see run()'s `watch`
        self.snapshots = list(snapshots)

    def word(self, off):
        """Signed word at bp+off."""
        return self.word_at(self.bp + off)

    def word_at(self, addr):
        """Signed word at an absolute address (see `data`)."""
        a = addr & M16
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
        self.carry = None          # CF of the instruction just executed,
        self.carry_next = None     # when an add/sub (see "adc"/"sbb")
        self.prog = []             # (mnemonic, [operands])
        self.labels = {}
        self.locals = {}
        self.data = {}             # fixed-address variable -> address
        self.next_data = DATA0
        self.fstack = []           # the floating-point runtime's stack
        self.fflags = None         # sign of fcmp's difference, for sahf
        for raw in text.splitlines():
            self._parse_line(raw)

    def _note_local(self, comment):
        m = re.match(r"\| _(\w+)=(-?\d+)\.$", comment)
        if m:
            self.locals[m.group(1)] = int(m.group(2))

    def _alloc(self, name, size):
        """A zero-initialized variable at a fixed address (first
        definition wins - ".comm" may repeat)."""
        if name not in self.data:
            self.data[name] = self.next_data
            self.next_data += (size + 1) & ~1

    def _put_bytes(self, names, values):
        """Initialized data - a ".byte" line: its labels name the next
        free address, and its values are laid down there. The lines of
        one object follow each other ("_text:.byte ..." then ".byte
        ..." - a string literal's or a char array's initializer, split
        9 values to a line), so consecutive ".byte" lines stay
        contiguous; ".even" rounds the next address up."""
        for name in names:
            self.data[name] = self.next_data
        for v in values:
            self.mem[self.next_data & M16] = v & 0xFF
            self.next_data += 1

    def _parse_line(self, line):
        # Labels glue onto whatever follows them ("L4:cmp ...",
        # "L8:L6:mov ...", "L2:| _a=-12.", "_hidden:.blkb\t2.").
        names = []
        while True:
            m = re.match(r"^(L\d+|_\w+):", line)
            if not m:
                break
            names.append(m.group(1))
            line = line[m.end():]
        m = re.match(r"^\.blkb\t(\d+)\.$", line)
        if m:                       # a static's own BSS block
            for name in names:
                self._alloc(name, int(m.group(1)))
            return
        m = re.match(r"^\.byte\t(/[0-9a-f]+(?:,/[0-9a-f]+)*)$", line)
        if m:                       # initialized bytes
            self._put_bytes(names, [int(v[1:], 16)
                                    for v in m.group(1).split(",")])
            return
        m = re.match(r"^\t\.(float|double)[ \t](\S+)$", line)
        if m:                       # a floating constant ("L10000:\t.float
            # ...", "L10000:\t.double ...") or a file-scope variable's
            # initializer ("_gi:\t.double\t2.5...")
            size = 4 if m.group(1) == "float" else 8
            self._put_bytes(names, mbf_encode(float(m.group(2)), size))
            return
        if line == ".even":
            self.next_data = (self.next_data + 1) & ~1
            return
        for name in names:
            self.labels[name] = len(self.prog)
        if not line:
            return
        if line.startswith("|"):
            self._note_local(line)
            return
        m = re.match(r"^\.comm\t(_\w+),(\d+)$", line)
        if m:                       # a file-scope common block
            self._alloc(m.group(1), int(m.group(2)))
            return
        if line.startswith("."):
            return                  # .globl/.text/.even/.data/.bss
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
        if op == "fac":
            # dmath.o's accumulator, where a function returning a double
            # leaves it ("lea ax,fac" / "call fstdp" / "lea ax,fac") - 8
            # bytes at a fixed address, made when first used
            self._alloc("fac", 8)
        if op in self.data:         # "L4", "_counter"
            return self.data[op]
        m = re.match(r"^(\d+)\.\+(_\w+|L\d+)$", op)
        if m and m.group(2) in self.data:
            # an element or member of a fixed-address object: "16.+_ga"
            return (self.data[m.group(2)] + int(m.group(1))) & M16
        # "(bx)", "*2.(si)", "#-132.(bp)", and an array element with a
        # symbol for its displacement: "_text(bx)", "#_text(bx)" (the
        # marker a load through v7's "#1" template writes)
        m = re.match(r"^(?:[*#](-?\d+)\.|#?(_\w+|L\d+))?\((\w+)\)$", op)
        if not m or m.group(3) not in self.regs:
            return None
        if m.group(2):
            if m.group(2) not in self.data:
                return None
            disp = self.data[m.group(2)]
        else:
            disp = int(m.group(1)) if m.group(1) else 0
        return (self.regs[m.group(3)] + disp) & M16

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
        if op.startswith("#") and op[1:] in self.data:
            return self.data[op[1:]]    # "#_counter": its address
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
                                                  dict(self.locals), dict(self.mem),
                                                  data=self.data)))

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
                                                            dict(self.locals), dict(self.mem),
                                                            data=self.data)))

    # -- the floating-point runtime ---------------------------------------
    def _fp_call(self, name):
        kind, size = FP_RUNTIME[name]
        ax = self.regs["ax"]
        mem = [self.mem.get((ax + i) & M16, 0) for i in range(size)]
        st = self.fstack
        if kind not in ("load", "itof", "ltof") and not st:
            raise SimError(f"'{name}' with an empty floating-point stack")
        if kind in ("s+", "s-", "s*", "s/", "cmp") and len(st) < 2:
            raise SimError(f"'{name}' with fewer than two stack entries")
        fflags = None
        if kind == "load":
            st.append(mbf_decode(mem))
        elif kind in ("store", "keep"):
            v = st.pop() if kind == "store" else st[-1]
            for i, b in enumerate(mbf_encode(v, size)):
                self.mem[(ax + i) & M16] = b
        elif kind in ("s+", "s-", "s*", "s/"):
            b = st.pop()
            a = st.pop()
            if kind == "s/" and b == 0:
                raise SimError("floating division by zero")
            st.append(fp_arith(kind[1], a, b))
        elif kind == "neg":
            st[-1] = -st[-1]
        elif kind == "dup":
            st.append(st[-1])
        elif kind == "cmp":
            b = st.pop()
            a = st.pop()
            fflags = (a > b) - (a < b)
        elif kind == "itof":
            st.append(float(s16(ax)))
        elif kind == "ltof":
            sp = self.regs["sp"]
            hi = self._rd(sp)
            lo = self._rd((sp + 2) & M16)
            v = (hi << 16) | lo
            st.append(float(v - (1 << 32) if v & 0x80000000 else v))
        elif kind in ("ftoi", "ftol"):
            v = int(st.pop())                  # truncated toward zero
            lim = 1 << (15 if kind == "ftoi" else 31)
            if not -lim <= v < lim:
                raise SimError(f"'{name}' overflow")
        else:
            b = mbf_decode(mem)
            if kind == "/" and b == 0:
                raise SimError("floating division by zero")
            a = st.pop()
            st.append(fp_arith(kind, a, b))
        # The runtime returns through cret: DI/SI/BP survive, AX, BX, CX
        # and DX do not - poisoned, so code relying on them shows up.
        for r in ("ax", "bx", "cx", "dx"):
            self.regs[r] = 0xDEAD
        # fcmp leaves the flags of the difference in AH, for "sahf" only
        self.fflags = fflags
        if kind == "ftoi":
            self.regs["ax"] = v & M16
        elif kind == "ftol":
            self.regs["ax"] = v & M16
            self.regs["dx"] = (v >> 16) & M16

    # -- the 'long' runtime ---------------------------------------------
    def _long_at(self, addr):
        """The long at `addr`, high word first (docs/MUTOS_C_ABI.md
        sect. 1.6)."""
        return s32((self._rd(addr) << 16) | self._rd((addr + 2) & M16))

    def _long_div(self, name, a, b):
        if b == 0:
            raise SimError("'long' division by zero")
        q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
        return s32(q if name in ("ldiv", "aldiv") else a - q * b)

    def _long_call(self, name):
        sp = self.regs["sp"]
        if name in ("almul", "aldiv", "alrem"):
            target = self._rd(sp)
            r = self._long_at((sp + 2) & M16)
            t = self._long_at(target)
            v = s32(t * r) if name == "almul" else self._long_div(name, t, r)
            self._wr(target, (v >> 16) & M16)
            self._wr((target + 2) & M16, v & M16)
        else:
            a = self._long_at(sp)
            b = self._long_at((sp + 4) & M16)
            if name == "lmul":
                v = s32(a * b)
            else:
                v = self._long_div(name, a, b)
        for r in ("bx", "cx"):
            self.regs[r] = 0xDEAD
        self.regs["ax"] = v & M16
        self.regs["dx"] = (v >> 16) & M16

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
            # CF of an "add"/"sub", for the "adc"/"sbb" right after it only
            self.carry = self.carry_next
            self.carry_next = None
            if mnem != "sahf" and not (mnem == "call" and ops[0] == "fcmp"):
                self.fflags = None
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
                                  dict(self.locals), self.mem, self.snapshots,
                                  data=self.data)
                if ops[0] not in self.labels:
                    raise SimError(f"jump target '{ops[0]}' not supported")
                pc = self.labels[ops[0]]
            elif mnem in BRANCHES:
                if self.cmp is None:
                    raise SimError(f"'{mnem}' on flags not set by a cmp, a "
                                   "cmpb, an and/or/xor or an 'orb r,r'")
                a, b = self.cmp
                take = {"blt": s16(a) < s16(b), "ble": s16(a) <= s16(b),
                        "bgt": s16(a) > s16(b), "bge": s16(a) >= s16(b),
                        "beq": a == b, "bne": a != b,
                        "blos": a <= b, "bhi": a > b,
                        "blo": a < b, "bhis": a >= b}[mnem]
                if take:
                    pc = self.labels[ops[0]]
            elif mnem == "sahf":
                # A floating comparison's flags (stkmath.o's fcmp: SF/ZF
                # of left - right, OF clear), read by the signed branches
                # exactly like "cmp <sign>,0"
                if self.fflags is None:
                    raise SimError("'sahf' not right after 'call fcmp'")
                self.cmp = (self.fflags & M16, 0)
                self.fflags = None
            elif mnem == "cmp":
                self.cmp = (self.get(ops[0]), self.get(ops[1]))
            elif mnem == "cmpb":
                # A byte compare: both bytes sign-extended, which keeps
                # the signed order and (0x80.. -> 0xff80..) the unsigned
                # one too, so the branches read it like a word "cmp".
                self.cmp = (sx8(self.get_byte(ops[0])), sx8(self.get_byte(ops[1])))
            elif mnem == "orb":
                a, b = self.get_byte(ops[0]), self.get_byte(ops[1])
                self.put_byte(ops[0], a | b)
                if ops[0] == ops[1] and ops[0] in self.BYTE_REGS:
                    # "orb dx,dx": mutos_c1's test of a byte loaded into DL
                    # - OF/CF cleared, SF/ZF from the byte: "cmpb dl,0"
                    self.cmp = (sx8(a), 0)
            elif mnem == "mov":
                self.put(ops[0], self.get(ops[1]))
            elif mnem == "lea":
                a = self._ea(ops[1])
                if a is None:
                    raise SimError(f"lea operand '{ops[1]}' not supported")
                self.put(ops[0], a)
            elif mnem in ("adc", "sbb"):
                # The high word of a 'long' '+'/'-': the carry or borrow
                # of the "add"/"sub" right before it (nothing else may
                # come between - a long's two halves are always adjacent
                # in mutos_c1's output and the real compiler's)
                if self.carry is None:
                    raise SimError(f"'{mnem}' not right after an add/sub")
                a, b = self.get(ops[0]), self.get(ops[1])
                res = a + b + self.carry if mnem == "adc" else a - b - self.carry
                self.put(ops[0], res & M16)
                self.carry = None
            elif mnem in ("add", "sub", "and", "or", "xor"):
                a, b = self.get(ops[0]), self.get(ops[1])
                res = {"add": a + b, "sub": a - b, "and": a & b,
                       "or": a | b, "xor": a ^ b}[mnem]
                if mnem in ("add", "sub"):
                    self.carry_next = 1 if res & ~M16 else 0
                res &= M16
                self.put(ops[0], res)
                if mnem in ("and", "or", "xor"):
                    # OF/CF cleared, SF/ZF from the result: exactly the
                    # flags of "cmp <result>,0" - "or r,r" (mutos_c1's truth
                    # test of a register) and an AND tested directly ("and
                    # dx,*12." / "beq" - a masked char, see Val's flagsv in
                    # c1_gen.c)
                    self.cmp = (res, 0)
            elif mnem == "inc":
                self.put(ops[0], self.get(ops[0]) + 1)
            elif mnem == "dec":
                self.put(ops[0], self.get(ops[0]) - 1)
            elif mnem == "not":
                self.put(ops[0], ~self.get(ops[0]))
            elif mnem == "neg":
                a = self.get(ops[0])
                self.put(ops[0], -a)
                # CF set unless the operand was 0 - for a 'long' negated,
                # "neg di" / "neg si" / "sbb di,*0" (fltprobe/p25_long3)
                self.carry_next = 1 if a & M16 else 0
            elif mnem in ("sal", "shl"):
                a, n = self.get(ops[0]), self.get(ops[1]) & 0x1F
                self.put(ops[0], a << n)
                if n == 1:              # CF, for an "rcl" right after it
                    self.carry_next = (a >> 15) & 1
            elif mnem == "sar":
                a, n = self.get(ops[0]), self.get(ops[1]) & 0x1F
                self.put(ops[0], s16(a) >> n)
                if n == 1:              # CF, for an "rcr" right after it
                    self.carry_next = a & 1
            elif mnem == "jz":
                # "jz .+8": past a long shift's one-bit pair and its "loop
                # .-4" (two bytes each) when the count in CX is 0 - the only
                # use (fltprobe/p31_long4.s.golden's "l << i": "mov cx,
                # *-14.(bp)" / "or cx,cx" / "jz .+8" / "sal si,*1" / "rcl
                # di,*1" / "loop .-4")
                nxt = [self.prog[k] if 0 <= k < len(self.prog) else None
                       for k in (pc, pc + 1, pc + 2)]
                if ops != [".+8"] or None in nxt or nxt[2][0] != "loop" or \
                        self.cmp is None or self.cmp[1] != 0:
                    raise SimError("'jz' other than past a long shift's loop "
                                   "after 'or cx,cx'")
                if self.cmp[0] == 0:
                    pc += 3
            elif mnem == "loop":
                # "loop .-4": back over the two one-bit shifts before it,
                # two bytes each - a 'long' shifted CX times, the only use
                # (fltprobe/p25_long3.s.golden's "mov cx,*3." / "sal si,*1"
                # / "rcl di,*1" / "loop .-4")
                pair = [self.prog[k] if 0 <= k < len(self.prog) else None
                        for k in (pc - 3, pc - 2)]
                if ops != [".-4"] or None in pair or \
                        (pair[0][0], pair[1][0]) not in (("sal", "rcl"),
                                                         ("sar", "rcr")) or \
                        pair[0][1][1] != "*1" or pair[1][1][1] != "*1":
                    raise SimError("'loop' other than back over a one-bit "
                                   "long shift pair")
                self.regs["cx"] = (self.regs["cx"] - 1) & M16
                if self.regs["cx"] != 0:
                    pc -= 3
            elif mnem in ("rcl", "rcr"):
                # The other word of a 'long' shifted by one: through the
                # carry of the "sal"/"sar" right before it
                if self.carry is None or self.get(ops[1]) != 1:
                    raise SimError(f"'{mnem}' not right after a one-bit "
                                   "sal/sar")
                a = self.get(ops[0])
                if mnem == "rcl":
                    res = ((a << 1) | self.carry) & M16
                    self.carry_next = (a >> 15) & 1
                else:
                    res = (a >> 1) | (self.carry << 15)
                    self.carry_next = a & 1
                self.put(ops[0], res)
                self.carry = None
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
            elif mnem == "call" and ops[0] in FP_RUNTIME:
                self._fp_call(ops[0])
                self.cmp = None
            elif mnem == "call" and ops[0] in LONG_RUNTIME:
                self._long_call(ops[0])
                self.cmp = None
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
    for name, addr in sorted(r.data.items(), key=lambda kv: kv[1]):
        print(f"{name}@{addr:#06x}={r.word_at(addr)}")
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
