# `src/libc_fixed/` — the corrected copy of `libc.a`

The corrected mode (`-F`, `MUTOS_MODE=corrected`; `docs/RULES.md`) links a
corrected copy of `libc.a`; the original mode links the original archive,
which stays as it is (decision 14 of `docs/TOOLCHAIN_PLAN.md`, revised
2026-11-08; step F3b). The copy corrects the bugs of the runtime that compiled
code calls without the programmer naming it - the routines step F3 measured
(`tests/libc_runtime/`) - and nothing else of the library.

## Where it is, and how it is reached

`make` builds the copy as `src/libc_fixed/libc.a`; the MUTOS root holds it as
`lib/fixed/libc.a` (`scripts/mutos_root.sh`), next to the original
`lib/libc.a`. In the corrected mode `ld`'s `-lX` looks for `/lib/fixed/libX.a`
before `/lib/libX.a` (decided by the project owner on 2026-11-09). On the
hardware the copy is an ordinary MUTOS archive: it can take the place of
`/lib/libc.a`, or be linked by name.

`make` also builds `src/libc_fixed/arith.a`, the corrected copy of the MUTOS
kernel's `conf/arith.a` (`tests/mutos1700_arith/`, the project owner's): its
long helpers `aldiv.o`, `ldiv.o`, `alrem.o`, `lrem.o` replaced as in
`libc.a` - they are `libc.a`'s byte for byte -, `locking.o`, `almul.o` and
`lmul.o` as they are, no table of contents (the original has none). The
kernel's makefile names `arith.a` by its path, so no rule of `ld` reaches it:
a kernel with the corrected long helpers is built with this file in place of
the tree's `conf/arith.a` (`docs/KERNEL_BUILD.md`).

## What is in this directory

| File | What it is |
|---|---|
| `orig/<name>.s` | A source of the original member `<name>.o`, reconstructed from the real object (`tests/mutos1700_libc/<name>.o`): `mutos_as -C` assembles it to that member byte for byte - header, text, data, both relocation tables, the symbol table. Drafted by `objsrc.py`, then annotated (comments only); a comment `| L-nn` marks the site of a bug of `docs/MUTOS_ORIGINAL_BUGS.md`. |
| `fixed/<name>.s` | The same source with its corrections, each change marked by a comment naming the correction (`| L-03 ...`), for the members that have one. |
| `fix_archive.py` | Makes the copy: the original archive with each member of `fixed/` assembled and replaced in place (`mutos_ar r`: the order kept, the old member's date, owner, group and mode), the table of contents made anew (`mutos_ranlib`), the file's time that of the table. It refuses to replace a member that is not the original's byte for byte. With another archive as its argument (or its `.base64.txt`) it corrects that one - the MUTOS kernel's `conf/arith.a`, whose `aldiv.o` ... `lrem.o` are the long helpers (decided 2026-11-09): `make` runs `python3 fix_archive.py -o arith.a ../../tests/mutos1700_arith/arith.a.base64.txt`; for another tree's `python3 src/libc_fixed/fix_archive.py -o arith.a ~/sys/conf/arith.a`. |
| `check_copy.py` | In `make test`: every `orig/` source is its member byte for byte; every `fixed/` source has its reconstruction and keeps the member's interface (the same global symbols, no new external reference); each copy - `libc.a`, `arith.a` - has its original's members in its order, each the original's but the corrected ones and `__.SYMDEF`, which names the same symbols, every header the original's (the table's date its own). |
| `objsrc.py` | The first draft of a reconstruction: an object written as a `mutos_as` source that assembles to it (needs the Python module `capstone`). |

Which lines of `fixed/` differ from `orig/`, and that every difference is
marked with a correction of the register in `docs/RULES.md` ("The corrected
copy of `libc.a`"), is `scripts/check_rules.py`'s part (`make test`, `make
check-docs`). What the corrections compute is checked by
`tests/libc_runtime/rtcheck.py --copy` (`make check-runtime`): every entry
point against Python's arithmetic, the original's behaviour no longer
explained where a correction is; `tests/libc_runtime/linkcheck.py` (there too)
links the goldens of `tests/mutos_cc` with the copy and with the original -
each runs the same - and compiles one trigger program per correction with
`cc` and `cc -F`: the original's result, then C's.

## The members

The nine members of `orig/` are the members of the runtime with a correction
(step F3, `tests/libc_runtime/README.md`): `aldiv.o`, `ldiv.o`, `alrem.o`,
`lrem.o` (the long helpers' division and remainder), `convert.o` (`itof`,
`ftoi`), `lconvert.o` (`ftol`, `ltof`), `dmath.o` (`dadd`, `dsub`, `dmul`,
`ddiv`), `stkmath.o` (the operations on the floating-point stack's top) and
`stacks.o` (the floating-point stack). All are hand-written assembler; their
labels are the original's, and `L1`, `L2` ... label the jump targets the
original reached without a symbol of its own (`-C` leaves them out of the
symbol table, as the original `cc`'s `as -gC` does). `objsrc.py` drafts 72 of
`libc.a`'s 167 members exactly; the others would need hand work (equates,
the compiled C members' constants).

The corrections, decided by the project owner on 2026-11-09 after step F3:
those that compute wrongly in ordinary programs - L-01, L-04, L-05, L-08,
L-11 - and four of the edge ones - L-03, L-10, L-12, L-14. Not corrected:
L-02 (`ltof(0)` leaves old bytes below its zero; no value changes), L-09
(`fsubrs`, which compiled code does not call), L-13 (`chkstk` wraps round the
segment where a program is out of stack either way); L-06 and L-07 (`ldexp`,
`atof`) are not part of the runtime compiled code calls.

| Correction | Member | `fixed/` | What changed |
|---|---|---|---|
| L-01 | `convert.o` | `convert.s` | `itof`: AX cleared before the store loop (one instruction) |
| L-03 | `convert.o` | `convert.s` | `ftoi`: the immediate 0x8000; the fraction bit's test left out |
| L-04 | `aldiv.o`, `ldiv.o`, `alrem.o`, `lrem.o` | `aldiv.s` ... | the short paths only where they are right; every other divisor below 2\*\*16 in magnitude two `div`s; the sixteen steps compared unsigned |
| L-05 | `dmath.o` | `dmath.s` | `pmuld`: one displacement, `4.(di)` for `2.(di)` |
| L-08 | `alrem.o` | `alrem.s` | the remainder left in DX:AX too (two instructions) |
| L-10 | `stkmath.o` | `stkmath.s` | `fcmp` compares without `dsub` |
| L-11 | `lconvert.o` | `lconvert.s` | `ftol`: no shift for a count of 0; -2\*\*31 from 2\*\*31 |
| L-12 | `stacks.o` | `stacks.s` | `jae`, `jb` for `jge`, `jl` |
| L-14 | `dmath.o` | `dmath.s` | `cmp ah,*/39` for `*/38` |

The register in `docs/RULES.md` says what each one changes; the entries of
`docs/MUTOS_ORIGINAL_BUGS.md` what was measured. Where `c1` and `as` re-enact
these routines in their own work - `c1` folding a long `/` or `%` or a
conversion, `c1` and `as` converting a constant's text with `atof()` - they
compute as the copy in the corrected mode (the rules L-01, L-03, L-04, L-05,
L-11 of `c1` and L-05 of `as`; `docs/RULES.md`, "The tools follow the copy"),
so that a folded constant is what the program computes with the copy at run
time (`tests/libc_runtime/foldcheck.py`). Every corrected member keeps
its interface; its size changes where instructions were added (the members
are linked by name, nothing depends on an offset inside them).
