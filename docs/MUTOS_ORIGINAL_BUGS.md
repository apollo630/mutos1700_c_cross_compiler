# Bugs of the original MUTOS 1700 tools

This file collects every bug found in the original MUTOS 1700 binaries -
`c0`, `c1`, `as`, `ld`, `cpp` and `libc.a`, and `c2`, `cc`, `make`, `ar` and
`ranlib` as their ports read them (`docs/TOOLCHAIN_PLAN.md`) - in one place, for the day this project offers to fix them (a
`mutos_*` option, say, that leaves the faithful default alone). Until then
the project's tools reproduce the original's behaviour wherever it can be
reproduced, bugs included, and refuse where it cannot (see each entry).
`CLAUDE.md`'s Workflow Guideline 9 makes keeping this file current part of
the change that finds a bug.

Each entry says what goes wrong (with a minimal trigger where one is
known), why (the mechanism, with the text or data offset in the binary as
`tests/realbin/c1map.txt` and `c1dis.py` give them for `c1`, `c0map.txt`
and `c0dis.py` for `c0`), how it was
established, what this project's tool does with it, and an idea for a fix.
Evidence is one of: **golden** (real-hardware output in the tree),
**emulator** (the original run under `tests/realbin/mutosemu.py` -
reproducible with the commands in `tests/realbin/README.md`), **code
reading** (the disassembly, not observed in a run). "Reproduced" means the
project's tool gives the original's output byte for byte; "refused" that
it stops with a message and exit status 2 where the original's output
depends on state that is not modelled.

Last updated: 2026-10-20.

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
| AS-01 | `as` | `-L`, `-W`, `-D`, `-T` documented, not implemented ("Unknown option L ignored"); `-e`, `-g`, `-C`, `-l` undocumented | real hardware, emulator | not reproduced (`mutos_as`: no `-L`, `-W` as documented; `-g`/`-C` planned) |
| AS-02 | `as` | an unknown mnemonic (`insw`, `pusha`, `enter`, ...) assembled silently as a word with an undefined external | emulator | not reproduced (`mutos_as` knows the 80186 ones; planned: as the original without `-1`) |
| AS-03 | `as` | the floating-overflow abort message prints garbage where a file name belongs | real hardware | not reproduced |
| C0-01 | `c0` | folding `-32768 / -1`, `-32768 / 1` (or `%`) faults (8086 divide error) | emulator | refused |
| C0-02 | `c0` | reads past the end of the file for ever: a name, a comment or a `#` line that runs into it | emulator, fuzzing | refused |
| C0-03 | `c0` | an initialized array of zero-length elements divides by 0 | emulator, fuzzing | refused |
| C0-04 | `c0` | a member redeclared with another type: its offset and the size come from an uninitialized word | emulator, fuzzing | refused |
| C0-05 | `c0` | structure holes are not counted against `mems[128]`: the list runs over `strdec()`'s frame | emulator | refused |
| C0-06 | `c0` | `goto <expression>` stores the tree through the `cp` of before `tree()` | emulator, fuzzing | reproduced; refused where it hits live data |
| C0-07 | `c0` | an input byte 0200..0377 outside strings indexes `ctab[]` past its end | emulator | reproduced; refused for 0210..0247 |
| C0-08 | `c0` | a number longer than 64 characters runs over `numbuf` | emulator | reproduced up to 94 characters, refused beyond |
| C0-09 | `c0` | a `# line` file name longer than 63 characters runs over `filename` into `symbuf` | emulator | reproduced |
| C0-10 | `c0` | `conexp()` after a syntax error returns the word at data 8 (the code segment) | emulator | reproduced (the emulator's segment) |
| C0-11 | `c0` | "undefined; func." before the first function prints bytes from data 000d | emulator | reproduced |
| C0-12 | `c0` | an enum's size is a stale register: whether a redefined enum tag is reported depends on the storage class | emulator | reproduced |

`cpp`, `ld` and `cc`: no bug known (see the sections below; one `cc` behaviour is
listed under "Suspect").

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

### AS-01 Documented options not implemented, others undocumented

- **Symptom.** `as -L ...` prints "Unknown option L ignored" and
  assembles as without it; `docs/MUTOS1700_Assembler_as.pdf` documents
  the option (`as [-LWDT] [-o <output>]`). So do the other three: `-W`
  (no error messages), `-D` and `-T` (debug and trace output) - each
  gives "Unknown option ... ignored." too. The binary instead knows
  `-e` (only external symbols in the symbol table), `-g`, `-C` and `-l`
  (no effect seen yet; the original `cc` always passes `-gC`). Options
  are read letter by letter.
- **Evidence.** Real hardware for `-L` (`docs/DEVLOG.md`, the `-L`
  experiment); emulator for the others (2026-10-20,
  `docs/devlog/originals_and_ports.md`).
- **This project.** Nothing depends on `-L`. `mutos_as` implements `-W`
  as the manual describes it; it is to accept `-g`/`-C`
  (`docs/TOOLCHAIN_PLAN.md`, decision 9). What `-g`, `-C` and `-l` do is
  to be read in the binary.
- **Fix idea.** Implement the manual's options, or correct the manual.

### AS-02 Unknown mnemonics become undefined symbols

- **Symptom.** `insw` and `outsw` (80186) assemble, without a message,
  as one-word references to undefined external symbols `insw` and
  `outsw` - and so does every identifier the original does not know as an
  instruction at the start of a statement (`pusha`, `enter`, 2026-10-20):
  it is read as an expression. The link then fails with "Undefined".
- **Evidence.** Emulator (`tests/mutos_as/kernel_opt/mch_insw_outsw`,
  `check_real.py` lists it apart; the other mnemonics 2026-10-20).
- **This project.** `mutos_as` assembles the 80186 instructions it knows
  (an extension); planned (`docs/TOOLCHAIN_PLAN.md`, section 4): only
  with `-1`, without it as the original.
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
  and 20db), and so do `x = 0100000 / 1;` and `x = 0100000 % 1;`. On the
  hardware that is a SIGFPE, and v7's `c0` sets no handler - not run on
  the hardware.
