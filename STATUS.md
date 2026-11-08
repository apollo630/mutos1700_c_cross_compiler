# MUTOS 1700 Cross-Compiler Toolchain — Status

This document tracks the current functional status of the toolchain components. It is
meant to be re-verified, not trusted: per the project's core methodology, no fix or
feature is considered real until it has been rebuilt and diff-checked byte-for-byte
against a real hardware-linked golden reference file. Each section says when its
claims were last verified ("verified 2026-10-20"); anything carried from an earlier
session without being re-run says so, and is to be treated with the same skepticism
the project applies to any unverified claim. The plan for the work ahead is
[`docs/TOOLCHAIN_PLAN.md`](docs/TOOLCHAIN_PLAN.md); the session records this file
held until 2026-10-20 are in [`docs/history/`](docs/history/), the narrative behind
them in [`docs/DEVLOG.md`](docs/DEVLOG.md).

Last updated: 2026-11-08.

---

## Top-level build (verified 2026-10-25)

A repo-root `Makefile` builds every component - through each one's own
`src/mutos_<tool>/Makefile` (`mutos_ld`'s since its port, 2026-10-22), `mutos_ld`,
`mutos_as`, `mutos_cpp`, `mutos_cc`/`mutos_c0`/`mutos_c1`/`mutos_c2` (`mutos_c2`
since 2026-10-25), since 2026-10-26 `mutos_ar` and `mutos_ranlib` and, since 2026-11-02,
`mutos_make`, `mutos_nm`, `mutos_size`, `mutos_strip`, `mutos_tsort`, `mutos_lorder`, `mutos_ed`
and `mutos_em` - and
makes the MUTOS root `mutos_root/` (`make root`, `scripts/mutos_root.sh`: `lib/crt0.o`,
`lib/libc.a` at its `__.SYMDEF` time, the passes and tools as symbolic links into
`src/`, `usr/lib/`, `usr/include/` - since 2026-10-25 the original headers, copied from
`mutos/mutos_include/usr/include/` -, since 2026-10-22 `tmp/`, and since 2026-10-26
`bin/ar` and `bin/ranlib`, and since 2026-11-02 `bin/make`, `bin/nm`, `bin/size`, `bin/strip`,
`bin/tsort`, `bin/lorder`, `bin/ed` and `bin/em`; `make clean` removes it). Every component is
compiled with `-DMUTOS_ROOT_DEFAULT='"<tree>/mutos_root"'`, the default of
`src/h/mutos_root.h`'s lookup (`$MUTOS_ROOT` overrides it); `mutos_cpp` and `mutos_ld`
use it since 2026-10-22 (the default include directory; `-lX`'s libraries and the
temporary file), `mutos_cc` since 2026-10-24 (its passes, the startup file, its
temporary files). A
`.gitignore` covers `mutos_root/` and the build
products, scoped so that no reference file can fall under it.
`make test` runs every component's golden-diff suite from one place (`mutos_as`'s
`kernel_nonopt`/`kernel_opt`/`libc_recon`/`float_coverage` plus its `.float`/`.double`
checks - `check_floatdat.sh` and `float_coverage/fltmodel.py` - and the assembly of
every real compiler `.s` in `tests/mutos_cc`, `mutos_cpp`'s, the compiler's,
`tests/mutos_root/check_root.py` on the root and its lookup, and since 2026-10-24
`tests/mutos_cc/check_driver.py`: the driver `mutos_cc` with the ported passes, since
2026-10-25 also `cc -O` with `mutos_c2`, and since 2026-10-26 `tests/mutos_ar/check_libc.py`:
`libc.a` rebuilt from its objects by `mutos_ar` and `mutos_ranlib`, and since 2026-11-02
`tests/mutos_make/check_corpus.py`: the thirteen `Makefile.mutos` of `tests/mutos_cc/`
run through `mutos_make` and the ported tools, `tests/mutos_binutils/check_tools.py`, and
`tests/mutos_ed/check_editors.py`: the MUTOS kernel's makefile run through `mutos_make` with
`mutos_ed` and `mutos_em`).
`make check-realbin` and `make check-libcatof` need the Python module `unicorn` and
are not part of `make test`: the first checks the original binaries and the ports of
`c0`, `c1`, `cpp`, `ld`, `cc`, `c2`, `ar`, `ranlib`, `make`, `nm`, `size`, `strip`, `tsort`,
`lorder`, `ed` and `em` against the emulated originals (and `mutos_ld`'s
extension `-r`/`-d`), the original `cc` traced through its
original passes against the goldens it made (Milestone 4), and the original `ld`
through the root (Milestone 1); the second the
`.float`/`.double` conversion and `mutos_c1`'s constant texts against `libc.a`'s own
`atof`/`ecvt` (Milestones 2 and 4). `make check-docs` checks the Markdown files.

Verified 2026-10-20: `make clean && make all` with zero warnings under
`-std=c11 -Wall -Wextra -Wpedantic` (a second `make` changes nothing in the root),
then `make test` - its output as before step 0 plus the root's checks, all passed
(results in the milestone sections) -, `make check-realbin` and
`make check-libcatof`. Re-run 2026-10-22 on a fresh clone with step 0's change
applied: `make all`, `make test`, `make check-realbin`, `make check-libcatof`,
`make check-docs` - the same results. Verified 2026-10-22 with the port of `cpp`
(Milestone 3): `make clean && make all` with zero warnings, `make test` - its output
the same as before, line for line -, `make check-realbin` with the port's new checks,
`make check-docs`. Verified 2026-10-22 with the port of `ld` (Milestone 1): `make clean
&& make all` with zero warnings, `make test` - its output the same as before, line for
line, plus the root's `tmp/` -, `make check-realbin` with the port's new checks,
`make check-libcatof`, `make check-docs`. Verified 2026-10-24 with the port of `cc`
(Milestone 4), on a fresh clone with the change applied: `make clean && make all`
with zero warnings, `make test` - its output as before, line for line, but
`check_root.py`'s list of dangling links (`lib/c2` alone now) and with
`check_driver.py`'s lines added -, `make check-realbin` with the port's new checks,
`make check-libcatof`, `make check-docs`.

