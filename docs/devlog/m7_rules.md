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

## Step F1: the corrections of `c1` (2026-11-08)

The four bugs of `c1` that make wrong code without a message, in the
order of the plan - the smallest change first -, each a patch of its own.

### C1-03: a copy of the name node

F0's hook sat after `delay()` had compiled the tree, where the damage is
done: the name node `sdelay()` put back is in the tree and in the
increment at once, and `unoptim()`'s LTOI case has already made it the
low word. A correction there would have to undo what `unoptim()` did -
`offset -= 2`, `type = LONG` -, which holds only as long as that case is
the only one that touches the node. The hook moved before the
compilation instead: in the corrected mode the increment of a long gets
a copy of its name node (`getblk()`, the whole node), so the tree may do
with its node what it does and the increment keeps the long. A tree that
leaves the node alone compiles exactly as before, and so does the
increment - the copy is the node, field for field.

Whether the copy mattered is known only once the statement is compiled.
The rule log's checks ran with the statement's text so far (C1-06's);
`c1_rule()` now also hands a check the pointer the rule named when its
hook was passed (`c1rules.h`), here the node left in the tree: it is
looked at in `c1_rule_mark()`, before `getree()` reads the next
expression into the same memory. In the original mode the node is the
increment's too, so "met" means what F0's diagnostic hook meant, and the
counts of F0 stayed (`check_accounting.py --report`: C1-03 four times,
`09_amx.c` twice, `p31_long4`, `p34_long5`).

### Checking what a correction computes

`check_accounting.py` proves that a difference belongs to a rule, not
that it is right. `tests/rules/check_corrected.py` adds the semantics:
triggers per correction, run by `x86sim.py` without a switch (the
original's value, pinned) and with `-F` (C's value), and with
`--realbin` linked and run under `mutosemu.py`, through `c2` too -
`x86sim.py` does not read `c2`'s output (`br`), the emulator runs the
program as MUTOS would. The first draft of the kernel-shaped trigger gave
0 in both modes: the second `x = (int)t--` brought the original's and
C's values together again. A trigger has to keep the two apart to the
end.

`fuzz_c.py --mode corrected` is the other half: the original `c0` and
`c1` cannot be the oracle of the corrected mode, so the semantic check
decides alone, and a failure is excused only by a bug of the original
the build met (its rule log) or by C1-13's code. Seed 3, 3,000 programs
with longs, before and after C1-03: 2,731 and 2,742 correct, C1-03 19
times and never; the 8 programs that met C1-03 and still fail meet
C1-06, C1-01 or C1-13 as well.

### C1-06: skip the loop, not the stores

The plan's fix idea was a label after the two stores. A template cannot
print one: its upper-case letters are directives (`L` among them), and
`c1`'s labels are `L<n>`. Computing the right distance - 8 and the
stores' bytes - would mean foreseeing what `pname()` prints for the
destination and what `mutos_as` makes of it, before the row is printed.
But a jump past the loop alone needs neither: the three instructions of
the loop - the shift and the rotate by 1, `loop .-4` - are two bytes each
whatever the operands (the original's `loop .-4` relies on it), and with
a count of 0 the two stores write back the registers the row loaded from
the place they store to. `jz .+8` is the original's own form in the rows
for the value of `l << i` (s1944 ...). In the corrected mode
`rule_c1_06()` returns the row's template with `jz\256+16` (or
`jz\256+16.`) made `jz\256+8`, a copy made once per row and kept, since
`cexpr()` may match an inner row of the same kind while it prints the
outer one. Every such row changes, the file-scope long's whose jump was
right too; no kernel source has one.

The second trigger found two things around the rows through a pointer
(`*p <<= i`: `mov (bx),di` / `mov +2(bx),si`): `x86sim.py` did not read the
operand `+2(bx)` (it does now), and it let `jz .+16` pass over those
stores - it checked only for the three-byte `*n.(bp)` form - though they
are 2 and 3 bytes; it counts the bytes now (`store_bytes()`), as the rule's
check does, which counted `+2(bx)` as four bytes (`mutos_as` gives a plain
number from -128 to 127 a byte). Run over the 132 `.s` goldens, the old and
the new `x86sim.py` give the same results. Seed 3, 3,000 programs with
longs in the corrected mode: 2,959 correct after C1-06 (2,742 before), no
C1-06 left.

### C1-02: a comma before the 0

`acommute()` has the operands of the flattened `*` or `&` list in
`llist[]`, optimized already (`insert()` calls `optim()` on each), and the
folded constant last. In the corrected mode `rule_c1_02()` keeps the
operands with a side effect - by the F0 hook's own test: an assignment,
`++`/`--`, a call, anywhere in the operand - in their order as `SEQNC`
nodes before the 0, the node `c0` writes for a comma and `rcexpr()`,
`cbranch()` and the argument code already take (the first operand for
its effect, the second for the value). The nodes get the degree
`optim()`'s last lines give a binary node, computed here rather than by
calling `optim()` on them, which would optimize the operands a second
time. The comma's type is the 0's, the type the original's result has.

A trigger meant to check a comparison, `((u = 4) * 0) == 0`, stopped the
original with "C error: const": the product folds to 0 in `c1`, not in
`c0`, and the relation of two constants is C1-12's - a second way into
it, confirmed under the emulator with `(x * 0) == 0` (no side effect, so
the corrected mode meets it too). The trigger keeps to shapes the original
compiles; the finding is in C1-12's entry. Seed 3, 3,000 programs with
longs in the corrected mode: 2,960 correct, no C1-02 left; the generator
writes `e * 0` with a side effect rarely, so the triggers carry the weight.

### C1-01: the test the fallback appends

The row 40c2 is `HA`: `H` compiles the tree through `cregtab` - a null
table pointer in the original, which `cexpr()` turns into `regtab`, so that
`rcexpr()`'s fallback from `cctab` with its `or r,r` is not taken - and the
branch reads the flags of the computation's last instruction. Two ways to
correct it were weighed. Returning -1 at `H` for the three ops would send
the tree to `rcexpr()`'s fallback, which appends the test; but the
fallback calls `cexpr()` directly, where `H` goes through `rcexpr()` with
its `reorder()`, so the computation itself could change. A template
`HAorJ\311A,IA\n` keeps the computation and only appends the test: `J`
prints `b` for a char tree (as the fallback's `orb`), `\311` is a tab and
`I`, `IA` the register `H` left the value in (`ax` after a call, `di`
mostly). The hook moved to where C1-06's is - the template of a row once
matched -, so both corrections of a template sit on two lines together,
and `H` is the original's again.

Triggers need flags that `x86sim.py` knows and that make the original
branch wrongly: a comparison before (`if (y == 0)`, `if (k != 0)`) sets
them. Under the emulator the original's code gives 100 without `c2` and
44 with it - `c2`'s code leaves other flags behind -, C 1230.

### Two more bugs, from the file-scope fuzz

With every hook of `c1` active, the corrected mode has no `met` line of
`c1` left to excuse a failure, so the semantic check sees everything
else. `fuzz_c.py --scope --mode corrected`, seed 4: three of 3,000
programs failed with no bug to put them down to (the original mode shows
the same three), and the emulated original `c0` and `c1` write the same
code - bugs of the original, not of the port:

- **C1-14**: `optim()` replaces a relation of a relation with 0 - `< 0` by
  0, `>= 0` by 1 - without a look at the inner relation's side effects,
  as C1-02 does for a product (`((u = 6, a[i][i]) != 7) < 0`,
  `((n-- != a[k][k]) < (6 >> 4))`).
- **C1-15**: `(t >> 3) <= 0` branches with `ble` on the flags of `sar
  di,cl`, whose overflow flag the 8086 leaves undefined for a count other
  than 1; `x86sim.py` stops there. What the hardware's flag is after the
  last step of a multi-bit shift is not measured.

Neither is hooked; both are listed for step F4. A fuzz run that meets
them in the corrected mode reports them as WRONG or BAD - `check_corrected.py`
runs no `--scope` programs, and its two seed-1 runs meet neither.

## Step F2: the corrections of `ld` (2026-11-09)

The two bugs of `ld` the plan names - LD-06 and LD-08 -, each a patch of
its own, and what checking them brought up.

### LD-06: raise, never lower

`error()` ends with `errlev = n`; the rule returns the higher of the two
in the corrected mode. The level steers two things: the exit status and
the `chmod()` that makes the output executable (`delexit()`, through
`delarg`), and whether `error()` writes `ld:` before its message (only at
level 0). The second comes along: in the original a warning after an
error brings the level back to 0, so the next message starts with `ld:`
again; corrected, it does not - as a message after an error never does.
That is the only change in the messages, and the bytes of the output are
the same, so the correction is a matter of how the link ends.

A warning is n 0 and comes only from pass 1 (`load1arg()`, an out-of-date
table of contents); so do the level-1 errors it can clear - an overflow,
"No relocation bits". Errors of pass 2, "Undefined:" (`middle()`'s
`errlev |= 01`) and level 2 (which exits at once) are never cleared.

### LD-08: the original's `add()`

The fix idea was `tsize = add(tsize, 6, "text overflow")`, and that is the
correction - with the original's own `add()`, which is static in `ld.c`:
the hook hands it to the rule (`rule_ld_08(tsize, add)`), so the rule
needs nothing of `ld.c` but the string's address (`S_TEXTOVF`, `ld.h`),
and no second copy of the check exists. `add()` returns the 16-bit sum
whether it reports or not, so the output is the original's byte for byte
- the header says the wrapped size, as after a "text overflow" of pass 1
- and only the end changes: `ld:text overflow` (no file name: `filname`
is 0 after pass 1, as for `middle()`'s own "bss overflow"), exit status 1,
not executable. After an earlier error the message has no `ld:` - the
level is 1 already.

### LD-09: the rounding, with LD-08

LD-08's correction alone would have left `-i` and `-n` links of 65,468 to
65,529 bytes of text wrapping without a message (LD-09, found while
measuring LD-08, below) - the very kind of link the kernel's makefile
makes, though the kernel is far from the size. The rounding is v7's line,
a bug of its own, so it is a rule of its own with the same shape: the
hook is the line of the rounding (`tsize = rule_ld_09(tsize, add)` under
`if (nflag || iflag)`), the rule adds the 077 with `add()` in the
corrected mode and masks as the original does. A text that LD-08 has
reported has wrapped to a small number already and rounds without a
second message.

### How a link is checked

For `c1` the reference of a correction is C's value. For `ld` it is how a
link must end: exit status 0 exactly when every message is a warning, the
output executable exactly then, and an executable as long as its header
says (16 bytes, text, data, their relocation unless `relflg`, symbols).
`check_corrected.py` gets triggers of `ld` with that test, pinned ends for
both modes (the original's measured under the emulator, and compared with
it again under `--realbin` through `ldcmp.py`'s `compare()`), the output's
bytes compared across the four runs and the rule log's `met`/`applied`
lines required exactly where the end changes. Two of LD-06's five
triggers are links next to the bug that must stay as they are - a warning
before the error, a warning alone.

The accounting needed only a change of what it allows: until F2 `ld`'s two
cases had to end the same in every mode; now a corrected run may end
otherwise with an `applied` entry, and never write other bytes.
`ldcmp.py` and `ldfuzz.py` got `--mode corrected`, the port in the
corrected mode against the emulated original: `_judge()`'s verdict stands
but where the difference is only in the exit status, stdout or the files'
modes and the port's rule log has an `applied` entry of `ld`
("corrected"). On the hand-written cases that is LD-06's case and nothing
else; on 300 + 300 fuzz cases (seed 1), one LD-06 among the plain ones;
on seeds 2 to 4, 1,500 cases each, plain and damaged, 5 to 8 LD-06 in
each run (41 of 9,000) and no other difference.

### Three more bugs, from the test of a right link

LD-08's limits, measured before correcting it: 65,528 bytes of text give
65,534 without `-i`, but 0 with it - `-i` and `-n` round the text to 64
bytes after the six bytes of "jmp $", with no check either. From 65,468
bytes (the objects' texts are even) a `-i` link wraps; that is **LD-09**,
v7's own line. LD-08's correction alone would leave it: a non-`-i` link
of 65,528 bytes is right, a `-i` link of the same text says 0.

The test of a right link was first part of `--mode corrected` in the
fuzzers too. On seed 1 it flagged `ld z.a -D 4000`: exit status 0, the
output 6 bytes shorter than its header says. `-D` takes a size in pass 1
(`dsize = num`) and pads to an address in pass 2 (`while (dorigin <
num)`), and without `-i` the data's addresses start after the text - so
the padding is `tsize` bytes short. With a data symbol after `-D` the two
passes disagree about its value and pass 2 says "Multiply defined": the
existing case `-D` of `ldcmp.py` has shown that message since the port,
taken then for the original's way. That is **LD-10**, v7's own code too.
On damaged files (`--mutate 0.5`, seed 3) it flagged an object cut right
after its header: `dseek()` sets a stream's size to 0 when the read gives
nothing at the position, and the loops then read nothing - no "Premature
EOF", which only a stream running out midway gets. That is **LD-11**.

The rest it flagged were objects with an odd text size (the generator
makes them; `mutos_as` never does): `readhdr()` rounds the size up, and
the rest of the file is read one byte out of step - garbage in, an
inconsistent output out, as in the original. Telling those apart from a
new bug would take a model of the file the test does not have, so the
test stays with `check_corrected.py`'s triggers, and the fuzzers check
the accounting. Each new bug has a case in `ldcmp.py cases` (and `-D -i`,
which is right), 96 in all, every one the same as the original.

## Step F3: the runtime measured (2026-11-09)

The fourteen members of `libc.a` that compiled code calls without naming
them, measured against Python's arithmetic - the list the corrected copy
of step F3b is made from. No tool's output changed.

### Why a checker of its own

`x86sim.py` checks the compiler, not the library: it stands in for the
runtime with C's semantics (its `aldiv` computes C's quotient), so a bug
of the runtime cannot show there. `libcatof.py` runs `libc.a`'s own
`atof` and, for `ops`, `dmul`, `ddiv` and `dadd` on positive operands -
the arithmetic the assembler's constants need. What compiled code calls
is more: the long helpers, the conversions, the floating-point stack and
the sixteen operations on it, the comparisons, `chkstk`.

`tests/libc_runtime/rtcheck.py` links those members with `crt0.o` and a
`main` of two HLTs (`mutos_ld -i`), lets `mutosemu.py` run `crt0` up to
`main`, and then calls each entry point as `c1`'s code does: AX holding
an address or an int, SI and DI for `dmath.o`'s core, a long's words on
the stack (high word next to the return address), the slots of the
floating-point stack and `fpsp` written directly. The second HLT is the
return address. A model per known bug predicts the original's exact
output; the run fails on any difference no model predicts. That makes
the report a measurement - every wrong result is counted for the bug that
explains it - and keeps a second bug in the same routine from hiding
behind the first: L-14 came out as `UNEXPLAINED` in the additions while
L-05's model covered every product.

Two things of the emulator needed handling. Unicorn keeps an exception
pending after the interrupt hook stops a divide error, and the next
start is a double fault (interrupt 8) - so a call that ends in a divide
error is followed by a fresh process (the image is kept, `crt0` run
again). And `ld` places the commons (`fpstk` among them) before the BSS
of the objects, so the layout group pads with data, in objects of 16K
(`mutos_ld` refuses one of 32K, LD-07).

### What was expected, and what the measurement says

- L-01: `itof` and `itod` are one routine (`convert.o` text 0000 has both
  names), so `d = i` has the bug `c1`'s folding was known for: a third of
  the ordinary ints. A float is right - `fstsp` drops the four bytes.
- L-04: `ldiv` and `lrem` are `aldiv` and `alrem` on the arguments, so `l
  / m` and `l % m` have it as well. The precise shape replaces the old
  description: the long way gives ±65535 once the quotient reaches 65536 -
  not its low sixteen bits - and fails for -2\*\*31 whatever the dividend
  (`hardldiv`'s compare is signed); the short path's set 0x18000..0x1ffff
  divides by the low word, `99999L / 100000` is 2. The remainder is right
  but for -2\*\*31 and `-2**31 % 32768`. `c1_aldiv()`'s comment said "the low
  sixteen bits"; corrected.
- L-05: every product goes through `dmul`; a product is right when the
  operand `dmul` takes as `[si]` has a low word of 0 - a float. So `f * g`
  on floats is right, `d * f` (`flds f` / `fmuld d`, `[si]` the double)
  is not.
- L-02 and L-03 were code reading; both are measured now. L-02 changes no
  value: every routine takes an exponent byte of 0 for zero.

### What reading the callers found

The interface of a routine is what its caller reads. `c1`'s template for
`=*`, `=/`, `=%` on longs ends `call Id` - the result taken from DX:AX.
`almul` and `aldiv` leave it there; `alrem` does on its short path, but
its long way returns with DX the divisor's high word and AX the
remainder's (**L-08**): `i = (l %= m)` is wrong for every divisor outside
one word, though `l` is right. `lrem` moves its result to DX:AX.

The other new ones came from reading each routine next to its siblings:
`fsubrs` has `mov di,si` where `fdivrs` has `mov si,di` (**L-09**; `c1`
uses no reverse form, so compiled code never meets it); `fcmp` compares
by `dsub` and so inherits its overflow and underflow (**L-10**); `ftol`
takes a shift count of 0 for an overflow, so 2\*\*30..2\*\*31 raise SIGFPE
(**L-11**; `c1_ftol()`'s comment said 2\*\*31); `incfpsp`/`decfpsp`
compare addresses with `jge`/`jl` (**L-12**: the layout group links the
runtime 16 times, `fpstk` from 0x7e00 to 0x9000, and every placement from
0x7fc0 to 0x8008 fails); `chkstk` subtracts without looking at the borrow
(**L-13**). Each was a reading first and a model second; the model had
to match every case before the bug was entered.

### The one only the long runs found

**L-14** appeared in the first runs of 20,000 cases per entry point: an
addition of a power of two and an operand of the other sign exactly 57
binary orders below gave the power of two. `dadd` copies the larger
operand when the exponent bytes differ by more than 56; with a power of
two the result lies in the binade below, whose last place is half as
large, and a difference of 57 still counts there. The edge set has had
such pairs since (a power of two and an operand 55 to 59 binary orders
below), and the model makes the copy for a difference above 56.

### What is right

`lmul` and `almul`, `ddiv` and every division on the stack, the additions
and subtractions but for L-14, the loads and stores, `fneg`, `ftest`, the
stack's ninth push and empty pop (SIGFPE, as designed), and in every
routine the registers it must keep, the stack pointer and the direction
flag. A float store truncates - the double's high four bytes -, which C
allows.

### For F3b

The members with a bug that computes wrongly in ordinary programs, the
copy's by decision 14: `convert.o` (L-01), `aldiv.o`, `ldiv.o`, `alrem.o`,
`lrem.o` (L-04, L-08), `dmath.o` (L-05), `lconvert.o` (L-11). The edge
bugs are the owner's to choose. `rtcheck.py --archive` takes the members
an archive has (the rest from the original), `--fixed` drops the models
of what it corrects - the check of the copy, and of the kernel's
`arith.a`, which is not in the tree.

## Step F3b: the corrected copy of `libc.a` (2026-11-09)

The owner's choice after F3: the five bugs that compute wrongly in
ordinary programs (L-01, L-04, L-05, L-08, L-11) and four edge ones
(L-03, L-10, L-12, L-14); L-02, L-09 and L-13 stay. The copy is the
root's `lib/fixed/libc.a`, `ld -F` takes it for `-lc`, and the kernel's
`arith.a` gets a copy from the same sources.

### The reconstruction

The plan asks for a source per member that `mutos_as` assembles to the
original member byte for byte before anything is corrected - the proof
that the correction is a change of the original's code and nothing else.
`ldexp.s` (step 5) was written by hand. For nine members with 1,300
instructions a draft generator paid off: `src/libc_fixed/objsrc.py`
decodes the text with capstone, writes each instruction as the original
assembler reads it (the operand markers decided by the opcode - `83` and
`80` take `*`, `81` and the accumulator forms `#`; `xchg`'s register
field first; `b` after a mnemonic whose only byte operand is in memory),
the relocated fields as symbol and addend, the data as `.byte` runs, and
places `.globl` and `.comm` lines where the symbol table's order - the
order of first mention - needs them. Seven of the nine came out exact at
once; `stacks.o` needed its data in two parts (`fpsp` before the text,
the two messages after it) and `dmath.o` the local label where a target
has a global one too (`round` for `dround`'s address).

The jump targets the original reached without a symbol were first
written `.+N` - exact, but a correction that adds a byte breaks every such
jump across it. Numeric labels would have been the answer: `mutos_as`
takes `1:` and `1f` (its manual page says so) - and assembles `jns 1f` to
a jump onto itself and writes `1:` into the symbol table. The original
assembler has no numeric labels at all: "syntax error" under the
emulator. So the targets are `L1`, `L2` ... and the sources are
assembled with `-C`, which leaves defined local `L` symbols out of the
symbol table, as `cc -c`'s `as -gC` does: exact again, and safe to edit.
(The numeric labels are `STATUS.md`'s open item 8.)

Of the other members of `libc.a`, `objsrc.py` drafts 72 of 167 exactly;
the rest need equates and the like by hand - not needed now.

### The copy, and LIBC-F

`fix_archive.py` replaces members in place with `mutos_ar r` (the order
kept; the new member gets the old one's date, so that only its size and
contents change) and has `mutos_ranlib` make `__.SYMDEF` anew; the file
gets the table's date, so `ld` finds it up to date. It refuses a member
that is not the original's byte for byte - which makes it the tool for
the kernel's `arith.a` as well: it corrects the long helpers there if
they are `libc.a`'s, and stops otherwise. Its own MUTOS root, in a
temporary directory, gives `mutos_ar` its `tmp/` and `mutos_ranlib` its
`bin/ar`, so that it runs before the build tree's root exists.

The corrected mode reaches the copy through `ld`: the rule LIBC-F, a
hook in `getfile()` before the original opens `/lib/libX.a`. It is a rule
like the corrections - a file, a hook, a row - because the accounting
needs it: a program linked with `-F` differs from the original's when
the copy does, and `check_accounting.py` takes a difference of the link
stage only with an `applied` entry of `ld`. The copy's name is not in
the original's image, so the messages keep `/lib/libX.a`; the rule logs
the real one.

### The corrections, one patch each

Smallest first. Each is a change of `fixed/<name>.s` against `orig/`, every
changed line in a hunk with a `| L-nn` mark (`check_rules.py` now takes
every mark on a line, so that a header line can name two), a row of the
copy's register, and the bug's entry turned to "corrected in the copy".
Each was measured with `rtcheck.py --archive ... --fixed <the corrections
so far>` - a corrected bug's model dropped, so the original's behaviour
would have been `UNEXPLAINED` - and, where `rtcheck.py`'s cases are too few
for the place, by a measurement of its own:

- L-03: the fix idea (the immediate) alone was not enough - the test of
  the first fraction bit after it made `(int)-32768.5` raise SIGFPE even
  with the right constant; truncation toward zero wants -32768 for all of
  -32768.x, so the test went too.
- L-12: two jumps, unsigned. `rtcheck.py`'s layout group places the stack
  from 0x7e00 to 0x9000: every placement right.
- L-05: one displacement. Every product of `dmul` and the stack's
  multiplications correctly rounded.
- L-14: `cmp ah,*/39`. Whether the alignment rounds a difference of 57
  right was the open question: measured for differences 50 to 65, 6,000
  additions and subtractions each, powers of two among the larger
  operands - every result correctly rounded (the original: 1,201 wrong at
  57). With L-14 corrected, `fsubrs`'s model (L-09: `dsub` on the eight
  bytes after the float) had to use the corrected `dsub` too - a rare
  junk operand 57 orders below showed it in a full run.
- L-08: `mov dx,ax` / `mov ax,di` after the stores. `rtcheck.py`'s model
  of L-04's wrong remainders now leaves the stored value in DX:AX when
  L-08 is corrected.
- L-01: `sub ax,ax` before the jump to the store loop; all 65,536 ints
  convert exactly.
- L-11: `jl` to a new path for e > 31, `je` past the shift loop for e =
  31 (a count of 0 would have looped 65,536 times); e = 32 overflows but
  for a mantissa of exactly 0x80000000 with the sign set (-2\*\*31 to
  -2\*\*31-0.99...). 20,072 doubles around the limits: every result C's.
- L-04: not the fix idea (32 steps would need a 64-bit shift register the
  routine has no registers for) but the routines' own structure used
  right: the divisions' short path only for a divisor of 0 to 32767
  (`js hardldiv` instead of the negated sign word), and in the long way,
  once both operands are positive, a divisor below 2\*\*16 goes back to
  the two `div`s; from 2\*\*16 the sixteen steps are right - the quotient
  is below 2\*\*16 - once their compares are unsigned. The remainders the
  same: their short path (-32768..32767) was right; 32768 to 65535 in
  magnitude now take two `div`s in `hardlrem`. Three seeds, up to 20,000
  cases per entry point and set: every result C's.
- L-10: `fcmp` without `dsub`: every zero +0, signs that differ decide,
  then the top words and the words below, unsigned, reversed for negative
  values; AX then set as the difference's top word would be, so that the
  original's tail (`or ah,ah` / `or al,*/7f` / `lahf`) makes the flags and
  AH. 120,032 pairs differing in one bit, both signs, zeros of every form:
  every comparison right.

### The copy on whole programs

`linkcheck.py` links every assemblable golden (the real compiler's
output) with the original archive and with the copy: 73 of 122 get the
same image (no corrected member), the other 49 run to the same exit
status and output - the interface held. The goldens hardly meet the
corrected bugs, so `triggers/` has one program per correction, compiled
by the port's `cc` and `cc -F` in the root: the original's wrong value or
SIGFPE, then C's. Two needed care: L-05's product is wrong only when
`dmul`'s `[si]` - the memory operand of `fmuld`, the right operand of
`*` - has a nonzero low mantissa word (`b * a`, not `a * b`); L-12's
`fpstk` must lie across 0x8000 in both links, which `cc -i` gives (the
data segment its own; with 0407 the copy's longer text moved it).

### `arith.a`

The owner put his `arith.a` into `tests/mutos1700_arith/` the same day:
its six long helpers are `libc.a`'s byte for byte, so `fix_archive.py`
corrects it as it is; `make` builds `src/libc_fixed/arith.a`. Comparing
the copy's headers with the original's showed that `mutos_ar r` takes
owner, group and mode from the file it adds - `fix_archive.py` now puts
the old header's back, and `check_copy.py` checks every header. The
kernel's makefile names `arith.a` by its path, so the corrected kernel
takes the copy by hand (`docs/KERNEL_BUILD.md`, step 7), not by a rule.
