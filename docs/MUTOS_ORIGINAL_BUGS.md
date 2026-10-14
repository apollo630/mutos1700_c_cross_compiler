# Bugs of the original MUTOS 1700 tools

This file collects every bug found in the original MUTOS 1700 binaries -
`c0`, `c1`, `as`, `ld`, `cpp` and `libc.a` (and `c2` once Milestone 5 reads
it) - in one place, for the day this project offers to fix them (a
`mutos_*` option, say, that leaves the faithful default alone). Until then
the project's tools reproduce the original's behaviour wherever it can be
reproduced, bugs included, and refuse where it cannot (see each entry).
`CLAUDE.md`'s Workflow Guideline 9 makes keeping this file current part of
the change that finds a bug.

Each entry says what goes wrong (with a minimal trigger where one is
known), why (the mechanism, with the text or data offset in the binary as
`tests/realbin/c1map.txt` and `c1dis.py` give them), how it was
established, what this project's tool does with it, and an idea for a fix.
Evidence is one of: **golden** (real-hardware output in the tree),
**emulator** (the original run under `tests/realbin/mutosemu.py` -
reproducible with the commands in `tests/realbin/README.md`), **code
reading** (the disassembly, not observed in a run). "Reproduced" means the
project's tool gives the original's output byte for byte; "refused" that
it stops with a message and exit status 2 where the original's output
depends on state that is not modelled.

Last updated: 2026-10-14.

## Overview

| ID | Tool | Bug | Evidence | This project |
|---|---|---|---|---|
| C1-01 | `c1` | branches on stale flags after `not` / `mov` (PDP-11 condition codes assumed) | emulator, fuzzing | reproduced |
| C1-02 | `c1` | `e * 0`, `e & 0` drop `e`'s side effects | emulator | reproduced |
| C1-03 | `c1` | `x = l++` (long `l`, int value) loses the carry | golden, emulator | reproduced |
| C1-04 | `c1` | an assignment passed as a floating argument pops the floating-point stack twice | golden | reproduced, messages too |
| C1-05 | `c1` | a hoisted `+=` under a comparison stored without a pop: values pile up on the floating-point stack | golden | reproduced, messages too |
| C1-06 | `c1` | `l <<= i` / `l >>= i` on a local long jumps into an instruction when `i` is 0 | golden | reproduced |
| C1-07 | `c1` | folding a long constant `>>` (any count) or `<<` (count 0) jumps into an instruction | emulator | refused |
| C1-08 | `c1` | `longrel()` stores a label in the saved `xzero`; `xlongrel()` then reads past `lrtab[]` | code reading, fuzzing | refused |
| C1-09 | `c1` | register "after" AX: `regname[-1]` prints 118 bytes of `_ctype_` as a register name | golden, emulator | refused |
| C1-10 | `c1` | the SIGFPE handler is not re-armed: a second floating overflow kills `c1` | emulator | reproduced |
| C1-11 | `c1` | bit-field `*=`, `/=`, `%=`, `<<=`, `>>=` rejected | emulator | reproduced |
| L-01 | `libc.a` `itod` | int to double writes the low mantissa byte into bytes 1 and 3: `1001` becomes `1001.0000152...` | emulator | modelled (`c1`) |
| L-02 | `libc.a` `ltof` | `ltof(0)` leaves six bytes of the slot's last value | code reading | refused (`c1`) |
| L-03 | `libc.a` `dtoi` | from 2\*\*15 compares with the word at address 8000 (an immediate was meant) | code reading | refused (`c1`) |
| L-04 | `libc.a` `aldiv`/`alrem` | long `/=`, `%=` wrong for many divisors (16 steps, short path by the low word) | emulator | modelled (`c1`) |
| L-05 | `libc.a` `dmath.o` | `pmuld` takes one partial product from the wrong word: `dmul` not correctly rounded | golden, emulator | modelled (`as`) |
| L-06 | `libc.a` `ldexp` | the exponent wraps silently | golden | modelled (`as`) |
| L-07 | `libc.a` `atof` | 5\*\*k overflows from k = 55: valid constants below about 1e-38 raise SIGFPE | golden | reproduced (`as`), modelled (`c1`) |
| AS-01 | `as` | `-L` documented, not implemented ("Unknown option L ignored") | real hardware | not reproduced (`mutos_as` has no `-L` either way) |
| AS-02 | `as` | unknown mnemonics (`insw`, `outsw`) assembled silently as undefined externals | emulator | not reproduced (`mutos_as` knows them) |
| AS-03 | `as` | the floating-overflow abort message prints garbage where a file name belongs | real hardware | not reproduced |
| C0-01 | `c0` | folding `-32768 / -1` or `-32768 % -1` faults (8086 divide error) | emulator | `mutos_c0`: see entry |

