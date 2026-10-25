# Bugs of the original MUTOS 1700 tools

This file collects every bug found in the original MUTOS 1700 binaries
(`mutos/mutos_bin/`) - `c0`, `c1`, `as`, `ld`, `cpp`, `cc`, `c2` and
`libc.a`, and `make`, `ar`, `ranlib`, `nm`, `size`, `strip`, `tsort`,
`ed` and `em` as their ports read them
(`docs/TOOLCHAIN_PLAN.md`) - in one place, for the day this project offers to fix them (a
`mutos_*` option, say, that leaves the faithful default alone). Until then
the project's tools reproduce the original's behaviour wherever it can be
reproduced, bugs included, and refuse where it cannot (see each entry).
`CLAUDE.md`'s Workflow Guideline 9 makes keeping this file current part of
the change that finds a bug.

Each entry says what goes wrong (with a minimal trigger where one is
known), why (the mechanism, with the text or data offset in the binary as
`tests/realbin/c1map.txt` and `c1dis.py` give them for `c1`, `c0map.txt`
and `c0dis.py` for `c0`, `cppmap.txt` and `cppdis.py` for `cpp`,
`ldmap.txt` and `lddis.py` for `ld`, `ccmap.txt` and `ccdis.py` for `cc`,
`c2map.txt` and `c2dis.py` for `c2`), how it was
established, what this project's tool does with it, and an idea for a fix.
Evidence is one of: **golden** (real-hardware output in the tree),
**emulator** (the original run under `tests/realbin/mutosemu.py` -
reproducible with the commands in `tests/realbin/README.md`), **code
reading** (the disassembly, not observed in a run). "Reproduced" means the
project's tool gives the original's output byte for byte; "refused" that
it stops with a message and exit status 2 where the original's output
depends on state that is not modelled.