Verified 2026-10-25 with the port of `c2` (Milestone 5), on a fresh clone with the
change applied: `make clean && make all` with zero warnings, `make test` - its output
as before, line for line, but `check_root.py` without the dangling `lib/c2` and with
`check_driver.py`'s two new lines (`cc -O -S` 9/9, `cc -O -o` 62/62) -, `make
check-realbin` with the port's new checks, `make check-libcatof`, `make check-docs`.

Verified 2026-10-25 after the original files moved into `mutos/` (commit `a53afdc`:
`mutos_bin/` became `mutos/mutos_bin/`): `tests/realbin/`'s scripts
(`mutosemu.py` and the `*tables.py`, `c1dis.py`) read the binaries from
`mutos/mutos_bin/` - after the move `make check-realbin` stopped at its first check,
the binary not found. On a copy of the tree with the change: `make clean && make all`
with zero warnings, `make test` - its output the same as before, line for line -,
`make check-realbin` (all checks passed, no difference against the originals),
`make check-libcatof`,
`make check-docs`.

Verified 2026-10-25 with the headers in the root: `scripts/mutos_root.sh` copies
`mutos/mutos_include/usr/include/` (73 files, `sys/` and `local/` included) into the
root's `usr/include/`, a file only when it differs; `tests/mutos_root/check_root.py`
checks them byte for byte and has `mutos_cc` compile and link a program with
`<stdio.h>`, `<sys/types.h>` and `<sys/stat.h>` through the root - with `--realbin` it
runs under the emulator and prints `hello, 512 30` (`BUFSIZ`, a 30-byte `struct stat`).
The original chain, the emulated `cc` running the original passes with the same headers
in its root (`tests/realbin/cctrace.py --run`), gives that program's `a.out` byte for
byte. On a copy of the tree: `make clean && make all` with zero warnings, `make test` -
its output as before, line for line, plus `check_root.py`'s three new lines -, `make
check-realbin` (the same, line for line, plus the new lines), `make check-libcatof`,
`make check-docs`.

Verified 2026-10-26 with step 5 (the measurement of `mutos_as`, the work of 2026-10-25;
`mutos_as`, `tests/realbin/` and the documentation changed, no other tool). On fresh
clones of the commit, with and without the change: `make clean && make all` with zero
warnings; `make test` exit 0, its output the same in both, line for line, but for the
two lines `tests/mutos_as/check_switches.sh` adds (`-1`, `-C`) and the reason
`mutos_as` gives for refusing the compiler's own invalid `fltprobe/p19_open3` (its line
229 is a hundred bytes of garbage: `statement too long` now, `could not classify
operands` before); all 76 golden objects byte for byte (`mch_insw_outsw` with `-1`);
`make check-docs`. In the
working tree: `make check-realbin` (all checks passed, the new ones among them:
`ascmp.py goldens` 432 runs and `ascmp.py cases` 158 runs against their pins,
`asfuzz.py --gen cc --strict` 300 cases, all the same object) and `make
check-libcatof` (0 differ). The final fuzzer run (about 5,500 cases) is in "Step 5 of
the plan", Milestone 2.

Verified 2026-10-26 with the ports of `ar` and `ranlib` (Milestone 6, step 6), on a fresh
clone of the commit with the change applied (`git apply`; the new files added with `git
add`, which `check-docs` reads): `make clean && make all` with zero warnings under
`-std=c11 -Wall -Wextra -Wpedantic`; `make test` exit 0, its output the same as the
commit's without the change, line for line, but for the lines the change adds - the two
`make` calls for `src/mutos_ar` and `src/mutos_ranlib`, `check_root.py`'s `bin/ar` and
`bin/ranlib`, and `check_libc.py`'s five; `make check-realbin` exit 0, all checks passed
(the new ones: `ardata.c` current, `arcmp.py cases` 366 runs, `arfuzz.py` plain and
damaged, `ranlibcmp.py cases` and `libc`, `ranlibfuzz.py` plain and damaged, seed 1 -
the results in Milestone 6); `make check-libcatof` (0 differ); `make check-docs`. The
longer fuzzer runs of Milestone 6 (seeds 2 and 3) ran in the working tree.

Verified 2026-11-02 with the port of `make` (Milestone 6, step 7), on a fresh clone of the
commit with the change applied (`git apply`; the new files added with `git add`, which
`check-docs` reads): `make clean && make all` with zero warnings under `-std=c11 -Wall
-Wextra -Wpedantic`; `make test` exit 0, its output the same as the commit's without the
change, line for line, but for the lines the change adds - the `make` call for
`src/mutos_make`, `check_root.py`'s `bin/make`, and the one line of `check_corpus.py` (131
`.s`, 132 `.i`, 132 `.1` and 132 `.2` as the goldens, 1 `.s` refused as `invalid_goldens.txt`
says, second runs idle, `clean` clean); `make check-realbin` exit 0, all checks passed (the
new ones: `makedata.c` current, `makecmp.py cases` 457 runs and `makecmp.py corpus` 312 runs,
`makefuzz.py` plain and damaged, seed 1 - the results in Milestone 6; the earlier ones as
before, `ascmp.py cases` against its pins unchanged); `make check-libcatof` (0 differ in both literal runs);
`make check-docs`. The longer fuzzer runs of Milestone 6 (seeds 11 to 13, 3,600 cases) ran
in the working tree.

Verified 2026-11-02 with the ports of `nm`, `size`, `strip` and `tsort` and the script `lorder`
(Milestone 6, step 8), on a fresh clone of the commit with the change applied (`git apply`; the new
files added with `git add`, which `check-docs` reads): `make clean && make all` with zero warnings under
`-std=c11 -Wall -Wextra -Wpedantic`; `make test` exit 0, its output the same as the commit's without the
change, line for line, but for the lines the change adds - the five `make` calls for `src/mutos_nm`,
`mutos_size`, `mutos_strip`, `mutos_tsort` and `mutos_lorder`, `check_root.py`'s five `bin/` links, and
`check_tools.py` (30 checks, 0 failed) - and a temporary directory's name; `make check-realbin` exit 0, all
checks passed, run in the working tree whose sources are the patch's (the new ones: `utilcmp.py cases` 1,118
cases, 3,729 the same, 125 refused, 18 where the original does not finish; `utilfuzz.py` seed 1, 300 cases
plain and 300 damaged, no difference - the results in Milestone 6); `make check-libcatof` (0 differ);
`make check-docs`. The longer fuzzer runs of Milestone 6 (seeds 2 and 3, 1,500 cases each) ran in the
working tree, too.

Verified 2026-11-02 with the ports of `ed` and `em` (Milestone 6, step 9), on a fresh clone of the commit
with the change applied (`git apply`; the new files added with `git add`, which `check-docs` reads): `make
clean && make all` with zero warnings under `-std=c11 -Wall -Wextra -Wpedantic`; `make test` exit 0, its
output the same as the commit's without the change (a second fresh clone), line for line, but for the lines
the change adds - the two `make` calls for `src/mutos_ed` and `src/mutos_em`, `check_root.py`'s `bin/ed` and
`bin/em`, and `check_editors.py` (212 checks, 0 failed) - and a temporary directory's name; `make check-docs`.
Of `make check-realbin`, the checks the change adds ran in that clone - `edcmp.py cases` (244 cases: 225 the
same, 7 refused, 12 where the original loops for ever and the port refuses), `edfuzz.py` seed 1 (ed: 400 the
same; em: 380 the same, 20 where em asks "sure?" for ever), all passed - and `check_root.py --realbin` (all
checks passed); the targets before them are unchanged by the change (no source of another tool, only the root's
two new links). The longer fuzzer run (seeds 1000 to 3999, 3,000 cases) ran in the working tree.

Verified 2026-11-07 with the kernel build's documents (`docs/KERNEL_BUILD.md`, `docs/PROJECT_MEMORY.md`,
`scripts/kcmp.py` and the documents brought up to date; no tool and no test changed), on a fresh clone of the
commit with the change applied (`git apply`; the new files added with `git add`, which `check-docs` reads):
`make clean && make all` with zero warnings; `make test` exit 0, its output the same as a second, unchanged
clone's, line for line, but for a temporary directory's name; `make check-docs`; `scripts/kcmp.py` on a kernel
tree built from the repository's sources (13 of 61 objects the goldens, the rest stand-ins) and on the 61 goldens
linked through the kernel's makefile (`same: 61`). The kernel build itself: "The MUTOS kernel, built with the cross
toolchain" below.

---

## The MUTOS kernel, built with the cross toolchain (verified 2026-11-07)

**Status: the MUTOS 1700 kernel builds on Linux with its original, unchanged
makefile, and is the kernel the original build made on the hardware, byte for
byte.** The guide: [`docs/KERNEL_BUILD.md`](docs/KERNEL_BUILD.md); how it was found:
`docs/devlog/originals_and_ports.md`, "The MUTOS kernel, cross-built". The kernel
tree and the original kernel are the project owner's and not in this repository;
its makefile is (`tests/mutos_ed/kernel/conf.Makefile`, without the dependency
lines `make depend` appends).

- **On the project owner's tree** (2026-11-07): the kernel's `conf/Makefile` - the
  7100 configuration (`TYP = M7100`, `DRIVER = V30IDE V24 IFSS`), with the
  dependency lines - run unchanged by `mutos_root/bin/make` in a copy of the tree
  without its old objects: 62 `cc` calls (`cc -c -O -K1024 -DM7100 -DASK -DIFSS
  -DV24 -DV30IDE` for 58 sources, `cc -c` with the same flags for `c.c` and
  `symtab.c`, `cc -P`, `em mch.i <fakeawk` and `cc -c mch.s` for `mch.c`), 55
  sources touched by the dependency rules (the headers were newer), `ld -i` with
  `conf/arith.a`; no message, exit status 0; `size mutos`: `50496+8288+44600 =
  103384b = 0311730b`.
- **All 61 objects are the real-hardware goldens of `tests/mutos_as/kernel_opt/`**
  (`scripts/kcmp.py`: `same: 61` - header, text, data and relocation, an external
  one by its symbol's name; the symbols are the golden's without the local `L`
  symbols, which `cc`'s `as -gC` leaves out). Until then the tests had compiled
  only `11_kernel`'s nine sources (`cc -O -S`, `tests/mutos_cc/check_driver.py`) and
  `mch` (`tests/mutos_ed/check_editors.py`) to them; of the other 51, 48 have no C
  source in the tree. It settles too that `kernel_opt/`'s objects are what `cc -c -O
  -K1024` makes with this configuration.
- **The kernel `mutos` has the MD5 sum of the original build's `conf/mutos`**
  (`5f60bb499b26df4d4adb6c82dd928b2e`): `mutos_make`, `mutos_cc` with `mutos_cpp`,
  `mutos_c0`, `mutos_c1`, `mutos_c2` and `mutos_as`, `mutos_em` and `mutos_ld -i`
  together give the hardware's kernel - the first real-hardware evidence for a
  0411 output of `mutos_ld` (open item 1) and for a whole program through the
  ported chain.
- **`arith.a`** (the owner's `conf/arith.a`, dated 1989; no source makes it):
  `locking.o` (`_locking`, `_locked`, `_unlckf`, `_deadloc`, `_lockadd`,
  `_lockall`, `_lockfre`) and `libc.a`'s long routines `aldiv.o`, `almul.o`,
  `alrem.o`, `ldiv.o`, `lmul.o`, `lrem.o`, without `__.SYMDEF` - `ld` reads it
  member by member; no "out of date" warning.
- **Checked beforehand with this repository's material** (2026-11-07, Ubuntu 24.04,
  `/bin/sh` dash): a kernel tree of the makefile, `tests/mutos_cpp/h/`, `mch.c`,
  `fakeawk`, the thirteen real kernel sources of `tests/mutos_cc/11_kernel/` and
  `tests/mutos_cpp/c/` and stand-ins for the other 48: `mutos_make` ran the same 62
  `cc` calls, `em`, `ld -i`, `size`; the thirteen gave the `kernel_opt/` objects;
  the 61 `kernel_opt/` objects linked through the `mutos` rule (0411); a second
  `make` did nothing; a newer header (`touch ../h/v30ide.h`) touched the two
  sources that include it and rebuilt their objects and the kernel.
- **Not checked**: the other configurations (7150, 1834, other drivers and
  options), whose objects have no goldens; the kernel was not started on its own
  (its bytes are the original's).

---

## Milestone 1 — `mutos_ld` (linker)

**Status: a port of the original `ld` binary (2026-10-22)** -
`docs/TOOLCHAIN_PLAN.md`, decision 5 and step 2, done. `mutos_ld` is the
original MUTOS 1700 `ld` - v7's `ld.c` with the MUTOS deltas -, every function
read from the stripped binary beside `v7/ld/ld.c` (`tests/realbin/ldmap.txt`),
running on an image of the original's data segment, with one documented
extension: `-r` and `-d`, which the original refuses. The port of v7's `ld.c` it
replaces (`mutos_ld.c`) is deleted; its record is in
[`docs/history/status_m1_ld.md`](docs/history/status_m1_ld.md). The original's
behaviour, the deltas and what the port refuses: `src/mutos_ld/README.md`; how it
went: `docs/devlog/originals_and_ports.md`, "The port of `ld`".

### The port (verified 2026-10-22)

- **Sources** (`src/mutos_ld/`): `ld.h` (the image, the binary's data addresses,
  read from its code), `ld.c` (`ld.o`'s 38 functions - v7's 37 and `bytrel()`),
  `ldlib.c` (the machine: the system calls on the host's files, libc.a's printf to
  stdout in 512-byte blocks, `atoi()`, `mktemp()`, the refusals), `lddata.c` (the
  binary's data, generated by `tests/realbin/ldtables.py --c`), `main.c`, a
  `Makefile` of its own. Zero warnings under `-std=c11 -Wall -Wextra -Wpedantic`.
- **Against the emulated original** (`tests/realbin/ldcmp.py`): every one of the 131
  assemblable `.s` goldens of `tests/mutos_cc`, assembled by `mutos_as`, linked
  with twelve command lines each (`-X crt0.o x.o -lc`, without `-X`, `-x`,
  `-s -o`, `-i`, `-n`, `-S -O`, the object alone, `-t -u -e` with `libc.a` by
  name and out of date, `libc.a` by name and current, `-D`, the object twice):
  1,572/1,572 the same - messages, exit status, and every file left in the
  directory, bytes and mode; 91 hand-written cases (every message, `-v`, `-D`,
  `-e`, `-u`, `-l` names of every length, archives with and without a table of
  contents, bad and short files, the tables' limits, byte-shifted relocations):
  89 the same, 2 refused (a directory; an object of 32K text, LD-07).
- **Differential fuzzing** (`tests/realbin/ldfuzz.py`, the final build): 2,000 cases
  - 1,000 generated (seed 2) and 1,000 with half of the files damaged (seed 3,
  `--mutate 0.5`) -, no difference: 1,955 the same, 45 refused (an input that is a
  directory; an `lseek()` to a negative offset from a damaged archive). An ASan/UBSan
  build of the port: 1,000 more (seeds 5 and 6: 976 the same, 24 refused), the
  goldens (1,572/1,572), the cases and `check_rflag.py` - no report, no difference
  (it found one fault of the port earlier, a signed left shift in `lookup()`'s hash,
  fixed). `make check-realbin` runs 300 of each kind (seed 1: 294 the same, 6
  refused, in each).
- **The extension `-r`/`-d`** (`tests/mutos_ld/check_rflag.py`): the 62 programs of
  the construct corpus, each linked through a relocatable object four ways (`ld -r
  x.o`, `ld -r crt0.o x.o -lc`, with `-d`, and `-d` then `-i`), run under the
  emulator as the program linked at once: 248/248 (two of them v7's `-d`
  defining `_end`, which the second link reports as "Multiply defined", as v7's
  does). The original refuses both (exit status 4).
- **`make test`**: as before, plus the root's new `tmp/`; `check_root.py` links
  through the root without "out of date". `make check-libcatof`, which links
  `libc.a`'s floating-point runtime through `mutos_ld`: the same results.
- **Paths**: `/lib/libX.a`, `/usr/lib/libX.a` and the temporary `/tmp/ldaXXXXXX` -
  the absolute paths the original has compiled in - are looked up under the MUTOS
  root, which has a `tmp/` since this step; names from the command line are the
  host's (the rule the port of `cpp` set and the project owner confirmed on
  2026-10-22).
- **Refused on purpose** (exit status 2, a message ending in "refused"): a write
  into libc.a's data by a `-l` name of 124 characters or more (LD-01), an
  `lseek()` to a negative offset (MUTOS's kernel takes it, the emulator does not -
  LD-07, damaged archives), an input that is a directory, a printf conversion the
  port does not have (only from formats a long `-l` name has overwritten).
- **The original's bugs**: LD-01 to LD-08 in `docs/MUTOS_ORIGINAL_BUGS.md`, each but
  LD-03 with a trigger run under the emulator - among them a `-l` name of 14
  characters or more written over the messages (LD-01), more than 256 archive
  entries run past `liblist[]` (LD-02, "internal error: symbol not found"), a
  warning that clears an earlier error (LD-06), a text of 65,530 bytes that wraps
  (LD-08).

### What the binary does differently from v7 (read 2026-10-22)

The relocation word's bit 15 marks a word that starts at the next byte (`bytrel()`,
new); data and BSS values are offsets in their own segment (`readhdr()` clears
`cdrel`, `cbrel`); symbol types are compared as words, the spare byte included;
the text gets six bytes of "jmp $" (`eb fe`) at its end, before `-i`'s 64-byte
rounding (the one difference step 0 had seen: `mutos_ld.c` rounded to 8); `-n` sets
`iflag` (a 0411 output); the entry point is the symbol's value; `-lX` tries
`/lib`, then `/usr/lib`; the output is written as `..ldtmp<pid>` in the current
directory and linked to `a.out` (without `-o`); `-r` and `-d` refused, v7's code
for them gone. Per function: `tests/realbin/ldmap.txt`.

### Auxiliary deliverables

- `mutos_ld.1` — man page (to be rewritten at the end of the project, `CLAUDE.md`'s
  man-page rule; it still describes `mutos_ld.c`).
- `src/mutos_ld/README.md` — the original's behaviour, the port, its refusals, the
  extension.
- `tests/realbin/`: `ldmap.txt`, `lddis.py`, `ldtables.py`, `ldcmp.py`, `ldfuzz.py`
  (its `README.md`); `tests/mutos_ld/check_rflag.py`.

---

## Milestone 2 — `mutos_as` (cross-assembler)

**Status: PROVISIONALLY COMPLETE for the real corpus** - every golden object
byte-identical (76/76 with `-1` for the one that has an 80186 instruction), and the
compiler's output assembled to the same object as by the original in every case the
original accepts. "Provisional" because, per this project's core rule, no milestone is
ever declared permanently closed. Step 5 of the plan (2026-10-25: `-C` and `-1`
done, the differential fuzzer built and run, below) measured how far the
re-implementation differs from the original `as`: in its options, its messages, its
instruction words and in what it accepts beyond the corpus. The decision on a port
(`docs/TOOLCHAIN_PLAN.md`, decision 9) is the project owner's; the measurements and a
recommendation are in "Step 5 of the plan" below.

`mutos_as` (`src/mutos_as/`: `mutos_as.h`, `lexer.c`, `parser.c`, `pass2.c`,
`symtab.c`, `encode.c`, `objwrite.c`, `fltconst.c`, `assemble.c`; secondary tools
`main.c` (→ `parse_dump`) and `classify_test.c`) is a two-pass cross-assembler
producing real MUTOS `a.out` relocatable object files for the 8086 (and the 80186
instructions listed under "`mutos_as` opcode coverage").

### Verified 2026-10-20

- **Clean rebuild** (`make clean && make all`), zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **`-g`/`-C`**: `-gC`, `-g -C` and `-Cg -ox.o` give the object of no option
  (`10_integ/01_wordcount.s.golden`, which has no `L` symbol to leave out; since
  2026-10-25 `-C` does what the original's does - "Step 5 of the plan" below);
  `-gx` and `-L` are still the usage error.
- **Full regression (`make test`): 76/76 golden files byte-for-byte identical**,
  full file compare (header + text + data + text-reloc + data-reloc + symtab):
  `tests/mutos_as/kernel_opt/` 62/62 (with `mch_insw_outsw.s`, whose golden is the
  real `mch.o` - see the opcode coverage below), `kernel_nonopt/` 5/5,
  `libc_recon/` 1/1 (`ldexp.s`, a source reconstructed from the real `libc.a`
  object), `float_coverage/` 8/8 (real-hardware objects from hand-written
  `.float`/`.double` sources).
- **`.float`/`.double` against real bytes**: `libc_recon/check_floatdat.sh` 13/13
  comparisons byte-identical to the real `atof.o`/`ecvt.o` data.
- **Conversion model**: `float_coverage/fltmodel.py survivors` - the 38
  known-source constants in the 8 goldens leave 2 of 80 combinations, exactly the
  set `fltconst.c` uses; `fltmodel.py check` - 2,107 texts, 0 differences between
  `fltconst.c` and the independent Python model.
- **Every real compiler output assembles**: `tests/mutos_as/assemble_cc_goldens.sh` -
  131/131 `tests/mutos_cc` `.s.golden` files (the 62-file corpus, `11_kernel`'s nine
  and 60 of the 61 `fltprobe` files); the 61st, `fltprobe/p19_open3`, is the real
  compiler's own invalid output and refused as `invalid_goldens.txt` lists. There
  are no reference objects for these; their bytes are checked only where the
  kernel and `libc_recon` goldens cover the same constructs.
- **Against `libc.a`'s own `atof`** (`make check-libcatof`): 34 of the 38 golden
  constants byte-identical to `libc.a`'s `atof.o` on `libc.a`'s own floating-point
  runtime under an 8086 emulator (with the real assembler's zero handling), the
  other 4 the known exception (all-zero texts with decimal exponent 0, not
  emulated), 0 different; 8,357 texts through `fltconst_test`, 7,588 compared, 0
  differ, all 210 refused as `RANGE` raise `SIGFPE` in the emulated runtime; the
  runtime's `dmul`/`ddiv`/`dadd` on 2,000 random operand pairs each identical to
  `fltmodel.py`'s arithmetic.

### The original `as`, compared (2026-10-20)

Run under `tests/realbin/mutosemu.py` (`docs/devlog/originals_and_ports.md`, "The
other originals as oracles"):

- **Options.** The original knows `-o`, `-e` (only external symbols in the symbol
  table), `-g`, `-C` and `-l`, letter by letter (`-gCe`); `-g`, `-C`, `-l` showed no
  effect on the probes. Every other letter gives `Unknown option X ignored.` - the
  manual's `-L`, `-W`, `-D` and `-T` included. `mutos_as` has `-o`, `-W` and `-v`,
  and **since 2026-10-20 takes `-g` and `-C`** - alone or together (`-gC`), letter by
  letter as the original reads them (plan decision 9): the original `cc`'s
  `as -gC -o x.o tmp` works. They ignored both then; `-g` still does, `-C` leaves out the
  `L` symbols since 2026-10-25, as the original's (below), and `-1` is the switch for
  the 80186 instructions - since 2026-11-08 with one line on the standard error that
  counts them by kind (`docs/TOOLCHAIN_PLAN.md`, decision 16) -, `-F` and `-H` the mode
  of `docs/RULES.md` (2026-11-08). Every other option is still its usage error.
- **Output** (checked under the emulator, 2026-10-20): `mutos_as` wrote a summary
  line (`x.s: text=... -> x.o`) to stderr on every run, the original nothing -
  **since 2026-10-24 only with `-v`**, so that the driver (step 3), which runs `as`
  for every source, writes what the original's chain writes (verified: `make test`
  unchanged, `tests/realbin/cccmp.py programs` with stderr compared exactly). It
  makes an object without undefined symbols executable (0755), the original leaves
  it as `creat` made it (0666 less the umask) - the one difference in the files of
  `ascmp.py`'s runs that is left on purpose (step 5, below).
- **Mnemonics.** The original's table has the whole 8087 set (not needed here) and
  no 80186 mnemonic; an unknown mnemonic assembles silently to a word referring to an
  undefined external of that name (AS-02). Spellings `mutos_as` accepts that the
  original's table does not have: `scas`/`scasb`, `movsw`, `inb`/`outb`, `bloss`
  (see "Untested / unconfirmed opcodes" below).
- **Exit status**: the original exits with 2 on syntax errors and 4 on the floating
  overflow abort; `mutos_as` with 1 for both - a documented difference no test
  depends on.

### Step 5 of the plan: measured against the original (2026-10-25)

`mutos_as` is a re-implementation, not a port (`docs/TOOLCHAIN_PLAN.md`, decision 9), so
the question of step 5 was not "is it the same?" but "where does it differ, and does it
matter?". The original `as` runs under the emulator (`tests/realbin/mutosemu.py`) as the
oracle, and two new tools in `tests/realbin/` (their `README.md`) ask it:

- **`ascmp.py`** gives the original and `mutos_as` the same command line in two copies
  of one directory and puts every pair into a class: `same`; `obj-same` (the objects
  are equal, something else - the file mode, a message - is not); `both-reject`; and the
  three that count - `PORT-REJECTS` (the original accepts, `mutos_as` refuses),
  `PORT-ACCEPTS` (the other way round) and `OBJ-DIFF` (both accept, the objects
  differ). Modes: `goldens` (every `.s` of the tree - the kernel, `libc_recon`, the
  seven `v30_speculative` files and the 132 compiler outputs - under `-gC -o` and under
  `-o`), `cases` (158 hand-written: output names, stdin, several inputs, every option,
  syntax errors, the original's limits, every form fixed below), `options` (the original
  against itself: what `-g`, `-C`, `-l`, `-e` change) and `files`. `--write`/`--check` pin
  the runs that are not `same` in `ascmp_goldens.txt` and `ascmp_cases.txt`.
- **`asfuzz.py`** fuzzes. Generators: `insn` (one to six instructions in every form),
  `prog` (programs with segments, labels, directives, expressions), `corpus` (lines of
  the real sources recombined), `cc` (the real compiler's output for `c1fuzz.py`'s
  and `fuzz_c.py`'s programs, a third of it after `mutos_c2`) and `mix`, with `--mutate`
  to damage the cases. A serious case is reduced to a minimal source and clustered by
  what each tool says; `--words` runs all 273 instruction words of the original (read
  from its binary) and of `mutos_as` through up to 30 operand shapes.

**The result for what the toolchain really feeds it.** The compiler's output is
assembled to the same object by both in every case the original accepts: all 1,284 of
1,284 programs of the `asfuzz.py --gen cc` run (1,500 cases, 91 not compilable). Of
the 132 compiler `.s` goldens and
the 84 other sources, 404 of 432 runs are `same`, 16 `obj-same` (the file mode only:
`mutos_as` makes an object without undefined symbols executable, the original leaves
`creat`'s mode), 12 `both-reject` - the five `v30_speculative` files that need `-1` and
`p19_open3`, the compiler's own invalid output, under both command lines; of the 76
real-hardware golden objects the original gives 75 and `mutos_as` 75, the 76th being
`mch_insw_outsw`, which needs `-1` (76/76 with it). The original refuses 125 of the
1,409 compiled programs of the `cc` run (byte shifts and operations on `si`/`di`,
`(dx)` as an index, a `j` to an undefined label: the compiler's own invalid output, as
`p19_open3`); `mutos_as` refuses 105 of them too and takes 20.

**Found and fixed in `mutos_as`** (each one a form the fuzzer showed and the original
assembles; those marked * gave a different object without a message, which is the
kind that matters):

- *Operands.* A shift or rotate of a global (`sarb _g,cl`, `shl _g,cl`) was refused or,
  with a displacement, wrong; `test cx,_d` took the address for an immediate*; `xchg
  al,bl` was assembled as the word form*; `xchg`, `xchgb`, `test` and `testb` with a
  memory operand in either order, `pop` of a memory operand, `lds`/`les` with an
  address, `incb`/`decb` of a register, `int *3.` (the one-byte `CC`*), `ret *n.` and
  `reti *n.` were missing or wrong.
- *Displacements.* The size of `disp(reg)` follows the original's: a number is taken as
  16 bits first (`65535.(bx)`, `/ff80(bx)` are short, `65536.(bx)` is 0), a `.` makes it
  long with a relocation, and - the one that matters for hand-written code* - an
  absolute value of symbols that are defined by then is short when it fits: `K = 5`,
  then `mov ax,K(bx)`, or `b-a(bx)` for two labels of one segment, is `8B 47 05`, not the
  `8B 87 05 00` that a symbol had always forced (a label alone, an external symbol
  and a symbol defined further down stay long).
- *`call`, `jmp`, `calli`, `jmpi`.* `call @(bx)` is the word at `[bx]` (`FF 17`), not the
  register (`FF D3`)*, and so for `(si)`, `(di)`, `(bp)`; `@x` (the word at an address,
  `FF 16` + relocation), `@(bx)(si)` and `@disp(bx)(si)` were refused; a bare
  register (`call ax`, `jmp bx`) is taken; `call (bx)`, `call 10(bx)`, `call x(bx)`
  without the `@` and an `@` on any other instruction (`mov ax,@bx`) are refused, as the
  original does.
- *Strings and escapes.* `.asciz` knew no backslash escapes (`\n`, `\t`, octal `\012`,
  `\\`, `\*`: the original's rules, with the quirks at odd digits)*; `.ascii` and strings
  delimited by `"` were missing.
- *Labels and symbols.* More than four labels in front of one statement lost the others
  (the original takes at least 24 and fails at 32; `08_tty` has eight)*; the symbols of
  an instruction are made in the order they are written (`sbb _tab,#_buf2` numbered
  `_buf2` first)*; a number alone on a line in `.text` went into the data*, in `.bss`
  likewise; an odd `.bss` size is rounded up to even*; a name that starts with a dot
  (`.baz`, `.foo = 3`) is a symbol unless it is one of the original's 21 directives.
- *Directives.* `.blkw`, `.ascii`, `.end`, `.list`, `.nlist`, `.insrt "file"` (ten
  levels), `.targref` and `.textref` were missing; `.dlptr` is an error, as in the
  original.
- *Expressions.* The original has no operator precedence - left to right - and the
  operators `+ - * / & ! ^ << >>` (`!` is or, `^` the remainder), one unary `-`, `+` or
  `~` before an operand, 16-bit signed arithmetic for the rest than `+` and `-`; numbers
  are decimal (`010` is 10; hexadecimal starts with `/`); a shift
  count of 16 or more saturates; `|` starts a comment; parentheses and a single `<` or
  `>` are errors; `name = expr` takes any expression of the symbols it knows, a
  symbol further down included (`_b = _a+1`, `_e = .+2`), and the symbol gets the kind
  of its value.
- *Silent wrong objects, found in the last rounds* (each one a form a person could
  write, and each one assembled without a message to a different object): a character
  constant (`'a'`, `'ab'`, with the escapes `\n \t \b \f \r` and `\c` for any other `c`)
  became 0; a `.word`, `.byte`, `.blkb`, `.blkw` or data value that
  `mutos_as` could not parse became 0; an expression of more than 16 tokens was cut
  after the 16th (the limit is 64 tokens in an operand and 32 operands now, and an
  error beyond it); the size of `K(bx)` for an assigned `K` (above); `call @(bx)`
  (above); the numbering of the symbols of `sbb _tab,#_buf2`.
- `-C` (below) and the order of `L` symbols under it.

**Options, measured on the original and done.** `-g` changes nothing; `-C` leaves out
every defined symbol that is not global and starts with a capital `L` - the compiler's
`L<number>` labels, a `La = 5`, a label in `.data` or `.bss` - and writes an `L` name
that stays undefined after all the others, one made external by `.globl` or `.comm`
at that directive; the symbol numbers in the relocations follow. `mutos_cc` has passed
`-gC` all along, so the objects `mutos_cc -c` makes have no `L` symbols now, as the
original chain's (`libc.a`'s 167 objects have none either, while every kernel golden
has them: the kernel was assembled without `-C` - open item 6). `-1` is the 80186/V30
switch of the plan, section 4.

