# `mutos_c1` as a port of the real `c1` — plan

Written 2026-10-12, after reading the original binary
(`mutos_bin/c1.golden.base64.txt`; findings in `docs/devlog/originals_and_ports.md`'s "The
original binaries"). This is the plan for step 3 of `STATUS.md`'s "Next
up" item 1: replace `src/mutos_cc/c1_gen.c` - a list of shapes inferred
from goldens - by a port of the mechanism that produced the goldens.

**Status (2026-10-13): done - `mutos_c1` is the port**, steps A to E
below all carried out (section 8 says how they went); `c1_gen.c` is gone.

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
part of this plan. (Written 2026-10-12. Since then `c0` is ported -
`docs/C0_PORT_PLAN.md` -, and `c2` is step 4 of `docs/TOOLCHAIN_PLAN.md`;
the emulated original `c2` gives nine of `kernel_opt/`'s sources from the
`11_kernel` goldens byte for byte.)

## 8. How it went (2026-10-13)

All five steps were done in one session, in the order of section 4; the
sources are `src/mutos_cc/c1port/` (`c10.c`, `c11.c`, `c12.c` in v7's
file split, `c1.h`, `c1tab.c` generated, `c1lib.c` for the library
routines), every function's status in `tests/realbin/c1map.txt`.

| Step | Result |
|---|---|
| A | depth 1: 132/132 |
| B | depth 2: 132/132 |
| C | depth 3 and 4: 131/132 identical, `p19_open3` refused where `invalid_goldens.txt` says - after 228 identical lines, at the register name outside `regname[]` (rule 5.2); round 14 included |
| D | differential fuzzing at depth 4 (`tests/realbin/c1fuzz.py`, three generators - its own over every type, `fuzz_c.py`'s, and long constants at the edges of `lconst()`'s library paths): no difference left (numbers in `STATUS.md`); ASan/UBSan builds clean on every golden at every depth and on the fuzz corpus; the nine kernel files at depth 4 |
| E | `mutos_c1` built from `c1port/`; `c1_gen.c`, `c1_main.c`, `c1_stream.c` removed; `make test` clean; `make check-realbin` checks depths 2, 3 and 4 on every golden and a fixed-seed fuzz run; `fuzz_c.py`'s default oracle is the emulated original (`x86sim.py` stays as the semantic check, its 8086 flags now complete) |

The questions of section 5:

1. **`%c` of 0** writes nothing: `printf("[%c%d.]", 0, 160)` in a program
   built with the real toolchain gives `[160.]` under the emulator. The
   port's `pnum0()`.
2. **The register-name overrun** stays refused (`rname()`), after
   everything before it - `invalid_goldens.txt` now says 228 lines, not
   `c1_gen.c`'s 223 (it refused at the statement's start).
3. **Uninitialized memory** shows in exactly one place: a STRASG's
   degree, which `getree()` never sets and only the `b`/`a` traces print
   (`06_struct/04_stassign` at depth 3; `c1trees.py` compares it as `#`).
   Nothing in any `.s` depends on it.
4. **Overflow during reading**: the first `atof` overflow gives the
   message and the `longjmp()`; the handler is not re-armed and v7's
   kernel resets a caught signal, so a second one kills `c1`, leaving the
   `.s` cut to whole 512-byte buffers - the port does the same and exits
   with 128 + SIGFPE (`mutosemu.py` now delivers signals, which settled
   it).

What the binary held beyond v7, and the port keeps (every bug of the
original among it is in `docs/MUTOS_ORIGINAL_BUGS.md`, with a trigger and
a fix idea):

- **Library routines that are not C.** `libc.a`'s `itod` writes the low
  mantissa byte into bytes 1 and 3 of the double (an int with more than
  eight significant bits becomes a double, not a float, constant); its
  `aldiv` divides by the low word alone when the high word is 0 and the
  low word positive - or 1 and the low word negative (it negates the
  sign word -1 to 1 before comparing) -, and otherwise does sixteen
  shift-and-subtract steps and clears the quotient's high word, so
  `0x7fffffffL / 0177777` folds to -65535; `alrem`'s long path is the
  same sixteen steps. Both are re-enacted on 16-bit words in `c1lib.c`. The fuzzer found the second:
  a whole generator for long constants (`c1fuzz.py --gen lconst`) took
  the old port from 232 of 300 programs different to none.
- **The original's own faults, refused rather than modelled:** a long
  shift folded by `lconst()` with a count of 0 jumps into the middle of
  an instruction (`>>` tests the count's high word, `<<` its low word);
  `xlongrel()` reading past `lrtab[]` with a stale operand; `ltof` of 0,
  whose low six bytes are left over in the floating-point stack; `dtoi`
  from 2\*\*15 (it compares with the word at address 8000) and the
  overflows of `dtoi`/`ftol`; a register name outside `regname[]`. Each
  stops the port with exit status 2 and "refused" (`c1trap()`); the
  fuzzer checks that what was written before is the original's, and
  that where the original does not finish at all, the port refused.
- **The original's wrong code**, which a faithful port has to keep: code
  table rows that still assume PDP-11 condition codes (`com` and `mov`
  set them, the 8086's `not` and `mov` do not - `~x || y` branches on
  stale flags), and v7's `acommute()` returning the 0 of `e * 0` without
  `e`'s side effects. `fuzz_c.py` finds these with `x86sim.py` and counts
  them apart, since `mutos_c1` must produce them.
- **Messages and status as the original's**: `printf`'s zero as
  unsigned, the floating-point stack model's messages in both cases, a
  second SIGFPE's truncated file.

The method held: the binary decided every question, and the traces put
each difference at the function that made it.