- **Mechanism.** `fold()` (text 200a, v7's `c01.c`) checks only for a
  divisor of 0 before `cwd` / `idiv`. The 8086 faults whenever the
  quotient's absolute value exceeds 7FFFH (the AP-186 note), the V30
  below -32,767 (its manual) - so -32768 / 1 too, which the 80186 and the
  80386 allow (Unicorn, the emulator, did not fault there until
  `mutosemu.py` modelled it, 2026-10-18). On the PDP-11 an overflowing
  `div` sets the V bit and does not trap; on the 8086 it is interrupt 0.
- **Evidence.** Emulator (`c1fuzz.py --gen lconst`'s programs; the
  triggers above, -32768 / 1 with `mutosemu.py`'s 8086 division).
- **This project.** Refused since `mutos_c0` is the port (2026-10-18;
  the earlier front end folded it with 16-bit truncation).
- **Fix idea.** Do not fold a division of -32768 by -1 or 1 (or fold it
  by hand: -32768 / 1 is -32768, -32768 % +-1 is 0).

### C0-02 Reading past the end of the file for ever

- **Symptom.** `c0` never ends - it reads the end of its input again and
  again - on a file that ends in the middle of a name (`int x` without
  a newline), inside a comment (`/* no end`) or in a `#` line that is
  not `# <number> "<name>"` (`# 5 "a.c" junk` without a newline).
- **Mechanism.** EOF is -1 to `getc()` and `c0`'s tokens end at EOFC,
  0. The name loop (text 06e7) tests `ctab[c]` for LETTER or DIGIT, and
  `ctab[-1]` is the byte before the table, `cvntab[15]` - 123, LETTER.
  The comment loop (text 0680) runs until `spnextchar()` returns 0,
  never -1. The "Illegal #" skip loop (text 0493) runs until a newline or
  `eof`, which only `symbol()` sets. v7 has the same three loops.
- **Evidence.** Emulator (the triggers; `mutosemu.py`'s `eof_limit`
  stops the run after 1,000 reads of nothing); `c0fuzz.py --mutate`: 26
  of 2,000 programs.
- **This project.** Refused ("a name ends the file", "a comment ends the
  file", "a '#' line ends the file"), after everything before. Through
  `cc` none of the three is reached: `cpp` removes comments and its
  output ends with a newline.
- **Fix idea.** Test for -1 in the three loops (`eof++` and stop).

### C0-03 An initialized array of zero-length elements divides by 0

- **Symptom.** `int a[][0] = {1};` or `int a[2][0] = {1};` (after "0-length
  row") stops `c0` with an 8086 divide error (under the emulator: a CPU
  exception at text 264b).
- **Mechanism.** `cinit()` (text 2592) computes the number of elements
  as the array's size divided by the element's (`div word ptr
  [bp-0xc]`, v7's `ldiv(0, width, o)`) without testing the element's
  size for 0. On the PDP-11 a `div` by 0 sets the C bit and does not trap.
- **Evidence.** Emulator; `c0fuzz.py`: 5 + 9 of 4,000 programs.
- **This project.** Refused ("an array of 0-length elements initialized").
- **Fix idea.** Report "0-length row" for the element as well and skip
  the division.

### C0-04 A redeclared member's offset and the size come from an uninitialized word

- **Symptom.** `struct s { int a; char a; int b; };` reports "a
  redeclared" and then lays `b` out at an offset - and gives the
  structure a size - taken from a stack word (the emulated original makes
  `sizeof (struct s)` 6 here; it depends on what earlier calls left).
- **Mechanism.** `decl1()` (text 3882) on a redeclaration with another
  type calls `redec()` (text 3b3a) and returns `elsize` (text 3de8:
  `mov dx,[bp-0x14]`) before anything has set it. In a member list that
  value is added to the offset of what follows. A redeclared member with
  the same type is checked (`chkoff`) and not affected. v7 too.
- **Evidence.** Emulator (the trigger); `c0fuzz.py`: 13 of 4,000
  programs.
- **This project.** Refused inside a structure's, union's or enum's
  list; elsewhere (a block's declarations, the parameters) the value is
  dropped by the caller and the port reproduces the original.
- **Fix idea.** `elsize = 0` before the redeclaration test.

### C0-05 Structure holes are not counted against `mems[128]`

- **Symptom.** A structure of 63 `char a<n>; int b<n>;` pairs and one
  more `char` - 127 members, 63 alignment holes - makes the original
  report "Too many structure members" at its end, then " redeclared",
  "Illegal storage class" and "Arg count" (the message `main()` gives
  for too few arguments - control has left `strdec()` through its
  overwritten frame) and exit with status 1, no output.
- **Mechanism.** `strdec()` (text 3564) collects the members in
  `mems[NMEMS]` (256 bytes at `bp-0x110`) through `memlist`; `decl1()`
  puts `&structhole` into the list for every alignment gap but counts
  only the members against NMEMS ("Too many structure members" moves
  `memlist` back by NMEMS/2 when they reach it). With enough holes the
  list runs past `mems[]` over `strdec()`'s locals, the saved registers
  and its return address. v7 too.
- **Evidence.** Emulator (the trigger).
- **This project.** Refused at the first store past `mems[]` ("a member
  list past the end of strdec()'s mems[]").
- **Fix idea.** Count the holes, or test `memlist` against the end of
  `mems[]`.

### C0-06 `goto <expression>` stores its tree through a stale `cp`

- **Symptom.** `goto (;` (an expression with a syntax error after
  `goto`) gives "Expression syntax", then "Illegal indirection" and
  "Illegal type of operand" from a tree that was never parsed; a goto
  expression as the first statement of a function with register
  parameters (`f(p) register int *p; { goto *p; }`) stores its tree over
  a saved register.
- **Mechanism.** `dogoto()` (text 476c) is v7's `*cp++ = tree();`, and
  the binary pushes `cp` before calling `tree()` (text 4771: `push word
  ptr [0x3684]`, after the call `pop bx` / `mov [bx],ax`): the tree goes
  where `cp` pointed before - into the operand stack of the last
  `tree()`'s frame, mostly dead stack by then -, and `build(STAR)` then
  works on what `tree()` left at its own `cp`. After a syntax error
  `tree()` returns 0 and `build()` takes a dead frame's word; after
  `funchead()` `cp` points at a saved register of the frame above.
- **Evidence.** Emulator (the triggers); `c0fuzz.py --mutate`: 8 of
  2,000 programs.
- **This project.** Reproduced where the store lands in dead stack (the
  usual case: `goto *p;`, `goto p;` with `p` not a label); refused after
  a syntax error, right after `funchead()`, and for a goto nested five
  or more statements below the last expression (where the old frame may
  lie under live ones).
- **Fix idea.** `t = tree(); *cp++ = t;`.

### C0-07 Input bytes 0200..0377 index `ctab[]` past its end

- **Symptom.** A byte 0200..0377 outside a string, character constant or
  comment is not "Unknown character": 0200 acts as `+` (`\200 int y;` at
  file level is "External definition syntax"), 0205 as `-` (`x = 1 \205
  2;` compiles), a byte from 0250 on as EOFC - skipped at file level
  (`int x;\n\344int y;` compiles both declarations without a message),
  "Unexpected EOF" in a function -, and 0210..0247 as whatever the stdio
  buffers' state makes them.
- **Mechanism.** `symbol()` (text 035e) indexes the 128-byte `ctab[]`
  (data 0a0c) with the byte as it is (text 03d5: `mov al,[di+0xa0c]`):
  0200..0207 read `libc.a`'s "(null)" string after the table, 0210..0247
  the FILE structures of stdin, stdout, stderr and temp2 (their buffer
  pointers and counts), and from 0250 on unused FILE structures, zero -
  EOFC.
- **Evidence.** Emulator (the triggers).
- **This project.** Reproduced for 0200..0207 and 0250..0377; refused
  for 0210..0247 (the port does not model `libc.a`'s stdio state).
- **Fix idea.** `c & 0177`, or "Unknown character" for any byte from 0200
  on.

### C0-08 A number longer than 64 characters runs over `numbuf`

- **Symptom.** `int x = 0000...0001;` with 80 zeros compiles (the
  digits overwrite `c0`'s member-list state and block level); with 100
  the digits reach stdout's stdio buffer.
- **Mechanism.** `getnum()` (text 0852) stores every character of a
  number into `numbuf[64]` (data 3c5c) without a bound: after it lie
  `memlist`, `nmems`, `structhole`, `blklev` and `mossym`, then from data
  3cba `libc.a`'s buffer of stdout - the `.1` not yet written out. v7
  too.
- **Evidence.** Emulator (80 and 100 zeros).
- **This project.** Reproduced up to 94 characters (the image holds
  those variables); refused beyond, where the original overwrites its own
  pending output.
- **Fix idea.** Stop storing at `numbuf + 63` (only a floating constant
  needs the text).

### C0-09 A long `# line` file name runs over `filename`

- **Symptom.** `# 1 "<70 a's>"` makes every later message start with
  the 64 a's and the name `symbuf` holds at the time
  (`aaa...az:2: z undefined; func. y`).
- **Mechanism.** `symbol()`'s `#` case (text 0440) copies the name into
  `filename[64]` (data 0b40) without a bound; after it lie `symbuf`,
  `hshused` and the first entries of `hshtab`. v7 too.
- **Evidence.** Emulator (the trigger).
- **This project.** Reproduced.
- **Fix idea.** Stop at `filename + 63`.

### C0-10 `conexp()` after a syntax error reads the null tree

- **Symptom.** `struct s { int a : (; };` gives "Expression syntax" and
  then "Field too long": the field's width after the syntax error is a
  value from nowhere.
- **Mechanism.** `conexp()` (text 21ce) returns `t->value` even when
  `tree()` returned 0 (text 21fb: `mov ax,[di+8]`, DI 0): the word at
  data 8, where `crt0` stored the code segment (the far pointers of
  `docs/MUTOS_C_ABI.md` 2.2). Under the emulator that is 0x1000; on the
  hardware wherever the kernel loaded the text. v7 reads address 8 too.
- **Evidence.** Emulator (the trigger).
- **This project.** Reproduced with the emulator's 0x1000 (`c0lib.c`'s
  `c0_init()`); after an error the exit status is 1 either way.
- **Fix idea.** Return 0 for a null tree.

### C0-11 "undefined; func." before the first function

- **Symptom.** `int a[y];` at the start of a file reports `y undefined;
  func. ÿ^A` (the bytes 0377, 001), then "(Warning only)" and "Constant
  required".
- **Mechanism.** `tree()` (text 0e9c..0eb5) prints the name of `funcsym`,
  13 bytes into it; before the first function definition `funcsym` is 0
  and the name is read from data 000d - the high byte of `peeksym` (-1)
  and `line`. v7 too.
- **Evidence.** Emulator (the trigger).
- **This project.** Reproduced.
- **Fix idea.** Print the function's name only when `funcsym` is set.

### C0-12 An enum's size is a stale register

- **Symptom.** `enum e { A }; enum e { B };` gives no message, `static
  enum e { A }; static enum e { B };` gives "e redeclared".
- **Mechanism.** `strdec()` (text 3564) sets `elsize` only from a
  structure's or union's member list; for an enum the register that holds
  it (DI) still has the caller's value, `getkeywords()`'s storage class,
  and that becomes the enum's `ssize` - 0 for none, 13 for `static`.
  "%.8s redeclared" tests `ssize` when the tag gets a second body. v7
  does not set `elsize` for an enum either.
- **Evidence.** Emulator (the triggers).
- **This project.** Reproduced (the port passes `getkeywords()`'s
  storage class in, as the register does).
- **Fix idea.** `elsize = 0` for an enum.

## `cpp`

No bug known: the real `cpp` gives all 137 `.i` goldens. Two v7
behaviours a fix might want to change, both described in
`src/mutos_cpp/README.md`: a comment's newlines are written even inside a
false `#if` region, and a function-like macro's name without `(` is
expanded with empty arguments (and a warning) - `mutos_cpp` reproduces the
first and leaves the name unexpanded in the second.

## `ld`

No bug known. Run under the emulator for the first time on 2026-10-20 (a
link through to its temporary file; the emulator does not serve `link`
yet): `-l` searches `/lib`, then `/usr/lib`; `-r` and `-d` are refused
("-r and -d flags not supported", exit status 4) - a limit, not a bug;
`mutos_ld` keeps both as an extension.

## `cc`

No bug known; traced under the emulator on 2026-10-20 with `fork` and
`exece` faked (`docs/devlog/originals_and_ports.md`). One behaviour is
listed under "Suspect".

## Suspect, no wrong output found

- **`c1`, `cexpr()`'s `T` for an ITOC** tests the type of the node at
  address 0 (DX, the hashed switch's remainder, is 0 there) - `crt0`'s
  data word 4, never INT - so the `cbw` it was written for never comes
  from there. The char conversions tried get their `cbw` elsewhere.
- **`c1`, an int division that overflows in `const()`** (-32768 / -1 or
  / 1, should `c1` itself fold one - text 5492: `cwd` / `idiv di`): the
  8086 divide error would be a SIGFPE, which `fpeintr()` reports as
  "floating point over or under flow" before its `longjmp()` into
  `getree()`. Code reading; `mutos_c1` refuses.
- **`c1`, a STRASG's degree** is never set by `getree()`: the `a`/`b`
  traces print a leftover word. Only in the traces.
- **`cc`, `-o` with a `.c` or `.o` name**: v7's `cc` refuses it ("Would
  overwrite"); the MUTOS `cc` takes the name as an input file as well
  and writes the output under the name without suffix and directory -
  `cc -o hello.c x.o` compiles `hello.c` and links `-o hello hello.o
  x.o`, `cc -o dir/p.o x.o` links `-o p dir/p.o x.o` (the output in the
  current directory, not in `dir/`). Possibly meant (no source file is
  overwritten); traced under the emulator, 2026-10-20. The port of `cc`
  reproduces it and reads the code that does it.
