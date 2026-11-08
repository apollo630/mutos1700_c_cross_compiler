# Milestone 7 — the corrected mode and `-1`: the rules

The plan of 2026-11-08 (`docs/TOOLCHAIN_PLAN.md`, section 7) corrects the
bugs of the originals in a corrected mode and adds 80186/V30 code behind
`-1`, every change a rule of its own (`docs/RULES.md`). This part records
how the work was done and what it found; `STATUS.md` ("The corrected mode
and `-1`") has the current state.

## Step F0: the shared infrastructure (2026-11-08)

### The names

The plan left the names of the mode's switch and variable to a proposal
with F0. The letters had to be free in `cc`, `ld` and `as` - `cc` reads only
the letter after the `-` and hands an unknown one to `ld`; `ld` reads its
options letter by letter, `-xF` being `x` and `F` -, so a single letter
each way: `-F` ("fixed") for the corrected mode, `-H` ("historic") for the
original's behaviour, and `MUTOS_MODE=corrected|original`. The project
owner chose these from three pairs (the others: `-R`/`-V`, `-M`/`-N`) and
the variable from two (the other: `MUTOS_FIX=1|0`).

### How the mode reaches the passes

`c0` takes any fourth argument for `-p`'s profiling, so the mode cannot be
an argument of it without changing what the original's command line
means. The environment carries it instead: every tool reads `MUTOS_MODE`,
and `cc` sets it from its own `-F` or `-H` before the passes run. `-1`, which
only `c1`, `c2` and `as` need, goes as an argument: to `c1` after its
others (its options are the characters of its arguments after the third,
and the original ignores `-` and `1` - not as a character option `1`,
since `-K1024` gives `c1` the argument `s1024`, whose `1` would count), to
`c2` first (the original would take `-1` for `-`, its statistics; the
port takes it off before the original's `main()` runs), to `as` after
`-gC`. `cc`'s new options and what they do live outside its image
(`cclib.c`): the image is the original's 64K segment, and a command line
without them must do exactly what the original does - `cccmp.py` and
`ccfuzz.py` compare that. `ccfuzz.py` drew `1`, `F` and `H` among the
letters the original hands to `ld`; they are out of its list now.

### A rule's shape, and why

The owner's first concern was wrong code, or existing code broken little
by little by special cases. So a rule is one file, one function, one
hook line in the ported code, one row in the register, and
`scripts/check_rules.py` checks the four together. The Makefiles compile
every file of a rules directory (`$(wildcard ...)`), so deleting a rule's
file and its hook takes it out. The hooks of F0 are written so that the
correction of F1 can go into the rule's file: `rule_c1_02()` returns the
tree `acommute()` returns, `rule_ld_06()` the error level, `rule_ld_08()`
the text size.

Header names: `c0.h` and `ld.h` define macros for the globals of their
images (`line`, `errlev`, `tsize`), which reached a parameter named so in
a header included after them; `src/h/mutos_mode.h` names its locals
`mm_...`, and the rule prototypes avoid the names.

### The rule log and the accounting

The accounting (`docs/TOOLCHAIN_PLAN.md`, 7.2) asks that every byte that
differs belongs to a logged rule. For `c1`, whose output is a stream, the
range is the bytes written for the intermediate-code operation (one
statement) in which the hook was passed: `c1_rule()` notes the entry,
`c1_rule_mark()` - at the top of `getree()`'s loop and at its EOFC -
writes it with `out=<start>-<end>`, `ftell(stdout)` before and after. A
rule whose decision needs the code itself (C1-06 must count the bytes of
two stores) gets the operation's text back from the output file then:
`stdout` is flushed - its contents do not change - and the file read by
the name `c1` was given. That only happens with the log on.

`c1` knows line numbers but not the file; with the log on, `cc` puts
`cpp`'s input into `MUTOS_RULELOG_SRC` before the passes, and the entries
carry `src=`. Without the log, the passes' environment is as it was.

`tests/rules/check_accounting.py` attributes a difference stage by stage:
`c1`'s `.s` (`cc -S`) by the ranges; `c2`'s (`cc -O -S`), the object and the
linked program only where the stage before differed (a consequence, already
accounted) or where the stage's own tool logged an applied rule. It was
checked by a stand-in `cc` that appends a line under `-F`: six failures, as
there are six such runs. With no active rule the five configurations give
the same output for every source, which is V0.

### What the hooks found

- **C1-03 in the kernel.** `09_amx.c` (`11_kernel/`) copies to and from a
  board's memory with `setbyte(ds,(int)temp++,*src_off++)` and `x =
  getbyte(ds,(int)temp++)`, `temp` a local long: the increment is `inc
  *-16.(bp)`, its low word only. The real kernel has this code; a correction
  in F1 changes `amx.o` in the corrected mode.
- **C1-03's mechanism.** The entry said `unoptim()` distributed the
  conversion into the `++`. Reading `delay()` for the hook showed otherwise:
  `sdelay()` puts the operand back into the tree as `ncopy()` gives it, a
  copy only for a register variable; the node is in the tree and in the
  increment at once, and `unoptim()`'s LTOI case turns that very node into
  the low word. The hook checks the effect - an increment of a long whose
  operand is not a long any more - after `delay()` compiled the tree.
- **C1-01's third form.** The list 40c2 of `cctab` (the row `H`: branch on
  the flags of the computation) holds `<<` and `>>` too; by a count in CL
  that may be 0, `sal di,cl` leaves the flags as they were.
- **Counts.** In 3,000 programs of `fuzz_c.py` (`check_accounting.py --fuzz
  3000 --base-only --report`, seed 1, half of them with longs): C1-01 69
  times (all `~`; a `!` reaches the row only in shapes like `!y >= 0`, which
  the generator does not write), C1-02 once, C1-03 59 times, C1-06 237
  times (`p <<= i` with `i` a subscript).

### Longs in `fuzz_c.py`, and two new bugs

The generator gained long nodes beside its int ones, behind a parameter
(`generate(..., longs=True)`, `--longs`) that draws no random number when
it is off - `c0fuzz.py`, `c1fuzz.py`, `c2fuzz.py` and `check_accounting.py`
call `generate()` too, and their seeds must keep giving their programs
(checked on 1,200). Constants sit around the 16-bit borders (65535, 65536,
-32769 ...), where a lost carry shows; `r` is touched only by `++`/`--`, as
`n` is, so that no value depends on C's order of evaluation; no two
constants meet under one operator and no constant is shifted, since a
folded long constant shifted is C1-07, which the port refuses.

Run against the emulated original `c0` and `c1` (150 programs), the port
agreed everywhere; 12 programs were "as the real compiler": ten C1-06,
and two the generator found first -

- `x = ((int)100L <= 13)`: `c0` folds a relation of int constants but not
  an LTOI of a long constant; `c1` makes it a constant and `optim()` hands
  the relation to `const_()`, which has no case for it: "C error: const",
  and the relation becomes its left constant (C1-12);
- `if ((-70000L) < 100L)`: nobody folds it, and the long comparison prints
  `cmp *-2.,*0` and `cmp #-4464.,*100.`, which the original `as` rejects
  (C1-13).

Without the oracle, 1,500 programs (seed 3): every wrong or failing one
meets a hooked bug (the rule log of its compilation says which) or is
C1-13.

### `x86sim.py`: the V30 forms and clocks

The three forms of section 7.4 run only with `v30=True`; without it they
are errors, as `mutos_as` refuses them without `-1`. Their flags are the
NEC manual's (`docs/V20_V30_Users_Manual_Oct86.pdf`, the pages of `SHL
reg,imm8`, `SHR reg,imm8`, `SHRA reg,imm8` and `MUL reg16,reg16,imm8/16`):
for a shift by a constant CY is the last bit out, S and Z come from the
result, V and AC are undefined; for the multiplication CY and V say whether
the product passed 16 bits, S, Z, AC and P are undefined - and it writes
only its destination, so DX keeps its value (V3 must not rely on DX after
it). The clocks are the manual's uPD70116 figures at even addresses: on the
V30 an instruction's time does not depend on how its address is formed, so
one figure per form (register, memory, immediate; word or byte) is the
whole table (`v30_clocks()`); the dynamic ones - branches taken (14) or not
(4), `loop` (13/5), shifts (7 + n, 19 + n to memory), `cwd` (4 or 5) -
are counted where they run. A range "according to data" (`imul`, `idiv`) is
kept as a range. `check_x86sim_v30.py` pins twelve programs whose clocks were
summed by hand from the manual.
