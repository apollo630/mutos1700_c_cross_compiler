# `mutos_c1` as a port of the real `c1` — plan

Written 2026-10-12, after reading the original binary
(`mutos_bin/c1.golden.base64.txt`; findings in `docs/DEVLOG.md`'s "The
original binaries"). This is the plan for step 3 of `STATUS.md`'s "Next
up" item 1: replace `src/mutos_cc/c1_gen.c` - a list of shapes inferred
from goldens - by a port of the mechanism that produced the goldens.

## 1. What is being ported

The real `c1` is v7's `c1` (`v7/cc/c10.c`..`c13.c` plus a code table)
ported to the 8086 by hand:

- **The same program.** 67 functions in v7's source order: the 62 of
  v7 minus `oddreg` and `psoct`, plus seven new ones (a SIGFPE handler,
  the `%c%d.` number printer and its size test, a tree printer for the
  debug traces, the FCON→SFCON test in the MUTOS floating format, a
  "register usable as a base" test, `werror`). Text 0x00b6..0x5b46 -
  23,184 bytes of unoptimized K&R compiler output - against 3,832 lines
  of v7 C. `tests/realbin/c1map.txt` lists every function with its text
  offset and how far it has been compared.
- **The same data structures.** `struct tnode` and friends, `opdope[]`,
  `opntab[]` (122 ops; new: 89 `*(x+c)`, 107/108 char<->long, 117..120
  unsigned `/` `%` `/=` `%=`, 121 char->int, 122/123 a floating `*(x+c)`
  and indirection, used only to look up a table), `instab`, `branchtab`,
  `isn` = 10000, `nreg` = 3.
- **The same tables, in v7's own encoding.** `regtab`, `efftab`, `cctab`,
  `sptab`: 496 rows, 292 template strings, compiled by v7's `cvopt` from a
  table source in v7's notation with MUTOS assembler text
  (`tests/realbin/c1_tables.txt` is that source, read back). Rules the
  probe rounds had to infer are single rows there: `%nw,nw` of `/` and `%` is
  "`SS`, `F`, `mov ax,R`, `T`, `pop cx`, `I cx`" - every computed divisor
  pushed first; `%nw,ew` of `+`/`-` is "`F`, `S1`, `I R,R1`" - v7's
  `%n,e` with the left operand in DI and the right one in SI.
- **The 8086 deltas**, local and readable: registers numbered ax=0 dx=1
  si=2 di=3 cx=4 bx=5 bp=6 sp=7 and allocated downwards (`reg1 = reg-1`,
  so a long is DI:SI or DX:AX); `cexpr()`'s macro interpreter extended
  (`<U>`/`<Q>`/`<R>` - the compile-time model of the floating-point stack
  and its messages -, push/pop counting, `R*` through BX, `F!`, `Fd`);
  `dcalc()`, `degree()`, `delay()`, `acommute()`, `optim()` and `match()`
  with small, listed changes; `rcexpr()`, `pname()`, `comarg()`,
  `doinit()`, `cbranch()`, `pswitch()`, `strasg()` rewritten for the
  8086 and the software floating-point runtime.

## 2. What makes it checkable

The binary runs (`tests/realbin/mutosemu.py`) and gives all 132 `.s`
goldens back, messages and exit status included. So the port is not
checked against goldens alone but against the original, for any input,
and at four depths - the original has the traces built in:

| Depth | Original's option | What it shows | What it checks in the port |
|---|---|---|---|
| 1 | `b` | every expression tree as read from `temp1` | `getree()`, `tnode()`, the tree layout |
| 2 | `a` | the same tree after `optim()` | the tree optimizer: `optim`, `unoptim`, `acommute`, `distrib`, `lvfield`, `const`, `lconst`, `insert`, `degree`, `hardlongs`, ... |
| 3 | `r`, `c`, `d` | every `rcexpr`/`cexpr`/`delay` call with its tree, the op of every line emitted, the register returned | `match`, `rcexpr`, `cexpr`, `reorder`, `delay`, the tables |
| 4 | none | the `.s`, the messages, the exit status | everything |

The port prints the same traces, from a port of the original's tree
printer, so each depth is a plain text comparison. Until the port emits
all of the code (step C below), the original's trace lines are mixed with
code the port cannot match yet; for depths 1 and 2 the comparison then
takes the trees out of the emulated memory instead - a hook on
`getree()`'s call of `optim()` (text 335f and 3367) walks each tree, and
the port writes its own trees in the same canonical form. Inputs, in growing
breadth: the 132 golden `.1`/`.2` pairs (the nine `11_kernel` ones among
them - the port can be checked on all nine kernel files before
`mutos_c0` compiles them), then `fuzz_c.py` programs (through `mutos_c0`,
or through the emulated real `c0` where `mutos_c0` refuses), thousands
per run.

## 3. Rules for the port

1. **The binary is the specification, v7's source the skeleton.** Each
   function is decompiled from the disassembly (`tests/realbin/c1dis.py`)
   with v7's text beside it; where they differ, the binary wins and the
   comment says so, with the text offset. `c1map.txt`'s status column is
   kept current (`unread` becomes `ported`).
2. **Tables are generated, never typed.** `c1tables.py` gets a C
   output mode; the generated file is checked in and `make
   check-realbin` checks it is current, as it does `c1_tables.txt`. The
   template strings stay byte for byte what the binary holds (0200 bits
   included); the port's `cexpr()` interprets them as the original does.
   `opdope[]`, `opntab[]`, `instab` and `branchtab` come out of the same
   generator.
3. **16-bit semantics explicit.** Every `int` of the original that can
   reach the output - constants folded by `const()`, offsets, label
   numbers, the stack and floating-stack counters - is an `int16_t`
   (`uint16_t` where the original is unsigned), `long` constants
   `int32_t`, with wrap-around where the original wraps. Tree nodes are
   host structures with host pointers; nothing in the output depends on
   addresses (to be confirmed - see 5.3).
4. **The original's library, where its output passes through it.**
   `printf` for the formats `c1` uses (`%s %d %u %o %x %c %.8s %.17e` -
   `%c` of 0 included, see 5.1); `atof` (`src/mutos_as/fltconst.c`) and
   `ecvt` (`src/mutos_cc/c1_fltdec.c`) as now; `fstsp`, `dtoi`, `itod`,
   `ltof`, `ftol` for `doinit()`'s conversions, each checked against
   `libc.a` under the emulator as `make check-libcatof` does.
5. **Built beside `c1_gen.c`, switched at parity.** The port is its own
   set of sources and its own (internal, not installed) binary until it
   passes depth 4 on every golden and the fuzz corpus; then `mutos_c1`
   is the port and `c1_gen.c` is deleted. No mixing of the two.

## 4. Order of work

| Step | Content | Done when |
|---|---|---|
| A | Header (v7's `c1.h` with the MUTOS ops and register numbers), the generated tables and op data, `getblk`, `tnode`/`tconst`, `getree`/`geti`/`outname`, the tree printer, `main` with the options, the SIGFPE handler | depth 1 identical on all 132 goldens |
| B | The tree optimizer (`c12.c`'s functions with the new FCON test, `degree`, `isfloat`, `ispow2`, `setype`/`decref`/`incref`) | depth 2 identical on all 132 goldens and on a fuzz corpus |
| C | Code generation: `match`, `rcexpr`, `cexpr`, `reorder`/`sreorder`, `delay`/`sdelay`/`ncopy`, `chkleaf`, `comarg`, `strfunc`, `doinit`, `movreg`, `pname`/`pbase` and the number printer, `prins`, `collcon`, `arlength`, `dcalc`/`xdcalc`/`notcompat` with the base-register test, `pow2`, `pswitch`/`breq`/`sort`, `cbranch`/`branch`/`longrel`/`xlongrel`, `label`/`popstk`, `strasg`, `error`/`werror` | depth 3 and 4 identical on all 132 goldens - round 14 (`p54`..`p57`) included |
| D | Breadth: differential fuzzing against the emulated original (depth 4, depth 2 for every difference found), the kernel files, ASan/UBSan builds | a fixed-seed corpus of several thousand programs without a difference |
| E | Switch: `mutos_c1` is the port, `c1_gen.c` and its README sections go, `fuzz_c.py`'s default oracle becomes the emulated original (`x86sim.py` stays as the semantic check), STATUS/DEVLOG/CLAUDE updated | `make test` and `make check-realbin` clean |

Step B before C because depth 2 isolates the optimizer, where most of
rounds 1..13's corrections came from; C is the largest step (`cexpr`
alone is 3,664 bytes of the binary).

## 5. Questions the oracle settles on the way

1. **`printf`'s `%c` with 0.** `cexpr`'s `#1`/`#2` macros print an
   indirection's offset outside -128..127 through `"%c%d."` with the
   character 0; no golden shows that case. One emulator run with such an
   offset settles what the real output holds.
2. **The register-name overrun** (`p19_open3`): the original reads a
   register name's pointer past the end of its table and prints the bytes
   that pointer happens to reach (the real `as` rejects them). The port
   keeps refusing at that point, as `invalid_goldens.txt` specifies,
   rather than modelling the data segment's layout.
3. **Uninitialized memory.** v7's `getblk()` hands out memory without
   clearing it, and the system presumably zeroes what a `break` adds
   (v7's kernel does). Run the original with that memory poisoned (an
   emulator option still to add): wherever its output changes, the port
   must zero what the original found zero.
4. **Overflow during reading.** An `atof` overflow in `getree()` raises
   SIGFPE; the original's handler prints "floating point over or under
   flow" and `longjmp`s back into `getree()`. The port reproduces the
   message and the resumption point.

## 6. Risks

- **Effort, not uncertainty.** All of the program is in the binary and
  the binary runs; what is left is reading 23 KB of straightforward
  compiler output carefully. The traces make a mistake visible at the
  function that made it.
- **Library behaviour** beyond what `c1` prints is irrelevant; what it
  prints is checked at depth 4.
- **The existing knowledge** in `c1_gen.c`, `src/mutos_cc/README.md` and
  the `fltprobe` sections of `DEVLOG.md` stays useful as test cases and
  explanations; none of it needs to be carried over as code.

## 7. Beyond `c1`

The same method applies to `c0` (v7's `c00.c`..`c05.c`; the real `c0`
already reproduces all 132 `.1`/`.2` goldens under the emulator, and
`mutos_c0` still refuses eight of the nine kernel files) and to `c2`
(Milestone 5: v7's `c20.c`/`c21.c`, the real `c2` in `mutos_bin/`, and
`tests/mutos_as/kernel_opt/`'s sources as `cc -O` output). Neither is
part of this plan.