**Measured, differences kept** (`ascmp_goldens.txt`, `ascmp_cases.txt`, the output of
`asfuzz.py`; none of them is in the compiler's output or in the corpus):

- *Command line.* The original reads its options letter by letter: `-e` (only external
  symbols in the symbol table) and `-l` (a listing, `x.L`) are not in `mutos_as`;
  `Unknown option X ignored.` and go on (`-L -D -T -X -u -`, `-W`, `-1`), where `mutos_as` has
  the usage error or - for `-W`, `-1`, `-v` and, since 2026-11-08, `-F` and `-H`, its own -
  takes them; `-oNAME` (attached) is
  `mutos_as`'s only. The default output is the input name with its last character
  made `o` (`x.s` gives `x.o`; any name: `prog` gives `proo`), `mutos_as` writes `a.out`
  unless `-o`; stdin and several inputs (the original: `Warning input file ... does not
  end in .s`, `More than one input, a.s used.`, status 4) are `mutos_as`'s own;
  the original's exit status is 2 for errors in the source, 4 for a file it cannot
  open, `mutos_as`'s 1; it leaves a partial object after an error, `mutos_as` none.
- *Messages.* `***ERROR*** syntax error, line N` on stdout and stderr, then `x.s: N errors.`
  - not `mutos_as`'s `x.s:N: error: ...`.
- *Words.* The original knows 273 words, `mutos_as` 173 (156 of them the original's):
  the 113 words of the 8087 (`fadd`, `fld`, ...: `asf` and `cc -f`, which the project
  does not need), `bc`, `bnc`, `scab` and `scaw`;
  `mutos_as` has 17 the original does not (`bloss`, `bound`, `enter`, `leave`,
  `pusha`, `popa`, `insb`, `insw`, `outsb`, `outsw` - those on `-1` -, `inb`, `outb`,
  `movsw`, `scas`, `scasb`, `repe`, `repne`), which the original reads as
  undefined symbols. `hlt` (AS-05) and `aam` (AS-04) are bugs of the original, which
  `mutos_as` does not repeat.
- *Bugs of the original, not repeated* (`docs/MUTOS_ORIGINAL_BUGS.md`, AS-04 to AS-14;
  AS-02 and AS-03 were known): `aam` is `D6 0A` (AS-04); `hlt` is in the table and not
  found (AS-05); `(bp)` and then the first `.float`/`.double` crashes it (AS-06); an odd
  `.data` followed by `.bss` gives a short object (AS-07); `name = sym` before `sym` is
  defined makes `name` an undefined external with the value `0x7870` (AS-08); a division
  or remainder by zero in an expression crashes it (AS-09); `(bp)` with an immediate
  after some statements gets the immediate's low byte as its displacement (AS-10);
  a conditional branch to an indirect address (`ble @cx`) writes `EA` where the `jmp`
  needs `FF` (AS-11); `jmpi X` and `calli X` with a plain address are seven bytes where
  the 8086's far jump has five (AS-12); a line of 128 characters or more is cut after
  the 127th and the rest is read as a line of its own, a comment's tail included
  (AS-13: the tail of a long comment is assembled); a byte of `0200` or more in code
  is read in a way that depends on its value, one of them (`0351`) jumping into unmapped
  memory (AS-14). `ascmp_cases.txt` keeps each of them as a pinned difference where one
  was shown.
- *Forms*, rare by hand and absent from the compiler: `pop cs`; a byte register as the
  operand of `push`, `pop` or `lea` (the original takes `pop al` as `pop ax`); `movb`
  and `cmpb` without an operand (`A4`, `A6`); a branch (`bgt`, `ble`, ...) to an
  indirect address (AS-11, which `mutos_as` refuses rather than repeat); `jmpi X` and
  `calli X` with a plain address (AS-12); a statement that is only an expression (a symbol
  plus a number, `_x+2`, or a name with a dot, `mov.b`); a line of 128 characters or more
  (AS-13); an operand of more than 64 tokens (a sum of 40 terms; the original's limit is
  the line's 127 characters); and `.globl L` twice under `-C` (the original writes the
  `L` symbol once more for each). (`jmpi` and `calli` of a register and `br`/`call` of a
  byte register are done.)
- *Leniency.* `mutos_as` does not make the checks that give the original
  `Byte instruction cannot use R`, `Relocatable values not allowed for bytes`,
  `Illegal relocatable expression`, `Short jump to external not allowed`,
  `Invalid index register`, `Value out of range`, `Illegal redeclaration of symbol`,
  and takes a missing last linefeed, CR LF and the shift count `1` unmarked; it refuses a byte
  with the high bit set (`0200` and up) in code and a NUL, which the original takes
  (AS-14).

**The last run of the fuzzer** (the final binary; the seed in brackets):

| generator | cases | `same` | `obj-same` | `both-reject` | `PORT-REJECTS` | `PORT-ACCEPTS` | `OBJ-DIFF` |
|---|---|---|---|---|---|---|---|
| `insn` (20) | 1,200 | 169 | 533 | 356 | 26 | 89 | 27 |
| `prog` (20) | 800 | 114 | 31 | 558 | 12 | 66 | 19 |
| `corpus` (20) | 800 | 338 | 80 | 230 | 0 | 150 | 2 |
| `mix --mutate 0.5` (21) | 600 (8 skipped) | 132 | 60 | 316 | 6 | 68 | 10 |
| `cc` (20) | 1,500 (91 skipped) | 1,284 | 0 | 105 | 0 | 20 | 0 |
| `cc --mutate 0.5` (21) | 600 (28 skipped) | 357 | 0 | 168 | 8 | 37 | 2 |

Every `OBJ-DIFF` and `PORT-REJECTS` of these runs is on the list of kept differences
above: the words only one of the two has (`aam`, `hlt`, `repe`, `repne`, `scas`, `scasb`,
`inb`, `outb`, `movsw`, `fadd`), `(bp)` with an immediate (AS-10: `lds`/`xorb`, `rorb`,
`and`, `cmp`, `lea`, `sub`), `.globl L` twice under `-C`, a comment line of 128 characters
or more (AS-13), `pop cs`, `mov.b`, `push bl`, `bloss`, a branch to an indirect address
(AS-11), `jmpi L` (AS-12), a sum of 54 terms (64 tokens in an operand), and a byte
`0377` (`mutos_as` refuses bytes with the high bit set). `asfuzz.py --words` ran all 273
words of the original and the 17 only `mutos_as` has through the operand shapes: its
5,582 cells give 94 `same`, 575 `obj-same`, 4,606 `both-reject`, 124 `PORT-REJECTS`,
133 `PORT-ACCEPTS` and 50 `OBJ-DIFF`; the 50 words with an `OBJ-DIFF` are the 39
operand-less words of the 8087, `aam`, `hlt`, `scab`, `scaw`, `inb`, `outb`, `movsw`,
`repe`, `repne`, `scas`, `scasb`. No cluster was unexplained; the three kinds the
confirmation run still turned up - AS-12, AS-13 and AS-14 - are bugs of the original,
not of `mutos_as`.

**Recommendation on a port** (the decision is the project owner's). *Not now.*

- *For the compiler chain a port would change nothing*: its output is assembled to the same
  object in every case the original accepts, and the 76 golden objects are byte for
  byte (76/76 with `-1`).
- *Where it would matter is hand-written assembler*, and there the record is the
  argument: every time the fuzzer was taught a new dimension - the size of `K(bx)`, `call
  @(bx)`, the order of symbols, character constants, a long sum, an operand it could
  not parse - it found a form that `mutos_as` assembled to a different object without
  saying so, more than a dozen in all, none of them in the corpus; all are fixed. The
  confirmation run on the final binary (5,500 generated cases) found none of that kind,
  which is evidence and not proof. A re-implementation can only be as good as the
  dimensions somebody thought to probe; a port is the original's code, its messages,
  options (`-e`, `-l`), exit statuses, 8087 words and its bugs (AS-04 to AS-14: listed,
  and reproduced where they can be, as the ports of `c1` and `c2` do).
- *For a port*: the project description names the original's sources as the basis; the
  oracle, the comparator, the pins and the 76 goldens exist, so a port is checked from
  its first line; the earlier ports (`c0`, `c1`, `cpp`, `ld`, `cc`, `c2`) took about one
  session each; the instruction table (273 words) can be read from the binary.
- *Against, or at least not urgent*: nothing the compiler writes is affected;
  the next steps (6: `ar`/`ranlib`, 7: `make`) do not use `as`'s differences; `-1` is
  an extension the original does not have and would be added to the port; a port
  also repeats the original's bugs, so that a hand-written source with a long comment
  line (AS-13) would fail as it fails on the hardware.
- *Proposal*: keep `mutos_as` for now and schedule the port after step 6 (`ar`,
  `ranlib`) - earlier if hand-written assembler (the kernel's `.s` files, a runtime
  library) becomes a goal of the project, because that is the only place where the
  present differences can matter. `mutos_as` stays until then; the tests (`make test`,
  `ascmp.py`, `asfuzz.py --gen cc --strict`) become the regression suite of the port.
  Steps 6 and 7 are done (2026-10-26, 2026-11-02) and the decision is still the project
  owner's: the port of `mutos_as` is not started.

### Carried from 2026-09-29 (not re-run this session)

- **AddressSanitizer + UBSan** (`-fsanitize=address,undefined`, `-O0 -g`,
  LeakSanitizer off): 0 errors over 156 `.s` inputs (the goldens, `floatdat.s`,
  `float_open/fltsig.s`, the seven `v30_speculative` sources and the 71 compiler
  goldens of then) plus a probe of every `.float`/`.double` acceptance and refusal
  path; `fltconst_test` under both sanitizers over 130,107 texts.
- The records of the sessions that added `.float`/`.double` and settled the real
  conversion (2026-09-27 to 2026-09-29) are in
  [`docs/history/status_m2_as.md`](docs/history/status_m2_as.md); the conversion
  itself is summarized in open item 7 below and in
  `tests/mutos_as/float_coverage/README.md`.

### Auxiliary deliverables
- `mutos_as.1` — English troff man page (to be rewritten at the end of the project,
  `CLAUDE.md`'s man-page rule; until then it may lag behind).
- `fltconst_test` — reads `f <text>`/`d <text>` lines and prints what
  `.float`/`.double` would store, or why it is refused (`SYNTAX`, `ROUNDING`,
  `RANGE`, `UNKNOWN`) (for `float_coverage/fltmodel.py check` and
  `libcatof.py check`).
- `float_coverage/libcatof.py` — `libc.a`'s own `atof` under an 8086 emulator.
- `run_goldens.sh` / `mk_goldenbase64.sh` — batch golden-diff test runner and base64
  golden-file generator.
- CLI: `-o output` (also `-ooutput`), `-W` (suppress diagnostics), `-v`, `-g` (accepted,
  no effect) and `-C` (leaves out the compiler's `L` symbols - the original `cc` passes
  `-gC`; since 2026-10-25), `-1` (the 80186/V30 instructions; since 2026-10-25),
  multi-file concatenation, stdin fallback, exec-bit on success, `asf` argv[0]
  alias. `-L` (control 'L'-prefixed compiler-internal label output) was implemented,
  found to break golden parity, and was **deliberately removed** per explicit prior
  user decision; the original does not have it either (it is `Unknown option L
  ignored.`), while its `-C` is what the manual's `-L` seems to have been.
- `tests/realbin/ascmp.py` and `asfuzz.py` - the differential comparator and fuzzer
  against the emulated original (`tests/realbin/README.md`); `tests/mutos_as/
  check_switches.sh` - `-g`/`-C`/`-1` (in `make test`).
- Modified native-kernel `conf/Makefile` (`-S` instead of `-c`, retains `.s` files,
  copies output to `.o.golden`) — mechanism for generating more real hardware golden
  files from the kernel source tree in the future.

---

## Milestone 3 — `mutos_cpp` (C preprocessor)

**Status: a port of the original `cpp` binary (2026-10-22)** -
`docs/TOOLCHAIN_PLAN.md`, decision 4 and step 1, done. `mutos_cpp` is the original
MUTOS 1700 `cpp` (Reiser's, `cpp.c 1.6 12/12/86` and `yylex.c 1.3 12/12/86`), every
function read from the binary beside `v7/cpp` (`tests/realbin/cppmap.txt`), running on
an image of the original's data segment. The from-scratch re-implementation it replaces
(`util.c`, `source.c`, `macro.c`, `ifexpr.c`, `directive.c`, `scan.c`, `main.c`,
`mutos_cpp.h`) is deleted; its records are in
[`docs/history/status_m3_cpp.md`](docs/history/status_m3_cpp.md). The original's
behaviour, its MUTOS deltas from v7 and what the port refuses: `src/mutos_cpp/README.md`;
how it went: `docs/devlog/originals_and_ports.md`, "The port of `cpp`".

### The port (verified 2026-10-22)

- **Sources** (`src/mutos_cpp/`): `cpp.h` (the image, the binary's data addresses),
  `cpp.c` (`cpp.o`'s 24 functions), `cpy.c` (`yylex()`, `tobinary()`, `yyparse()`),
  `cpplib.c` (the machine: files, the output in 512-byte blocks, libc.a's printf,
  `calloc()`, the frames, the refusals), `cppdata.c` (the binary's data, generated by
  `tests/realbin/cpptables.py --c`), `main.c`. Zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **`make test`**: `tests/mutos_cpp/run_goldens.sh` 5/5 byte-identical; through
  `tests/mutos_cc/run_goldens.sh` all 132 `.i` goldens of the compiler corpus, its
  results unchanged (Milestone 4).
- **Against the emulated original** (`tests/realbin/cppcmp.py goldens`): the source of
  every one of the 137 `.i` goldens three ways - with `-P` (the original's output also
  against the golden), without `-P` (the `# N "file"` lines, as `cc` runs `cpp`), into
  an output file named on the command line: 411/411 identical (output, messages, exit
  status); the same with an ASan/UBSan build.
- **Differential fuzzing** (`tests/realbin/cppfuzz.py`), with the final port and
  tools: 2,000 programs - 1,000 generated (seed 20) and 1,000 with half of them mutated
  (seed 21, `--mutate 0.5`), one in ten at cpp's limits -, no difference: 1,802 identical; 33
  refused by the port, its output and messages a prefix of the original's (a frame
  array run past or read unwritten, an `#include` of a directory); 130 on which the
  original does not finish (it crashes on its overwritten stack, an `idiv` faults, it
  loops) - each refused by the port, its output a prefix of what the original wrote -;
  35 endless in both (v7's endless expansions; the outputs agree as far as the shorter
  goes). `make check-realbin` runs 200 of each kind (seed 1): 172 identical in each, the
  rest refused, not finished by the original or endless. The fuzzing also found a
  fault of the emulator: it lost the end of an output file when the original crashed
  (fixed in `mutosemu.py`; `tests/realbin/README.md`, "Limits of the emulator").
- **`# N "file"` lines**: written without `-P` as the original writes them - `CLAUDE.md`'s
  exception for `mutos_cpp` has ended. Through them the real `c0`'s messages carry the
  file name and the right line (`e.c:3:`, where the re-implementation gave `4:`).
- **Paths**: the default include directory `/usr/include` is looked up under the MUTOS
  root (`src/h/mutos_root.h`), and so is an absolute `#include` name and everything found
  through either; names from the command line (input, output, `-I`) are host paths
  (`src/mutos_cpp/README.md`, "Options and files").
- **Refused on purpose** (exit status 2, a message ending in "refused"; the output
  written is a prefix of the original's): an access to libc.a's stdio state, crt0's words
  or unallocated memory through a wild pointer; a frame array run past (`doincl()`'s
  `filname[]`, `dodef()`'s `formtxt[]`, `stsym()`'s `buf[]`, `subst()`'s frame up to its
  saved BP) or read where it was not written (`#include "name` without the closing
  quote); an `#include` that opens a directory; a `#if` division by 0 or of -32768 by
  +-1 (the 8086's `idiv` faults). The port keeps the original's stack pointer, so those
  frames are where the original's are (under the emulator's layout of the arguments).
- **The original's bugs**: CPP-01 to CPP-14 (CPP-14 found 2026-10-25) in
  `docs/MUTOS_ORIGINAL_BUGS.md`, each with
  a trigger run under the emulator - among them a miscompiled octal character constant
  in `#if` (CPP-01, the compiler reused a register), eight `-I` options switching `-P`
  off (CPP-03), actuals over 512 bytes running over `subst()`'s frame (CPP-05,
  reproduced: the frame is in the image), a comment's linefeeds in a macro call written
  twice (CPP-10).

### What the binary does differently from v7 (read 2026-10-22)

A name is significant to 128 characters (v7: 8) and may contain `$`; `mutos` is the
only predefined name; the symbol table has 1000 entries, the string space 32000 bytes;
"missing endif" at the end of the input; a macro call across lines keeps the output's
line count (its actuals' linefeeds come back with the expansion); an escaped linefeed
in a `#define` writes a linefeed; `#line N "name"` takes the file name; no expansion
inside `<...>` of an `#include`; `#undef` of an unknown name enters nothing; comments in
`#if` lines removed under `-C` too; `tobinary()` reads a-f right. Per function:
`tests/realbin/cppmap.txt`.

### Auxiliary deliverables

- `mutos_cpp.1` — English troff man page (to be rewritten at the end of the
  project, `CLAUDE.md`'s man-page rule; it still describes the re-implementation).
- `src/mutos_cpp/README.md` — the original's behaviour, the port, its refusals.
- `tests/mutos_cpp/run_goldens.sh` — batch golden-diff test runner.
- `tests/realbin/`: `cppmap.txt`, `cppdis.py`, `cpptables.py`, `cppcmp.py`,
  `cppfuzz.py` (its `README.md`).

---

## Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler)

**Status: the driver and the two compiler passes are ports of the originals.**
`mutos_c1` (since 2026-10-13), `mutos_c0` (since 2026-10-18) and the driver `mutos_cc`
(since 2026-10-24, `docs/TOOLCHAIN_PLAN.md`, step 3) are ports of the original `c1`,
`c0` and `cc`, read from their binaries and checked against the originals running
under an 8086 emulator. Every golden of `tests/mutos_cc` is reproduced at every stage
(`.i`, `.1`, `.2`, `.s`, messages and exit status): the 62-file construct corpus,
the nine real kernel files of `11_kernel/` and the 61 `fltprobe/` probes of fourteen
rounds - 132 files, `fltprobe/p19_open3` up to the real compiler's own invalid
output -, and since the driver also through `mutos_cc` itself. `mutos_c2` followed
(Milestone 5, 2026-10-25). `src/mutos_cc/README.md` describes the three ports; the records of the
hand-written `mutos_c0`/`mutos_c1` before them are in
[`docs/history/status_m4_compiler.md`](docs/history/status_m4_compiler.md).

### `mutos_cc` is the port of the real `cc` (verified 2026-10-24)

`mutos_cc` is the original MUTOS 1700 `cc` - v7's `cc.c` with the MUTOS deltas -,
every function read from the stripped binary beside `v7/cc/cc.c`
(`tests/realbin/ccmap.txt`), running on an image of the original's 64K segment. The
original's behaviour, the image, the paths and what the port refuses:
`src/mutos_cc/README.md`, "The port of the real `cc`"; how it went:
`docs/devlog/originals_and_ports.md`, "The port of `cc`".

- **Sources** (`src/mutos_cc/ccport/`): `cc.h` (the image, the binary's addresses),
  `cc.c` (`cc.o`'s ten functions), `cclib.c` (the machine: the image's initial
  state with `main()`'s frame, libc.a's printf, `strcpy()`, `strcat()`, `malloc()`
  and `mktemp()` on the image, the system calls and the passes on the host, the
  paths, the refusals), `ccdata.c` (the binary's text and data, generated by
  `tests/realbin/cctables.py --c`), `main.c`. Zero warnings under `-std=c11 -Wall
  -Wextra -Wpedantic`.
- **Against the traced original** (`tests/realbin/cccmp.py`; the port's passes are
  `tests/realbin/ccstub.c`, which logs its arguments and leaves the files the pass
  would write): the passes run with their MUTOS paths and arguments, stdout and
  stderr before, between and after them, the exit status, the files left in the
  directory and the root's `tmp/` - `cases`, 177 command lines (every option,
  `-o` in its forms, sources, objects, libraries, the limits and overruns,
  passes that fail or are killed): 175 the same, 2 refused (CC-01, a pointer
  past the break); `run`, the construct corpus with the emulated original passes
  run in both: 124/124 the same.
- **Whole programs**: `cccmp.py programs`, the port with the ported passes against
  the original chain (`cc x.c`, `cc -O -o x x.c` on the construct corpus): 124/124
  the same `a.out` and messages; `cccmp.py goldens`, the ported chain against the
  real-hardware goldens: `cc -S` 132/132 (`c1`'s messages and exit status
  included, `p19_open3` refused as `invalid_goldens.txt` says), `cc -P` 132/132,
  `cc -O -S` 9/9 (with the original `c2`; since 2026-10-25 with `mutos_c2`, the same -
  Milestone 5). `make test`'s
  `tests/mutos_cc/check_driver.py` (no emulator): `cc -S` 131/131 and 1 refused,
  `cc -P` 132/132, `cc -o` on the construct corpus 62/62. The corpus's
  `Makefile.mutos` files with the host's `make` and the root's tools (re-run
  2026-10-25): all thirteen - the eleven categories', `11_kernel`'s and `fltprobe`'s -,
  527 of their 528 `.s`, `.i`, `.1` and `.2` files the golden; the one that is not,
  `fltprobe/p19_open3.s`, `mutos_c1` refuses as `invalid_goldens.txt` says.
  `fltprobe`'s runs with `make -i`: its ten probes in `c1_errors.txt` end with `c1`'s
  messages and exit status 1, as on the hardware (on 2026-10-24 the twelve others were
  run).
- **Differential fuzzing** (`tests/realbin/ccfuzz.py`, the passes traced): 6,000
  cases - 1,000 with seed 1 (985 the same, 15 refused) and 5,000 with seed 2 (4,949
  the same, 51 refused) -, no difference; every refusal is CC-01. An ASan/UBSan
  build: the cases, `run` and 2,000 more fuzz cases (seed 5: 1,981 the same, 19
  refused) - no report, no difference. Thirteen mutants of the port, one small
  fault each: every one that can change what `cc` does is found by the cases or
  the fuzzing; the four that are not cannot (`tests/realbin/README.md`).
  `make check-realbin` runs `cases`, `run`, `programs`, `goldens` and 1,000 fuzz
  cases (seed 1).
- **Paths**: what the original names by an absolute path of the MUTOS file system -
  its passes, the startup files, `/tmp/ctm...` - is looked up under the MUTOS root,
  and given to the passes as the host path under it; the command line's names are
  the host's (decision 4), also inside a name `cc` builds (`-B`'s prefix): the image
  marks every byte from the command line. The passes run as host processes with
  `MUTOS_ROOT` set to `cc`'s root.
- **Refused on purpose** (exit status 2, a message ending in "refused"): libc.a's
  data and buffers (about 150 `-P`: CC-02), memory past the break (CC-01's broken
  `malloc()` list), a heap that reaches `main()`'s frame, arguments longer than
  about 32K.
- **The original's bugs**: CC-01 to CC-06 in `docs/MUTOS_ORIGINAL_BUGS.md`, each
  with a trigger run under the emulator but CC-06 (no visible effect) - a source
  path of 50 characters or more runs past `ts[]` or its `malloc()` block (CC-01),
  more than 100 `-P` run over `main()`'s `pv` (CC-02), a long `-K` over the passes'
  paths (CC-03), a long `-B` prefix into the next path (CC-04), `-o` before an
  option without an object after it (CC-05).

### What the binary does differently from v7 (read 2026-10-24)

`-a` writes "Executing ..." before each pass (the messages the traces had seen in
the data); `-K<n>` gives `c1` its option `s<n>` (`kopt[]`, appended without a
check); `-f` swaps in `/lib/c0f`, `/lib/c1f`, `/bin/asf` and adds `-lf` (no
`fcrt0.o`, no `-lfpsim`); `-o` with a `.c` or `.o` name, or one starting with `-`,
reads it again as an input, and the next object's name without suffix and
directory becomes the output's (v7: "Would overwrite"); one
`mktemp("/tmp/ctm0XXXXXX")`; `as -gC` (v7: `-u`); a failed `c0` unlinks `tmp4`, a
failed `c1` `tmp1` and `tmp2`, no `cunlink()` before `as`; `ld -X pref [-o out]
files [-l2 | [-lf] -lc]`. The option switch is hashed (`c % 5`, then `c / 5`).

### Verified 2026-10-20

- **`make test`** (`tests/mutos_cc/run_goldens.sh`): 121 files byte-exact at all
  four stages - the 62-file corpus, all nine `11_kernel` files and 50 `fltprobe`
  files -, `fltprobe/p19_open3` in category 8 (the first 228 lines, then refused
  where the original prints bytes past `regname[]`), ten `fltprobe` files in
  category 9 with all of the real `c1`'s messages (`p21_fltexp` 2, `p28_fltstk` 12,
  `p37_fltstk3` 17, `p41_fltstk4` 23, `p45_fltstk5` 23, `p49_fltstk6` 26,
  `p52_fltinf8` 22, `p53_fltstk7` 5, `p56_fltinf9` 18, `p57_fltstk8` 18);
  categories 2 to 7 and 10 empty, 0 mismatches.
- **`make check-realbin`**: the real `cpp`, `c0`, `c1` and `as` against the goldens -
  137/137 `.i`, 132/132 `.1`/`.2`, 132/132 `.s` (with the messages of
  `c1_errors.txt`), 75 of the 76 `.o` (the 76th, `mch_insw_outsw`, as expected); 160
  of the 496 `c1` code-table rows used; `c1_tables.txt`, `c1tab.c` and `c0data.c`
  current; `mutos_c1` at depths 2, 3 and 4 identical on 131 of 132 files,
  `p19_open3` refused as specified; `c1fuzz.py` (200 programs, seed 1): 175 the
  same, none different, 20 refused by the port on purpose, 4 rejected by the real
  `c0`, 1 on which the original does not finish (refused by the port); `mutos_c0` on
  all 137 `.i` inputs identical to the original; `c0fuzz.py` (200 programs, seed 1,
  plain and mutated): none different - 198 and 195 the same, the rest refused by the
  port or not finished by the original, as specified. New with step 0 of the plan:
  **the original `cc`, traced** (`tests/realbin/cctrace.py goldens`), running the
  original passes from inside each corpus directory as on the hardware - `cc -S`
  132/132 `.s` goldens, with exactly the messages of `c1_errors.txt` and the round
  logs and exit status 1 where `c1` printed any, none and 0 elsewhere; `cc -P` 132/132
  `.i` goldens, `cpp` the only pass; `cc -O -S` on `11_kernel` 9/9 the
  `kernel_opt/` sources, `c2` the last pass.
- **`make check-libcatof`**: `mutos_c1`'s constant texts against `libc.a`'s own
  `ecvt(atof())` - 5,630 literals compared, 0 differ; float initializers through
  `fstsp` - 5,649 compared, 0 differ (the `mutos_as` half: Milestone 2).

### `mutos_c0` is the port of the real `c0` (verified 2026-10-18)

`docs/C0_PORT_PLAN.md` is done: `mutos_c0` is built from
`src/mutos_cc/c0port/` - the original `c0`'s 73 functions decompiled from
the binary with v7's `c00.c`..`c04.c` as the skeleton
(`tests/realbin/c0map.txt`: 60 as v7's, 12 with local MUTOS deltas,
`werror()` new), running on an image of the original's 64K data segment
whose initialized part is generated from the binary (`c0data.c`,
`c0tables.py --c`). The from-scratch front end (`c0_lex.c`, `c0_diag.c`,
`c0_outcode.c`, `c0_sym.c`, `c0_parser.c`, `c0_main.c`, their headers and
`mutos_cc.h`, 8,189 lines) is deleted. The port was written in the session
of 2026-10-14 and left uncommitted, its plan already saying "done"; the
session of 2026-10-18 built it in, checked it, corrected the emulator and switched.
Verified 2026-10-18, with the final build:

- **`make test`**: 121 files byte-exact at all four stages - the 62-file
  corpus, 50 `fltprobe` files and all nine `11_kernel` files (`02_prim`
  to `09_amx` end-to-end for the first time) -, `p19_open3` in category
  8 (228 lines), ten files in category 9 with all of the real `c1`'s
  messages; category 3 (now "`mutos_c0`: refused"; any other failure of
  `mutos_c0` counts as a mismatch, since the real `c0` wrote every
  golden) and category 10 empty; 0 mismatches. `mutos_as` 76/76, 131/131
  compiler goldens assemble (`p19_open3` refused as listed), `mutos_cpp`
  5/5.
- **`make check-realbin`** (with the new `c0` steps): the real `cpp`,
  `c0`, `c1`, `as` against the goldens as before (137/137, 132/132,
  132/132, 75 of 76); `mutos_c1` at depths 2, 3 and 4 131/132 identical,
  `p19_open3` refused as specified, its 200-program fuzz run without a
  difference; `c0data.c` current; `mutos_c0` on all 137 `.i` goldens
  identical to the original - `.1`, `.2`, messages, exit status -; its two
  200-program fuzz runs without a difference.
- **Differential fuzzing** of `mutos_c0` against the emulated original
  (`tests/realbin/c0fuzz.py`, final build and emulator): 5,800 programs
  - 2,000 of its own declaration-heavy generator (seed 21), 2,000 of them
  mutated at the token level (seed 22), 600 each of `c1fuzz.py`'s and
  `fuzz_c.py`'s generators (seeds 23, 24) and 600 of `c1fuzz.py`'s
  mutated (seed 25) - 5,723 identical (`.1`, `.2`, messages and exit
  status), **none different**, 22 refused by the port, each checked
  against what the original wrote up to there (12 a member redeclared in
  a structure's list, C0-04; 10 a `goto` after a syntax error, C0-06),
  and 55 on which the original does not finish, all refused by the port
  (33 reading past the end of the file, C0-02; 22 an initialized array
  of zero-length elements, C0-03). Earlier in that session, with the
  emulator's shifts fixed but not yet its division, 11,800 more programs
  (seeds 100 to 500, 9 and 10: 10,000 of its own generator, 1,200 of
  `c1fuzz.py`'s, 600 of `fuzz_c.py`'s, 5,600 of all of them mutated) and
  1,000 through an ASan/UBSan build - no difference either.
- **ASan/UBSan** build of the port: no report on the 137 goldens nor on
  1,000 fuzz programs (seeds 31 and 32, half of them mutated: 976
  identical, 7 refused, 17 on which the original does not finish).
- **The emulator was an 80386.** Unicorn masks a shift count to CL's
  low five bits and lets a signed quotient of -32768 through; the 8086
  and the V30 do neither (the AP-186 note; the V20/V30 manual: "temp <-
  CL, while temp != 0", the most negative quotient -32,767). So the
  emulated `c0` folded `1 << 40` to 256 (the hardware: 0) and `-32768 /
  1` to -32768 (the hardware: a divide error). `tests/realbin/mutosemu.py`
  now does the 8086's shift and the 8086's divide error itself (hooks on
  those instructions that can reach the cases); the real binaries give
  every golden as before, `mutos_c1` is unchanged at every depth. The
  first port's refusal of large shift counts - which rested on the
  emulator - is gone (`c0port/c01.c` shifts as the 8086, as
  `c1port/c1.h` did); `-32768 / 1` and `% 1` are refused by both ports.
- **The originals' bugs**: `docs/MUTOS_ORIGINAL_BUGS.md` has C0-02 to
  C0-12 (new, each with a minimal trigger run under the emulator) and
  C0-01 extended to `-32768 / 1` - now eleven in `c1`, seven in `libc.a`,
  three in `as`, twelve in `c0`. Among them: a structure with enough
  alignment holes overruns `strdec()`'s member list and frame (the
  original ends in `main()`'s "Arg count"), input bytes 0200..0377 index
  `ctab[]` past its end (0200 acts as `+`, from 0250 on as EOFC), and
  whether a redefined enum tag is reported depends on its storage class.
- **`tests/mutos_cc/fuzz/fuzz_c.py`** accepts the port's refusals (exit
  status 2) and counts a `mutos_c0` rejection of a program the real `c0`
  accepts as BAD; `make fuzz` (500 programs against the real `c0` and
  `c1`): 499 correct, 1 wrong exactly as the real compiler, 0 WRONG, 0
  BAD. `c1fuzz.py --gen lconst` (300) without a difference; its real `c0`
  now stops on a `-32768 / 1` it folds, as the 8086 does.
- **`dump_temp.py`**: every one of the 264 golden `.1`/`.2` files decodes;
  its comments point to the port. The man pages `mutos_c0.1` (rewritten)
  and `mutos_c1.1` (its description of `c1_gen.c` replaced by the port's)
  describe the ports.

### `mutos_c1` is the port of the real `c1` (verified 2026-10-14)

`docs/C1_PORT_PLAN.md`'s steps A to E are done: `mutos_c1` is built from
`src/mutos_cc/c1port/` - the original `c1`'s 67 functions decompiled from
the binary with v7's `c10.c`..`c12.c` as the skeleton (each function's
text offset and its changes against v7 in `tests/realbin/c1map.txt`), its
code tables and op data generated from the binary (`c1tab.c`,
`c1tables.py --c`), and the `libc.a` routines its output passes through
re-enacted as the binary has them (`c1lib.c`). `c1_gen.c` (15,143 lines),
`c1_main.c` and `c1_stream.c` are deleted. Verified 2026-10-14:

- **`make test`**: 113 files byte-exact at all four stages (62/62 corpus,
  50 `fltprobe` files - round 14's `p54_long10` and `p55_elem11` among
  them - and `11_kernel/01_delay`), `p19_open3` in category 8 (now 228
  lines: the port writes the statement's valid start and refuses at the
  register name outside `regname[]`), ten files in category 9 with all of
  the real compiler's `c1` messages and exit status 1 (`p56_fltinf9` and
  `p57_fltstk8` new, eighteen messages each, from `round14.log` into
  `c1_errors.txt`), and a new category 10: `mutos_c1` alone, from the
  `.1`/`.2` goldens of a file `mutos_c0` refuses - all eight other
  `11_kernel` files byte-exact; 0 mismatches. `mutos_as` 76/76, 131/131
  compiler goldens assemble (`p19_open3` refused as listed), `mutos_cpp`
  5/5.
- **`make check-realbin`** (now with the port): on all 132 golden
  `.1`/`.2` pairs, depth 2 (the trees as read and after `optim()`), depth
  3 (the `.s` with all of the original's traces on) and depth 4 (the
  `.s`, the messages and the exit status) identical on 131, `p19_open3`
  refused after 228 identical lines as `invalid_goldens.txt` specifies;
  then a 200-program fuzz run at depth 4.
- **Differential fuzzing** against the emulated original
  (`tests/realbin/c1fuzz.py`, depth 4, with the final build, run
  2026-10-14): 6,000 programs of its own generator (seeds 1 to 6: char
  to double, pointers, a structure with bit-fields, register variables,
  calls, switches) - 5,151 identical, **none different**, 694 refused by
  the port on purpose, each refusal checked against the original (634 the
  register "after" AX, `p19_open3`'s case; 22 `ltof` of 0; 20
  `xlongrel()`; 17 a long constant shift; 1 `regname[8]`), 16 where the
  original does not finish and the port refused, 139 the real `c0`
  rejects (the generator's own errors); 1,500 programs of `fuzz_c.py`'s
  generator - all identical; 1,000 of long constant arithmetic (`--gen
  lconst`) - 929 identical, the rest rejected by the real `c0` (its own
  divide checks).
- **ASan/UBSan** builds of the port: no report on any golden at any depth
  nor on 500 more fuzz programs (seed 7: 426 identical, 59 refused as
  above, none different).
- **Found by the fuzzer and fixed in the port**: `libc.a`'s `aldiv`,
  through which `lconst()` folds a long constant `/`, is not C's division
  - its short path divides by the low word alone when the high word is 0
  and the low word positive or 1 and the low word negative, its long path
  does sixteen shift-and-subtract steps and clears the quotient's high
  word (`0x7fffffffL / 0177777` is -65535); `alrem`'s long path is the
  same sixteen steps. Both re-enacted on 16-bit words; a generator for
  long constants at those edges (`--gen lconst`) found the old port
  different on 232 of 300 programs, the new one on none.
- **Refused on purpose** (exit status 2, everything before written; the
  fuzzer checks that the original wrote the same up to there, or did not
  finish): a register name outside `regname[]`, a long constant shifted
  by a count whose tested word is 0 (`lconst()` jumps into an
  instruction), `xlongrel()` reading past `lrtab[]`, `ltof` of 0, `dtoi`
  and `ftol` overflows and `dtoi` from 2\*\*15.
- **`tests/mutos_cc/fuzz/fuzz_c.py`**: its default oracle is now the
  emulated original - `mutos_c0`'s `.1`/`.2` and `mutos_c1`'s `.s`,
  messages and exit status must be the real `c0`'s and `c1`'s byte for
  byte -, `x86sim.py` stays the semantic check, and now models the
  8086's flags as the 8086 sets them (the port branches on the flags of
  `add`, `sar`, `test`, ..., which `c1_gen.c` never did); same results on
  every golden as before. 5,000 programs (seeds 1 to 4, with and without
  arrays and file scope): 4,957 correct, 0 WRONG, 0 BAD, 43 wrong or not
  checkable exactly as the real compiler's own code (next item).
- **The real `c1` miscompiles** some expressions, and `mutos_c1` with it
  (counted apart by `fuzz_c.py`, not a failure - about 0.9% of its
  programs): its code tables keep PDP-11 assumptions where the 8086
  differs. On the PDP-11 `com` and `mov` set the condition codes, on the
  8086 `not` and `mov` set no flags - so `s = (~a[k][j] || n++);` is
  `not di` / `bne`, branching on the flags of the `add` before it (`n++`
  is lost), and `y = (!y >= 0);` branches after a `mov`; `(u = 4, x) * 0`
  drops the assignment (v7's own `acommute()` returns the 0 of `e * 0`
  or `e & 0` without looking for side effects). `x86sim.py` finds these
  because it models `not` and `mov` as leaving the flags alone.
- **`mutos_c0` fixed** (found by the same oracle in 14 of the first 4,000
  programs): it folded `!c`, `c1 && c2` and `c1 || c2` of constants;
  v7's `fold()` has no EXCLA, LOGAND or LOGOR, and the real `c0` writes
  `CON 1 / EXCLA` for `!(12 >> 3)`. Now it does too; `make test`
  unchanged, the 5,000 programs above have no `c0` difference.
- **The originals' bugs in one place**: `docs/MUTOS_ORIGINAL_BUGS.md`
  (new, kept current by `CLAUDE.md`'s Workflow Guideline 9) lists every
  bug found so far in `c0`, `c1`, `as` and `libc.a` - eleven in `c1`,
  seven in `libc.a`, three in `as`, one in `c0`, none known in `cpp` or
  `ld` - with trigger, mechanism, evidence, what this project's tools do
  with it, and a fix idea. Found while writing it: the original `c1`
  cannot fold a long constant `>>` at all (`0x12345678L >> 4` crashes
  it - C1-07), and `libc.a`'s `itod` makes `double d = 1001;`
  `1.00100001525902189e+03` (L-01).
- Constructs `c1_gen.c` refused that `mutos_c0` accepts - an int divided
  by a constant, `++`/`--` and compound assignment on a global, a member
  of a file-scope struct, a call's value tested - now compile through
  `mutos_c0` and `mutos_c1` exactly as through the real `c0` and `c1`
  (checked under the emulator); the remaining refusals are `mutos_c0`'s.

### The original binaries: an emulated oracle, `c1`'s tables (verified 2026-10-12, re-run 2026-10-20)

The original `as`, `c0`, `c1`, `c2`, `cc`, `cpp` and `ld` came from the
hardware (commit `e8c7516`; since 2026-10-25 in `mutos/mutos_bin/`, until then
`mutos_bin/` - see its `README.md`). They run on
the modern host under an 8086 emulator - `tests/realbin/` (`make
check-realbin`, needs the Python module `unicorn`, not part of `make
test`) - and give the project's goldens back:

- **real `cpp`: 137/137** `.i` goldens (`tests/mutos_cc` and
  `tests/mutos_cpp/c`); **real `c0`: 132/132** `.1`/`.2` goldens;
- **real `c1`: 132/132** `.s` goldens, with exactly the messages of
  `c1_errors.txt` and of `round14.log` (`p56`, `p57`) and exit status 1
  where it printed any; `p19_open3`'s invalid lines included;
- **real `as`: 75/76** `.o` goldens - the 76th, `kernel_opt/
  mch_insw_outsw`, is not the real `as`'s output for its source: the real
  `as` does not know `insw`/`outsw` and assembles each as an undefined
  external word, silently; the golden is the real `mch.o` (see Milestone
  2's table).

Reading the stripped `c1` (`tests/realbin/c1map.txt`, `c1dis.py`): it is
v7's `c1` ported to the 8086 - 67 functions in v7's source order (v7's
62 without `oddreg`/`psoct`, plus seven new), v7's data structures, and
code tables in v7's own `cvopt` encoding with MUTOS assembler text:
`regtab`, `efftab`, `cctab`, `sptab`, 496 rows and 292 templates, read
back into v7's `table.s` notation in `tests/realbin/c1_tables.txt`. The
goldens use 160 of the 496 rows (`check_real.py coverage`). Compared with
v7 so far: ten functions do what v7's do (`reorder`, `sreorder`, ...),
seven have small local changes (`main`, `match`, `degree`, `dcalc`,
`delay`, `acommute`, `optim`), twelve are rewritten for the 8086 and the
floating-point runtime (`rcexpr`, `cexpr`, `comarg`, `doinit`, ...),
seven are new; the other 31 are placed, not compared yet. The real `c1`'s own debug traces (options `a`, `b`, `c`,
`d`, `r`) show its trees before and after `optim()` and every
`rcexpr`/`cexpr` call; option `s<n>` is the `chkstk` frame threshold.
Full findings: `docs/devlog/originals_and_ports.md`'s "The original binaries".

**Since step 0 of the plan (2026-10-20)** the emulator fakes `fork`/`exece`/`wait`
and serves `stat`/`fstat`, `link`, `chmod` and `umask`, so the original `cc` and `ld`
run too: `tests/realbin/cctrace.py` traces `cc` - each pass's path and arguments, the
messages, the exit status - or, with `--run`, runs the whole original chain; through
it `cc -S` gives the 132 `.s` goldens (with `c1`'s messages and exit status as the
hardware's transcripts have them), `cc -P` the 132 `.i` goldens, `cc -O -S` the nine
`kernel_opt/` sources. The original `ld`'s output against `mutos_ld`'s: Milestone 1.
Findings: `docs/devlog/originals_and_ports.md`, "Step 0 of the plan".

**Decision (2026-10-12): `mutos_c1` becomes a port of the real `c1`**
(v7's sources, the binary's deltas, the binary's tables generated, not
typed), built beside `c1_gen.c` and checked against the emulated
original at four depths - see `docs/C1_PORT_PLAN.md`. Round 14 is left
to the port.

**`make test` at that commit failed, on round 14 only** (the four
round-14 goldens in the tree, `c1_gen.c` refusing `p54_long10`,
`p55_elem11` and `p57_fltstk8` and differing on `p56_fltinf9`) - until
the port, see the section above.

### Real-hardware finding: the real `as` on `p19_open3.s` (2026-10-07)

The real assembler refuses the real compiler's invalid lines too. On
MUTOS, `as -o p19.o p19_open3.s` prints `***ERROR*** syntax error, line
229` four times and `***ERROR*** syntax error, line 231` twice, then
`p19_open3.s: 3 errors.`, exits with status 2 and leaves a 0-byte `p19.o`
(`tests/mutos_cc/fltprobe/p19_as.log`). No MUTOS 1700 binary can contain
that code. `mutos_as`'s refusal of the same two lines ("could not classify
operands", exit status 1, no object) and `p19_open3`'s place in
`invalid_goldens.txt` are confirmed; nothing in the code changes. The
real exit statuses are 2 for syntax errors and 4 for the floating overflow
abort; `mutos_as` exits with 1 for both, a documented difference that no
test depends on. A first attempt, run with `> p19_as.log 2>&1`,
left the log empty, even the `echo` appended after it, and that is
unexplained (the disk was not full). The transcript was copied from the
screen.

### Next up

The order of [`docs/TOOLCHAIN_PLAN.md`](docs/TOOLCHAIN_PLAN.md), section 3 (decided
2026-10-20):

0. ~~**Infrastructure**~~ - **done 2026-10-20**: `tests/realbin/mutosemu.py` fakes
   `fork`/`exece`/`wait` and serves `stat`/`fstat`, `chmod`, `umask` and `link`;
   `tests/realbin/cctrace.py` traces the original `cc` (the goldens through it: see
   "The original binaries" above); `mutos_root/` is built by `make`
   (`scripts/mutos_root.sh`), `src/h/mutos_root.h` is the lookup, every Makefile
   passes its default; a `.gitignore` for `mutos_root/` and the build products;
   `mutos_as` accepts `-g`/`-C`. Checked by `tests/mutos_root/check_root.py` (`make
   test`) and `make check-realbin`.
1. ~~**Port of `cpp`**~~ - **done 2026-10-22** (Milestone 3): `mutos_cpp` is the
   original `cpp`, read from its binary - the `# N "file"` lines, `/usr/include` under
   the root, the original's behaviour where the re-implementation simplified, its bugs
   (CPP-01 to CPP-13; CPP-14 found 2026-10-25). Checked by `tests/realbin/cppcmp.py` and
   `cppfuzz.py`.
2. ~~**Port of `ld`**~~ - **done 2026-10-22** (Milestone 1): `mutos_ld` is the
   original `ld`, read from its binary - `-l` under the root, the six bytes of
   "jmp $", the byte-shifted relocations, its bugs (LD-01 to LD-08); `-r`/`-d` kept as
   an extension. Checked by `tests/realbin/ldcmp.py` and `ldfuzz.py` (the emulated
   original in place of the missing `myhello`/`idhello` goldens) and
   `tests/mutos_ld/check_rflag.py`.
3. ~~**The `mutos_cc` driver**~~ - **done 2026-10-24**: `mutos_cc` is the original
   `cc`, read from its binary - `-a`, `-K`, `-f`, the new `-o`, `as -gC`, the
   passes and the startup file under the root, its bugs (CC-01 to CC-06); `-P` stops
   after `cpp`, with or without `-S`. Checked by `tests/realbin/cccmp.py` and
   `ccfuzz.py` against the traced original, and in `make test` by
   `tests/mutos_cc/check_driver.py`. `mutos_as`'s summary line is written only with
   `-v` since (Milestone 2).
4. ~~**Port of `c2`**~~ - **done 2026-10-25** (Milestone 5): `mutos_c2` is the
   original `c2`, read from its binary, 8086 only - v7's `c2` rewritten for the 8086
   with its new passes, libc.a's stdio on the image, its bugs (C2-01 to C2-07). Checked
   by `tests/realbin/c2cmp.py` and `c2fuzz.py` against the emulated original, and in
   `make test` by `tests/mutos_cc/check_driver.py` (`cc -O -S` against `kernel_opt/`).
   Every pass `mutos_cc` runs is a port of the original now.
5. ~~**`mutos_as`**~~ - **done 2026-10-25** (Milestone 2): `-C` (found to be a real
   switch of the original) and `-1` for the 80186 instructions; the differential
   comparator `tests/realbin/ascmp.py` and the fuzzer `asfuzz.py` against the emulated
   original; the compiler's output is assembled to the same object in every case
   the original accepts; the forms the fuzzer showed that matter for hand-written
   assembler are fixed, the rest measured and listed. The decision on a port is the project owner's - see
   "Step 5 of the plan" in Milestone 2 for the measurements and a recommendation.
6. ~~**Ports of `ar` and `ranlib`**, `bin/` in the root for the new tools~~ - **done
   2026-10-26** (Milestone 6): `mutos_ar` and `mutos_ranlib` are the originals, read from
   their binaries - none of the 38 functions differs from v7's in what it does; `ranlib`
   starts `$MUTOS_ROOT/bin/ar`, the root has `bin/ar` and `bin/ranlib`; their bugs (AR-01
   to AR-05, RL-01 to RL-07). Checked by `tests/realbin/arcmp.py`, `arfuzz.py`,
   `ranlibcmp.py` and `ranlibfuzz.py` against the emulated originals, and in `make test` by
   `tests/mutos_ar/check_libc.py` (`libc.a` rebuilt from its 167 objects).
7. ~~**Port of `make`**~~ (decided 2026-10-25 in place of the wrapper `mutos_make` of
   2026-10-20) - **done 2026-11-02** (Milestone 6): `mutos_make` is the original, read from
   its binary - 49 of its 50 functions are v7's, `yyparse()` is yacc's skeleton on tables
   taken from the binary; `bin/make` in the root; its bugs (MK-01 to MK-13). Checked by
   `tests/realbin/makecmp.py` and `makefuzz.py` against the emulated original, and in `make
   test` by `tests/mutos_make/check_corpus.py` (the thirteen `Makefile.mutos` of the
   corpus run through `mutos_make` and the ported tools: every golden).
8. ~~**Ports of `nm`, `size`, `strip`, `tsort`**, and `lorder`~~ - **done 2026-11-02**
   (Milestone 6): `mutos_nm`, `mutos_size`, `mutos_strip` and `mutos_tsort` are the
   originals, read from their binaries - all v7's functions, on the libc.a stdio that
   `src/h/mutos_stdio.h` models -, and `mutos_lorder` is the original shell script, run by
   the host's shell with the root's `bin/nm`; `bin/nm`, `bin/size`, `bin/strip`, `bin/tsort`
   and `bin/lorder` in the root; their bugs (SZ-01 to LO-03, 22 in all). Checked by
   `tests/realbin/utilcmp.py` and `utilfuzz.py` against the emulated originals, and in `make
   test` by `tests/mutos_binutils/check_tools.py` (an independent reading of the a.out headers
   and symbol tables, no emulator).
9. ~~**Ports of `ed` and `em`**~~ - **done 2026-11-02** (Milestone 6): `mutos_ed` is the
   original `ed` - v7's `ed.c` with two MUTOS changes, rebuilt byte for byte by the project's
   own `cc -O -s` -, `mutos_em` the original `em`'s line mode (the original `ed` with em's
   own `main()`, `commands()`, input reader, session log and commands; the screen mode
   refused); `bin/ed` and `bin/em` in the root; their bugs (ED-01 to ED-08, EM-01 to EM-13).
   Checked by `tests/realbin/edcmp.py` and `edfuzz.py` against the emulated originals, and in
   `make test` by `tests/mutos_ed/check_editors.py` (the MUTOS kernel's makefile through
   `mutos_make`: its targets that edit it with `ed`, its `mch` rule with `em` - the real
   `mch.s` and `mch.o`). **Next** (decided by the project owner 2026-11-08,
   `docs/TOOLCHAIN_PLAN.md`, section 7): stage F - the bugs of the originals corrected in
   a mode that becomes the default at the end, with a corrected copy of `libc.a` for the
   runtime bugs measured (the original archive stays) - and stage V - 80186/V30 code in
   `c1`/`c2` behind `-1`, three rules -, in the order F0 (shared infrastructure, no
   change to any output), F1, F2, F3, F3b, V1 to V4, F4, F5. Then `make
   install` and the manual pages. The decision on a port of `as` is still open.
   **F0 done 2026-11-08** ("The corrected mode and `-1`" below, `docs/RULES.md`): the
   switches `-F`/`-H`, `MUTOS_MODE` and `-1` reach the passes, the rule log, register
   and accounting checks, diagnostic hooks at C1-01, C1-02, C1-03, C1-06, LD-06 and
   LD-08 (real kernel code meets C1-03), `mutos_as`'s report of `-1`, longs in
   `fuzz_c.py`, the V30 forms and clocks in `x86sim.py`; every output unchanged. Next:
   F1.

With steps 0 to 9 done, the MUTOS kernel was built with the cross toolchain on
2026-11-07 - its original makefile unchanged, all 61 objects the real-hardware goldens,
the kernel the original build's byte for byte (above, "The MUTOS kernel, built with the
cross toolchain"; `docs/KERNEL_BUILD.md`).

Steps 6 to 9 are Milestone 6 (below), all four done; their originals are in
`mutos/mutos_bin/` since 2026-10-25, the order was proposed that day.

Smaller, not on the path: `tests/mutos_cc/fuzz/fuzz_c.py`'s generator still keeps
the limits it had for `c1_gen.c`'s sake (no constant divisor, no `x = y;`, simple
right operands, small frames); the ports refuse none of them, and
`tests/realbin/c1fuzz.py`'s generator covers those shapes against the original.

---

## Milestone 5 — Optimizer (`c2`) & NEC V30 (`-1`)

**Status: the optimizer is a port of the original `c2`, for the 8086** (since
2026-10-25, `docs/TOOLCHAIN_PLAN.md`, decision 7 and step 4); the 80186/V30 half was
decided on 2026-11-08 and is not begun (below). `src/mutos_cc/README.md` ("The port of the real `c2`") describes the
port; how it went: `docs/devlog/originals_and_ports.md`, "The port of `c2`".

### `mutos_c2` is the port of the real `c2` (verified 2026-10-25)

`mutos_c2` is the original MUTOS 1700 `c2` - v7's `c2` with `c21.c`'s machine half
rewritten for the 8086 and the MUTOS assembler (`op dst,src`, `*n.` constants, the
short branches), and new passes: register renaming (`pushopt()`, `renback()`,
`renfwd()`), `mov`/`push` and `push`/`pop` pairs, index modes (`indexopt()`),
`simplify()`, short branches (`shortbr()`) - every function read from the stripped
binary beside `v7/cc/c20.c` and `c21.c` (`tests/realbin/c2map.txt`), running on an
image of the original's data segment.

- **Sources** (`src/mutos_cc/c2port/`): `c2.h` (the image, the binary's addresses,
  the 16-byte node), `c20.c` (`c20.o`'s functions), `c21.c` (`c21.o`'s, the new passes
  among them), `c2lib.c` (the machine: the image's initial state with crt0's words and
  the arguments, `sbrk()`, libc.a's stdio on the image - FILE structures and buffers
  at the original's addresses -, the system calls on the host, the refusals),
  `c2data.c` (the binary's initialized data, generated by `tests/realbin/c2tables.py
  --c`), `main.c`. Zero warnings under `-std=c11 -Wall -Wextra -Wpedantic`.
- **Real-hardware goldens**: the nine `11_kernel` `.s` goldens give
  `tests/mutos_as/kernel_opt/`'s optimized kernel sources byte for byte - `delay.s`,
  `prim.s`, `mem.s`, `pipe.s`, `nami.s`, `fio.s`, `v24.s`, `tty.s`, `amx.s` -, by
  themselves and through `mutos_cc -O -S` with every pass ported
  (`tests/mutos_cc/check_driver.py`, in `make test`: 9/9).
- **Against the emulated original** (`tests/realbin/c2cmp.py`; the output, the
  messages, the exit status): `goldens`, every `.s` golden three ways - `c2 in out`,
  `c2 - in out` (the statistics), `c2 <in >out` -: 396/396 the same; `cases`,
  66 hand-written cases (the command lines, the input's form, the line buffer's end,
  long tokens and operands over the fixed buffers, many chunks, a large function, the
  heap's end, label numbers, every bug): 59 the same, 6 where the original never ends
  and the port refuses (C2-01, C2-07), 1 refused (a directory as the input).
- **Differential fuzzing** (`tests/realbin/c2fuzz.py`), no difference: the real
  compiler's output for random programs, through the ported chain - `c1fuzz.py`'s,
  1,000 with seed 2 (886 the same, 114 the chain does not compile) and 100 with seed 1
  (92 the same, 8), `fuzz_c.py`'s, 500 with seed 2 and 100 with seed 1 (all the same)
  -; assembly made for `c2`'s passes, 2,000 with seed 2 (1,997 the same, 3 where the
  original never ends: C2-07) and 100 with seed 1 (99 the same, 1 C2-07); damaged input
  (`--mutate 0.5`), 500 with seed 3 (382 the same, 49 refused - memory past the break,
  C2-03 -, 50 where the original never ends: 24 C2-01, 26 a lookup in the overwritten
  opcode hash table, C2-03; 19 not compiled) and 100 with seed 1 (81 the same, 12
  refused, 5 where the original never ends, 2 not compiled). `make check-realbin` runs
  the four with seed 1. An ASan/UBSan build of the port over every golden and 2,400
  generated cases (seeds 11 to 13, plain and damaged): no report.
- **The whole chain**: `tests/realbin/cccmp.py` with `mutos_c2` in place of the
  emulated original: `programs` - the construct corpus, `cc x.c` and `cc -O -o x x.c`,
  against the original chain - 124/124 the same `a.out` and messages; `goldens` -
  every pass ported - `cc -S` 132/132, `cc -P` 132/132, `cc -O -S` 9/9.
- **Refused on purpose** (exit status 2, a message ending in "refused", after the
  original's output up to there): memory past the break (reached through pointers a
  long operand has written over the opcode hash table, C2-03), a system call after
  crt0's far pointer was changed, a heap that would reach the stack, and the
  original's two endless loops where the port sees them (C2-01, C2-07).
- **The original's bugs**: C2-01 to C2-07 in `docs/MUTOS_ORIGINAL_BUGS.md`, each with a
  trigger run under the emulator - `dest()`'s endless loop on a destination equal to a
  constant or empty (C2-01), "mov/push converted to push" never counted (C2-02), the
  operand buffers filled without a check (C2-03), a `test` without a comma removed
  (C2-04), `lea` into memory clearing a counter's byte (C2-05), a jump to an undefined
  label turning `br` into `beq` (C2-06), an empty line after a label taken for the
  list's head (C2-07). None needs input that `c1` writes.

### What the binary does differently from v7 (read 2026-10-25)

`getline()` drops an empty line after a line; `jmp` is read as `br`, the short `j`,
`je` .. `jae` as `br` and the conditional branches, `loop` is a branch; `ret` ends a
block as `br` does, `.bss` stops the deletion of unreachable code; a redirected
branch takes its new target's code; `findrand()` searches the constants too and
returns the register's number; `setcc()` and `reducelit()` are gone (`ccloc` only
ever cleared); `toofar()` measures branches in bytes from the instructions' text; the
statistics have four new counters ("mov/push converted to push", "pop/push converted
to mov", "ors -> tests", "long branches to short") and no "sob's added". The node has
two more bytes, the registers read and written (`regmask()`).

**80186/V30 code generation** in `mutos_c1` and `mutos_c2` was decided on 2026-11-08
(`docs/TOOLCHAIN_PLAN.md`, decision 15 and section 7.4; no rule yet - step F0 made
`-1` reach `c1` and `c2`, with their output unchanged, and `x86sim.py` run the three
forms: "The corrected mode and `-1`" below): behind `-1`, the
name `mutos_as` has for the 80186/V30 instructions (section 4), three rules - a
constant pushed (`c2`), a shift by a constant count and a multiplication by a constant
(`c1`) -, and every assembly with `-1` reports the 80186/V30 instructions it produced
(decision 16). The target is the project owner's A7100 with a NEC V30 in place of its
8086. One design question was
settled ahead of it and stands: code built for the V30 must stay link-compatible
with the real, unmodified `libc.a`/`crt0.o` - no separate runtime is planned, so the
ABI-visible frame layout from Milestone 4's ABI research can never change for it.
Under that constraint, real NEC
µPD70116 (V30) timing data (`docs/V20_V30_Users_Manual_Oct86.pdf` - NEC calls
`ENTER`/`LEAVE` `PREPARE`/`DISPOSE`) shows the only layout-compatible use of
`PREPARE`/`DISPOSE` is an exact cycle-for-cycle tie against the discrete-instruction
prologue/epilogue, with a small code-size regression - so not those. `PUSHA`/`POPA`,
once listed as candidates, do not fit a compiled function's frame (they save eight
registers in their own order) and are left to hand-written assembler (2026-11-08). Full numbers and
derivation in `docs/devlog/m5_c2_v30.md`.

---

## Milestone 6 — `make`, `ar`, `ranlib` and the other MUTOS tools

**Status: steps 6 to 9 done - `mutos_ar` and `mutos_ranlib` (2026-10-26), `mutos_make`, `mutos_nm`,
`mutos_size`, `mutos_strip`, `mutos_tsort`, `mutos_ed` and `mutos_em` (2026-11-02) are ports of the
real `ar`, `ranlib`, `make`, `nm`, `size`, `strip`, `tsort`, `ed` and `em` (its line mode), and
`mutos_lorder` is the real shell script**
(`docs/TOOLCHAIN_PLAN.md`, decision 10, steps 6 to 9). **Decided 2026-10-25**: `make` is ported from its binary like every other tool - the
wrapper `mutos_make` around the host's `make`, planned on 2026-10-20, is dropped -, and
so are all the other binaries brought from the hardware that day: `ar`, `ranlib`,
`nm`, `size`, `strip`, `tsort`, the editors `ed` and `em`, and the shell script
`lorder`. The executables: `mutos_make`, `mutos_ar`, `mutos_ranlib`, `mutos_nm`,
`mutos_size`, `mutos_strip`, `mutos_tsort`, `mutos_lorder`, `mutos_ed`, `mutos_em`.

The material is in the tree since 2026-10-25 (decision 11): the binaries
(`mutos/mutos_bin/`, headers in its `README.md`), their manual pages, `make.1.d`
included (`mutos/mutos_man/`, German), the original headers (`mutos/mutos_include/`)
and v7's sources (`v7/make/`, `v7/ar/`, `v7/ranlib/`, `v7/nm/`, `v7/size/`,
`v7/strip/`, `v7/tsort/`, `v7/ed/`; their header copies are partly v7's, not MUTOS's;
`em` has no v7 source). Read so far (2026-10-25, from the binary's strings, not its
code): the original `make`'s built-in rules and macros are v7's `builtin[]`
(`v7/make/files.c`, `AS=as -`) string for string; `make.1.d` gives another default
suffix list than the binary has (v7's manual's). Since 2026-10-26 `ar` and `ranlib` run
under `tests/realbin/` (below), and since 2026-11-02 `make`, `nm`, `size`, `strip`, `tsort`, `ed`
and `em`. What the real `make` is known to do and not do on the hardware is in
`docs/devlog/m4_foundations.md`, "MUTOS 1700 host-tooling findings"; what the new
files showed in `docs/devlog/originals_and_ports.md`, "The originals of 2026-10-25".

### `mutos_ar` and `mutos_ranlib` are ports of the real `ar` and `ranlib` (verified 2026-10-26)

`mutos_ar` is the original MUTOS 1700 `ar` - v7's `ar.c` for the 8086 - and
`mutos_ranlib` the original `ranlib` - v7's `ranlib.c`, which is `nm`'s code with another
`main()` -, every function read from the stripped binary beside the v7 source
(`tests/realbin/armap.txt`, `ranlibmap.txt`; `ardis.py` and `ranlibdis.py` disassemble one
function by name). **None of the 33 functions of `ar.o` and the five of `ranlib.o`
differs from v7's in what it does**; what differs is the representation and the libc.a
underneath: the 26-byte archive header with its longs high word first, the 12-byte
`nlist`, 16-bit `int`s, signed `char`s, libc.a's `perror()` (41 messages), `mktemp()`,
`ctime()`, and stdio with twenty slots and its `isatty()` at the first output.

- **Sources**: `src/mutos_ar/` (`ar.h`, `ar.c` - the 33 functions in v7's order with the
  binary's text offsets -, `arlib.c` - what `ar` takes from libc.a and the MUTOS paths -,
  `ardata.c` - libc.a's `sys_errlist[]`, generated from the binary by `tests/realbin/
  artables.py --c` and checked by `--check-c` -, `README.md`) and `src/mutos_ranlib/`
  (`ranlib.c`, `README.md`). Each has a Makefile; the top-level `make all` builds both
  (`all: ld as cpp cc ar ranlib root`).
- **In the MUTOS root**: `bin/ar` and `bin/ranlib` as links into `src/`
  (`scripts/mutos_root.sh`, `tests/mutos_root/check_root.py`). `mutos_ranlib` runs
  `$MUTOS_ROOT/bin/ar` with `fork`/`execv` where the original goes through `/bin/sh -c`.
  The temporary files of `ar` are `/tmp/v*` under the root (`vXXXXX` in the current
  directory with the key `l`).
- **`libc.a`, rebuilt**: `tests/mutos_ar/check_libc.py` (in `make test`, no emulator)
  makes `libc.a` from its 167 objects with `mutos_ar rc` and `mutos_ranlib`: 72,178 bytes,
  the original archive byte for byte but for the three header fields of `__.SYMDEF` -
  its first member - that the tools write themselves: the date (the clock plus five
  seconds), the group (0, the original's 1) and the mode (0100644, the original's
  0100664); `t`, `x` and `p` list and extract its 167 objects as the original archive
  has them, and `ranlib` run twice, `ar d` and `ar rb` do what is expected of them.
  `ranlibcmp.py libc` does the rebuild with the emulated `ar` and `ranlib`: the same.
- **Against the emulated originals** (`tests/realbin/arcmp.py`, `ranlibcmp.py`; the
  messages, the exit status and both trees - names, bytes, modes - after every step, in two
  copies of a directory, `TZ=UTC`): `arcmp.py cases` 366 runs (one run is one `ar`
  command line) of 90 hand-written cases - every key and message, positions, `u`,
  `l`, names of every length, damaged and foreign archives, owners above 127, times to
  past 2038, files that change between commands -: 364 the same, 2 refused (an opened
  directory); `ranlibcmp.py cases` 146 runs of 57 cases - every message, every magic
  number, symbols of every type, cut tables, names of 14 characters, more than seventeen
  files that are no archives, the limits -: 139 the same, 6 refused, 1 where neither the
  original nor the port ends (a negative member size, RL-06).
- **Differential fuzzing** (`tests/realbin/arfuzz.py`, `ranlibfuzz.py`; random files and
  random objects, random commands, files that change between the steps), no difference:
  `ar`, 300 with seed 1 (all the same) and 1,500 with seed 2 (1,499 the same, 1 limits); damaged
  archives (`--mutate 0.12`), 300 with seed 1 (all the same) and 600 with seed 3 (598 the same, 2 limits);
  `ranlib`, 150 with seed 1 (all the same) and 400 with seed 2 (396 the same, 4 refused); damaged
  (`--mutate 0.15`), 150 with seed 1 (148 the same, 2 refused) and 400 with seed 3 (385 the same, 14 refused, 1 slow).
  `make check-realbin` runs the seed-1 runs.
- **Result classes** of the two comparators, besides *same*: *refused* (below), *orig-fails*
  (the original does not finish and the port refused), *endless* (neither finishes),
  **limits** (new: the port is stopped by the harness's 16 MB file-size limit - a damaged
  header that makes `ar` copy or print a member of a gigabyte -, by `SIGXFSZ` or by a short
  write that `ar` reports), **slow** (new: the original is stopped by the instruction budget
  on such a member while the port, on the host, finishes; what the original said by then is
  a prefix of the port's; one case re-run with a budget of three billion instructions gave
  the same output), and *DIFFERENT*. In both of the new classes what the original had
  written is a prefix of what the port wrote, or the other way round.
- **The emulator** (`tests/realbin/mutosemu.py`) learned what `ar` and `ranlib` need:
  `time()`/`ftime()`, the error number of a failed `write()` and `open()`, files without
  buffering, host file names as bytes, and a write at a file-size limit that is short
  (it returns the count; `EFBIG` only when nothing fits) - the fuzzing had found the
  last: a `write error: No such file or directory` where the original said `File too
  large` (10 of the first 600 damaged archives of seed 3, none after the fix). The next
  run found two more differences in the port, both now fixed and in the bug list: the
  `ENOTTY` the original's first output to a file or pipe leaves in `errno`, which
  `perror()` prints after a short write (`ar_out()`), and the 16-bit counter of short
  reads in `copyfil()` (AR-05).
- **Refused on purpose** (exit status 125, a last message line ending in "refused",
  `ar`'s temporary files removed first): `ar` - an opened directory (the original reads
  its entries as bytes), a file name past the host's limits, a time the host's calendar
  cannot give; `ranlib` - a directory as an argument, a seek to a negative offset, a symbol
  table that ends inside its first entry (the original uses its stack's old bytes), an
  `ar_name` with no 0 among its 42 bytes, a command line of 256 characters or more (the
  original smashes its frame, RL-04), a name the shell would take apart or expand, no
  `bin/ar` in the root or one that refused.
- **The originals' bugs**: AR-01 to AR-05 and RL-01 to RL-07 in
  `docs/MUTOS_ORIGINAL_BUGS.md`, each with a trigger run under the emulator - a name of
  more than 14 characters never matches (AR-01), a member cut short copied whole with the
  old bytes (AR-02), owner and group as signed chars (AR-03), a key given 256 times
  counted as not given (AR-04), 65536 short reads counted as none (AR-05); a 14-character name that runs on through the header
  (RL-01), a file that is no archive keeping its stdio slot (RL-02), headers that are not
  reset (RL-03), `buf[256]` for `ar`'s command line (RL-04), `ar` run through the shell
  (RL-05), a negative member size that sends the scan round for ever (RL-06), `fixdate()`
  dating the first member whatever it is (RL-07).
- **Not done**: `ar`'s and `ranlib`'s manual pages (the manual pages come at the end,
  Workflow Guideline 10).

### `mutos_make` is a port of the real `make` (verified 2026-11-02)

`mutos_make` is the original MUTOS 1700 `make` - Unix V7's `make` for the 8086 -, every
function read from the stripped binary (a 0411 a.out: text 16192, data 3288, bss 6430
bytes) beside the v7 source (`tests/realbin/makemap.txt`; `makedis.py` disassembles one
function by name). **49 of the 50 functions of `make.o` are v7's**: v7's `main.c`,
`doname.c`, `misc.c`, `files.c`, `dosys.c` and `gram.c`, compiled with this project's own
`cc -O -Dunix`, give the original's bytes, with three differences that are the headers' -
`METERFILE` is not defined (no `meter()`), MUTOS's `struct nlist` has `char n_type` where
v7's has an `int`, and `NLEFTS`, the number of names in one rule, is 300 where v7's `defs`
says 40. The fiftieth, `yyparse()`, is yacc's skeleton (v7's `yaccpar` without `yydebug`)
on eight tables; they and `builtin[]`, the built-in rules and macros (string for string
v7's `files.c`), are generated from the binary into `makedata.c` by `tests/realbin/
maketables.py --c`, and `--check-c` keeps the file current. What differs from a build of
v7's code for the host is the representation and the environment, which the port makes
explicit: 16-bit `int`s (`status >> 8` of a command that exits 200 is -56: MK-10), signed
`char`s, 32-bit times, libc.a's stdio (512-byte blocks, `printf("%s", 0)` prints `(null)`),
a directory read as MUTOS gives it (`.` and `..`, then the names in byte order, those of more
than 14 characters left out), an empty path name that is the current directory, and the
heap and the frames of the recursive functions counted in the original's sizes against the
64K segment.

- **Sources**: `src/mutos_make/` (`make.h`; `main.c`, `doname.c`, `misc.c`, `files.c`,
  `dosys.c`, `gram.c` - v7's functions with the binary's text offsets in their comments -;
  `mklib.c`, the libc/segment model; `makedata.c`, generated; `README.md`). Its Makefile is
  called by the top-level `make all` (`all: ld as cpp cc ar ranlib make root`).
- **In the MUTOS root**: `bin/make` as a link into `src/` (`scripts/mutos_root.sh`,
  `tests/mutos_root/check_root.py`). The commands of a makefile run through the host's
  `/bin/sh -c` (`-ce` without `-i`) when they have a shell character, and through `execvp()`
  otherwise, with `$MUTOS_ROOT/bin` first in the `PATH`: `cc`, `as`, `ld`, `ar`, `ranlib`
  and `make` in a recipe are the MUTOS tools of the root. **An absolute path in a recipe**
  (`/lib/c0`, `/lib/cpp` in the corpus's makefiles) **is run by the host and cannot be
  redirected by any `make`**: such a makefile takes it as a macro
  (`make CC=$MUTOS_ROOT/bin/cc CPP=$MUTOS_ROOT/lib/cpp C0=$MUTOS_ROOT/lib/c0`,
  `docs/TOOLCHAIN_PLAN.md`, decision 10).
- **The corpus through `mutos_make`** (`tests/mutos_make/check_corpus.py`, in `make test`, no
  emulator, a second): the thirteen `tests/mutos_cc/*/Makefile.mutos`, written for the real
  `make` on the hardware, run in scratch copies with the macros above and `-i` - and make
  131 `.s`, 132 `.i`, 132 `.1` and 132 `.2` that are the real-hardware goldens byte for byte
  (the `.s` that `mutos_c1` refuses, `fltprobe/p19_open3`, by the golden's first lines, as
  `invalid_goldens.txt` says); a second run does nothing in all thirteen, and `clean` leaves
  each directory as it was. The same makefiles also run under the comparator, below.
- **Against the emulated original** (`tests/realbin/makecmp.py`; `fork`, `exece` and `wait`
  are faked and the commands it runs are the host's, the same for both, with fake `cc`, `as`,
  `ld`, `yacc`, `lex`, `cpp` and `c0` in a root's `bin/` that say what they were asked and
  make the file they were asked for; after every step the messages, the exit status and both
  directory trees - names, bytes, modes -, in two copies of a directory, `TZ=UTC`):
  `makecmp.py cases` 457 runs (one run is one `make` command line) of 307 hand-written
  sequences - the description file and `-f`, every flag, rules and dependencies, `::`, macros
  in every form, the line syntax (comments, continuation, tabs, CR LF, long lines, NUL and
  high bytes), every syntax error, the prefixes of commands, exit statuses and signals,
  `.DEFAULT` and the other special targets, the built-in rules for every suffix, wildcards,
  archives of every kind (damaged ones, entry points `lib((sym))`, a directory in the
  name) -: 430 the same, 25 refused, 2 where the original does not finish (a command that
  expands to more than `string[2500]` ends at address 0 without an exit: MK-06); `makecmp.py corpus` 312 runs - the thirteen makefiles under three
  timestamp conditions and eight command lines each (`-n`, a run, a second run, `-q`, `-pn`,
  `-t`, `clean`, `-n clean`) -: all the same.
- **Differential fuzzing** (`tests/realbin/makefuzz.py`; random makefiles with macros, suffix
  rules, explicit rules, wildcards, archive members and every flag, command lines and files
  that change between the steps; with `--mutate` the makefile also damaged), no difference:
  plain, 1,500 cases with seed 11 (1,377 the same, 123 refused) and 600 with seed 13 and six
  steps each (528 the same, 72 refused); damaged (`--mutate 0.5`), 1,500 with seed 12 (1,363
  the same, 137 refused). `make check-realbin` runs the seed-1 runs (300 cases, plain and
  damaged).
- **Result classes** of the comparator, besides *same*: *refused* (below), *orig-fails*,
  *endless*, *limits*, *DIFFERENT* and *BAD-REFUSAL* (the port refused, but wrote what the
  original did not). The first runs found three differences, all in the port and fixed: a
  null `%s` that the original prints `(null)` (the macro `@` between two commands, in `-p` and
  `-d`), the empty path name, and the 301st name of a rule, which the original stores over
  `nlefts` and survives by luck (MK-03); and one fault of the judge itself (an empty
  output is no line). The longest fuzz run then showed two false BAD-REFUSALs that were the
  comparator's: `-d` prints `TIME(name)=<clock>` for a target a step has made, the two runs are
  some milliseconds apart, and when the second ticked between them the lines differed - a time
  within an hour of the run now counts as `<now>`.
- **Refused on purpose** (exit status 125, a last message line ending in "refused"; what was
  written before is the original's, in order): `$?` read when it holds what `mkqlist()` returns
  for an empty chain (MK-01), a `$` as the last character of a string (MK-02), `$*` after the
  `doname()` that set it has returned (MK-05), a command or a name that would run over one of
  the original's stack buffers (MK-06), a byte of 0x80 or more where the original indexes
  `funny[]` or the ctype table, a path component of more than 14 characters (MK-12), a
  directory as the description file, an archive of one byte (MK-09), a member size that takes
  the position below zero (MK-08), the 14 characters of a member name with no end (MK-07),
  more than 300 names in a rule while the heap is above 32K, the stack and the heap together
  above the segment model's limit, and a `fork()` that fails.
- **The originals' bugs**: MK-01 to MK-13 in `docs/MUTOS_ORIGINAL_BUGS.md` - `$?` of a command
  with nothing newer (MK-01), a `$` at the end of a string (MK-02), the 301st name of a rule
  (MK-03), `-d` with a wildcard (MK-04), `$*` read after its frame has gone (MK-05), buffers
  of the stack without checks (MK-06), a 14-character member name (MK-07), a negative archive
  position (MK-08), an archive of one byte (MK-09), `status >> 8` on a signed int (MK-10), a
  command that is blank after its prefixes (MK-11), a path component of more than 14
  characters (MK-12), and the "out of memory" the hardware reported for a 19 KB makefile
  (MK-13). The port reproduces MK-03, MK-10, MK-11 and the cases of MK-01 whose value is
  defined.
- **Not verified**: the limit of the segment (`SEG_LIMIT` 0xF000) is a model - the hardware
  ran out of memory for the 124-target makefile that `tests/mutos_cc` had before it was split
  per category (MK-13) while the emulated original and the port run it; the order of a
  directory (the port sorts it), the policy for names of more than 14 characters and the
  empty path name are V7's, not measured on the hardware; the signal handlers (`intrupt()`,
  `enbint()`) are read, not compared, and a `fork()` that fails cannot be tested. A byte of
  0x80 or more is refused, though the first fifty of the 128 bytes before `funny[]` are the
  tail of libc's ctype table, which could be modelled.
- **Not done**: `make`'s manual page (the manual pages come at the end, Workflow
  Guideline 10).

### `mutos_nm`, `mutos_size`, `mutos_strip`, `mutos_tsort` and `mutos_lorder` (verified 2026-11-02)

`mutos_nm`, `mutos_size`, `mutos_strip` and `mutos_tsort` are the original MUTOS 1700 `nm`,
`size`, `strip` and `tsort` - Unix V7's programs for the 8086 -, every function read from the
stripped binary beside the v7 source (`tests/realbin/nmmap.txt`, `sizemap.txt`, `stripmap.txt`,
`tsortmap.txt`; `nmdis.py`, `sizedis.py`, `stripdis.py` and `tsortdis.py` disassemble one
function by name): `size` has one function, `strip` three, `nm` three (and libc.a's `qsort()`
under it), `tsort` eight (and `fscanf`) - **none differs from v7's in what it does**. What the
port makes explicit is the representation and the libc.a underneath: 16-bit `int`s, signed
`char`s, the 16-byte a.out header, the 12-byte `struct nlist` with its word `n_type`, the
26-byte archive header, longs with the high word first, and **libc.a's stdio, which the four share
in one header, `src/h/mutos_stdio.h`** - standard output in 512-byte blocks, standard error with
no buffer, `_IOERR` set by a short `write()` and never cleared, `printf()` returning
`ferror(stdout) ? -1 : 0`, and `main()` without `exit()` handing crt0 what is left in AX
(the exit status of `size nothere` is 0, that of `tsort < /dev/null` 255: SZ-02, TS-03).
`mutos_lorder` is the shell script of the original, which is not ported but run (the host's
`sh`, `sed`, `sort` and `join` in the C locale, with `$MUTOS_ROOT/bin/nm`); the three changes to
its text are in `src/mutos_lorder/README.md`.

- **Sources**: `src/mutos_{size,strip,nm,tsort}/` (the one C file of each, the offsets of the
  binary in the comments; a `Makefile` and a `README.md` each), `src/mutos_lorder/`
  (`mutos_lorder.in`, built into `mutos_lorder` by its `Makefile`), `src/h/mutos_stdio.h`. The top-level
  `make all` builds them (`all: ld as cpp cc ar ranlib make nm size strip tsort lorder root`),
  zero warnings under `-std=c11 -Wall -Wextra -Wpedantic`.
- **In the MUTOS root**: `bin/nm`, `bin/size`, `bin/strip`, `bin/tsort` and `bin/lorder` as links into
  `src/` (`scripts/mutos_root.sh`, `tests/mutos_root/check_root.py`). `lorder` finds `nm` there.
- **Against the emulated originals** (`tests/realbin/utilcmp.py`; a case is a directory and a few
  steps, run in two copies, each with a MUTOS root of its own, the messages, the exit status and
  both trees - names, bytes, modes - compared after every step; `nm` and `tsort` also with both output
  streams on one pipe, for the order of their messages; `size` and `tsort` also with standard output
  on `/dev/full`): `utilcmp.py cases` **1,118 cases, 3,872 runs: 3,729 the same, 125 refused, 18 where
  the original does not finish, no difference** - `size` 294 cases (313 the same, 3 refused), `strip`
  311 (636, 1), `nm` 410 (2,504, 30), `tsort` 89 (226 the same, 90 refused, 18 where the original
  ends in an invalid instruction), `lorder` 14 (50, 1). Every object and archive of the tree is in
  them - the 76 `.o` goldens, `crt0.o`, `libc.a` and its 167 objects, executables linked from them
  - each with every option (`nm -gnopru` and combinations, an invalid one, one file and several, archives
  and objects mixed; `strip` of objects, executables and stripped files; `size` of every magic number),
  besides headers of every shape, cut and damaged files, archives of odd sizes, full disks,
  directories, graphs of every kind for `tsort` (DAGs, cycles, self-loops, duplicates, odd data,
  items of up to 200 characters, separators of every kind) and `lorder` on `libc.a`'s objects; chains of
  1900, 1974 and 1975 pairs (the port's limit) and of 2010 (passes) and 2011 (the original: "too many
  items") for `tsort`.
- **Differential fuzzing** (`tests/realbin/utilfuzz.py`; objects and archives the fuzzer makes - symbols
  of every type, names of 1 to 14 characters, bytes of 0200 and more, equal names and values, the four
  magic numbers and some that are none, cut tables -, command lines with random options, graphs for
  `tsort`, `lorder` into `tsort`; with `--mutate` the files are also cut or changed between the
  runs; one case runs the five tools in turn): no difference. Seed 1, 300 cases plain and 300 with `--mutate
  0.3` (`make check-realbin`); seed 2, 1,500 cases plain (1,382 the same, 117 refused, 1 where the
  original does not finish); seed 3, 1,500 cases with `--mutate 0.3` (1,411 the same, 89 refused).
  The first fuzz run found one fault of the judge (the process number `join` prints into a message
  is normalised); the comparison found one of the port, fixed: the heap limit of `tsort`
  (`TS_HEAP` was 0xA000 and refused a chain of 1500 pairs which the original sorts; **measured on the
  original: 2010 pairs pass, 2011 end "too many items"**, so about 56K of blocks fit; the port
  stops 1K short at 0xD800).
- **Without an emulator** (`tests/mutos_binutils/check_tools.py`, in `make test`, 30 checks, no
  difference): the a.out headers and symbol tables of `crt0.o`, the 167 objects of `libc.a` and
  a program the ported `cc` and `ld` make are read independently of `nm` and `size`, and what they print
  - the sizes, the type letters, `-g`, `-u`, `-n`, `-r`, `-o`, `-p` - is compared with it; the
  `__.SYMDEF` of `libc.a` with the table `nm -g` gives; a program after `strip` (no symbols,
  the flag set, the file as long as header, text and data; "already stripped" the second time);
  `tsort` on a chain, a diamond, a cycle, odd data, a missing file and a file argument;
  `lorder` of one object (the pair twice), of `crt0.o` (no name list) and of `libc.a` - every pair
  derived from the symbol tables - and `lorder libc.a | tsort`, which names each of the 167 objects once.
- **Refused on purpose** (exit status 125, a last message line ending in "refused"; what was written
  before is the original's, in order): a directory named as a file (all four: the original reads its entries),
  `nm`'s eighteenth file left open (NM-04), a negative member size that sends the original's
  `fseek()` below zero (NM-06), a 14-character member name with no 0 in the 42 bytes behind it
  (NM-03), more than `NM_MAXSYM` = 2000 symbols in one object (NM-08), `tsort`'s item of 54
  characters or more in the first place or 104 in the second (TS-01), a byte of 0200 or more
  in `tsort`'s input (TS-02), more graph than 54K of blocks (TS-04, TS-05); `lorder` passes `nm`'s refusal on.
- **The originals' bugs**: 22 in `docs/MUTOS_ORIGINAL_BUGS.md` - `size`: the header buffer is not reset (SZ-01),
  no `exit()` (SZ-02); `strip`: the header is not reset (ST-01), the file is truncated before the copy is
  written (ST-02), the temporary name from 15 bits of the process number (ST-03), an object's
  relocation is dropped (ST-04); `nm`: unaligned columns (NM-01), `arch_flg` never reset
  (NM-02), the 14-character member name (NM-03), the unclosed file (NM-04), `arp`/`exp`/`sym` never
  reset (NM-05), a negative member size (NM-06), signed name comparison (NM-07), the heap
  (NM-08); `tsort`: the 50-character item (TS-01), `__sctab` (TS-02), no `exit()` (TS-03), the
  unchecked `malloc()` (TS-04), the 64K (TS-05); `lorder`: only `T` and `D` are definitions (LO-01),
  working files in the current directory (LO-02), names must end in `.o` and `$*` is unquoted
  (LO-03). The port reproduces them all but NM-04, NM-08, TS-02, TS-04 and TS-05, which it refuses,
  and the parts of NM-03, NM-06 and TS-01 named above.
- **Not verified**: the limits of the 64K segment are models - `tsort`'s 2010 pairs are measured on
  the emulated original, but a failing `malloc()` of a predecessor list is silent (TS-04), so the
  limit can only be read from `index()`'s message, and `NM_MAXSYM` = 2000 is a safe bound, not a
  measured one; none of this was run on the hardware; `strip`'s temporary name takes the host's process
  number, as `mktemp()` takes the original's; `lorder`'s `sed`, `sort` and `join` are the host's,
  not MUTOS's (the C locale makes them agree in every case measured); the stdio model is
  libc.a's as read for these four programs, nothing more; `tsort`'s `findloop()` errors 1 and 2
  are read, not reached.
- **Not done**: the manual pages of the five tools (they come at the end, Workflow Guideline 10).

### `mutos_ed` and `mutos_em` are ports of the real `ed` and `em` (verified 2026-11-02)

`mutos_ed` is the original MUTOS 1700 `ed`: **v7's `ed.c` with two changes**, which this
project's own `cc -O -s` (the ported chain) compiles and links with `libc.a` to the original
binary **byte for byte** - 11,522 bytes of text, 234 of data, 4,886 of bss. The two changes,
found by diffing the rebuild against the binary instruction by instruction: `j` with one address
joins it and the next line (v7: nothing), and `m` to the line before the first one moved, or to the
first one itself, does nothing. `tests/realbin/edmap.txt` is the rebuild's symbol table, `eddis.py`
disassembles a function by name. The port keeps what a caller can tell of the 8086: 16-bit numbers
and addresses that wrap around the segment before they are checked (`1+65536p` is line 1), the
line table `malloc()`ed at 0x4104 (measured) and grown in place, the bss arrays `savedfile`,
`file`, `linebuf`, `rhsbuf` and `expbuf` one behind the other (a long file name runs on through
them), signed chars, the input read a byte at a time, the seek to the end of the standard input
after an error (a script from a file ends at its first error; through a pipe it goes on), the
output a line at a time, `/tmp/eXXXXX` under the root. `!` runs the host's `/bin/sh` with the
root's `bin/` first in the `PATH`.

`mutos_em` is the original `em`'s **line mode**. `em` has no source: matching the original `ed`'s
functions against its text found **33 of ed's functions unchanged and ten changed**; the rest -
`main()`, `commands()`, the input reader, the session log, `ask()`, `confirm()`, `mark()`, the help,
the screen mode - is em's own (`tests/realbin/emmap.txt`, `emdis.py`). What it adds to ed: "Editor"
and the prompt ">" (`-`, `-s` silent; `<`, `>` switch), `-e` (only the named file), the session log
`<file>.elog` and its replay `-r` (only with a terminal), 8-bit text, "sure? " for `q`/`e` with
changes, `s///n` (the count), `z` (`s` asking at each match), `"`, `&`, `%` (screenfuls), `|cmd`, `!`
with `%` and `!!`, `h` (help files of `/usr/lib/emhelp/`), `b+`/`b-`. **The screen mode `o` is
refused.**

- **Sources**: `src/mutos_ed/ed.c`, `src/mutos_em/em.c` (the offsets of the binaries in the
  comments; a `Makefile` and a `README.md` each). The top-level `make all` builds them (`all: ld as cpp
  cc ar ranlib make nm size strip tsort lorder ed em root`), zero warnings under `-std=c11 -Wall
  -Wextra -Wpedantic`.
- **In the MUTOS root**: `bin/ed` and `bin/em` as links into `src/` (`scripts/mutos_root.sh`,
  `tests/mutos_root/check_root.py`), where the kernel's makefile finds them through `mutos_make`'s
  `PATH`.
- **The MUTOS kernel's makefile** (`tests/mutos_ed/kernel/conf.Makefile` and `fakeawk`, as the
  project owner gave them; `tests/mutos_ed/check_editors.py`, in `make test`, no emulator, 212 checks,
  no difference): run through `mutos_make` in a scratch kernel tree, `make 7150`, `make 1834`, `make
  7100` edit the configuration blocks and the `mutos:` lines as an independent reading of the eddep
  script gives them (and `make 7100` gives the original file back byte for byte); all 34 option targets
  (`AMX`, `noAMX`, .. `V30IDE`, `noV30IDE`) change `DRIVER`, `CONFIG` or `FLAGS` as their scripts say and
  touch the sources that test the option; `make depend` writes the dependencies of every source behind
  the marker, twice the same; **`make ../mdep/mch_M7100.o` runs `cc -P` (the configuration's
  `-DM7100 -DASK -DIFSS -DV24 -DV30IDE`, those the kernel's goldens were made with), `em mch.i
  <fakeawk` and `cc -c`: `mch.i` is `tests/mutos_cpp/c/mch.i.golden`, `em` makes of it
  `tests/mutos_as/kernel_opt/mch.s`, and the object's text, data and relocation are
  `kernel_opt/mch.o.golden`'s** - real hardware all three; its symbols are the golden's without the
  five local `L` symbols (`cc` runs `as -gC`, the golden was assembled without `-C`). The editors'
  shell commands are checked there too (`!` of both, em's `%`, `!!`, `|`).
- **Against the emulated originals** (`tests/realbin/edcmp.py`; a case is a directory, a command line
  and the script on the standard input - a file, and again through a pipe, which the seek after an
  error cannot skip -, run in two copies with a MUTOS root each; the standard output and error, the exit
  status and both trees compared): `edcmp.py cases` **244 cases: 225 the same, 7 refused, 12 where the
  original loops for ever and the port refuses, no difference** - ed and em on every command, the error
  paths, the 16-bit addresses, overrunning names, long lines, NUL and 8-bit bytes, a full temporary file
  ("?TMP"), 4,000 lines; the kernel makefile's eddep scripts on it (18, and three of them in one run);
  em's options, "sure?", `z`, `"`, `&`, `%`, `<`, `>`, `b`, `h`, `s///n`, a byte 0377, replayed logs,
  and `fakeawk` on the real `mch.i`.
- **Differential fuzzing** (`tests/realbin/edfuzz.py`: random files - tabs, control characters, for em
  8-bit bytes - and random scripts of every command, address form, regular expression and replacement,
  most through a pipe): no difference. Seed 1, 400 cases each for ed and em (`make check-realbin`: ed
  400 the same; em 380 the same, 20 where em asks "sure?" for ever at the end of the script); seeds 1000
  to 3999, 3,000 cases (2,919 the same, 1 refused, 80 where em asks "sure?" for ever).
- **Refused on purpose** (exit status 125, a last message line ending in "refused"; what was written
  before is the original's): em's screen mode `o`; a write or read past the bss arrays (ED-01, ED-02,
  ED-06, EM-02 .. EM-04); a line table above 0xF000 (where the break fails is not measured); where the
  originals loop for ever (ED-05, EM-05, EM-06) or read the stack's garbage (EM-05); the encryption
  (`-x`, an `e` after `x`: ED-03) - `x` itself fails as the original's does without
  `/usr/lib/makekey` (ED-04).
- **The originals' bugs**: 21 in `docs/MUTOS_ORIGINAL_BUGS.md` - `ed`: the file name without a bound
  (ED-01, ED-02), the encryption's errors (ED-03, ED-04), the empty group repeated (ED-05), a NUL in a
  pattern (ED-06), the last line without a newline counted (ED-07), the address that wraps (ED-08);
  `em` (ed's bugs, and): `!` alone takes the next line (EM-01), the shell command, `|` and `h` without a
  bound (EM-02 .. EM-04), "sure?" reads the script (EM-05), `z` reads the standard error (EM-06), a byte
  0377 ends the input (EM-07), 8-bit replacement text loses its top bit (EM-08), the byte after a
  replayed log (EM-09), the line break that does nothing (EM-10), the log's first line number (EM-11),
  `<` and `>` (EM-12), `|`'s signals (EM-13).
- **Not verified**: what needs `fork()`, `pipe()` or a terminal - `!`, `|`, the prompt's 128-byte
  reads from a terminal, the session log and its replay from a terminal, the lines and columns of a
  terminal (the port takes the host's window size for MUTOS's `sgttyb` bytes 10 and 11), `z` with a
  readable standard error, `-x` - is checked by reading the binaries (and `!`, `|` in
  `check_editors.py` against what the reading says; em's prompt, log, "sure?" and replay through a
  pseudo-terminal, by hand), not against the originals; where the kernel
  refuses the break for a big line table is not known (the port refuses above 0xF000); nothing was run
  on the hardware.
- **Not done**: em's screen mode (`o`, an ANSI terminal with the A7100's function keys - refused); the
  manual pages of the two tools (at the end, Workflow Guideline 10).

---

## The corrected mode and `-1` (`docs/TOOLCHAIN_PLAN.md`, section 7)

Decided by the project owner on 2026-11-08: the bugs of the originals are corrected
in a corrected mode that becomes the default at the end (decision 13), and `c1`/`c2`
write 80186/V30 code behind `-1` (decision 15), each change a rule of its own, listed
in [`docs/RULES.md`](docs/RULES.md).

### Step F0: the shared infrastructure (verified 2026-11-08)

Nothing any tool writes changed; what F1 and the rules after it need is in place.

- **The switches** (names confirmed by the project owner on 2026-11-08): `-F` the
  corrected mode, `-H` the original's behaviour, to `cc`, `as` and `ld` (the last one
  counts; `ld` finds them wherever they stand, `ldrules/ldrules.c`); without them
  `MUTOS_MODE=corrected|original` (unset or empty: the default, the original's until
  F5; any other value: exit status 1). `cc` puts the mode a switch chose into the
  environment of its passes - `c0` takes any fourth argument for `-p`, so no pass gets
  a new argument for it - and takes `-1` itself, which the original hands to `ld`: `c1`
  gets an argument `-1` after its others (the original reads its characters and ignores
  both), `c2` a first argument `-1` (taken off before the original's `main()`), `as`
  `-1` after `-gC`. None of it is in `cc`'s image: a command line without them does what
  the original does (`tests/realbin/ccfuzz.py` no longer counts `1`, `F`, `H` among the
  letters the original hands to `ld`). `src/h/mutos_mode.h` is the shared code.
- **Rules and the register**: a rule is a file of its own in its tool's rules
  directory (`src/mutos_cc/c1rules/`, `src/mutos_ld/ldrules/`), one hook in the ported
  code and a row of `docs/RULES.md`; `scripts/check_rules.py` (in `make test` and `make
  check-docs`) checks the three against each other and against the bug's entry.
- **The rule log** (`MUTOS_RULELOG=<file>`, a developer aid): a line per hook passed -
  `c1 C1-03 met src=09_amx.c line=1344 out=41935-42035 op=++` -, `out=` the bytes of
  `c1`'s output that came from the statement; `cc` names the source
  (`MUTOS_RULELOG_SRC`) when the log is on. `scripts/rulelog_summary.py` sums a log up
  by rule - for the whole kernel: `MUTOS_RULELOG=/tmp/rules.log mutos_root/bin/make` in
  its `conf/`.
- **The accounting** (`tests/rules/check_accounting.py`, in `make test`): the 62
  programs of the construct corpus, the 61 floating-point probes, the kernel sources
  of the tree (the nine of `11_kernel/`, the five of `tests/mutos_cpp/c/`) and 40 programs of
  `fuzz_c.py` (half with longs) through `cc -S`, `cc -O -S`, `cc -O -c` and `cc -o`, each
  without a switch and with `-H`, `-F`, `-1` and `-F -1`, the rule log on: every byte
  that differs must lie in an `applied` rule's range - with no active rule, every
  output is the same in all five (177 sources, 694 stages); `ld`'s two cases of LD-06
  and LD-08 the same in every mode. Checked against a planted difference (a line
  appended under `-F`: six failures).
- **Diagnostic hooks** at the bugs F1 and F2 correct (`docs/RULES.md`, state
  "diagnostic": they report "met", the code is the original's): C1-01, C1-02, C1-03,
  C1-06, LD-06, LD-08. What the code meets (`check_accounting.py --report`, and
  `--fuzz 3000 --base-only` for the fuzz programs, seed 1): **the kernel source
  `11_kernel/09_amx.c` meets C1-03 twice** (`setbyte(ds,(int)temp++,...)`, `x =
  getbyte(ds,(int)temp++)`: the local long's increment is done on its low word); the
  construct corpus, the other kernel sources and the probes meet nothing else but
  C1-03 (`p31_long4`, `p34_long5`) and C1-06 (`p38_long6`, `p42_long7`), as the
  probes were written to; in the 3,000 fuzz programs C1-01 69 times (all `~`), C1-02
  once, C1-03 59 times, C1-06 237 times. `ld`'s two cases meet LD-06 and LD-08. The
  whole kernel (62 sources) is the project owner's to run (above). Reading the hooks
  found more: C1-03's mechanism is a node shared by `delay()`'s tree and the increment
  (the entry said `unoptim()` distributed the conversion), and C1-01 has a third form,
  a shift by a variable count (`sal di,cl` with a count of 0 leaves the flags).
- **`mutos_as -1` reports** (decision 16): one line on the standard error per
  assembly, `as: -1: 12 80186/V30 instructions: push 7, shift 3, imul 2` or `as: -1:
  no 80186/V30 instructions`, counted by opcode from the bytes written; also under
  `-W`; `cc -S -1` assembles nothing and reports nothing. The seven
  `v30_speculative/` sources' lines are pinned (`v30_with_1.report`, in
  `check_switches.sh`), their counts checked against `objdump -m i8086`.
- **`fuzz_c.py --longs`**: `long` variables `p`, `q` and `r` (`r` only through
  `++`/`--`), long constants around the 16-bit borders, `+ - * / % & | ^`, shifts by a
  constant and, as statements, `p op= e`, `p <<= i` (a count of 0 when `i` is), `x =
  r++`, conversions and comparisons in int expressions, under C's 32-bit semantics;
  `x86sim.py` reads a long local as two words, the high one first. Without `--longs`
  the generator gives the programs it gave (checked on 1,200 programs; `c0fuzz.py`,
  `c1fuzz.py`, `c2fuzz.py` use it so). Against the emulated original c0 and c1, 150
  programs (seed 1): 138 correct, 12 "as the real compiler", no difference; without
  the oracle, 1,500 (seed 3): every wrong or failing program meets one of the hooked
  bugs or C1-13. **Two new bugs of the original `c1`** (`docs/MUTOS_ORIGINAL_BUGS.md`):
  C1-12, a relation of a long constant cast to int (`(int)100L <= 13`) reported as
  "C error: const" and replaced by its left constant, and C1-13, a relation of two
  long constants compiled to `cmp` of two immediates, which `as` rejects.
- **`x86sim.py`**: the three V30 forms - `push` of a constant, a shift by a constant
  (`shr` too), `imul` by a constant with two or three operands - run with `v30=True`
  only, with the NEC manual's flags (shift: CY the last bit out, V undefined; `imul`:
  CY = V = the product past 16 bits, S and Z undefined, DX untouched); every run
  counts its V30 clocks (`Result.clocks`, lowest and highest; the manual's uPD70116
  figures at even addresses; a runtime call counted as the call). The 132 `.s`
  goldens give what they gave. `tests/mutos_cc/fuzz/check_x86sim_v30.py` (in `make
  test`) pins twelve programs' values and clocks, three undefined flags, and that
  `mutos_as -1` takes their syntax.
- **Verified** on a fresh clone of `5d40048` with the patch: `make clean && make all`
  without a warning under `-std=c11 -Wall -Wextra -Wpedantic`; `make test` exit 0,
  its output the same as before but for the new lines (the `-1` report in
  `check_switches.sh`, `check_rules.py`, `check_accounting.py`,
  `check_x86sim_v30.py`); `make check-docs` clean. `make check-realbin` did not run
  whole on the machine of the session (two cores); the parts of the tools F0 changed
  ran against the final tree, all with exit status 0: `cctables`, `cccmp.py cases`
  (177 runs: 175 the same, 2 refused), `run` and `programs` (124/124 each),
  `goldens` (132/132, 132/132, 9/9), `ccfuzz.py` (200: 197 the same, 3 refused);
  `ldtables`, `ldcmp.py cases` (91: 89, 2 refused) and `goldens` (1,572/1,572),
  `ldfuzz.py` plain and `--mutate 0.5` (200 each: 195 the same, 5 refused),
  `check_rflag.py` (248/248); `c2tables`, `c2cmp.py goldens` (396/396) and `cases`
  (66: 59 the same, 6 the original fails, 1 refused), `c2fuzz.py` with the four
  generators (as before); `ascmp.py goldens` and `cases` against their pins,
  `asfuzz.py --gen cc --strict` (300: 251 the same object of the 251 the original
  accepts); `check_root.py --realbin`; `makecmp.py corpus` (312/312). The checks of
  `c0`, `c1` and `cpp` (`check_real.py`, `c1trees.py`, `c1fuzz.py`, `c0cmp.py`,
  `c0fuzz.py`, `cppcmp.py`, `cppfuzz.py`) passed against a tree of the session from
  before the last changes, which touched `c0` and `c1` only where the rule log is on.
  A whole `make check-realbin` is the project owner's to run.

### Step F1: the corrections of `c1` (verified 2026-11-08)

The `c1` bugs that make wrong code silently, corrected in the corrected mode (`-F`,
`MUTOS_MODE=corrected`), each a rule of `docs/RULES.md` delivered as a patch of its
own; without a switch every output is still the original's (`check_accounting.py`).

- **C1-03** (`x = l++` loses the carry), active: the increment of a long gets a copy
  of its name node before `delay()` compiles the tree, so that `unoptim()`'s change of
  the node left in the tree (the low word, for `(int)l`) is not done to the increment:
  `mov di,2.+_l` / `add 2.+_l,*1.` / `adc _l,*0` where the original has `inc 2.+_l`.
  The hook moved before the compilation; the rule log tells, once the statement is
  compiled, whether the node in the tree stopped being a long ("met" in the original
  mode, "applied" in the corrected one - `c1_rule()` hands a rule's check the pointer
  it named, `c1rules.h`). In the tree's real code it changes the kernel's `amx.o` at
  its two `(int)temp++` (lines 1344 and 1427 of `11_kernel/09_amx.c`); the corpus's
  probes `p31_long4` and `p34_long5` compile as before (their `l++`'s value is never
  converted to an int).
- **C1-06** (`l <<= i` on a local long jumps into an instruction for `i == 0`),
  active: the twelve rows with `jz .+16` skip the loop only - `jz .+8`, the
  original's own form for the value of `l << i` -, and with a count of 0 the stores
  write back the unchanged registers. Every such row changes, a file-scope long's too
  (whose jump was right); no kernel source has one. `x86sim.py` reads `+2(bx)` now
  and counts the stores' bytes for `jz .+16` (a long through a pointer is 2 + 3 bytes,
  which it let pass); its results on the 132 `.s` goldens are unchanged.
- **C1-02** (`e * 0` and `e & 0` drop `e`'s side effects), active: the operands with
  a side effect stay, in their order, as a comma expression before the 0 (`SEQNC`
  nodes with `optim()`'s degree); a list without one still folds to 0. The tree's
  real code never meets it. Its triggers found a second way into C1-12: `(x * 0) ==
  0`, a product only `c1` folds, gives "C error: const" (the emulated original
  too); with a side effect in the product the corrected mode compiles it.
- **C1-01** (branches on stale flags after `not` and `mov`), active: in `cctab`'s
  row for `!`, `-`, `~`, `+`, `-`, `>>`, `<<`, `|`, `^`, the three that end in an
  instruction setting no flags - `~` (`not`), `!` (its value by `mov r,*0.` /
  `mov r,*1.`), a shift by a count that is not a constant (`sal r,cl` with 0) - get
  `or r,r` (`orb` for a char) after the computation, the test `rcexpr()` appends where
  `cctab` has no row; the hook moved from the template letter `H` to the template of a
  row once matched, where C1-06's is. The tree's real code never meets it.
- **Two new bugs of the original `c1`**, found by `fuzz_c.py --scope --mode corrected`
  (seed 4, 3,000 programs: 2,997 correct, three failures no hooked bug explains; the
  emulated original `c0` and `c1` write the same code): **C1-14**, `(rel) < 0` and
  `(rel) >= 0` folded by `optim()` to 0 and 1 without the relation's side effects
  (`((u = 6) != 7) < 0` leaves `u`), and **C1-15**, a signed comparison with 0 of a
  shift by a count other than 1 (`(t >> 3) <= 0`: `sar di,cl` / `ble`) branching on
  the overflow flag, which the 8086 leaves undefined after such a shift - whether the
  hardware branches wrongly is not measured. Both are candidates for step F4
  (`docs/MUTOS_ORIGINAL_BUGS.md`).
- **`tests/rules/check_corrected.py`** (new, in `make test`): what an active
  correction writes computes what C says. Its triggers - for C1-03 two, `x = l++` /
  `x = m--` / `y = a++` (C 91, the original 0) and the kernel's `f((int)t++)` (C 10,
  the original 1); for C1-06 two, a local, a file-scope and a pointed-to long shifted
  by 0 and 2 (C 70) and a count computed first (C 44), where `x86sim.py` refuses the
  original's code; for C1-02 three, the symptom's shape (C 752, the original 111),
  a condition, an argument and a sum (C 131, the original 1001), and a char, an
  unsigned, a long, an index, a pointer sum, `?:`, `switch` (C 347, the original
  11); for C1-01 two, the symptom's `~a || n++` and `!y >= 0` and shifts by a count of
  0 after a comparison (C 1230, the original 100) and `~(a + b)`, `(x << k) && a`, `?:`
  and `~f()` (C 140, which `x86sim.py` refuses in the original's code) - compiled
  without a switch, with `-H`, `-F` and `MUTOS_MODE=corrected` and run under
  `x86sim.py`; then `fuzz_c.py --mode corrected` on seed 1, 1,000 programs with arrays
  and 1,000 with longs: every program correct but those failing through a bug not
  corrected yet (1,000 and 989 correct; 7 C1-13, 4 refused). With `--realbin` (in `make
  check-realbin`) the triggers are linked against the root's `libc.a` and run under
  `mutosemu.py`, with and without `c2`: C's value with `-F`, the original's without.
- **`fuzz_c.py --mode original|corrected`**: `c0`, `c1` and `as` in that mode, the
  semantic check alone, each build with a rule log; a failure through a bug of the
  original (a `met` line in its log, or C1-13's `cmp` of two immediates) is tallied
  by bug, not as WRONG or BAD. Seed 3, 3,000 programs with longs: the original mode
  2,731 correct (C1-01 11, C1-02 1, C1-03 19, C1-06 220, C1-13 22 - a program can
  meet more than one), the corrected mode with C1-03 2,742 (no C1-03 left), with
  C1-06 too 2,959 (C1-01 9, C1-02 1, C1-13 26), with C1-02 too 2,960 (C1-01 9, C1-13
  26), with all four 2,968 (C1-13 27), 5 refused, no WRONG or BAD in any. With all
  four: seed 3 without longs 3,000 of 3,000 correct (the original mode: 2,989, C1-01
  11); seed 5, 5,000 with longs and file scope, 4,934 correct, 18 refused, 48 C1-13.
- **Verified** - each of the four patches (C1-03, C1-06, C1-02, C1-01, applied in
  that order) on a fresh clone of `5d578a8` with the ones before it: `make clean &&
  make all` without a warning under `-std=c11 -Wall -Wextra -Wpedantic`; `make test`
  exit 0, its output the same as before but for the new line of
  `check_corrected.py` (and the section's title); `make check-docs` clean. Without a
  switch every output is the original's: `check_accounting.py` (177 sources, 694
  stages, five configurations), and against the emulated originals on the final
  tree - `c1tables.py --check-c`, `c1trees.py` at depths 2, 3 and 4 (131/132, 1
  refused on purpose, each), `c1fuzz.py` (200, seed 1: 175 the same, none
  different, the rest refused as before), `fuzz_c.py` with the oracle (300, seed 1:
  300 correct; 300 with longs, seed 2: 272 correct, 28 as the real compiler, none
  different), `cccmp.py goldens` (132/132, 132/132, 9/9), `c2fuzz.py --gen fuzz_c`
  (100/100) - and `check_corrected.py --realbin` (the nine triggers linked and run
  under `mutosemu.py`, with and without `c2`). A whole `make check-realbin` did not run
  on the machine of the session (two cores); it is the project owner's to run.

## `mutos_as` opcode coverage

This is the detailed breakdown requested for this document. "Confirmed real" means
verified byte-exact against an actual hardware-linked object file; "unconfirmed" means
implemented per the standard 8086/80186 ISA (and, where available, hand-checked via a
smoke test) but never exercised by any real sample. `MUTOS1700_Assembler_as.pdf` (this project's
only period-accurate reference manual) documents the **K1810WM86**, a 1:1 Soviet clone
of the plain 8086 — it does not cover any 80186/V30 opcode at all, confirmed by
grepping all 51 OCR'd pages for "80186"/"V30"/"V20". 80186/V30 coverage below is
therefore either backed by real corpus evidence or added speculatively toward the
project's stated goal of broad 80186/V30 support in the final version, with no
manual/corpus material to check it against.

### 80186/V30 opcodes — implemented

| Mnemonic | Opcode | Status |
|---|---|---|
| `pusha` | `0x60` | implemented — no real corpus sample; cross-checked¹ |
| `popa` | `0x61` | implemented — no real corpus sample; cross-checked¹ |
| `push #imm16` | `0x68 iw` | implemented — no real corpus sample; cross-checked¹ |
| `push *imm8` | `0x6A ib` | implemented — no real corpus sample; cross-checked¹ |
| `insb` | `0x6C` | implemented — no real corpus sample; cross-checked¹ |
| `insw` | `0x6D` | implemented — encoding **confirmed real** (`mch.s`'s `.byte /6d`, `mch_insw_outsw.s`); the original `as` has no such mnemonic (AS-02) |
| `outsb` | `0x6E` | implemented — no real corpus sample; cross-checked¹ |
| `outsw` | `0x6F` | implemented — encoding **confirmed real** (`mch.s`'s `.byte /6f`, `mch_insw_outsw.s`); the original `as` has no such mnemonic (AS-02) |
| shift/rotate, immediate count ≠1/CL | `C0`/`C1 /digit ib` | implemented (register + indirect-memory dest) — no real sample (every real sample uses count=1 or CL); cross-checked¹ |
| `leave` | `0xC9` | implemented — no real corpus sample; cross-checked¹ |
| `enter framesize,nestlevel` | `0xC8 iw ib` | implemented — no real corpus sample; cross-checked¹ |
| `bound reg,mem` | `0x62 /r` | implemented (indirect-memory operand only, matching this codebase's LDS/LES/LEA convention) — no real corpus sample; cross-checked¹ |
| `imul dst,imm` (2-op) | `0x69`/`0x6B /r` | implemented — no real corpus sample; cross-checked¹ ² |
| `imul dst,src,imm` (3-op) | `0x69`/`0x6B /r` | implemented — no real corpus sample; cross-checked¹ ² |

¹ "Cross-checked" = independently re-encoded with NASM (`CPU 186`, `BITS 16`, `-O0`)
and/or independently re-decoded with `objdump -D -b binary -m i8086`; see
`tests/mutos_as/v30_speculative/README.md`. This is a **strictly weaker** confidence
tier than "confirmed real" and does **not** change any row's fundamental status below —
no real MUTOS 1700 toolchain exists that supports these opcodes to produce genuine
hardware-linked golden output against. Do not read "cross-checked" as "verified"
anywhere else in this document's terminology.

² See that same README for a specific, deliberately-surfaced open question on this
row: `encode_imul_imm()`'s marker-forces-form behavior (`imul cx,ax,*200.` truncates
200 to `0xC8` rather than promoting to the word form) is something NASM's own encoder
refuses to do even with its optimizer fully disabled — a genuine, unresolved design
divergence between this codebase's established convention and a mainstream
independent assembler's default behavior, not a bug that cross-checking fixed.

Every 80186 addition covered in this project's working spec (register/flag behavior
aside, which is a CPU runtime distinction, not an assembler-encoding one) is now
implemented in at least best-effort form. **Only INSW/OUTSW carry real hardware
confirmation** — the other twelve entries are unconfirmed and should be the first
candidates for verification if real V30-targeted source/object pairs ever become
available; they now additionally carry independent-tooling cross-checks (see above),
which is the best available substitute in the absence of a real MUTOS 1700 toolchain
that supports them, but is explicitly NOT a replacement for that real evidence.

**Behind the switch `-1` since 2026-10-25 (`docs/TOOLCHAIN_PLAN.md` section 4, step 5).**
Until then `mutos_as` always accepted these instructions; the original `as` has
none of them and assembles each such word, silently, as a reference to an undefined
external (AS-02). Without `-1` `mutos_as` does what the original does (the
mnemonics are unknown words, the 80186 forms of `push`, `imul` and the shifts are
refused); `mch_insw_outsw.s` and `tests/mutos_as/v30_speculative/` (seven test files
covering the twelve rows without a real sample, cross-checked with NASM and objdump -
its `README.md`; the session that added them is recorded in
[`docs/history/status_m2_as_v30.md`](docs/history/status_m2_as_v30.md)) are assembled
with `-1` (`tests/mutos_as/check_switches.sh`, in `make test`).

### Untested / unconfirmed opcodes (implemented, no real sample)

Self-documented as `UNCONFIRMED`/"no real sample" in `encode.c`, beyond the 80186 set
above:

- `jnae`/`jnb`/`jng`/`jnge`/`jnl`/`jnle` — Jcc alias spellings (opcode itself is
  confirmed via the "positive" spelling; the alias acceptance is not).
- `bloss` — 5-letter alias of the confirmed-real 4-letter `blos`.
- `cmps`/`cmpsb`/`scas`/`scasb`.
- bare `movs` defaulting to the word form (`0xA5`); `movsw` (only `movsb` is directly
  confirmed real).
- `inb`/`outb` as accepted synonyms for the confirmed-real bare `in`/`out` byte forms.
- `aam`/`aad`.
- `clc`/`cmc`/`stc`/`hlt`/`lahf`/`sahf`/`into`/`xlat`/`lock`/`daa`/`das`/`aaa`/`aas`
  (unambiguous single-byte, zero-operand 8086 opcodes with no encoding quirk possible).
- `lds`/`les` (same ModRM shape as the confirmed-real `lea`, opcode itself untested).
- Group1 store-direction ("+1", `r/m,reg`) form — the load direction ("+3") is
  confirmed real; the store direction follows the identical rule but has no direct
  sample.
- `incb`/`decb` (byte-sized `FE`/`FF` ModRM form — only the word short-form `40–4F` is
  directly confirmed).
- XCHG general `reg,mem` ModRM form — only reg,reg and the AX-shortcut forms have real
  samples (see "missing" below for the case that's not implemented at all).

Of these spellings, `bloss`, `scas`/`scasb`, `movsw`, `inb`/`outb`, `repe` and `repne`
are not in the original `as`'s mnemonic table (read from its binary, 2026-10-20, and
run through all operand shapes by `asfuzz.py --words`, 2026-10-25): the original
assembles each as a word referring to an undefined external (AS-02). They are
`mutos_as`'s own spellings and are kept (step 5 of the plan: no source the
toolchain makes uses them, and the object differs only where someone wrote them).
`aam` is confirmed against the original's binary and differs from it: the original
writes `D6 0A` (AS-04), `mutos_as` the instruction's `D4 0A`; the same for `hlt`, which
the original does not find (AS-05) and `mutos_as` writes as `F4`. `incb`/`decb` of a
register, `xchg` with a memory operand and the shift of a global are now confirmed
against the original's output too.

### 8086 opcodes still missing

Identified from `MUTOS1700_Assembler_as.pdf` Anlage A/B (the K1810WM86 base-chip
manual) and, since 2026-10-25, measured against the original binary and its table of
273 words (`asfuzz.py --words`, `tests/realbin/ascmp_goldens.txt`):

- **Done 2026-10-25**: `ret`/`reti` with an immediate (`C2 iw`/`CA iw`), the one-byte
  `int *3.` (`CC`; the original does that, `int *4.` is `CD 04`), `xchg`/`xchgb`
  with a memory operand in either order, `test`/`testb` of memory and register,
  `pop` of a memory operand, `lds`/`les` with an address, and the shifts and rotates
  of a bare address (`shl _g,cl`; the old note that Group2 had no DIRECT destination).
- **`esc`/`escb`** (`0xD8`-`0xDF`, coprocessor escape): not missing in the original's
  sense - the original has neither word (it reads them as undefined symbols); its
  8087 words (`fadd`, `fld`, ..., 113 of them, used by `asf`, the assembler
  `cc -f` calls) are what makes the escape bytes. `mutos_as` has none of them.
- `lods`/`stos` under their literal Anlage-A spelling: the original does not have
  them either (it has `lodb`/`lodw`/`stob`/`stow`).
- `bc`, `bnc` (the original's carry branches) and `scab`/`scaw`: in the original's
  table, not in `mutos_as`.
- Forms the original takes and `mutos_as` refuses (rare by hand, absent from the
  compiler's output and from the corpus): `pop cs`; a byte register as the operand of
  `push`, `pop` or `lea` (the original takes `pop al` as `pop ax`); `movb` and `cmpb`
  without an operand (`A4`, `A6`); a conditional branch (`bgt`, `ble`, ...) to an
  indirect address (AS-11: the original gets it wrong); `jmpi X` and `calli X` with a
  plain address (AS-12: seven bytes); a statement that is only an expression (a symbol
  plus a number, or a name with a dot such as `mov.b`); an operand of more than 64 tokens;
  `.globl L` twice under `-C`. (`jmpi`/`calli` of a register and `br`/`call` of a byte register are done
  since 2026-10-25.)

Already-known, deliberately-not-implemented gap (corrected 2026-10-25):

- **CALLI/JMPI with a direct address.** The note of earlier sessions - a `d:s`
  (segment:offset) form needing a colon-separated operand syntax - was a guess:
  the original has no such syntax (`jmpi 5:6` and `jmpi 5,6` are syntax errors). It
  has a plain expression, `jmpi X`/`calli X`, which it writes as seven bytes (AS-12).
  Not offered, no corpus evidence of use. The indirect (`@reg`/`@disp(reg)`) form of
  both is fully implemented, and the same as the original's.

### 80186/V30 opcodes still missing

None from this project's working 80186/V30 specification — see the "implemented"
table above. This is **not** an exhaustive audit against a complete 80186 instruction
reference (no such document exists in this project's materials); it reflects full
coverage of the specific instruction set discussed and specified in this project so
far.

---

## Open items / suggested next steps

1. Milestone 1's golden binaries (`myhello`/`idhello`) are missing from the tree.
   Since the port of `ld` (2026-10-22) the emulated original `ld` is its oracle -
   1,572 links of the compiled goldens and the fuzzing without a difference. Real-hardware
   evidence came on 2026-11-07 from another side: the MUTOS kernel, linked by
   `mutos_ld -i` (0411) from 61 objects and `arith.a`, has the MD5 sum of the kernel
   linked on the hardware ("The MUTOS kernel, built with the cross toolchain"). The kernel
   is the project owner's, not in the tree; the small goldens would still be welcome as
   evidence in it.
2. If real V30-targeted MUTOS source ever surfaces, prioritize re-checking INSB/OUTSB,
   PUSHA/POPA, PUSH imm, ENTER/LEAVE/BOUND, the IMUL immediate forms, and the
   count≠1/CL shift/rotate form — currently the only unconfirmed-by-corpus opcodes in
   active use by this project's stated final-version goal. Test files and an
   independent-tooling cross-check for all twelve now exist in
   `tests/mutos_as/v30_speculative/` (see above) — this makes verification against a
   real sample, if one ever surfaces, a drop-in comparison rather than starting from
   nothing, but does **not** reduce the priority of finding that real sample. In
   particular, resolve the open `imul`-immediate marker-truncation question (see the
   opcode-coverage table's footnote ²) one way or the other once real evidence exists.
3. ~~`esc`/`escb`, `ret`/`reti` with an immediate, the dedicated `int 3` encoding~~ -
   **closed 2026-10-25** by the measurement against the original (step 5): `ret N` and
   `int *3.` are done, `esc`/`escb` do not exist in the original. What differs
   from the original is listed under "Step 5 of the plan" and in "8086 opcodes still
   missing" above; whether any of it should be closed is the port decision.
4. ~~Finish Milestone 4~~ - **done 2026-10-24**: `mutos_c1`, `mutos_c0` and the
   driver `mutos_cc` are ports of the real `c1`, `c0` and `cc` (`docs/C1_PORT_PLAN.md`,
   `docs/C0_PORT_PLAN.md`, `docs/TOOLCHAIN_PLAN.md` step 3), every golden reproduced
   through the driver too. `mutos_c2` followed on 2026-10-25 (Milestone 5), then
   `mutos_as`'s differential fuzzer (Milestone 2, step 5), then `mutos_ar` and
   `mutos_ranlib` (Milestone 6, step 6), `mutos_make` (step 7) and `mutos_nm`, `mutos_size`,
   `mutos_strip`, `mutos_tsort`, `mutos_lorder` (step 8) and `mutos_ed`, `mutos_em` (step 9);
   what follows is the project owner's to order - see "Next up".
5. ~~`mutos_cpp`'s documented simplifications~~ - **closed 2026-10-22**: the port of
   `cpp` does what the original does in each (a formal inside a string of a macro body,
   a macro with parameters named without `(` - CPP-11 -, a macro call across lines -
   CPP-10), checked against the emulated original; the old list is in
   [`docs/history/status_m3_cpp.md`](docs/history/status_m3_cpp.md).
6. `mutos_as`'s `-L` deviation (see `CLAUDE.md` rule 3) - **explained 2026-10-25, as far
   as the evidence goes, by `-C`**. The 2026-09-28 experiment on the hardware had ruled
   the flag `-L` out (`as -L -o v30ide.oL v30ide.s` printed `Unknown option L ignored`
   and proceeded: the binary does not implement `-L`, whatever
   `docs/MUTOS1700_Assembler_as.pdf` sect. 3.1 documents), and left a question: none
   of `libc.a`'s 167 objects - compiled C such as `atof.o`/`ecvt.o` included - has an
   `L`-number label, while every kernel golden does (`malloc.o`: 14). The
   emulated original (step 5, `tests/realbin/ascmp.py options`) answers it: it has the
   option **`-C`**, which leaves out every defined, non-global symbol that starts with a
   capital `L`, and the original `cc` runs `as -gC` on every source. So an object made
   by `cc -c` has no `L` symbols and one made by `as` without `-C` has them - and the
   kernel goldens were made by the native kernel build's `cc -S` followed by a plain
   `as` (`kernel_opt/Makefile.mutos`), `libc.a` presumably by `cc -c`. That last step is
   a reading, not a record: nothing in the tree says how `libc.a` was built, but the
   two facts fit and nothing else is known to. `mutos_as` does the same since
   2026-10-25: `-C` leaves them out, without it it keeps them (every kernel golden),
   and `mutos_cc` passes `-gC` as the original does, so its objects have none.
   **Confirmed 2026-11-07** by the kernel build: compiled by `cc -c` (`as -gC`), the 61
   kernel objects are the goldens without their `L` symbols, and the kernel linked from
   them is the original build's byte for byte - so the original kernel build, too, ran
   `cc -c`, and the `L` symbols of the goldens come from assembling `kernel_opt/`'s `.s`
   with a plain `as`. What is not done: a whole `atof.o`/`ecvt.o` as a `libc_recon/` golden (the `L` labels
   were their last blocker, item 7) - it needs the disassembly turned into a source.
7. **Floating constants: settled (2026-09-29), overflow included.** The real
   conversion is re-enacted (Milestone 2's "The real conversion, re-enacted",
   "Rounding pinned, zeros on both paths", "The real product, zeros at
   exponent 0" and "Range: `ldexp` wraps, an overflow aborts"): v7 `atof()` on
   the 56-bit double, nearest-even rounding everywhere, `libc.a`'s inexact
   product in the multiplication, zeros from what the accumulator `fac` held,
   and `libc.a`'s `ldexp`, which wraps the exponent byte of a result outside
   the range (`float_coverage/fltovf.o.golden`). A step of the arithmetic that
   overflows (5\*\*k for k >= 55, e.g. `.float 1.00000000000000000e-38`)
   makes the real assembler abort - `***ERROR*** floating point over/under
   flow- assembly aborted`, exit status 4, no object
   (`tests/mutos_as/float_open/fltsig.s`, kept there as the evidence) - and
   `mutos_as` refuses it too (`FLT_RANGE`; exit status 1, not 4). Still
   unobserved, and refused (`FLT_UNKNOWN`): LOGHUGE after a dropped digit
   (the value is below the format's range anyway; the compiler never writes
   such a text). Not understood but pinned by real bytes: where the digit
   loop's `ff ff ff` for an all-zero text comes from. With `atof.o`'s and
   `ecvt.o`'s floating constants all reproduced (`floatdat.s`, `ecvt.o`'s
   `.03` included), only the `L` labels (open item 6; `-C`, which `mutos_as`
   has since 2026-10-25) kept them from being `libc_recon/` goldens - what is missing
   now is a source for each, written from the disassembly.