Last updated: 2026-10-25.

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
| AS-01 | `as` | `-L`, `-W`, `-D`, `-T` documented, not implemented ("Unknown option L ignored"); `-e`, `-g`, `-C`, `-l` undocumented | real hardware, emulator | not reproduced (`mutos_as`: no `-L`, `-W` as documented; `-g`/`-C` accepted, no effect) |
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
| CPP-01 | `cpp` | an octal character constant in `#if` (`'\101'`) is tested and converted at the wrong address (the compiled code reuses a register) | emulator, fuzzing | reproduced |
| CPP-02 | `cpp` | after "illegal macro name" nothing is written up to the next `#endif` or `#else` | emulator | reproduced |
| CPP-03 | `cpp` | eight `-I` options disable `-P` (the directory list's end mark lands on `pflag`); a 21st `-D` or `-U` lands on the next variable | emulator | reproduced |
| CPP-04 | `cpp` | the yacc stack: one state and value more than it holds (the caller's saved SI, `errno`); then "yacc stack overflow" | emulator, fuzzing | reproduced |
| CPP-05 | `cpp` | actuals longer than 512 bytes run over `subst()`'s frame before "actuals too long": a wrong expansion, a crash | emulator, fuzzing | reproduced up to the frame's saved BP, refused beyond |
| CPP-06 | `cpp` | `#include "name` without the closing quote: the name is not terminated | emulator, fuzzing | refused |
| CPP-07 | `cpp` | formals' names longer than 512 bytes run over `dodef()`'s frame | emulator | refused |
| CPP-08 | `cpp` | a `-D` option longer than about 510 bytes runs over `stsym()`'s frame (a crash) | emulator | refused |
| CPP-09 | `cpp` | the linefeeds of a macro call are pushed back without the buffer check: below the buffer, over `ptrtab` | emulator | reproduced; refused where it reads libc.a's state |
| CPP-10 | `cpp` | a comment's linefeeds inside a macro call's actuals are written twice: the output's line numbers run off | emulator, fuzzing | reproduced |
| CPP-11 | `cpp` | a macro with parameters named without `(` loses the next token and leaves a spurious "unterminated macro call" for the end of the input | emulator, fuzzing | reproduced |
| CPP-12 | `cpp` | an `#if` whose expression does not parse (empty, "syntax error") counts as true | emulator, fuzzing | reproduced |
| CPP-13 | `cpp` | the token after an object-like macro's name is dropped, whatever it is: `#define X+1` defines `X` as `1` | emulator | reproduced |
| CPP-14 | `cpp` | "Can't find include file" names the line after the `#include` | emulator | reproduced |
| LD-01 | `ld` | a `-l` name of 14 characters or more is written over the strings after `/usr/lib/libxxxxxxxxxxxxxxx`: the messages change | emulator | reproduced; refused where it reaches libc.a's data |
| LD-02 | `ld` | more than 256 archive entries (members loaded, end marks) run past `liblist[]` over `cursym` and `symtab[]` | emulator | reproduced |
| LD-03 | `ld` | `ldrand()` reads a symbol type through a null pointer (crt0's word 8) | code reading | reproduced |
| LD-04 | `ld` | an undefined external's word does not complete a byte-shifted word before it: the carry goes one word on | emulator | reproduced (completed under the extension `-r`) |
| LD-05 | `ld` | "Entry point not in text" leaves the last input's entry word in the header | emulator | reproduced |
| LD-06 | `ld` | a warning sets `errlev` back to 0: an earlier error no longer counts (exit status 0, executable) | emulator | reproduced |
| LD-07 | `ld` | an object of 32K or more of text or data: its symbol table is looked for 64K too early | emulator, code reading | refused |
| LD-08 | `ld` | a text of 65,530 bytes or more: the six bytes of "jmp $" wrap the text size | emulator | reproduced |
| CC-01 | `cc` | a source's name of 50 characters or more runs past `ts[]` into `av[]`, or past its `malloc()` block into the next block's header | emulator | reproduced; refused where a pointer leads past the break |
| CC-02 | `cc` | `-P` and `-E` fill `ptemp[]` without a check: the 101st overwrites `main()`'s `pv`, `cpp` gets words of the frame as arguments | emulator | reproduced; refused from about 150 `-P` (libc.a's buffers) |
| CC-03 | `cc` | a `-K` of 9 characters or more runs over the passes' paths (`Can't find `) | emulator | reproduced |
| CC-04 | `cc` | a `-B` prefix of more than 17 characters runs into the next pass's path | emulator | reproduced |
| CC-05 | `cc` | `-o` before an option, and no object after it: `ld` gets the string at address ffff as the output name | emulator | reproduced |
| CC-06 | `cc` | with `-P`, `tmp1` .. `tmp3` are copies of a null pointer (crt0's code) | code reading, emulator | reproduced |
| C2-01 | `c2` | `dest()` clears the register of a matching constant, not the constant, and an empty entry stays empty: a destination equal to a constant or empty loops for ever | emulator, fuzzing | refused |
| C2-02 | `c2` | "mov/push converted to push" is never counted (the conversions count as redundant moves) | emulator | reproduced |
| C2-03 | `c2` | operands and the first token copied without a check into `regs[12]`/`regs[13]` (32 bytes), `tmpop[32]`, `rt1[50]`/`rt2[50]`: from 32 characters the destination is lost, further on the opcode hash table and the nodes are overwritten | emulator, fuzzing | reproduced; refused where the opcode lookup follows text past the break |
| C2-04 | `c2` | a `test` without a comma is removed as a redundant test (`ccloc` is never set) | emulator | reproduced |
| C2-05 | `c2` | `lea` into memory clears `regs[-1]`: the low byte of the "redundant jumps" counter | emulator | reproduced |
| C2-06 | `c2` | a branch to a label followed by a jump to an undefined label: `iterate()` increments the "node" at address 0 - `optab[0]`'s opcode: `br` becomes `beq` for the rest of the run | emulator | reproduced |
| C2-07 | `c2` | an empty line (after a label alone on its line) equals the list's head: `xjump()` jumps to a label put before the head, never written, or leaves the list in a circle | emulator | reproduced; refused where the deletion after a branch never ends |

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
  gives "Unknown option ... ignored." too. The manual page as(1)
  (`mutos/mutos_man/as.1.d`, 3/89) has `-L` and `-W` (`as [-LW] [-o
  output] file ...`). The binary instead knows
  `-e` (only external symbols in the symbol table), `-g`, `-C` and `-l`
  (no effect seen yet; the original `cc` always passes `-gC`). Options
  are read letter by letter.
- **Evidence.** Real hardware for `-L` (`docs/DEVLOG.md`, the `-L`
  experiment); emulator for the others (2026-10-20,
  `docs/devlog/originals_and_ports.md`).
- **This project.** Nothing depends on `-L`. `mutos_as` implements `-W`
  as the manual describes it; it accepts `-g` and `-C` and ignores them
  (since 2026-10-20, `docs/TOOLCHAIN_PLAN.md`, decision 9). What `-g`,
  `-C` and `-l` do is to be read in the binary.
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

Read in full on 2026-10-22 for its port (`src/mutos_cpp/`,
`tests/realbin/cppmap.txt`); every entry was run under the emulator with
the trigger given, and the port does the same. v7's `cpp` shares all but
CPP-01 (a fault of the MUTOS compiler's code, not of the source) and the
consequences of the MUTOS deltas in CPP-10 and CPP-11.

### CPP-01 An octal character constant in `#if` reads the wrong address

- **Symptom.** `#if '\101' == 65` is false; `#if '\0' == 0` is false and
  may say "Illegal number" with text from elsewhere in the buffer;
  `#if '\7'` is true. The escapes `\b \t \n \f \r \\` and a plain
  character are right.
- **Mechanism.** `yylex()` (text 215a..21a0): the source is v7's `if
  (inp[2]<='9' && inp[2]>='0') yylval=c=tobinary(inp+2,8);`. The compiled
  code holds `inp` in DX, loads `newp` into DX for the test of the
  closing quote (215a), then reloads only DL with `inp[2]` in the loop
  over the escape table (217e) - and at 218a takes DX as `inp` again. So
  the test `>= '0'` and the conversion read at `(newp & 0xff00) |
  inp[2]`, two bytes on: some byte of the 256-byte page `newp` is in.
- **Evidence.** Emulator (the triggers), fuzzing.
- **This project.** Reproduced (`cpy.c` computes that address).
- **Fix idea.** `tobinary(inp+2, 8)` and test `inp[2]`, as the source
  says.

### CPP-02 After "illegal macro name" nothing is written

- **Symptom.** `#define 1 x` warns "illegal macro name"; every line after
  it is dropped from the output up to the next `#endif` or `#else` (which
  are taken as closing a false conditional - no "If-less endif").
- **Mechanism.** `dodef()` raises `flslvl` to prevent expansion and
  returns from the illegal-name branch (text 0dc2) without lowering it
  again. v7 too.
- **Evidence.** Emulator (`#define 1 x` / `abc` / `#endif` / `def`: only
  `def` is written).
- **This project.** Reproduced.
- **Fix idea.** `--flslvl` before that return.

### CPP-03 Too many options run over their arrays

- **Symptom.** `cpp -P -Ia -Ib -Ic -Id -Ie -If -Ig -Ih x.c` writes `# 1
  "x.c"` lines - `-P` has no effect with eight `-I` options. A 21st `-D`
  is stored in the first `-U` slot, a 21st `-U` in `exfail` (which `main()`
  then clears; the 21st name is "removed" as the empty string at data 0).
- **Mechanism.** `main()` (text 1ce9) accepts an `-I` while `nd <= 8`, so
  `dirs[1..8]`; then `dirs[nd++]="/usr/include"; dirs[nd++]=0;` (text
  1e17) writes `dirs[9]` and `dirs[10]` - which is `pflag` (data 91ce).
  The `-D` and `-U` tests (text 1c7e, 1cc7) are `predef > prespc+NPREDEF`,
  one too late. v7 too.
- **Evidence.** Emulator (eight and seven `-I`).
- **This project.** Reproduced (the image has the variables where the
  original has them).
- **Fix idea.** `nd >= 8`, `>=` in the two tests.

### CPP-04 The yacc stack holds one more than it has

- **Symptom.** An `#if` expression 150 states deep (about 150 nested
  parentheses) works; one deeper gives "yacc stack overflow" - and the
  `#if` is taken as true (CPP-12).
- **Mechanism.** v7 yacc's `yyparse()` (text 2353) tests `++yyps >
  &yys[YYMAXDEPTH]`, so it stores `yys[150]` - the word above the array
  in its frame, the caller's saved SI, which `control()` does not use
  again - and `yyv[150]`, which is libc.a's `errno` (data a518); no
  failed system call comes between the store and its use.
- **Evidence.** Emulator (148 to 152 levels), fuzzing.
- **This project.** Reproduced (`errno` is in the image).
- **Fix idea.** `>=` in the test; a bigger `YYMAXDEPTH`.

### CPP-05 Long actuals run over `subst()`'s frame

- **Symptom.** `#define f(a) [a]` / `f(<520 x's>)` says "f: actuals too
  long" and writes `[xxxxxxxx]` (8 x's); 514 x's give `[xx]`; longer
  actuals crash `cpp`.
- **Mechanism.** `subst()` (text 18e5) copies each token of the actuals
  into `acttxt[512]` and only then compares (18eb): the text runs on over
  `actflg[]`, `actual[]`, `params` and `vp` (bp-0x66 .. bp-6), then the
  saved registers and the return address. The `[xx]`: the actual's end
  pointer is right (the push-back reads an actual from its end), but
  `actflg[0]`, which the text now fills from its 512th byte, is cleared
  after the copy - the push-back stops there. v7 too (without
  `actflg[]`).
- **Evidence.** Emulator (the triggers; a crash with a few hundred bytes
  more), fuzzing.
- **This project.** Reproduced while the text stays below the saved BP
  (the frame is in the image at the original's offsets and address - a
  pointer half overwritten keeps the stack's high byte); refused from
  there.
- **Fix idea.** Stop copying at `acttxt + BUFSIZ`.

### CPP-06 `#include "name` without the closing quote

- **Symptom.** The name is taken with whatever bytes follow it on the
  stack, up to a 0 - "Can't find include file name<garbage>", or a
  different file.
- **Mechanism.** `doincl()` (text 0b5d..0b80) copies the token after
  `"` into `filname[]` and puts the 0 only over a final `"`; the scanner
  ends an unterminated string at the linefeed, so there is none. v7 too.
- **Evidence.** Emulator (the original happened to find a 0 right after
  the name), fuzzing.
- **This project.** Refused (the stack's contents are not modelled).
- **Fix idea.** Terminate the name in either case.

### CPP-07 Long formal names run over `dodef()`'s frame

- **Symptom.** `#define f(<300 a's>,<300 b's>) x` says "(null) redefined"
  and does not define `f`.
- **Mechanism.** `dodef()` (text 0eef..0f17) copies each formal's name
  into `formtxt[512]` without a bound, over `formal[]` and its other
  locals (`np` among them). v7 too.
- **Evidence.** Emulator (the trigger).
- **This project.** Refused.
- **Fix idea.** Check the space left before each copy.

### CPP-08 A long `-D` option runs over `stsym()`'s frame

- **Symptom.** `cpp -DX=<600 y's> x.c` crashes.
- **Mechanism.** `stsym()` (text 1470) copies the option into `buf[512]`
  without a bound. v7 too.
- **Evidence.** Emulator (an invalid instruction after the return).
- **This project.** Refused.
- **Fix idea.** Bound the copy.

### CPP-09 A call's linefeeds pushed below the buffer

- **Symptom.** A macro call with several hundred linefeeds in a comment
  between its actuals, with the right length of body before it, writes
  linefeeds over the bytes below the scanning buffer - `ptrtab` among
  them; the scanner then classifies characters through libc.a's FILE
  structures.
- **Mechanism.** `subst()` pushes the linefeeds no actual took back in
  front of the expansion (text 1a4f..1a66, a MUTOS delta) without the
  `bob()` / `unfill()` check every other push makes; below `pbeg` lie 128
  bytes of `buffer[]`, then `ptrtab`, `slotab`, `fastab`.
- **Evidence.** Emulator (`#define f(a) <318 b's>` / `f(/*<700
  linefeeds>*/)`).
- **This project.** Reproduced as far as the image goes; refused where the
  scan reads libc.a's FILE structures.
- **Fix idea.** `if (bob(p)) {outp=inp=p; p=unfill(p);}` in that loop too.

### CPP-10 A comment's linefeeds in a macro call are written twice

- **Symptom.** After a macro call with a comment of n lines among its
  actuals, every output line is n lines late - `__LINE__` and the
  compiler's messages disagree with the output's lines from there on.
- **Mechanism.** The comment scan (`cotoken()`, text 04c5) writes each
  linefeed of a comment at once and counts it; `subst()` (text 1939)
  then pushes `lineno - maclin` linefeeds back, less those its actuals
  carried - the comment's again.
- **Evidence.** Emulator (`#define f(a) [a]` / `f(/*` / `` / `*/x)` /
  `__LINE__`: the comment's two linefeeds come out twice, and
  `__LINE__`, line 5 of the input, gives `5` on line 7 of the output),
  fuzzing.
- **This project.** Reproduced.
- **Fix idea.** Count only the linefeeds of the actuals' text, or write
  none in the comment while actuals are collected.

### CPP-11 A macro with parameters named without `(`

- **Symptom.** `#define F(a) <a>` / `F x` / `last` writes ` <>` (the `x`
  is lost), warns "F: argument mismatch" - and at the end of the input
  says "F: unterminated macro call" with the place of the last call that
  had `(` (line 0 and no file name if there was none); the exit status
  counts it.
- **Mechanism.** `subst()` sets `plvl = -1` (text 182d) before looking
  for the `(`, and only the `(` branch sets it again; the token read in
  its place is overwritten by the expansion. `refill()` at the end of the
  input (text 0258) takes `plvl != 0` for an open call, `maclin` and
  `macfil` from the last one. v7 too.
- **Evidence.** Emulator (the trigger), fuzzing.
- **This project.** Reproduced.
- **Fix idea.** `plvl = 0` when there is no `(`, and push the token back.

### CPP-12 An `#if` that does not parse is true

- **Symptom.** `#if` with nothing after it, `#if 1 +`, `#define W` /
  `#if W`: "syntax error", and the lines up to `#else` or `#endif` are
  written.
- **Mechanism.** yacc's `yyparse()` returns 1 on an error it cannot
  recover from (cpy.y has no error rules), and `control()` (text 12dc)
  takes 1 as true. v7 too.
- **Evidence.** Emulator (the triggers), fuzzing.
- **This project.** Reproduced.
- **Fix idea.** Take the error as false (`yynerrs`).

### CPP-13 The token after an object-like macro's name is dropped

- **Symptom.** `#define X+1` defines `X` as `1`, `#define Y"s"` as
  nothing.
- **Mechanism.** `dodef()` (text 0e22..0e3f) reads the token after the
  name to see whether it is `(` or a linefeed, and starts the body after
  it whatever it is - meant for the blank. v7 too.
- **Evidence.** Emulator (the triggers).
- **This project.** Reproduced.
- **Fix idea.** Start the body at that token unless it is a blank.

### CPP-14 A missing include file is reported one line late

- **Symptom.** `#include <nosuch.h>` on line 1 gives `a.c: 2: Can't
  find include file nosuch.h`; on line 3, `4:`.
- **Mechanism.** The `#include` line's linefeed is counted before
  `doincl()` looks for the file; when no directory has it, `doincl()`
  (text 0c99..0ca2, the call to `pperror()` at 0bd7) reports with
  `lineno[ifno]` as it is. v7's source steps it back for its other
  messages after a line (`missing )`, `redefined`). v7 too.
- **Evidence.** Emulator (both triggers; found 2026-10-25 while checking
  the headers in the MUTOS root).
- **This project.** Reproduced.
- **Fix idea.** `--lineno[ifno]` around the message, as `dodef()` does
  for `redefined`.

## `ld`

Read in full on 2026-10-22 for its port (`src/mutos_ld/`,
`tests/realbin/ldmap.txt`); every entry with a trigger was run under the
emulator - most are cases of `tests/realbin/ldcmp.py cases` -, and the
port does the same. All but LD-04 and LD-08 (the MUTOS deltas
`bytrel()` and the six bytes of "jmp $") are v7's own. Not bugs: `-r` and
`-d` refused ("-r and -d flags not supported", exit status 4) - a limit,
which `mutos_ld` lifts as an extension -, and `-n` making a 0411 output
like `-i`.

### LD-01 A long `-l` name runs over the strings after it

- **Symptom.** `ld x.o -lqqqqqqqqqqqqqq` (14 characters, no such library)
  says `ld:/usr/lib/libqqqqqqqqqqqqqq.a: ` and nothing more; with 30
  characters the message is `qqqqqqqqqqqqqq.a`, the tail of the name.
  Longer names reach the formats `error()` prints with (`%s`, `(%.14s)`,
  `: `, `%s\n`) and the later messages; from 124 characters libc.a's
  stdio structures. The strings stay changed for the rest of the run,
  also when the library is found.
- **Mechanism.** `getfile()` (text 1aad..1afe) writes `-lX`'s `X` into
  the 28-byte string `/usr/lib/libxxxxxxxxxxxxxxx` (data 0260) from its
  13th byte on, then `.a` and a 0 - no length check. With 14 characters
  the 0 lands on `cannot open` (data 027c), the message `error()` then
  prints.
- **Evidence.** Emulator (`ldcmp.py cases`: `-l of 12 characters` to
  `-l of 100 characters`, `-l overwriting the formats`).
- **This project.** Reproduced (the strings are in the image); a write
  into libc.a's data refused.
- **Fix idea.** A buffer of its own, or "name too long" past 13.

### LD-02 More than 256 archive entries run past `liblist[]`

- **Symptom.** A link that takes more than about 250 members from
  archives - or names `-lc` 300 times - stops with `ld:crt0.o: internal
  error: symbol not found` (exit status 4).
- **Mechanism.** Each member pass 1 loads and each archive's end mark
  takes one of `liblist[256]`'s entries (`step()`, text 0886..08a0 and
  08dc..08f1; `load1arg()`, 07ab), with no check; the 257th is written over
  `cursym` (data 2e3c), the next ones over `symtab[]` from its first
  entry - a symbol's name becomes 0xff bytes and pass 2 cannot find it.
- **Evidence.** Emulator (`ldcmp.py cases`: `300 members from a table`,
  `300 members by a scan`, `-lc 300 times`; 250 members link).
- **This project.** Reproduced (`liblist[]` and what follows are in the
  image).
- **Fix idea.** Check `libp` against `&liblist[NROUT]`.

### LD-03 `ldrand()` reads a type through a null pointer

- **Symptom.** None seen: for every table-of-contents name not in the
  symbol table, the "type" compared with EXTERN+UNDEF is the word at data
  0008 - `crt0`'s code segment of the system-call far pointer -, so the
  member is skipped as it should be unless the code segment is 0x0020.
- **Mechanism.** `ldrand()` (text 093a..0945): `slookup()` returns a hash
  slot, never 0, so v7's test `(pp = slookup(...)) == 0` never fires and
  `sp = *pp` is 0 for an unknown name; `sp->stype` is read at address 8.
- **Evidence.** Code reading (under the emulator the word is 0x1000).
- **This project.** Reproduced (the image's crt0 word 8 is the emulator's
  0x1000).
- **Fix idea.** `if (*pp == 0) continue;`.

### LD-04 An undefined external does not complete a byte-shifted word

- **Symptom.** For data `.byte 0` / `.word d` / `.word und` (`d` defined,
  `und` undefined - both words byte-shifted), the carry out of `d`'s low
  byte is not added to `d`'s high byte but to `und`'s: data words `ff00
  0000 0000` with relocation `8004 8008 0000` give `00 05 00 00 01 00`
  where `00 05 01 ...` is meant (`d` = data 6). Only with an undefined
  symbol, so the link fails anyway (`Undefined:`, exit status 1, the
  output not executable).
- **Mechanism.** `load2td()` (text 1478..14b2) sends every word through
  `bytrel()` but one with a relocation to an undefined external.
  `bytrel()` completes a byte-shifted word (its high byte is the next
  word's low byte) on its next call - here the word after.
- **Evidence.** Emulator (`ldcmp.py cases`: `byte-shifted before an
  undefined`).
- **This project.** Reproduced; under `-r` (the extension) the word goes
  through `bytrel()` and is completed.
- **Fix idea.** `t = bytrel(t, 0, r, creloc)` there too.

### LD-05 "Entry point not in text" leaves a stale entry word

- **Symptom.** `ld -e _fdat x.o` (`_fdat` a data symbol) says `ld:Entry
  point not in text` (exit status 1) and writes the header with the
  entry word of the last input's header (`x.o`'s 0x1234 in the case).
- **Mechanism.** `setupout()` (text 0f60..0f6e) reports and does not set
  `filhdr.entry`; the header is still the last one `readhdr()` read.
- **Evidence.** Emulator (`ldcmp.py cases`: `-e not text, an entry
  word`).
- **This project.** Reproduced.
- **Fix idea.** `filhdr.entry = 0` there.

### LD-06 A warning clears an earlier error

- **Symptom.** `ld x.o y.o p.a -lt`, with `p.a` holding a member without
  relocation bits and `libt.a` an out-of-date table of contents: `ld:p.a(nr.o):
  No relocation bits`, `/lib/libt.a(__.SYMDEF): out of date (warning)`,
  exit status 0 and the output made executable; without `-lt` exit
  status 1. Any level-1 error of pass 1 (an overflow, "No relocation
  bits") is cleared by a later warning; the warning is printed without
  the `ld:` an error after a warning gets again.
- **Mechanism.** `error()` (text 1ed1) ends with `errlev = n` - also for
  the warning's n = 0.
- **Evidence.** Emulator (`ldcmp.py cases`: `a warning clears an error`).
- **This project.** Reproduced.
- **Fix idea.** Raise `errlev`, never lower it.

### LD-07 Objects of 32K or more of text or data

- **Symptom.** An object whose text (or data) is 32,768 bytes or more
  cannot be linked: under the emulator `ld:big.o: Local symbol botch`
  (exit status 4) for a 32K text with one external reference.
- **Mechanism.** `load2()` (text 114e..1168) adds `tsize` and `dsize`
  sign-extended to the long position of the symbol table (`load1()` adds
  them as a 16-bit sum, zero-extended); from 0x8000 the position is 64K
  too small - negative for a file of its own -, and the data's relocation
  likewise (`loc + tsize + dsize/2`, text 12fd). What is read there
  depends on the kernel: v7's `seek()` takes a negative offset and the
  read after it fails; the emulator refuses the seek and reads on from
  where the file was.
- **Evidence.** Emulator (`ldcmp.py cases`: `text 32K`), code reading.
- **This project.** Refused (an `lseek()` to a negative offset).
- **Fix idea.** Unsigned sizes in `load2()`.

### LD-08 A text of 65,530 bytes or more wraps

- **Symptom.** An object of 65,530 bytes of text links without a message
  (exit status 0, the output executable) and the header says text 0;
  65,528 bytes give 65,534.
- **Mechanism.** `middle()` (text 0cdd) adds the six bytes of "jmp $"
  that `finishout()` writes to `tsize` without `add()`'s check.
- **Evidence.** Emulator (`ldcmp.py cases`: `text 65530 (6 bytes wrap)`).
- **This project.** Reproduced.
- **Fix idea.** `tsize = add(tsize, 6, "text overflow")`.

## `cc`

Read in full on 2026-10-24 for its port (`src/mutos_cc/ccport/`,
`tests/realbin/ccmap.txt`); every entry was run under the emulator with
`fork` and `exece` faked - each is a case of `tests/realbin/cccmp.py
cases` -, and the port does the same. CC-01, CC-02, CC-04 and CC-06 are
v7's own (`v7/cc/cc.c` has the same code); CC-03 and CC-05 come with the
MUTOS deltas (`-K`, the new `-o`). One more behaviour is listed under
"Suspect".

### CC-01 A source's name runs past `ts[]` or its `malloc()` block

- **Symptom.** Sources with long paths make `ld` get a wrong name: nine
  sources of 99 characters, one of 29, one of 69 (`pp...p/a.c`) and then
  `qq...q/b.c` (64 characters) link `ld -X /lib/crt0.o a.o ! -lc` - `!`
  where `b.o` belongs. With more of them `malloc()`'s list of blocks is
  broken; under the emulator `cc` then follows a pointer made of two
  characters of a name.
- **Mechanism.** `copy()` (text 0eb2) copies a name - `setsuf()` copies
  every source's whole path, once for the object list and once for each
  `as` call - to `tsp` and only then checks `tsa + 1000 < tsp`, for a
  buffer of 1050 bytes: `ts[]` (data 22e8) or a block from `malloc()`. A
  name of 50 characters or more that starts near the end runs past it -
  out of `ts[]` into `av[]` (2702, then `clist[]` at 2766), where the
  next pass's arguments overwrite it; out of a block into the header word
  of the block after it, which `malloc()` (text 189c) then follows.
- **Evidence.** Emulator (`cccmp.py cases`: `ts[] full at tsa + 1000, a
  name into av[]`, `ts[] full, a name into clist[] (refused)`, `malloc()
  blocks, one overrun (refused)`).
- **This project.** Reproduced in `ts[]`, `av[]` and the blocks; refused
  where a pointer leads past the break (memory the original has not
  allocated: what MUTOS has there is not known).
- **Fix idea.** Check before the copy (`if (tsp + strlen(as) + 1 > tsa +
  CHSPACE + 50)`), or `malloc()` each name.

### CC-02 `-P` and `-E` fill `ptemp[]` without a check

- **Symptom.** `cc` with 101 `-P` options runs `cpp a.c a.i` with 104
  arguments more - the `-P`s, then words of `main()`'s frame and of the
  arguments area as pointers: strings of crt0's code (`ëþëþ`), `a.o`,
  `g`. A 102nd `-P` is written into the arguments area.
- **Mechanism.** `main()` (text 01b4..01d1) stores `-P` and `-E` into
  `ptemp[100]` (bp-d4) without the check the `-D`, `-I`, `-U`, `-C` case
  has (`Too many DIUC options`, text 0248). The 101st store lands on
  `pv` itself (bp-0c, the word after `ptemp[]`), so `pv` becomes the
  argument's address plus 2 and the next ones go wherever that is. The
  loop that copies `ptemp[]` into `cpp`'s `av[]` (text 07bd) runs up to
  that `pv`: with 101 `-P` 221 words, past `av[]` over `clist[]` and
  into `llist[]`; from about 150 `-P` on over the flags after it into
  libc.a's stdout buffer.
- **Evidence.** Emulator (`cccmp.py cases`: `100 -P`, `101 -P`, `102
  -P`; `cccmp.py files` with 150 and 300).
- **This project.** Reproduced (`main()`'s frame and the arguments are
  in the image); refused where the copy reaches libc.a's buffers.
- **Fix idea.** The `-D` case's check for `-P` and `-E` too.

### CC-03 A long `-K` runs over the passes' paths

- **Symptom.** `cc -K123456789 a.c` runs `cpp`, then tries `c0` under the
  empty name and says `Can't find ` (exit status 1); `-Kxxxxxxxxxxxx`
  says `Can't find xxx`.
- **Mechanism.** `-K` (text 01d8) appends its argument to `kopt[10]`
  (data 1f68, `"s"`) with `strcat(kopt + 1, ...)` - no length check. The
  string's 0 lands on `pass0[]` (1f72) from 9 characters on; longer ones
  run on over `pass1[]`, `pass2[]`, `passp[]`, `passa[]` and the strings
  after them.
- **Evidence.** Emulator (`cccmp.py cases`: `-K into pass0`, `-K into
  pass1`, `-K over the strings`).
- **This project.** Reproduced.
- **Fix idea.** Refuse a `-K` of more than 8 characters.

### CC-04 A long `-B` prefix runs over the next path

- **Symptom.** `cc -B<25 characters> -t2 -O a.c` runs `cpp` as
  `bbbbbc2` - `Can't find bbbbbc2`, exit status 1 - although `-t2`
  replaces only `c2`.
- **Mechanism.** `main()`'s loop over `chpass` (text 04f4..05bb) builds
  each replaced pass with `strcpy(passN, npassname)` and `strcat(passN,
  "cN")` into arrays of 20 bytes (data 1f72, 1f86, 1f9a, 1fae, 1fc2): a
  prefix of more than 17 characters runs into the next array - `pass2[]`
  into `passp[]`, `passp[]` into the `as` path `passa[]`.
- **Evidence.** Emulator (`cccmp.py cases`: `-B long`, `-B into pass1`,
  `-B long -t1`).
- **This project.** Reproduced.
- **Fix idea.** Longer arrays, or "prefix too long".

### CC-05 `-o` before an option, and no object after it

- **Symptom.** `cc -o -x prog` (`prog` an object without the `.o`
  suffix) links `ld -X /lib/crt0.o -o "" -x prog -lc`: `ld:cannot create
  output`, exit status 4.
- **Mechanism.** `-o` (text 0145..01a3) reads its name again as an
  argument when it is a `.c` or `.o` name or starts with `-`, and sets
  `outfile` to -1 until an object comes (`x.o`, or `x.c`'s `x.o`); its
  name without the suffix becomes the output's (see "Suspect"). Without
  one `outfile` stays -1, and `ld` gets the string at address ffff -
  under the emulator a 0 above the argument strings; on the hardware
  whatever the top byte of the segment is.
- **Evidence.** Emulator (`cccmp.py cases`: `-o -lm`, `-o never
  resolved, a library`).
- **This project.** Reproduced as under the emulator (the image has the
  emulator's layout of the arguments).
- **Fix idea.** v7's "Would overwrite" for a `.c` or `.o` name, a usage
  message for a missing one.

### CC-06 With `-P`, copies of a null pointer

- **Symptom.** None visible: `cc -P a.c` makes the temporary names
  `tmp1`, `tmp2`, `tmp3` (and `tmp5` with `-O`) from the bytes at
  address 0 - crt0's first instruction, `e9 7d` - and writes their `'1'`,
  `'2'`, ... six bytes after each such string's end, into `ts[]`. None
  of them is used.
- **Mechanism.** `tmp0` is made only without `-P` (text 05d8), but
  `copy(tmp0)` (text 067c..06b1) runs in any case.
- **Evidence.** Code reading; emulator (`cccmp.py cases`: `-P a.c`, the
  same names in `ts[]`).
- **This project.** Reproduced (the text is in the image, read only).
- **Fix idea.** Make the copies only when `tmp0` is set.

## `c2`

Read in full on 2026-10-25 for its port (`src/mutos_cc/c2port/`,
`tests/realbin/c2map.txt`, `c2dis.py`); every entry was run under the
emulator - each is a case of `tests/realbin/c2cmp.py cases` -, and the
port does the same. None needs input that `c1` writes: `c1` puts no label
alone on a line, no `test` without a comma, no `lea` into memory, no
operand of 32 characters (C2-03 needs a long name, C2-06 a jump to a label
that is not defined). C2-03, C2-06, C2-07 and the empty destination of
C2-01 are v7's own (`v7/cc/c20.c`, `c21.c` have the same code; what C2-06
hits is the MUTOS layout's); C2-01's constants, C2-02, C2-04 and C2-05
come with the MUTOS deltas.

### C2-01 A destination equal to a constant, or empty, loops for ever

- **Symptom.** `mov ax,*5.` / `mov *5.,bx` - an immediate as a
  destination -, or an instruction without operands (`mov` alone on its
  line) where a register's entry is empty, and `c2` never ends. A source
  operand of exactly 32 characters gets there too (C2-03).
- **Mechanism.** `dest()` (text 1b96) forgets every register that holds
  the destination: as long as `findrand()` (text 1cb2) finds the
  destination among `regs[]`, it clears the entry found. v7's
  `findrand()` searched the six registers' entries and returned the
  index; the MUTOS one searches all twelve - the registers' contents and,
  in `regs[6..11]`, their constants - and returns the index modulo 6, the
  register's number (what `repladdr()` wants). So a match on a constant
  clears the register's other entry, never the matching one, and the loop
  finds it again. An empty destination matches an empty entry, and
  clearing an empty entry leaves it empty - v7's loop does the same.
- **Evidence.** Emulator (`c2cmp.py cases`: `C2-01: a destination equal
  to a register's constant`, `C2-01: an instruction without operands`,
  `C2-03: a source operand of 32 characters`, `an operand of 32
  characters`); the emulator stops the original inside the loop.
  Differential fuzzing: 24 of 500 damaged cases (seed 3) ran into it.
- **This project.** Refused: the port stops when the entry it just
  cleared still matches.
- **Fix idea.** Clear `regs[j]` itself, the entry that matched, and skip
  an empty destination.

### C2-02 "mov/push converted to push" is never counted

- **Symptom.** `c2 -` reports `0 mov/push converted to push` whatever it
  converted: on `mov ax,*4.(bp)` / `mov dx,ax` / `push dx`, which becomes
  `push *4.(bp)`, it says `2 redundant moves` and `0 mov/push converted to
  push`.
- **Mechanism.** `pushopt()` (text 2362) turns `mov r,x` / `push r` into
  `push x` and increments `redunm` (data 07dc), the counter of redundant
  moves; `npush` (07f8), the counter `main()` prints for the conversion,
  is never incremented.
- **Evidence.** Emulator (`c2cmp.py cases`: `C2-02: mov/push not
  counted`).
- **This project.** Reproduced.
- **Fix idea.** Increment `npush` there.

### C2-03 Fixed buffers filled without a check

- **Symptom.** With a source operand of 31 characters, `mov ax,_zz...z` /
  `mov bx,ax` / `mov cx,_zz...z` / `add dx,cx` / `mov si,ax` is
  optimized to three lines; with 33 or more nothing changes. At exactly 32
  `c2` never ends (C2-01). Longer operands overwrite the opcode hash
  table - a destination from 160 characters on, a source from 192 - and,
  through `repladdr()`, from about 220 characters the first nodes: later
  lines are read as other instructions or through pointers made of
  characters, or `oplook()` goes round the table for ever.
- **Mechanism.** `dualop()` (text 1c6a) copies the destination into
  `regs[13]` (data 09ac) and then the source into `regs[12]` (098c), 32
  bytes each, without a check: a source of 32 characters puts its 0 on
  `regs[13][0]` - an empty destination -, a longer one its tail, so that
  the destination looks like a name and nothing about the register is
  kept. A destination runs over the 32 unused bytes at 09cc, `conloc`,
  `conval`, `ccloc` and into `ophash[]` at 0a4c; `oplook()` (text 081a)
  then follows those words as pointers to the opcode table. `oplook()`
  copies the first token into `tmpop[32]` (0eee), over `labhash[]`
  (0f0e), which `refcount()` fills anew before it is read; `repladdr()`
  (text 1dee) copies the operands into `rt1[50]` (100c) and `rt2[50]`
  (103e), over libc.a's one-byte buffers (1070), `_doprnt()`'s work area
  (1084) and, from 1120, the heap - the nodes.
- **Evidence.** Emulator (`c2cmp.py cases`: `a first token of 30 .. 40
  characters`, `an operand of 31 .. 100 characters`); differential
  fuzzing with operands of up to 480 characters (500 damaged cases, seed
  3: 49 refused by the port, 26 where the original never ends).
- **This project.** Reproduced - the whole data segment and the heap are
  in the image; refused where `oplook()` follows a pointer made of text
  past the break (what MUTOS has there is not known; the emulator gives
  zeros).
- **Fix idea.** Bounded copies, and "operand too long" for the rest.

### C2-04 A `test` without a comma is removed

- **Symptom.** `mov ax,bx` / `test ax` / `beq L1` becomes `mov ax,bx` /
  `je L1` - the branch then tests the flags of whatever came before -,
  and `c2 -` counts `1 redundant tst's`.
- **Mechanism.** `rmove()`'s `test` case (text 0fb8, jump table 1441)
  removes a `test` whose operand equals `ccloc`, the place the flags were
  last set from. v7's `setcc()` filled `ccloc`; the MUTOS `c2` has no
  `setcc()`, `ccloc` (0a2c) is only ever cleared, so only an empty operand
  matches - and `dualop()` leaves the second operand of `test ax` empty.
- **Evidence.** Emulator (`c2cmp.py cases`: `C2-04: test without a
  comma`).
- **This project.** Reproduced.
- **Fix idea.** Leave a `test` with an empty operand alone (or set
  `ccloc` as v7 did).

### C2-05 `lea` into memory clears `regs[-1]`

- **Symptom.** `c2 -` on `mov ax,*1.` / `jmp L1` / `L3:inc bx` /
  `L1:cmp ax,*1.` / `beq L9` / `lea _x,(si)` / `L9:inc cx` / `jmp L3`
  reports `0 redundant jumps`; with `lea bx,(si)` it reports 1 - the
  same jump was redirected in both. `lea _x,(si)` itself becomes `mov
  _x,si`.
- **Mechanism.** `rmove()` turns `lea d,(r)` (`r` one of di, si, bp, bx)
  into `mov d,r` and then forgets the destination register:
  `regs[isreg(d)][0] = 0` and the same for `regs[isreg(d) + 6]`. For a
  destination that is no register `isreg()` is -1: `regs[-1]` is data
  07ec, the low byte of `nredunj`, the "redundant jumps" counter; and
  `regs[5]`, cx, is forgotten.
- **Evidence.** Emulator (`c2cmp.py cases`: `C2-05: lea into memory`,
  `C2-05: lea into a register`).
- **This project.** Reproduced.
- **Fix idea.** Forget a register only when `isreg()` found one.

### C2-06 A jump to an undefined label changes `br`

- **Symptom.** `br L5` / `inc ax` / `L5:br L99` (no `L99:`), then after a
  `.globl` `br L5` / `inc ax` / `L5:` - the second `br L5` is written `je
  L5`, a conditional branch; every `br` read after that point is one.
- **Mechanism.** A branch to a label that is not defined keeps a null
  `ref`. `iterate()` (text 0a7a) redirects a branch whose label is
  followed by a `br` to that `br`'s target and increments the target
  label's `refc` - for an undefined target the `refc` of the "node" at
  address 0, the word at data 000c: `optab[0]`'s opcode, `br`'s JBR (1),
  which becomes CBR (2) - each further such redirection adds one.
  `oplook()` reads the opcode from `optab[]` at each line, so from then
  on `br` is CBR with condition 0, `beq` (`shortbr()` writes `je` for a
  short one). v7's `iterate()` has the same increment; on the PDP-11 it
  hit whatever v7's `c2` had at address 0. The other use of a null `ref`
  only reads: for a conditional branch before a `br`, `iterate()` goes
  back from it through crt0's word 4.
- **Evidence.** Emulator (`c2cmp.py cases`: `C2-06: a jump to an
  undefined label`, `C2-06: an empty line between`, `C2-06: a branch to
  code`).
- **This project.** Reproduced: the image holds crt0's words and
  `optab[]` at their addresses.
- **Fix idea.** Test `rp->ref` before the increment.

### C2-07 An empty line is taken for the list's head

- **Symptom.** `L13:` alone on a line, then `jmp L13`, gives `j L20000`
  and no label `L20000` - the assembler or `ld` then fails. With two
  such labels (`L10:` / `L13:` / `jmp L13`) `c2` never ends.
- **Mechanism.** `getline()` (text 04e6) returns a label at its `:`; the
  rest of the line, here only the linefeed, is read as a line of its own
  - an empty line, a node with op 0 and no code, as the list's head
  `first` (data 07c6) has. (The MUTOS `getline()` drops an empty line
  after a line, not after a label.) `xjump()` (text 0c3c) goes back from
  a jump and from its target as long as `equop()` (text 19d2) finds the
  nodes equal: the empty line before `jmp L13` equals `first` before
  `L13:`. So `insertl()` puts a new label `L20000` before `first` - its
  links into crt0's words at address 0 - and the empty line becomes `br
  L20000`; `output()` starts after `first` and never writes the label.
  With two labels the steps leave the list in a circle, and `iterate()`
  deletes the code after a branch for ever.
- **Evidence.** Emulator (`c2cmp.py cases`: `C2-07: an empty line taken
  for the head`, `C2-07: endless`).
- **This project.** Reproduced; refused where `iterate()` would delete
  for ever - once it has deleted more nodes after one branch than the
  data segment can hold.
- **Fix idea.** Drop the empty line after a label too, or stop
  `xjump()` at `first`.

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
  overwritten); traced under the emulator, 2026-10-20. Read in the binary
  for the port of `cc` (2026-10-24): `main()` (text 0145..01a3) reads the
  name again as an argument (`i--`) and sets `outfile` to -1; the first
  object put into the list after that (text 0479..04af) becomes the
  output's name, `setsuf(t, 0)` with the `.` cut off. A name starting
  with `-` is treated the same way - see CC-05 for the case without an
  object. `mutos_cc` reproduces it.
