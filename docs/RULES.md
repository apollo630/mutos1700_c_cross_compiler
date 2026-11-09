# The rules of the corrected mode and of `-1`

Every change the corrected mode and the switch `-1` make to what the tools
write is a rule, and every rule is listed here (`docs/TOOLCHAIN_PLAN.md`,
section 7.2, decided 2026-11-08). The corrected mode corrects bugs of the
originals (`docs/MUTOS_ORIGINAL_BUGS.md`, decision 13); `-1` makes `c1` and
`c2` write 80186/V30 code (decision 15). Until step F5 of the plan the
original's behaviour is the default.

## Switches and environment

| What | How |
|---|---|
| The corrected mode | `-F` to `cc`, `as` or `ld`; or `MUTOS_MODE=corrected` |
| The original's behaviour | `-H` to `cc`, `as` or `ld`; or `MUTOS_MODE=original` |
| 80186/V30 code | `-1` to `cc` (which gives it to `c1`, `c2` and `as`), or to `as` alone |
| The rule log (a developer aid) | `MUTOS_RULELOG=<file>` |

A switch counts over the variable; of `-F` and `-H`, the last one given
counts (in `ld` wherever it stands). `MUTOS_MODE` unset or empty is the
default; any value but `corrected` and `original` is an error (exit status
1). `cc` puts the mode a switch chose into the environment of the passes it
runs, so that `c0`, `c1`, `as` and `ld` need no new argument; `-1` it gives
to `c1` (a last argument `-1`, whose characters the original ignores), `c2`
(a first argument `-1`, taken off before the original's `main()` runs) and
`as` (after `-gC`). The names were confirmed by the project owner on
2026-11-08. `src/h/mutos_mode.h` has the details.

## What a rule is

One correction (named by its bug: C1-03, LD-08, ...), one V30 rule
(V30-PUSH, V30-SHIFT, V30-IMUL) or LIBC-F, which makes `ld` take the
corrected copy of `libc.a` (below), is

- a source file of its own in its tool's rules directory, named after the
  rule (`src/mutos_cc/c1rules/c1_03.c`), with one function
  `rule_<name>()` (`rule_c1_03()`);
- reached through one line in the ported code, its hook, which names the
  rule in its comment (`/* EXTENSION: C1-03, docs/RULES.md */`);
- a row of the register below.

Taking a rule out is deleting its file, its hook and its row; the
Makefiles compile every file of a rules directory. `scripts/check_rules.py`
(in `make test` and `make check-docs`) checks that the three agree and that
each correction's entry in `docs/MUTOS_ORIGINAL_BUGS.md` names this file.

A rule is in one of two states:

- **diagnostic** - its hook reports in the rule log when the original's
  wrong path is taken ("met"); what the tool writes is the original's in
  either mode. Step F0 put such hooks at the sites of the bugs that steps
  F1 and F2 correct, to learn which of them real code meets.
- **active** - in its mode (`-F`) or under its switch (`-1`) it changes
  what the tool writes ("applied"); otherwise it reports as a diagnostic
  one does.

## The register

| Rule | Tool | Switch | State | File | Hook | Record |
|---|---|---|---|---|---|---|
| C1-01 | c1 | `-F` | active | `src/mutos_cc/c1rules/c1_01.c` | `src/mutos_cc/c1port/c10.c`, `cexpr()`: the template of a row once matched | [C1-01](MUTOS_ORIGINAL_BUGS.md) |
| C1-02 | c1 | `-F` | active | `src/mutos_cc/c1rules/c1_02.c` | `src/mutos_cc/c1port/c12.c`, `acommute()`: a product or AND that folds to 0 | [C1-02](MUTOS_ORIGINAL_BUGS.md) |
| C1-03 | c1 | `-F` | active | `src/mutos_cc/c1rules/c1_03.c` | `src/mutos_cc/c1port/c10.c`, `delay()`: before the tree without its postfix `++`/`--` is compiled | [C1-03](MUTOS_ORIGINAL_BUGS.md) |
| C1-06 | c1 | `-F` | active | `src/mutos_cc/c1rules/c1_06.c` | `src/mutos_cc/c1port/c10.c`, `cexpr()`: the template of a row once matched | [C1-06](MUTOS_ORIGINAL_BUGS.md) |
| LD-06 | ld | `-F` | active | `src/mutos_ld/ldrules/ld_06.c` | `src/mutos_ld/ld.c`, `error()`: the error level set | [LD-06](MUTOS_ORIGINAL_BUGS.md) |
| LD-08 | ld | `-F` | active | `src/mutos_ld/ldrules/ld_08.c` | `src/mutos_ld/ld.c`, `middle()`: the six bytes of `jmp $` added to the text | [LD-08](MUTOS_ORIGINAL_BUGS.md) |
| LD-09 | ld | `-F` | active | `src/mutos_ld/ldrules/ld_09.c` | `src/mutos_ld/ld.c`, `middle()`: the text rounded to 64 bytes for `-i` and `-n` | [LD-09](MUTOS_ORIGINAL_BUGS.md) |
| LIBC-F | ld | `-F` | active | `src/mutos_ld/ldrules/libc_f.c` | `src/mutos_ld/ld.c`, `getfile()`: `-lX`'s `/lib/libX.a` opened | [The corrected copy of `libc.a`](#the-corrected-copy-of-libca) |

The rules directories: `src/mutos_cc/c1rules/` (`c1rules.c`: the mode,
`-1` and the rule log of `c1`), `src/mutos_ld/ldrules/` (`ldrules.c`: the
mode of `ld`); `c0rules/`, `c2rules/` and `src/mutos_as/asrules/` come with
their first rule. `c0` and `as` read the mode already (`c0lib.c`'s
`c0_mode`, `assemble.c`'s `as_mode`), `c2` its `-1` (`c2_v30`).

## The corrected copy of `libc.a`

The runtime bugs of `libc.a` are not corrected by a rule in a tool but in a
copy of the library (decision 14, step F3b): the corrected mode links
`lib/fixed/libc.a` of the MUTOS root where the original mode links
`lib/libc.a`, which stays the original archive. The rule LIBC-F of `ld`
does it: in the corrected mode `-lX` looks for `/lib/fixed/libX.a` before
the original's `/lib/libX.a` and `/usr/lib/libX.a` (the place decided by
the project owner on 2026-11-09), and logs `applied` where it takes it -
which accounts for a linked program that differs; its messages still name
the library `/lib/libX.a`. The copy is the original with some members
replaced in place (`src/libc_fixed/`, its `README.md`):

- `src/libc_fixed/orig/<name>.s` is a source of the member `<name>.o`,
  reconstructed from the real object, that `mutos_as -C` assembles to the
  original member byte for byte (`src/libc_fixed/check_copy.py`, in `make
  test`);
- `src/libc_fixed/fixed/<name>.s` is that source with its corrections,
  each change marked by a comment that names its correction (`| L-03
  ...`): every line of `fixed/<name>.s` that differs from `orig/<name>.s`
  lies in a change with such a mark, and every mark names a row of the
  register below for that member;
- the copy (`src/libc_fixed/fix_archive.py`, run by `make`) has every
  member of the original in its order, each the original's but those of
  `fixed/` and `__.SYMDEF`; a corrected member keeps its interface - the
  same global symbols, no new external reference -, so that what the
  original compiler made, and the original `cc` and `ld` on the hardware,
  link with it.

Taking a correction out is undoing its marked changes in `fixed/` (and
deleting the file when nothing is left) and its row.
`scripts/check_rules.py` checks the marks against the register;
`tests/libc_runtime/rtcheck.py --copy` (`make check-runtime`) checks what they
compute - every entry point with the models of the bugs this register names
dropped -, and `tests/libc_runtime/linkcheck.py` (there too) checks the copy on
whole programs: the goldens linked with it run as with the original, and a
trigger program per correction gives C's result with `cc -F`.

| Correction | Member | Source | What it changes | Record |
|---|---|---|---|---|
| L-01 | `convert.o` | `src/libc_fixed/fixed/convert.s` | `itof`/`itod`: AX cleared before the store loop, so that bytes 0 to 3 of the double are 0 - every int converts exactly | [L-01](MUTOS_ORIGINAL_BUGS.md) |
| L-03 | `convert.o` | `src/libc_fixed/fixed/convert.s` | `ftoi`/`dtoi` from 2\*\*15 in magnitude: compares with the immediate 0x8000, not with the word at address 8000; -32768 to -32768.99... give -32768 (the original raised SIGFPE from -32768.5 on), the rest SIGFPE | [L-03](MUTOS_ORIGINAL_BUGS.md) |
| L-04 | `aldiv.o` | `src/libc_fixed/fixed/aldiv.s` | `aldiv` (`l /= m`): the short path only for a divisor of 0 to 32767; any other divisor below 2\*\*16 in magnitude two `div`s once both operands are positive, from 2\*\*16 the sixteen steps compared unsigned - C's quotient for every divisor | [L-04](MUTOS_ORIGINAL_BUGS.md) |
| L-04 | `ldiv.o` | `src/libc_fixed/fixed/ldiv.s` | `ldiv` (`l / m`): the short path only for a divisor of 0 to 32767; any other divisor below 2\*\*16 in magnitude two `div`s once both operands are positive, from 2\*\*16 the sixteen steps compared unsigned - C's quotient for every divisor | [L-04](MUTOS_ORIGINAL_BUGS.md) |
| L-04 | `alrem.o` | `src/libc_fixed/fixed/alrem.s` | `alrem` (`l %= m`): a divisor of 32768 to 65535 in magnitude two `div`s as well, from 2\*\*16 the sixteen steps compared unsigned - C's remainder for every divisor | [L-04](MUTOS_ORIGINAL_BUGS.md) |
| L-04 | `lrem.o` | `src/libc_fixed/fixed/lrem.s` | `lrem` (`l % m`): a divisor of 32768 to 65535 in magnitude two `div`s as well, from 2\*\*16 the sixteen steps compared unsigned - C's remainder for every divisor | [L-04](MUTOS_ORIGINAL_BUGS.md) |
| L-05 | `dmath.o` | `src/libc_fixed/fixed/dmath.s` | `pmuld` (`dmul`, and `fmul`, `fmuld`, `fmuls`, `atof` through it): the a0\*b2 partial product from b2's word, `4.(di)`, not b1's - every product correctly rounded | [L-05](MUTOS_ORIGINAL_BUGS.md) |
| L-08 | `alrem.o` | `src/libc_fixed/fixed/alrem.s` | `alrem` (`l %= m`): the long way leaves the remainder it stores in DX:AX too, where compiled code reads the value | [L-08](MUTOS_ORIGINAL_BUGS.md) |
| L-10 | `stkmath.o` | `src/libc_fixed/fixed/stkmath.s` | `fcmp` (every comparison of floating values): compares without subtracting - signs, then the words from the top, unsigned, reversed for negative values, every zero +0 - so that no comparison raises SIGFPE and different values never compare equal; the flags and AH as before | [L-10](MUTOS_ORIGINAL_BUGS.md) |
| L-11 | `lconvert.o` | `src/libc_fixed/fixed/lconvert.s` | `ftol` (`(long)d`, `(unsigned)d`): 2\*\*30 to 2\*\*31 in magnitude converted without a shift, not taken for an overflow; from 2\*\*31 SIGFPE but for -2\*\*31 to -2\*\*31-0.99... | [L-11](MUTOS_ORIGINAL_BUGS.md) |
| L-12 | `stacks.o` | `src/libc_fixed/fixed/stacks.s` | `incfpsp`, `decfpsp`: the floating-point stack's ends compared unsigned (`jae`, `jb`), so that a stack across address 0x8000 works | [L-12](MUTOS_ORIGINAL_BUGS.md) |
| L-14 | `dmath.o` | `src/libc_fixed/fixed/dmath.s` | `dadd`, `dsub` (and every addition and subtraction through them): an operand 57 binary orders below the other is aligned and rounded (`cmp ah,*/39`), dropped from 58 | [L-14](MUTOS_ORIGINAL_BUGS.md) |

## The rule log

With `MUTOS_RULELOG=<file>` every hook a tool passes appends one line to
the file:

```
<tool> <rule> <what> [<key>=<value> ...]
c1 C1-03 met line=6 out=152-185 op=++
c1 C1-06 met line=7 out=264-384 stores=6
ld LD-06 applied from=1 to=0
ld LD-08 met tsize=65530
```

`met`: the original's path was taken; `applied`: the rule changed the
output. `line=` is the source line, as the tool's messages give it.
`out=` (`c1`) are the bytes of the output file - offsets, the end
exclusive - that `c1` wrote for the intermediate-code operation the rule was
met in: a whole statement, so that every byte a rule changes lies in its
range. `c1` notes an entry when the hook is passed and writes it when the
operation is done (`c1rules.c`), after asking the rule, if it gave a check,
whether the entry counts: a rule that must see the code it concerns (C1-06
counts the bytes of two stores) gets it back from the output file then, and
one that must see what compiling the statement did to a tree node (C1-03:
whether the name node left in the tree stopped being a long) gets the node
it named when its hook was passed. The rest of the keys are the rule's own.

## The accounting

`tests/rules/check_accounting.py` (in `make test`) compiles the construct
corpus, the floating-point probes, the kernel's sources in the tree and
`fuzz_c.py`'s programs (longs included) with `mutos_cc` - `cc -S`, `cc -O
-S`, `cc -c`, `cc -O -c`, and a link of every program - once as the
original does and once each with `-H`, `-F`, `-1` and `-F -1`, with the rule
log on, and requires:

- a byte of `c1`'s `.s` that differs lies in the `out=` range of an
  `applied` entry of `c1` from that run;
- `c2`'s output, an object or a linked program may differ only where the
  stage before it differed (and was accounted for), or where an `applied`
  entry of that tool says so; the messages and the exit status likewise;
- `-H` changes nothing, and logs nothing `applied`;
- under `-1`, each assembly reports one line, and it names 80186/V30
  instructions only when a V30 rule was applied; `cc -S -1` reports none.

With no active rule (step F0) that is: every output is the original's in
every configuration. Since step F1 the corrections of `c1` are active;
under `-F` (and `-F -1`) their `applied` entries account for what they
change, `c2`'s output, the objects and the programs following from it.
`--report` prints which rules the code met.

The corpus's links never meet a rule of `ld`; the checker links cases
that do, one per rule (those of `tests/realbin/ldcmp.py cases`), without
a switch, with `-H`, with `-F` before and after the files and with
`MUTOS_MODE=corrected`: without a switch each meets its rule, `-H` links
and logs as no switch does, the three corrected runs agree, and they end
otherwise - the exit status, the messages, the output's mode - only with
an `applied` entry of `ld`. `ld`'s corrections change how a link ends,
never the bytes it writes: the output is the same in every mode. Since
step F2, `tests/realbin/ldcmp.py cases` and `ldfuzz.py` also run with
`--mode corrected` (in `make check-realbin`): the port in the corrected
mode against the emulated original, on the hand-written cases and on
random links - a difference is allowed only in the exit status, the
messages and the files' modes, and only with an `applied` entry of
`ld`.

## What a correction computes

The accounting shows that every change belongs to a rule, not that the
change is right. For that, C's semantics on a 16-bit `int` is the
reference (`docs/TOOLCHAIN_PLAN.md`, section 7.2):
`tests/rules/check_corrected.py` (in `make test`) compiles small programs
that meet each active correction ("triggers") with `cc -S` as the
original does, with `-H`, with `-F` and with `MUTOS_MODE=corrected`, and
runs `c1`'s code under `tests/mutos_cc/fuzz/x86sim.py`: `main()`'s value
must be the original's as pinned there (a wrong value, or the message
`x86sim.py` stops with) without a switch and with `-H`, and C's with `-F`;
every active correction of `c1` must have a trigger. Then it runs
`fuzz_c.py --mode corrected` on two fixed seeds (one with longs): every
program must compute what C says unless it fails through a bug of the
original that the corrected mode does not correct yet - the rule log's
`met` entries say which (or the code is C1-13's comparison of two
immediates, which has no hook). With `--realbin` (in `make
check-realbin`) the triggers are also linked against the MUTOS root's
`crt0.o` and `libc.a`, with and without `c2` (`cc -o`, `cc -O -o`), and
run under `tests/realbin/mutosemu.py`.

For `ld` (since step F2) the reference is how a link must end: with
exit status 0 exactly when every message is a warning, the output made
executable exactly then, and an executable as long as its header says.
`check_corrected.py` links triggers of each active correction of `ld` -
the links the correction changes, and links next to them it must leave
alone - without a switch, with `-H`, with `-F` and with
`MUTOS_MODE=corrected`: without a switch and with `-H` each ends as the
original's link does (pinned: the exit status, whether the output is
executable, the messages), with `-F` as pinned for the corrected mode
and as a link must; the output's bytes are the same in all four, and the
rule log says `met` and `applied` exactly for the links the correction
changes. With `--realbin` the emulated original `ld` links each trigger
too and must end as `mutos_ld` does without a switch.
