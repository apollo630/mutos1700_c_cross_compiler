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

One correction (named by its bug: C1-03, LD-08, ...) or one V30 rule
(V30-PUSH, V30-SHIFT, V30-IMUL) is

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
| C1-01 | c1 | `-F` | diagnostic | `src/mutos_cc/c1rules/c1_01.c` | `src/mutos_cc/c1port/c10.c`, `cexpr()`: the template letter `H` | [C1-01](MUTOS_ORIGINAL_BUGS.md) |
| C1-02 | c1 | `-F` | diagnostic | `src/mutos_cc/c1rules/c1_02.c` | `src/mutos_cc/c1port/c12.c`, `acommute()`: a product or AND that folds to 0 | [C1-02](MUTOS_ORIGINAL_BUGS.md) |
| C1-03 | c1 | `-F` | diagnostic | `src/mutos_cc/c1rules/c1_03.c` | `src/mutos_cc/c1port/c10.c`, `delay()`: after the tree without its postfix `++`/`--` | [C1-03](MUTOS_ORIGINAL_BUGS.md) |
| C1-06 | c1 | `-F` | diagnostic | `src/mutos_cc/c1rules/c1_06.c` | `src/mutos_cc/c1port/c10.c`, `cexpr()`: a row once matched | [C1-06](MUTOS_ORIGINAL_BUGS.md) |
| LD-06 | ld | `-F` | diagnostic | `src/mutos_ld/ldrules/ld_06.c` | `src/mutos_ld/ld.c`, `error()`: the error level set | [LD-06](MUTOS_ORIGINAL_BUGS.md) |
| LD-08 | ld | `-F` | diagnostic | `src/mutos_ld/ldrules/ld_08.c` | `src/mutos_ld/ld.c`, `middle()`: the six bytes of `jmp $` added to the text | [LD-08](MUTOS_ORIGINAL_BUGS.md) |

The rules directories: `src/mutos_cc/c1rules/` (`c1rules.c`: the mode,
`-1` and the rule log of `c1`), `src/mutos_ld/ldrules/` (`ldrules.c`: the
mode of `ld`); `c0rules/`, `c2rules/` and `src/mutos_as/asrules/` come with
their first rule. `c0` and `as` read the mode already (`c0lib.c`'s
`c0_mode`, `assemble.c`'s `as_mode`), `c2` its `-1` (`c2_v30`).

## The rule log

With `MUTOS_RULELOG=<file>` every hook a tool passes appends one line to
the file:

```
<tool> <rule> <what> [<key>=<value> ...]
c1 C1-03 met line=6 out=152-185 op=++
c1 C1-06 met line=7 out=264-384 stores=6
ld LD-08 met tsize=65530
```

`met`: the original's path was taken; `applied`: the rule changed the
output. `line=` is the source line, as the tool's messages give it.
`out=` (`c1`) are the bytes of the output file - offsets, the end
exclusive - that `c1` wrote for the intermediate-code operation the rule was
met in: a whole statement, so that every byte a rule changes lies in its
range. `c1` notes an entry when the hook is passed and writes it when the
operation is done (`c1rules.c`); a rule that must see the code it concerns
(C1-06 counts the bytes of two stores) reads it back from the output file
then. The rest of the keys are the rule's own.

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
every configuration. `--report` prints which rules the code met.