`cpp` and `ld`: no bug known (see the sections below).

## `c1`

### C1-01 Branches on stale flags after `not` and `mov`

- **Symptom.** `if (~a || n++) ;` with `a = -1` skips `n++` (C: `~a` is 0,
  so `n++` runs). `z = (!y >= 0);` with `y = -5` gives 0 (C: 1).
- **Mechanism.** `cctab`'s list for op 34 - used by `!`, unary `-`, `~`,
  `+`, `-`, `>>`, `<<`, `|`, `^` - is the single row `%n,n` / `H` (row
  40c2, `tests/realbin/c1_tables.txt`): compute the operand and branch on
  the flags its computation set. On the PDP-11 every instruction those
  templates end in sets the condition codes; on the 8086 `not` sets no
  flags, and neither does the `mov di,*0.` / `mov di,*1.` of a logical
  value. So `~e` as a condition is `... / not di / bne L`, branching on
  whatever instruction set the flags before (in a `fuzz_c.py` program,
  the `add` of an address computation; after an `imul` the flags are
  undefined), and `!y >= 0`, which `cbranch()` (text 2bba) sends to
  `cctab` as `!y` against 0, branches after the `mov` on the flags of the
  `cmp y,*0` before it.
- **Evidence.** Emulator: the two triggers above, run through the real
  `c0` and `c1` and executed with `tests/mutos_cc/fuzz/x86sim.py` (which
  models the 8086 flags). Most of `fuzz_c.py`'s "as the real compiler"
  programs (about 0.9% of all) are this.
- **This project.** `mutos_c1` reproduces it (it is the original's table).
- **Fix idea.** End op 38's (`~`) row and the logical-value path with a
  flag-setting test (`or R,R`, as `rcexpr()` already appends when it
  falls back from `cctab` to `regtab`); check the other ops of the list
  for instructions that leave the flags (a shift by `cl` with a count of
  0 does).

### C1-02 `e * 0` and `e & 0` drop `e`'s side effects

- **Symptom.** `s = (u = 7, x) * 0;` leaves `u` unchanged.
- **Mechanism.** `acommute()` (text 4f0a, v7's `c12.c` unchanged here)
  returns the constant 0 of a `*` or `&` chain without looking at the
  other operands. A v7 bug, not a MUTOS one.
- **Evidence.** Emulator (the trigger, `x86sim.py`); `fuzz_c.py`.
- **This project.** Reproduced.
- **Fix idea.** Return the constant only if no other operand has a side
  effect (an assignment, `++`/`--`, a call); otherwise keep the operands
  as a comma list before the 0.

### C1-03 `x = l++` loses the carry

- **Symptom.** `long l; int x; l = 65535; x = l++;` leaves `l` 0, not
  65536.
- **Mechanism.** `unoptim()` (text 4746) distributes the int conversion
  (LTOI) into the `++` as into a `+`: the increment is done on the low word
  as an int - `mov di,2.+_l` / `inc 2.+_l` -, the carry into the high
  word lost.
- **Evidence.** Golden `tests/mutos_cc/fltprobe/p31_long4.s.golden`
  (the code); emulator (the value, `x86sim.py`: returns 1 where C
  gives 11 for `(int)(l >> 16) * 10 + (x == -1)`).
- **This project.** Reproduced.
- **Fix idea.** Do not distribute an LTOI into `++`/`--` (or a compound
  assignment) of a long.

### C1-04 An assignment passed as a floating argument pops twice

- **Symptom.** `f = half(d = 3.0);` stores `d` with `fstdp` (store and
  pop) and then pushes the argument from the floating-point stack again -
  one value popped twice. `c1`'s own model of the stack notices and
  reports "floating point stack underflow" (exit status 1, the `.s`
  written whole).
- **Evidence.** Goldens `p21_fltexp`, `p28_fltstk`, `p37_fltstk3`,
  `p41_fltstk4`, `p45_fltstk5`, `p49_fltstk6`, `p53_fltstk7`,
  `p57_fltstk8` (`tests/mutos_cc/c1_errors.txt` lists their messages);
  `x86sim.py` stops on them with "'fstdp' with an empty floating-point
  stack".
- **This project.** Reproduced, messages included.
- **Fix idea.** Store with `fstd` (no pop) when the assignment's value is
  used, as `x = half(d = 3.0)` already gets.

### C1-05 Hoisted `+=` values left on the floating-point stack

- **Symptom.** `x = (d += ++w + ++y) > 2.0;` stores `d` with `fstd` (no
  pop): the value stays on the runtime's floating-point stack, used by
  nothing, without a message. Enough such statements in one file push
  `c1`'s model past its top: "Floating point stack overflow; simplify
  expression" (`p52_fltinf8`: twenty-two, `p56_fltinf9`: eighteen).
- **Mechanism.** The compound store under a comparison whose value is
  taken, with two or more hoists in its right operand, takes the
  value-keeping store; nothing pops it later.
- **Evidence.** Goldens `p48_fltinf7`, `p52_fltinf8`, `p56_fltinf9`
  (messages in `c1_errors.txt` and `fltprobe/round14.log`).
- **This project.** Reproduced, messages included.
- **Fix idea.** `fstdp` there, or pop the value after the comparison.

### C1-06 `l <<= i` on a local long jumps into an instruction for `i == 0`

- **Symptom.** With a count of 0, `l <<= i` / `l >>= i` on a local
  (`bp`-relative) long jumps two bytes into the instruction after the
  shift loop.
- **Mechanism.** The template skips the shift loop and the two stores
  with `jz .+16`, counting four bytes per store - right for a file-scope
  long (`mov _l,di` is `89 3E lo hi`), wrong for a local (`mov
  *-8.(bp),di` is `89 7E F8`, three bytes).
- **Evidence.** Golden `fltprobe/p38_long6.s.golden`; `x86sim.py`
  refuses to run that path ("'jz .+16' with a count of 0 past three-byte
  stores").
- **This project.** Reproduced (the code is the original's).
- **Fix idea.** A label after the stores instead of `.+16`.

### C1-07 Folding a long constant shift jumps into an instruction

- **Symptom.** `l = 0x12345678L >> 4;` - any long constant shifted right
  by a constant count - makes `c1` crash, loop or write garbage (under the
  emulator: stopped at a wild address; `100000L >> 1` ended in "Missing
  temp file"; one fuzz program got a NUL in its `.s` and "Arg count").
  `l = 0x12345678L << 0;` the same.
- **Mechanism.** `lconst()` (text 5538) shifts a long `>>` by the count's
  HIGH word (text 5699: `mov cx,[bp-0xc]`) and `<<` by its LOW word; when
  that word is 0 its `test cx,cx` / `je` lands at text 56b4, one byte into
  the `jmp` before the switch's jump table (text 56b6), and the CPU runs
  the table as code. For `>>` the high word of any real count is 0.
- **Evidence.** Emulator (the triggers above); `c1fuzz.py`: 17 refusals
  and 15 runs of the original that did not finish in 6,000 programs.
- **This project.** Refused ("a long constant shifted right by a count
  below 65536 (lconst() jumps into an instruction)"); the fuzzer checks
  that the original wrote the same up to there - only what it had
  written out, since its unflushed stdio buffer may be changed - or did
  not finish.
- **Fix idea.** Shift by the count's low word in both directions, and
  jump past the loop to the store (text 56ac/56fc), not into the `jmp`.

### C1-08 `longrel()` overwrites the saved `xzero`

- **Symptom.** A later long comparison takes its branch from outside
  `lrtab[]`.
- **Mechanism.** When `longrel()` (text 2e6a) falls back to testing a
  register pair word by word ("test hi,hi / jne / test lo,lo"), it stores
  the label it makes in the variable that holds the saved `xzero` (text
  2f83), so `xzero` comes back as a label number. A later `xlongrel()`
  (text 2fc4) indexes `lrtab[xzero]` with it (or with an `xop` of 0) and
  reads whatever lies past the table.
- **Evidence.** Code reading; `c1fuzz.py`: 20 refusals in 6,000
  programs (the stale value comes from an earlier statement in the file,
  so a one-statement trigger does not show it).
- **This project.** Refused ("xlongrel() reads outside lrtab[] (a stale
  xzero or xop)").
- **Fix idea.** A variable of its own for the label.

### C1-09 The register "after" AX

- **Symptom.** `d = (d * e) + u;` (doubles `d`, `e`, an unsigned `u`) -
  an unsigned converted to floating after a computed operation - writes
  118 bytes of `libc.a`'s `_ctype_` table as a register name ("mov
  <garbage>,ax" / "sub ax,ax" / "push <garbage>" / "push ax" / "call
  ltof"). The real `as` rejects the lines ("***ERROR*** syntax error").
- **Mechanism.** The template builds a long in a register pair starting
  at the register it was given; registers are handed out downwards
  (`reg1 = reg - 1`), so the pair starting at AX (number 0) wants
  register -1, and `regname[-1]` is the word before the table (data
  001e), which points into `_ctype_`. Register 8 (`regname[8]`, data
  0030) prints `C=<A@?>EDCBJ` - seen once in fuzzing.
- **Evidence.** Golden `fltprobe/p19_open3.s.golden` (lines 229 on,
  `tests/mutos_cc/invalid_goldens.txt`), the real `as` on it
  (`fltprobe/p19_as.log`), emulator; the most common refusal of
  `c1fuzz.py` (634 of 6,000 programs).
- **This project.** Refused at the register name, the number in the
  message ("a register number outside regname[], -1"), after everything
  before it; `c1fuzz.py` checks the original wrote exactly the bytes
  `regname[r]` points at.
- **Fix idea.** Give the unsigned-to-floating template a fixed pair
  (DX:AX) or refuse AX as its first register.

### C1-10 The SIGFPE handler is not re-armed

- **Symptom.** A second floating overflow in one compilation (an
  `atof` overflow, L-07) kills `c1`: no message, the `.s` cut to whole
  512-byte buffers, exit by the signal.
- **Mechanism.** `main()` (text 00d6) installs `fpeintr()` (text 00b6)
  once with `signal(8, fpeintr)`; v7's kernel resets a caught signal to
  its default, and `fpeintr()` does not call `signal()` again.
- **Evidence.** Emulator (`mutosemu.py` delivers signals as v7's kernel
  does).
- **This project.** Reproduced (`mutos_c1` truncates its `.s` the same
  way and exits with 128 + SIGFPE).
- **Fix idea.** `signal(SIGFPE, fpeintr)` in `fpeintr()`.

### C1-11 Bit-field `*=`, `/=`, `%=`, `<<=`, `>>=`

- **Symptom.** For a bit-field `s.f` (signed or unsigned), `s.f *= x`
  and `s.f <<= x` give "Unimplemented field operator" (twice) and "No
  code table for op: =*" / "=<<"; `s.f /= x` "No code table for op 118",
  `s.f %= x` "... op 120", `s.f >>= x` "No code table for op: >>" - exit
  status 1. `+=`, `-=`, `&=`, `|=`, `^=`, `++`, `--` work.
- **Mechanism.** v7's `lvfield()` (`c12.c`) handles only some operators
  on a field; the code tables have no rows for the others.
- **Evidence.** Emulator.
- **This project.** Reproduced (messages, status, `.s`).
- **Fix idea.** Rewrite them as `s.f = s.f op x` in `lvfield()`.

## `libc.a`

These routines are linked into `c1` (and `as`), where their results end
up in the output, and into every MUTOS program. The text offsets are
`c1`'s.

### L-01 `itod` writes the low mantissa byte twice more

- **Symptom.** `double d = 1001;` is written `.double
  1.00100001525902189e+03`; every int whose normalized magnitude has a
  nonzero low byte converts to a slightly wrong double (and, in `c1`,
  becomes an 8-byte constant where a 4-byte one would do).
- **Mechanism.** `itod` (text 7972) normalizes the magnitude in BX and
  stores the top word and byte 5 correctly; its store loop then writes
  AX - which still holds BL in its high byte - into the two words below:
  bytes 0 to 3 are 0, BL, 0, BL instead of 0.
- **Evidence.** Emulator (the trigger through the real `c0`/`c1`).
  Whether the runtime's own conversion of compiled code (`itof`) shares
  the fault is not checked.
- **This project.** `mutos_c1` re-enacts it (`c1port/c1lib.c`'s
  `c1_itod()`).
- **Fix idea.** Clear AX before the store loop.

### L-02 `ltof(0)` leaves the slot's old bytes

- **Mechanism.** `ltof` (text 7a6b) clears only the top word (bytes 6 and
  7) of its floating-point stack slot for 0, leaving the other six bytes
  as the slot's last value left them. The value reads as zero (exponent
  byte 0), but the bytes differ - and `c1`'s float-or-double test of a
  constant looks at them.
- **Evidence.** Code reading.
- **This project.** `mutos_c1` refuses an LTOF of a constant 0 ("ltof of
  0"; 22 of 6,000 fuzz programs).
- **Fix idea.** Clear all eight bytes.

### L-03 `dtoi` compares with memory instead of a constant

- **Mechanism.** From 2\*\*15 in magnitude, `dtoi` (text 79c9) compares
  with the word at address 8000 where an immediate 8000 was meant; the
  result depends on that word. (From 2\*\*16 it raises SIGFPE, as
  intended.)
- **Evidence.** Code reading.
- **This project.** `mutos_c1` refuses the conversion of such a
  constant.
- **Fix idea.** The immediate operand.

### L-04 `aldiv` and `alrem` are not C's division

- **Symptom.** `0x7fffffffL / 0177777` (`/ -1`) folds to -65535 in `c1`;
  a MUTOS program's `l /= m` and `l %= m` get the same results at run
  time (these are the routines compiled code calls).
- **Mechanism.** `aldiv` (text 7cb4): its short path (two `div`s by one
  word) is taken when the divisor's high word equals the low word's sign
  extension - with the sign word -1 negated to 1 first, so a divisor
  from 0x18000 to 0x1ffff is divided by its low word alone, and a
  negative one-word divisor goes the long way. The long way
  (`hardldiv`) shifts and subtracts sixteen times, comparing high words
  signed, and clears the quotient's high word: right for a divisor of
  65536 or more in magnitude, only the low sixteen bits of the quotient
  below that. `alrem` (text 7d9a) is exact on its short path (a divisor
  -32768..32767) and has the same sixteen-step long path.
- **Evidence.** Emulator: `c1fuzz.py --gen lconst` (long constants at the
  edges of both paths) - a port with C's division differed on 232 of 300
  programs. `ldiv`/`lrem` (`l / m`, text 7e4a/7ef0) are not checked yet.
- **This project.** `mutos_c1` re-enacts both on 16-bit words
  (`c1_aldiv()`, `c1_alrem()`). `x86sim.py` computes C's quotient, so a
  compiled program's `l /= m` at run time is not modelled.
- **Fix idea.** Compare the high word with the sign extension itself
  (not negated), and do 32 steps on the long way.

### L-05 `dmath.o`'s multiplication uses a wrong partial product

- **Symptom.** `dmul` misses the correctly rounded product for most wide
  mantissas (2,217 of 3,000 random pairs, by up to 403 units in the last
  place); `atof`'s results inherit it - `.float
  1.26765060022822940e+30` (2\*\*100) assembles one unit below its value.
- **Mechanism.** `pmuld` (in `dmath.o`, text 0x2e4..0x425 of that object)
  forms sixteen 16-bit partial products; the a0\*b2 one loads its
  multiplier with `mov dx,2[di]` (b1) where `4[di]` (b2) was meant. The
  product is a\*b + (a0\*b1 - a0\*b2)\*2\*\*32, rounded to nearest-even.
- **Evidence.** Golden `tests/mutos_as/float_coverage/fltmul.o.golden`;
  emulator (`tests/mutos_as/float_coverage/libcatof.py`).
- **This project.** `mutos_as`'s `fltconst.c` (also in `mutos_c1`)
  re-enacts that product (`RM_LIBC_MUL`).
- **Fix idea.** `mov dx,4[di]`.

### L-06 `ldexp` wraps the exponent

- **Symptom.** `.double 1.0e+39` assembles to exponent byte 2 (a tiny
  value), `1.0e-39` to 255; no diagnostic.
- **Mechanism.** `ldexp.o` adds the exponent to `fac`'s exponent byte in
  16 bits, checks only for signed 16-bit overflow (`jo`) and stores the
  low byte.
- **Evidence.** Golden `tests/mutos_as/float_coverage/fltovf.o.golden`
  (real hardware); `ldexp.o` reconstructed in
  `tests/mutos_as/libc_recon/ldexp.s`.
- **This project.** `mutos_as` reproduces the wrapped bytes.
- **Fix idea.** Range-check the sum against 1..255 and report.

### L-07 `atof` cannot convert small constants

- **Symptom.** `.float 1.00000000000000000e-38` - representable - aborts
  the real assembler ("floating point over/under flow- assembly
  aborted", exit status 4); in `c1` the same constant gives "floating
  point over or under flow" (and a second one kills it, C1-10).
- **Mechanism.** `atof` scales by `flexp` = 5\*\*k, which overflows from
  k = 55 on: `dmul`'s exponent overflow calls `__ovfl`, which raises
  SIGFPE.
- **Evidence.** Golden run `tests/mutos_as/float_open/fltsig.s` on real
  hardware.
- **This project.** `mutos_as` refuses such a constant with the reason;
  `mutos_c1` gives the original's message.
- **Fix idea.** Scale in two steps (or by powers of ten from a table).

## `as`

### AS-01 `-L` is not implemented

- **Symptom.** `as -L ...` prints "Unknown option L ignored" and
  assembles as without it; `docs/MUTOS1700_Assembler_as.pdf` sect. 3.1
  documents the option.
- **Evidence.** Real hardware (`docs/DEVLOG.md`, the `-L` experiment).
- **This project.** Nothing depends on it.

### AS-02 Unknown mnemonics become undefined symbols

- **Symptom.** `insw` and `outsw` (80186) assemble, without a message,
  as one-word references to undefined external symbols `insw` and
  `outsw`.
- **Evidence.** Emulator (`tests/mutos_as/kernel_opt/mch_insw_outsw`,
  `check_real.py` lists it apart).
- **This project.** `mutos_as` assembles them as the instructions (an
  extension, like its other 80186 opcodes).
- **Fix idea.** Report an unknown mnemonic in operator position.

### AS-03 Garbage in the floating-overflow message

- **Symptom.** The abort of L-07 prints two lines, `W` and `?h), line
  41`, where a file name is expected.
- **Evidence.** Real hardware (`fltsig.s`, transcript in
  `docs/DEVLOG.md`).
- **This project.** Not reproduced: `mutos_as` prints its own message
  (and exits with 1, not 4).

## `c0`

### C0-01 Folding a division that overflows

- **Symptom.** `x = 0x8000 % 0xffff;` and `x = 0100000 / 0177777;`
  (-32768 % -1, -32768 / -1 in 16-bit ints) stop the real `c0` with an
  8086 divide error (under the emulator: a CPU exception at text 20fe
  and 20db). On the hardware that is a SIGFPE, and v7's `c0` sets no
  handler - not run on the hardware.
- **Mechanism.** `fold()` (v7's `c01.c`) checks only for a divisor of 0
  before `idiv`.
- **Evidence.** Emulator (`c1fuzz.py --gen lconst`'s programs).
- **This project.** `mutos_c0` folds such a division (16-bit
  truncation) - not the original's behaviour, which is a crash.
- **Fix idea.** Check for -32768 / -1 as well.

## `cpp`

No bug known: the real `cpp` gives all 137 `.i` goldens. Two v7
behaviours a fix might want to change, both described in
`src/mutos_cpp/README.md`: a comment's newlines are written even inside a
false `#if` region, and a function-like macro's name without `(` is
expanded with empty arguments (and a warning) - `mutos_cpp` reproduces the
first and leaves the name unexpanded in the second.

## `ld`

No bug known; the real `ld` has not been run under the emulator yet
(`tests/realbin/` runs `cpp`, `c0`, `c1` and `as`).

## Suspect, no wrong output found

- **`c1`, `cexpr()`'s `T` for an ITOC** tests the type of the node at
  address 0 (DX, the hashed switch's remainder, is 0 there) - `crt0`'s
  data word 4, never INT - so the `cbw` it was written for never comes
  from there. The char conversions tried get their `cbw` elsewhere.
- **`c1`, an int division that overflows in `const()`** (-32768 / -1,
  should `c1` itself fold one): the 8086 divide error would be a SIGFPE,
  which `fpeintr()` reports as "floating point over or under flow" before
  its `longjmp()` into `getree()`. Code reading; `mutos_c1` refuses.
- **`c1`, a STRASG's degree** is never set by `getree()`: the `a`/`b`
  traces print a leftover word. Only in the traces.
