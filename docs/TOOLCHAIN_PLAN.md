# Toolchain plan — from the compiler passes to a complete cross toolchain

Written 2026-10-20, from the decisions taken that day with the project
owner. It continues where `docs/C1_PORT_PLAN.md` and `docs/C0_PORT_PLAN.md`
ended: the compiler passes are ports of the originals; the driver, the
optimizer, the archiver and support for MUTOS makefiles are missing, and
three of the existing tools (`mutos_cpp`, `mutos_ld`, `mutos_as`) are
re-implementations whose behaviour beyond the goldens was never compared
with the originals. Decisions taken later with the project owner are
dated where they stand (2026-10-25: the headers in the MUTOS root,
decision 3; the port of `make` and of the other tools brought from the
hardware, decision 10).

**Status (2026-11-02): steps 0 to 9 are done** (section 3). Step 5 -
the differential fuzzer for `mutos_as`, the switch `-1`, `-C` - measured the
re-implementation against the original; whether to port `mutos_as` is the
project owner's decision, with the measurements and a recommendation in
`STATUS.md` (Milestone 2). Step 6 - the ports of `ar` and `ranlib`, with
`bin/ar` and `bin/ranlib` in the root - was done on 2026-10-26. Step 7 - the
port of `make`, with `bin/make` in the root - was done on 2026-11-02, and so was
step 8, the ports of `nm`, `size`, `strip`, `tsort` and `lorder`, with `bin/nm`,
`bin/size`, `bin/strip`, `bin/tsort` and `bin/lorder` in the root, and step 9,
the ports of `ed` and of `em`'s line mode, with `bin/ed` and `bin/em` in the
root - the MUTOS kernel's makefile, which edits itself with `ed` and
post-processes `mch.s` with `em`, runs through `mutos_make` with them.

**What follows (decided 2026-11-08)**: the bugs of the originals are
corrected in a mode of their own, which becomes the default at the end, and
`c1`/`c2` generate 80186/V30 code behind `-1` - section 7 (decisions 13 to
16, stages F and V). The corrected mode links a corrected copy of `libc.a`,
in which only measured runtime bugs are corrected; the original stays.
`make install` and the manual pages come after; `STATUS.md`'s "Next up" follows this plan.

**The test of it all (2026-11-07)**: the project owner built the MUTOS 1700
kernel on Linux with its original, unchanged makefile, through `mutos_make` and
the ported tools - all 61 objects are the real-hardware goldens of
`tests/mutos_as/kernel_opt/`, and the kernel is the one the original build made
on the hardware, byte for byte (`docs/KERNEL_BUILD.md`; `STATUS.md`, "The MUTOS
kernel, built with the cross toolchain").

## 1. Where things stand

| Tool | Today | Checked against |
|---|---|---|
| `mutos_cpp` | port of the original `cpp` (2026-10-22; until then a re-implementation without `# N "file"` lines or a default include directory) | the emulated original: every `.i` golden's source with and without `-P`, differential fuzzing |
| `mutos_c0` | port of the original `c0` (2026-10-18) | the emulated original: every golden, differential fuzzing |
| `mutos_c1` | port of the original `c1` (2026-10-13) | the emulated original at four depths, differential fuzzing |
| `mutos_c2` | port of the original `c2` (2026-10-25) | the emulated original: every `.s` golden three ways (`c2 in out`, `c2 - in out`, `c2 <in >out`), hand-written cases, differential fuzzing; `tests/mutos_as/kernel_opt/`'s nine real-hardware sources, also through `mutos_cc -O -S` |
| `mutos_as` | re-implementation (2026-10-25: measured against the original, step 5); `-g` (no effect), `-C` (leaves out the local `L` symbols) and `-1` (the 80186/V30 opcodes, off without it) as the original and the plan have them | 76 `.o` goldens, the floating conversion against `libc.a`'s own `atof`; the emulated original: every golden and compiler `.s`, differential fuzzing (`tests/realbin/ascmp.py`, `asfuzz.py`) |
| `mutos_ld` | port of the original `ld` (2026-10-22; until then a port of v7's `ld.c` with the MUTOS deltas known then), `-r`/`-d` an extension | the emulated original: every compiled golden linked with a matrix of options, hand-written cases, differential fuzzing (its own goldens `myhello`/`idhello` are missing from the tree); the extension by running programs linked through `-r`; its `-i` output against the MUTOS kernel linked on the hardware (2026-11-07: the same bytes) |
| `mutos_cc` | port of the original `cc` (2026-10-24) | the emulated original, its passes traced: a matrix of command lines, the corpus with the original passes run in both, the ported chain against the original chain and the goldens, differential fuzzing |
| `mutos_ar` | port of the original `ar` (2026-10-26) | the emulated original: hand-written command sequences, differential fuzzing (files and archives changed between the commands, damaged archives); libc.a rebuilt from its 167 objects |
| `mutos_ranlib` | port of the original `ranlib` (2026-10-26); runs `mutos_ar` through the root's `bin/ar` as the original runs `ar` through the shell | the emulated original `ranlib` and `ar`: hand-written cases, differential fuzzing; libc.a rebuilt from its 167 objects, `__.SYMDEF` included |
| `mutos_make` | port of the original `make` (2026-11-02); its commands run by the host's `/bin/sh` and the `PATH` with the root's `bin/` first | the emulated original (its `fork`/`exece`/`wait` faked, the commands the host's, fake tools in the root): hand-written cases, the corpus's `Makefile.mutos` files, differential fuzzing; the corpus's makefiles through `mutos_make` and the ported tools against the goldens |
| `mutos_nm`, `mutos_size`, `mutos_strip`, `mutos_tsort` | ports of the original `nm`, `size`, `strip` and `tsort` (2026-11-02); the stdio they share is `src/h/mutos_stdio.h` | the emulated originals: every object and archive of the tree (the compiled goldens, `crt0.o`, `libc.a`) with every option, hand-written cases, differential fuzzing; `tests/mutos_binutils/check_tools.py` without an emulator |
| `mutos_lorder` | the original shell script (2026-11-02), run by the host's `sh` with `$MUTOS_ROOT/bin/nm` and the host's `sed`, `sort` and `join` | the original script with the emulated `nm`: `libc.a`'s objects, hand-written cases, differential fuzzing; `lorder libc.a \| tsort` without an emulator |
| `mutos_ed` | port of the original `ed` (2026-11-02): v7's `ed.c` with two MUTOS changes, rebuilt byte for byte by the project's own `cc -O -s` | the emulated original: hand-written scripts (every command, the error paths, the 16-bit addresses, overruns, the kernel makefile's eddep scripts) from a file and through a pipe, differential fuzzing; the kernel's makefile through `mutos_make` without an emulator |
| `mutos_em` | port of the original `em`'s line mode (2026-11-02): the original `ed` with em's `main()`, `commands()`, input reader, session log and commands; the screen mode (`o`) refused | the emulated original: ed's scripts and em's own (options, "sure?", `z`, `"`, `&`, `%`, `<`, `>`, `h`, 8-bit text, `-r`), differential fuzzing; the kernel's `mch.i` with `fakeawk` gives the real `mch.s` |

The originals run under `tests/realbin/mutosemu.py`: `cpp`, `c0`, `c1`,
`as` since 2026-10-12, and on 2026-10-20 also `c2`, `ld` and - with
`fork`/`exece`/`wait` faked, first in a scratch harness, since step 0 by
the emulator and traced by `tests/realbin/cctrace.py` - `cc`. What they showed
is in `docs/devlog/originals_and_ports.md`, "The other originals as
oracles (2026-10-20)"; the points that shape this plan are in section 5.

## 2. Decisions (2026-10-20)

1. **Ports as `c0` and `c1` were ported**: from the binary, with the v7
   source as the scaffold where there is one, checked against the
   emulated original on every golden and on random input.
2. **A MUTOS root.** One directory mirrors the MUTOS file system; every
   absolute path an original tool has compiled in is looked up under it
   (`/lib/crt0.o` → `$MUTOS_ROOT/lib/crt0.o`). Its location: the
   environment variable `MUTOS_ROOT`, otherwise a default compiled in at
   build time - the absolute path of the build tree's `mutos_root/`
   (rebuild, or set `MUTOS_ROOT`, after moving the tree). No new
   command-line option, so the command lines stay those of the
   originals. The host's own `/lib` and `/usr/lib` are never used (on
   some distributions `/usr/lib/libc.a` is glibc's).
3. **`mutos_root/`** sits at the top of the repository, is ignored by git
   and removed by `make clean`; the build fills it:

   | Path | What |
   |---|---|
   | `lib/crt0.o` | decoded from `tests/mutos1700_crt0/crt0.o.base64.txt` |
   | `lib/libc.a` | decoded from `tests/mutos1700_libc/libc.a.base64.txt` as it is (not re-archived from the 167 objects), its modification time set to its `__.SYMDEF` time, 1989-01-31 20:41:17 UTC, so that `ld` uses the table without "out of date" |
   | `lib/cpp`, `lib/c0`, `lib/c1`, `lib/c2`, `bin/as`, `bin/ld`, `bin/cc` | symbolic links to `mutos_cpp`, `mutos_c0`, ... - the passes are found through the root, so the driver maps each of the original's paths one to one, and `-B`/`-t` keep their meaning |
   | `usr/lib/` | empty: for the user's own libraries |
   | `usr/include/` | the original headers: `mutos/mutos_include/usr/include/` copied as it is, `sys/` and `local/` included (decided 2026-10-25; until then empty) |
   | `tmp/` | the temporary files the originals name under `/tmp` (`ld`'s `/tmp/ldaXXXXXX`, `cc`'s `/tmp/ctm...`) - added with step 2 (2026-10-22) |

   `make install` comes later.
4. **`mutos_cpp` becomes a port** of the original `cpp` (Reiser's, SCCS
   `cpp.c 1.6 12/12/86`; the binary has its symbol table). That brings
   the `# N "file"` lines a compile without `-P` writes - the real `c0`
   takes its messages' file names and line numbers from them -, the
   default include directory (`/usr/include` → `$MUTOS_ROOT/usr/include`),
   and the four cases `mutos_cpp` simplifies. Settled with the port
   (2026-10-22): an absolute `#include` name in a source, and every file
   found through the default directory or such a name, is a MUTOS path too
   (under the root); names from the command line - the input, the output,
   `-I` - are the host's. The project owner confirmed this rule on
   2026-10-22: paths of the MUTOS file system are looked up under
   `$MUTOS_ROOT`; the port of `ld` follows it (decision 5).
5. **`mutos_ld` becomes a port** of the original `ld`. `-lX` is
   `$MUTOS_ROOT/lib/libX.a`, then `$MUTOS_ROOT/usr/lib/libX.a`, never the
   current directory (the original's order). `-r` and `-d`, which the
   original refuses, stay as a documented extension. Settled with the port
   (2026-10-22): its temporary file `/tmp/ldaXXXXXX` is under the root too
   (`tmp/`, decision 3); the output's own temporary name `..ldtmpXXXXXX`,
   `a.out` and the command line's names are relative to the current
   directory or the host's; the extension gives `-r` v7's meaning with
   MUTOS's object convention (data and BSS values offsets in their own
   segment, the byte-shifted relocation words kept).
6. **`mutos_cc` is a port of the original `cc`** (0407, text 8,026
   bytes), not of `v7/cc/cc.c`, which it differs from (section 5). It
   passes `$MUTOS_ROOT/lib/crt0.o` and `-lc` to `ld`, as the original
   passes `/lib/crt0.o` and `-lc`. Files the root does not have
   (`mcrt0.o` for `-p`, `crt2.o`/`crt20.o`/`lib2.a` for `-2`/`-20`, the
   `-f` passes and `libf.a`) fail as on a MUTOS system without them.
   Settled with the port (2026-10-24): the passes get host paths - `cc`
   passes its MUTOS paths on (`/tmp/ctm4...`, `/lib/crt0.o`) as the paths
   under the root, its command line's names as they are - and run with
   `MUTOS_ROOT` set to `cc`'s root; a name `cc` builds from both (`-B`'s
   prefix and `c0`) is a MUTOS path only if its leading `/` is not the
   command line's.
7. **`mutos_c2` is a port** of the original `c2`, for the 8086 only (the
   original has no 80186/V30 transformation).
8. **`mutos_c1` and `mutos_c2` stay without 80186/V30 code generation**
   until the 8086 toolchain works completely; whether and how it comes is
   decided then. **Decided 2026-11-08**: it comes, behind `-1`, as a closed
   list of three rules (decision 15, section 7).
9. **`mutos_as` is not ported now.** It takes `-g` and `-C` (which the
   original `cc` always passes): `-g` has no effect in the original and
   none here; `-C` leaves the defined, non-global symbols starting with `L`
   out of the symbol table - measured on the original, 2026-10-25, and done
   in `mutos_as`. No 8087 support is needed. A differential fuzzer against
   the emulated original measured the differences (step 5, done
   2026-10-25: `STATUS.md`, Milestone 2); the port itself is decided from
   them - the owner's decision, with a recommendation. The 80186/V30
   instructions are behind the switch `-1` (section 4).
10. **`make` and the other tools are ported** (decided 2026-10-25). The
    plan of 2026-10-20 was a wrapper `mutos_make` around the host's `make`
    (its built-in rules off, the MUTOS `make`'s rules from a file,
    `$MUTOS_ROOT/bin` first in `PATH`) and a port of `make` decided
    later; it is replaced: `mutos_make` is a port of the original `make`,
    from its binary with `v7/make/` as the scaffold, as every other tool
    (decision 1). So are all the other binaries brought on 2026-10-25
    (decision 11): `ar`, `ranlib` - a makefile that builds a library needs
    them, and the host's `ar` writes a format `mutos_ld` cannot read -,
    `nm`, `size`, `strip`, `tsort`, the editors `ed` and `em` (`em` has no
    v7 source: the binary alone), and `lorder`, which is a shell script,
    not a binary. The executables: `mutos_make`, `mutos_ar`,
    `mutos_ranlib`, `mutos_nm`, `mutos_size`, `mutos_strip`, `mutos_tsort`,
    `mutos_lorder`, `mutos_ed`, `mutos_em`. A recipe with an absolute path
    (`/lib/c0`) is run by the host's shell and cannot be redirected by any
    `make`; such makefiles need the path as a macro (`make
    CC=$MUTOS_ROOT/bin/cc CPP=$MUTOS_ROOT/lib/cpp C0=$MUTOS_ROOT/lib/c0`).
    With the host's `make` that works for all thirteen
    `tests/mutos_cc/*/Makefile.mutos` since the driver exists: every `.s`,
    `.i`, `.1` and `.2` the golden but `fltprobe/p19_open3.s`, which
    `mutos_c1` refuses as `invalid_goldens.txt` says; `fltprobe`'s needs
    `-i`, because its ten probes in `c1_errors.txt` end with `c1`'s
    messages and exit status 1, as on the hardware (2026-10-25; on
    2026-10-24 the twelve others were run).
11. **Binaries and manual pages from the hardware**: the project owner
    brings `make`, `ar`, `ranlib` (and `lorder`, `tsort`, `nm`, `size`,
    `strip` as makefiles need them) into `mutos_bin/`, and the MUTOS `make`
    manual page as reference material. **Done 2026-10-25**: every
    original file is under `mutos/` since - the eight tools (`lorder` a
    shell script) with `ed` and `em` in `mutos/mutos_bin/`, the manual
    pages of all eight, `make`'s included (and `as`, `cc`, `ld`, `ed`,
    `a.out(5)`, `ar(5)`, `types(5)`, `filsys(5)`) in `mutos/mutos_man/`,
    the original headers in `mutos/mutos_include/`; v7's
    sources of `make`, `ar`, `ranlib`, `nm`, `size`, `strip`, `tsort` and
    `ed` in `v7/`. What they showed: `docs/devlog/originals_and_ports.md`,
    "The originals of 2026-10-25" - among it that the original `make`'s
    built-in rules and macros are v7's `builtin[]`, string for string.
12. **Manual pages** (`man/`) are written at the end of the project; the
    existing ones stay as they are until then, also where they no longer
    describe the tools. Requirements: English, no development details,
    references to the other tools, consistent wording and structure
    throughout.

## 3. Order of work

| Step | What | Checked by |
|---|---|---|
| 0 | Infrastructure: `mutosemu.py` serves `fork`/`exece`/`wait` (faked as above), `fstat`, `chmod`, `umask`, `link`, and a tracer for `cc` joins `tests/realbin/`; `mutos_root/` and the `MUTOS_ROOT` lookup (one header in `src/h/` for every tool); a `.gitignore` (the repository had none) for `mutos_root/` and the build products; `mutos_as` accepts `-g`/`-C` | the real `cc` and `ld` run as oracles; `make test` unchanged. **Done 2026-10-20**: `cctrace.py goldens` - the traced `cc`, running the original passes, gives 132/132 `.s` (`cc -S`, with `c1`'s messages and exit status), 132/132 `.i` (`cc -P`), 9/9 `kernel_opt/` sources (`cc -O -S`); the original `ld` leaves its output (against `mutos_ld`: section 5); `make test` as before, plus `tests/mutos_root/check_root.py` |
| 1 | Port of `cpp` | the emulated original on the 137 `.i` goldens with and without `-P`, differential fuzzing (macros, conditionals, includes, comments). **Done 2026-10-22**: `src/mutos_cpp/` is the original read from its binary, on an image of its data segment; `cppcmp.py goldens` 411/411 identical (with `-P`, without, into an output file), the differential fuzzing in `STATUS.md` (Milestone 3) without a difference; thirteen bugs of the original found (CPP-01 to CPP-13; a fourteenth, CPP-14, on 2026-10-25) |
| 2 | Port of `ld` | the emulated original: every compiled golden linked with `crt0.o` and `libc.a`, with `-i`, `-n`, `-s`, `-x`, `-X`, `-u`, `-e`, `-o`, archive search, messages and exit status; this replaces the missing `myhello`/`idhello` goldens. **Done 2026-10-22**: `src/mutos_ld/` is the original read from its binary, on an image of its data segment; `ldcmp.py goldens` 1,572/1,572 the same (131 goldens, twelve command lines each: messages, exit status, the files left), the hand-written cases and the differential fuzzing in `STATUS.md` (Milestone 1) without a difference; the extension `-r`/`-d` checked by running programs linked through it (`tests/mutos_ld/check_rflag.py`); eight bugs of the original found (LD-01 to LD-08) |
| 3 | Port of `cc` | the tracer: every pass's path and arguments, messages and exit status over a matrix of options, the original's against the port's; whole programs through the chain. **Done 2026-10-24**: `src/mutos_cc/ccport/` is the original read from its binary, on an image of its 64K segment (text, data, heap, `main()`'s frame); `cccmp.py` - the port's passes a stub that logs its arguments - 177 command lines traced (175 the same, 2 refused), the corpus with the original passes run in both 124/124, the ported chain against the original chain 124/124 (the same `a.out`) and against the goldens (`cc -S` 132/132, `cc -P` 132/132, `cc -O -S` 9/9 with the original `c2`), differential fuzzing in `STATUS.md` (Milestone 4) without a difference; six bugs of the original found (CC-01 to CC-06) |
| 4 | Port of `c2` | the emulated original on all 132 `.s` goldens and on `c1fuzz.py`'s programs; `tests/mutos_as/kernel_opt/`'s nine sources from `11_kernel` (the original gives them byte for byte). **Done 2026-10-25**: `src/mutos_cc/c2port/` is the original read from its binary, on an image of its data segment with libc.a's stdio in it; `c2cmp.py goldens` 396/396 the same (132 goldens three ways: into a file, with the statistics, through stdin and stdout), the nine `kernel_opt/` sources byte for byte - also through `mutos_cc -O -S` (`check_driver.py`, `cccmp.py goldens`) -, the hand-written cases and the differential fuzzing (the real compiler's output for `c1fuzz.py`'s and `fuzz_c.py`'s programs, assembly made for `c2`'s passes, both damaged) in `STATUS.md` (Milestone 5) without a difference; seven bugs of the original found (C2-01 to C2-07), two of them endless loops the port refuses |
| 5 | `mutos_as`: differential fuzzer, the `-1` switch | the emulated original; then the decision on a port. **Done 2026-10-25**: `tests/realbin/ascmp.py` (a comparator that sorts every run into a class) and `asfuzz.py` (differential fuzzing, five generators, reduced and clustered findings, the table of all 273 instruction words); `-1` and `-C` implemented (`tests/mutos_as/check_switches.sh`); the compiler's own output assembles to the same object as the original's in all 1,284 of the 1,409 compiled test programs the original accepts (the other 125 are the compiler's own invalid output) and in every `.s` of the tree it accepts; the fuzzer's findings that hand-written code could meet are fixed (list in `STATUS.md`), the rest measured and kept (the options `-e`/`-l`, the exit statuses, the messages, the 8087 words, leniency); eleven bugs of the original found (AS-04 to AS-14, besides AS-01 and AS-02 re-measured); the decision on a port is the project owner's - recommendation in `STATUS.md` ("Step 5 of the plan"): not now, after step 6 |
| 6 | Ports of `ar` and `ranlib`; `bin/` in the root for the new tools | the emulated original: archives made and changed with every key, `libc.a` rebuilt from its 167 objects against the original archive, `__.SYMDEF` included; differential fuzzing. **Done 2026-10-26**: `src/mutos_ar/` and `src/mutos_ranlib/` are the originals read from their binaries - none of the 33 functions of `ar.o` and the five of `ranlib.o` differs from v7's in what it does, the representation (26-byte headers, longs high word first, signed chars) and the libc.a underneath do (`perror`'s 41 texts, `mktemp`, `ctime`, the twenty stdio slots) -; `mutos_ranlib` starts `$MUTOS_ROOT/bin/ar`, the root's link to `mutos_ar`, where the original goes through `/bin/sh -c`; `bin/ar` and `bin/ranlib` in the root (`scripts/mutos_root.sh`, `check_root.py`); `arcmp.py cases` 366 runs (364 the same, 2 refused) and `ranlibcmp.py cases` 146 runs (139 the same, 6 refused, 1 endless), the differential fuzzing in `STATUS.md` (Milestone 6) without a difference; libc.a rebuilt from its 167 objects by `mutos_ar rc` and `mutos_ranlib` is the original archive byte for byte but for `__.SYMDEF`'s time (five seconds past the clock), group and mode - against the emulated originals (`ranlibcmp.py libc`) and in `make test` without an emulator (`tests/mutos_ar/check_libc.py`); twelve bugs of the original found (AR-01 to AR-05, RL-01 to RL-07) |
| 7 | Port of `make` | the emulated original, its `fork`/`exece`/`wait` faked as for `cc` and the commands it runs traced: the corpus's `Makefile.mutos` files, makefiles written for its rules, macros and options, differential fuzzing; then the corpus's makefiles through `mutos_make` and the ported tools against the goldens. **Done 2026-11-02**: `src/mutos_make/` is the original read from its binary - 49 of its 50 functions are v7's (they compile with the project's own `cc -O -Dunix` to the original's bytes, but for `METERFILE`, MUTOS's `struct nlist` and `NLEFTS` 300), `yyparse()` is yacc's skeleton on the eight tables and `builtin[]` the generator `tests/realbin/maketables.py --c` writes from the binary -, with the libc.a underneath modelled (16-bit `int`, signed `char`, 512-byte stdout blocks, `(null)`, the 64K segment counted, directories as MUTOS shows them); `bin/make` in the root; the commands run by the host's `/bin/sh`, with `$MUTOS_ROOT/bin` first in `PATH`. `makecmp.py cases` 457 runs (307 sequences) without a difference, `makecmp.py corpus` 312 runs of the thirteen `Makefile.mutos`, `makefuzz.py` in `STATUS.md` (Milestone 6); `tests/mutos_make/check_corpus.py` (in `make test`): the thirteen makefiles through `mutos_make` and the ported tools give 131 `.s` (and the refused `p19_open3`), 132 `.i`, `.1`, `.2` as the goldens; thirteen bugs of the original (MK-01 to MK-13) |
| 8 | Ports of `nm`, `size`, `strip`, `tsort`; `lorder` | the emulated original on every object and archive of the tree (the compiled goldens, `crt0.o`, `libc.a`) with each option; `lorder` and `tsort` on `libc.a`'s objects. **Done 2026-11-02** (`STATUS.md`, Milestone 6) |
| 9 | Ports of `ed` and `em` | the emulated original on scripted editing sessions. **Done 2026-11-02**: `src/mutos_ed/` is the original `ed` - v7's `ed.c` with two changes (`j` with one address joins it and the next, `m` onto itself does nothing), which the project's own `cc -O -s` compiles to the original binary byte for byte -, with the 8086's representation kept (16-bit addresses that wrap, the line table at 0x4104, the bss arrays one behind the other, the seek to the end of the input after an error); `src/mutos_em/` is the original `em`'s line mode - em has no source: 33 of ed's functions are in it unchanged, ten changed, the rest em's own (`tests/realbin/emmap.txt`) -, the screen mode `o` refused; `bin/ed` and `bin/em` in the root. `edcmp.py cases` and `edfuzz.py` against the emulated originals without a difference (`STATUS.md`); `tests/mutos_ed/check_editors.py` (in `make test`): the kernel's makefile through `mutos_make` - the machine and option targets and `depend`, which run `ed - Makefile <eddep`, and the `mch` rule (`cc -P`, `em mch.i <fakeawk`, `cc -c`), which gives the real `mch.s` and `mch.o`; 21 bugs of the originals (ED-01 to ED-08, EM-01 to EM-13) |

Steps 6 to 9 in this order were proposed on 2026-10-25 (the archiver
first, since `make`'s library makefiles need it; the editors last, since
no makefile does). After step 9: the corrections and the 80186/V30 code of
section 7 (decided 2026-11-08), then `make install` and the manual pages.

## 4. The switch `-1` (decided 2026-10-20)

`mutos_as -1` accepts the 80186 instructions `mutos_as` implements
(`pusha`, `popa`, `push` with an immediate, `insb`/`insw`/`outsb`/`outsw`,
`enter`, `leave`, `bound`, `imul` with an immediate, shifts and rotates
by an immediate count) - the set the 80186, 80188, NEC V20 and V30 share;
no NEC-only instruction. The name: the original reads its options letter
by letter (`-mv30` would be four unknown options to it, and to a later
port), `-1` is free in `as`, `cc` and `ld`, `cc` already has digit
options (`-2`, `-20`), and the 1 stands for the 80186.

Without `-1`, `mutos_as` does what the original does with such a line:
an identifier it does not know as an instruction starts an expression,
which assembles to a word referring to an undefined external of that
name (AS-02 in `docs/MUTOS_ORIGINAL_BUGS.md`); the link then fails with
`Undefined: pusha` - a C program cannot define a name without the
leading `_`. That keeps the differential fuzzer exact. The tests that use
those instructions (`tests/mutos_as/kernel_opt/mch_insw_outsw.s`,
`tests/mutos_as/v30_speculative/`) get `-1`. **Implemented 2026-10-25**
(step 5): `tests/mutos_as/run_goldens.sh` passes `-1` for `mch_insw_outsw.s`
(76/76 byte for byte; without `-1` 75/76, like the original), and
`tests/mutos_as/check_switches.sh` (in `make test`) pins that the seven
`v30_speculative` sources give their recorded objects (`v30_with_1.md5`) with
`-1` and not without it. The `mutos_as` forms of `push` and `imul` with an
immediate and of the shifts by a count are refused without `-1`, as the
original refuses them; its 80186 words are the unknown words of AS-02.

**The compiler's switch is `-1` too** (confirmed by the owner 2026-11-07):
should 80186/V30 code generation in `c1`/`c2` come (decision 8), it is
behind `-1` as well - the reasons above hold for `cc`. It comes (decision
15, 2026-11-08): `cc -1` passes the switch to `c1`, `c2` and `as`, and every
assembly with `-1` reports the 80186/V30 instructions it produced (decision
16, section 7). `-mv30`, the
working name of the documents of Milestones 4 and 5 from before
2026-10-20, was replaced by `-1` in them on 2026-11-07; only the comment of
`tests/mutos_cc/01_expr/04_shift.c` keeps it, since that file is the
source of real-hardware goldens.

## 5. What the originals showed (2026-10-20)

Details and evidence: `docs/devlog/originals_and_ports.md`.

- **`cc`**: link step `/bin/ld -X /lib/crt0.o [-o out] <files> -lc`;
  `-p`, `-2`, `-20` and `-f` change the startup file and libraries as
  listed in decision 6; `-f` runs `c0f`, `c1f`, `asf`; `as` is called
  with `-gC`; a compile runs `cpp` without `-P`; `-P` stops after `cpp`,
  with or without `-S`; unknown options go to `ld`; `-o name.c` or `-o
  name.o` compiles or uses that file and writes `name` (no "Would
  overwrite").
- **`ld`**: `-l` searches `/lib`, then `/usr/lib`, never `./`; `-r` and
  `-d` refused. It appends **6 bytes** of `eb fe` (three `jmp $`) to the
  text, whatever its length - `mutos_ld` rounded the text up to a multiple
  of 8 instead, the one difference on fourteen compiled goldens linked
  both ways (found in step 0; the port takes the original's rule). An
  archive newer than its `__.SYMDEF` time draws `out of date (warning)`,
  as from `mutos_ld`. Read in full for its port (2026-10-22): v7's `ld`
  with local MUTOS deltas (`tests/realbin/ldmap.txt`) - a new `bytrel()`
  for the byte-shifted relocation words, symbol types compared as words,
  `-n` the same as `-i`, the output written under a temporary name in the
  current directory and linked to `a.out` -, and eight bugs
  (`docs/MUTOS_ORIGINAL_BUGS.md`, LD-01 to LD-08).
- **`cc`, traced** (step 0): `Can't find <path>` on stdout and exit
  status 1 for a pass that is not there (`-f`'s `/lib/c0f`); `x.c:`
  before each source's passes when there are several; the `.o` of a
  single source removed after the link. Read in full for its port
  (2026-10-24): v7's `cc` with local MUTOS deltas
  (`tests/realbin/ccmap.txt`) - `-a` switches on the "Executing ..."
  messages, `-K<n>` gives `c1` its option `s<n>`, `-f` swaps the paths of
  `c0`, `c1`, `as` and adds `-lf`, `-o`'s `.c`/`.o` name read again as an
  input, one `mktemp()` for the temporary files -, and six bugs
  (`docs/MUTOS_ORIGINAL_BUGS.md`, CC-01 to CC-06).
- **`as`**: options `-o`, `-e`, `-g`, `-C`, `-l`; the manual's `-L`,
  `-W`, `-D`, `-T` (`docs/MUTOS1700_Assembler_as.pdf`; the manual page
  `mutos/mutos_man/as.1.d` names `-L` and `-W`) unknown to the binary;
  the 8087 set in its table, no 80186 mnemonic; an unknown mnemonic
  assembles silently as a word with an undefined external.
- **`c2`**: gives `kernel_opt/`'s nine kernel sources from the
  `11_kernel` `.s` goldens byte for byte. Read in full for its port
  (2026-10-25): v7's `c2` (`tests/realbin/c2map.txt`) with `c21.c`'s
  machine-dependent half rewritten for the 8086 and the MUTOS operand order
  `op dst,src`, `setcc()` and `reducelit()` gone, and new passes - register
  renaming, `mov`/`push` and `push`/`pop` pairs, index modes, `mov r,*0.`
  and `cmp r,*0.` simplified, long branches made short -, and seven bugs
  (`docs/MUTOS_ORIGINAL_BUGS.md`, C2-01 to C2-07).
- **`cpp`**: writes `# N "file"` lines without `-P`; the real `c0`'s
  messages depend on them (`e.c:3:` where the chain with the
  re-implementation said `4:`). Read in full for its port (2026-10-22):
  v7's `cpp` with local MUTOS deltas (`tests/realbin/cppmap.txt`), and
  thirteen bugs (`docs/MUTOS_ORIGINAL_BUGS.md`, CPP-01 to CPP-13; CPP-14 was
  found on 2026-10-25).

## 6. Open questions

- ~~What `as`'s `-g`, `-C` and `-l` do~~: measured 2026-10-25 (step 5). `-g`
  no effect; `-C` leaves the defined, non-global symbols starting with `L` out
  of the symbol table; `-l` writes a listing `x.L` beside the object (not
  implemented in `mutos_as`); `-e` keeps only the external symbols (not
  implemented either). (`cc`'s "Executing ..." messages: its option `-a`, read
  with step 3.)
- Whether `cc -o name.o` writing `name` is meant (listed as suspect in
  `docs/MUTOS_ORIGINAL_BUGS.md`).
- `c0f`, `c1f`, `asf` and `libf.a`: the compiler passes, the assembler
  and the library for 8087 (K 1810 WM 87) code, as `cc.1.d` and
  `as.1.d` in `mutos/mutos_man/` say (2026-10-25; until then only
  presumed). Not on hand; `-f` fails until they are.
- How `mutos_make` runs its recipes: the original hands each line to
  `/bin/sh -c`; on the host that can only be the host's shell, with
  `$MUTOS_ROOT/bin` first in `PATH` so that `cc`, `as`, `ld`, `ar` in a
  recipe are the MUTOS tools - **settled with step 7 (2026-11-02)**: so it
  is; `mk_setpath()` puts `$MUTOS_ROOT/bin` in front of the `PATH` of the
  commands. An absolute path in a recipe (`/lib/c0`) is the host's and cannot
  be redirected (decision 10).
- Where the new tools sit in the MUTOS file system (`/bin` or
  `/usr/bin`), and so in the root: to be read from the binaries that
  name them - settled for the editors with step 9: `bin/ed` and `bin/em`
  (the kernel's makefile calls them through the `PATH`; they name only
  `/bin/sh`, `/tmp/eXXXXX`, `/usr/lib/makekey`, `/lib/makekey` and em's
  `/usr/lib/emhelp/`). (`make` names `/bin/sh`, and libc's
  `execvp` in it the search path `:/bin:/usr/bin` for a `PATH` not set.)
  Settled for `ar` and `ranlib` with step 6: `bin/ar` and `bin/ranlib`, next
  to `as`, `ld` and `cc` - `ranlib` names only "ar" (`ar rlb %s %s %s`,
  `ar rl %s %s`, through `/bin/sh -c`, so the shell's search path finds it).
  Settled for `nm`, `size`, `strip`, `tsort` and `lorder` with step 8: `bin/`
  too - the originals are single files that name no other
  tool but `lorder`, which calls `nm`, `sed`, `sort` and `join` through the `PATH`.
- `lorder` is a shell script: its port is the script, run by the host's
  shell with the MUTOS `nm` - **settled with step 8 (2026-11-02)**: so it is;
  `src/mutos_lorder/mutos_lorder.in` is the original's text with `nm` named
  as `$MUTOS_ROOT/bin/nm`, and `sed`, `sort` and `join` the host's, in the C locale.
- ~~What `ed` and `em` need of the terminal (`sgtty`, `ioctl`) under the
  emulator~~ - **read with step 9 (2026-11-02)**: `ed` only for `-x`/`x`
  (`gtty()`, "?Input not tty" under the emulator); `em` asks `ioctl()` before
  every read of the input (128 bytes from a terminal, else one), for its
  log (only with a terminal), for the lines and columns of `"`, `&`, `%`
  (bytes 10 and 11 of MUTOS's `sgttyb`; 20 and 80 without a terminal) and for
  the screen mode, which the port refuses. The emulator's `ioctl()` is
  always "not a tty": the terminal's paths are checked by reading.
- The names of the mode's switch and of its environment variable
  (decision 13): proposed with step F0, for the project owner to confirm.
  The switch must be free in `cc` (whose unknown options go to `ld`), `ld`,
  `as`, `c0` and `c1`.
- Whether the kernel's `conf/arith.a` (from the owner's kernel tree; its
  long helpers are likely the code of `libc.a`'s) gets the corrections of
  decision 14 too: compare its members with `libc.a`'s after F3.

## 7. Corrections and 80186/V30 code (decided 2026-11-08)

With steps 0 to 9 done and the kernel built byte for byte (2026-11-07), the
project owner weighed two directions: correcting the known bugs of the
originals (`docs/MUTOS_ORIGINAL_BUGS.md`), and 80186/V30 code generation in
`c1`/`c2`. His first concern is code that is wrong, or existing code broken
little by little by special cases; the code base is to stay clean and
testable. Claude's analysis and recommendation of that day are the basis of
this section.

### 7.1 Decisions (2026-11-08)

13. **The bugs of the originals are corrected, in a corrected mode that
    becomes the default at the end.** While the corrections are made, the
    default stays the original's behaviour and a switch chooses the
    corrected mode. Once stage F is done (7.3), one change makes the
    corrected mode the default; the original's behaviour stays selectable.
    Besides the switch, an environment variable chooses the mode, either way
    - for makefiles whose command lines cannot carry a switch: the corpus's
    `Makefile.mutos` files, and the kernel's makefile, whose `c.c`,
    `symtab.c` and `mch` rules do not use `$(CFLAGS)` and whose link runs
    `ld` itself. So the kernel can be built in the corrected mode before the
    change, and in the original mode after it. From the change on, the
    checks against the goldens and the emulated originals run in the
    original mode. The mode reaches `c0`, `c1`, `as` (its floating constants,
    decision 14) and `ld` (its own corrections, and which `libc.a` `-lc`
    means); `c2` has no correction planned and gets `-1` only. Names:
    section 6.
14. **The original `libc.a` stays; the corrected mode links a corrected
    copy** (decided 2026-11-08; the same day's first decision, `libc.a`
    unchanged, was revised after Claude's recommendation). The original
    archive stays untouched where it is (`tests/mutos1700_libc/`) and is
    the library of the original mode. The copy differs only in
    members whose routines are measured to compute wrongly in ordinary
    programs: the runtime that compiled code calls without the programmer
    naming it (step F3 lists its fourteen members) - L-04 (`aldiv`,
    `alrem`) and L-05 (`dmath.o`'s `dmul`) so far, and what F3 finds. No
    general revision of the library: its other members are not examined.
    Each corrected member keeps its interface exactly - its symbols, the
    private calling convention of `docs/MUTOS_C_ABI.md` (section 1.8 for the
    long helpers: `bx` saved as well, the result written through the
    pointer and left in DX:AX), the registers it preserves, no new external
    reference - so that the copy is an ordinary MUTOS archive for the
    original `cc` and `ld` on the hardware as well, and objects compiled by
    the original compiler link with it. It stays 8086 code (no `-1`): it
    must run on every A7100 and A7150. Where `c1` and `as` re-enact a
    routine of the library for a constant (`c1`'s `itod` and `aldiv`,
    `fltconst.c`'s `atof` with `dmul`, `ldexp`), the corrected mode re-enacts
    the corrected routine, checked against the corrected library under the
    emulator - so a program computes the same whether a constant is folded
    or computed at run time, in either mode. Limits: the original `c1` and
    `as` on the hardware carry their own copies of the old routines, so a
    program compiled there folds constants the old way; the kernel does
    not link `libc.a` but its own `conf/arith.a` (with `aldiv`, `alrem`,
    `ldiv` ...), which this decision does not touch.
15. **`c1` and `c2` generate 80186/V30 code behind `-1`** (replaces decision
    8). The target is the project owner's Robotron A7100, whose 8086 he has
    replaced by a NEC V30 (and to which he has added an IDE interface): the
    machine for the hardware checks of stage V. The goal is faster code. The
    rules are a closed list of three (7.4); nothing is added without a
    measured gain. The frame layout and the link compatibility with the
    original `libc.a` and `crt0.o` stay as `docs/devlog/m5_c2_v30.md` settled
    them.
16. **`-1` reports what it produced**: an assembly with `-1` prints one line
    to the standard error - how many 80186/V30 instructions the object holds,
    by kind (`push` of a constant, shift by a constant, `imul` by a
    constant, and any other form `-1` accepts). `mutos_as` counts them as it
    encodes them, so the count is the object's, whichever pass produced the
    instruction: `cc -1` prints a line for each source it assembles (after
    `cc`'s own `x.c:` line when there are several), hand-written assembler is
    counted too, and `cc -S -1`, which assembles nothing, prints none.

### 7.2 How the work is kept safe

- **The default stays the original until decision 13's change.** Every
  existing check - the goldens, the emulated originals, the fuzzers, the
  kernel built byte for byte - runs as it does today. That is what keeps
  the existing code from being damaged: a change that reaches the default
  fails them.
- **A closed list of rules.** Each correction (named by its bug, C1-03 ..)
  and each V30 rule (V30-PUSH, V30-SHIFT, V30-IMUL) is in a source file of
  its own, reached through a one-line hook in the ported code, guarded by
  its switch, and delivered as a patch of its own. A check in the style of
  `scripts/check_docs.py` compares the hooks with the list and the
  documents. Taking a rule out is deleting its file and its hook.
- **Every difference accounted for.** A developer option makes `c1`, `c2`,
  `as` and `ld` log each rule they apply (its name and the source line). A
  checker compiles the corpus, the kernel's sources and fuzz programs with
  and without the switch: every byte that differs must belong to a logged
  rule, or the check fails. A rule cannot have a side effect unnoticed. The
  same for the library: the corrected copy differs from the original archive
  only in the members replaced and in `__.SYMDEF`.
- **Semantics instead of bytes where no golden can exist.** For a
  correction, C's semantics on a 16-bit `int` is the reference:
  `tests/mutos_cc/fuzz/fuzz_c.py` computes it and `x86sim.py` executes the
  generated code, modelling the flags and stopping on a flag the 8086 leaves
  undefined. For `-1`, the same program compiled without `-1` is the
  reference: the V30 computes as the 8086 (shifts by `cl` not masked, a
  quotient of -32768 a divide error - `docs/devlog/m2_as.md`, "CPU
  reference"), so both must give the same values. `tests/realbin/mutosemu.py`
  runs whole programs with the original `libc.a` or its corrected copy (Unicorn emulates an 80386
  and masks shift counts; the V30 rules use counts 3 to 15 only, where that
  makes no difference). `x86sim.py` stands in for the runtime routines by
  C's semantics (its `aldiv` computes C's quotient): it checks the compiler,
  not the library. The library is checked by running its own code under
  `mutosemu.py` against Python's arithmetic.
- **The hardware for acceptance**, not for every change: probes of each
  80186/V30 form with edge values, a program per stage that prints its
  values (compared on the host, not by the compiler under test), and the
  kernel, built in the corrected mode and with `-1`, booted from a test
  disk. The owner's machine has a V30: it shows what the V30 does, not what
  an 8086 would. What keeps code built without `-1` 8086 code is `mutos_as`,
  which refuses the 80186 forms without `-1`, and `x86sim.py`, which stops on
  a flag the 8086 leaves undefined (the two processors may leave different
  values there). A check on an unmodified A7100 or A7150, should one be at
  hand, would add to it.

### 7.3 Stage F: the corrections

| Step | What |
|---|---|
| F0 | Infrastructure, shared with stage V: `cc` passes the mode to `c0`, `c1`, `as` and `ld` and `-1` to `c1`, `c2` and `as` (today an unknown option such as `-1` goes to `ld`), the environment variable read where the switch is (decision 13); the rule log and the accounting checker (7.2); `fuzz_c.py` generates longs; `x86sim.py` executes the three V30 forms (with the NEC manual's flag rules) and counts V30 clocks; `mutos_as`'s report (decision 16). Hooks at the known bug sites in `c1` and `ld` report, in a diagnostic mode, when the original's wrong path is taken - output unchanged -, run over the corpus and the kernel's sources: which bugs real code meets. Nothing the tools write changes. |
| F1 | The `c1` bugs that make wrong code silently: C1-03 (`x = l++` loses the carry), C1-06 (`l <<= i` on a local long, `i == 0`, jumps into an instruction), C1-02 (`e * 0`, `e & 0` lose `e`'s side effects), C1-01 (a branch on stale flags after `not` and `mov`; with C1-02 about 0.9% of `fuzz_c.py`'s programs) - the smallest change first. |
| F2 | `ld`: LD-06 (a warning clears an earlier error: exit status 0 and an executable), LD-08 (a text of 65,530 bytes or more wraps). |
| F3 | Measure the runtime compiled code calls: the routines `c1`'s code tables call (in the corpus's and the kernel's code: `fldd`, `fstdp`, `faddd`, `itof`, `ftoi`, `fmuld`, `fcmp`, `lmul`, `ldiv`, `lrem`, `aldiv` ...), fourteen members of `libc.a` - `aldiv.o`, `almul.o`, `alrem.o`, `ldiv.o`, `lmul.o`, `lrem.o`, `chkstk.o`, `convert.o` (`itof`, `ftoi`, `itod`, `dtoi`), `lconvert.o` (`ltof`, `ftol`), `dmath.o` (`dadd`, `dsub`, `dmul`, `ddiv`), `stacks.o`, `stkmath.o`, `singles.o`, `doubles.o` -, which in turn pull in `fperr.o` (`__ovfl`, `__div0`: the SIGFPE), `cret.o`, `atof.o` and `fltpr.o` (through `__fltu3`, `__fltu2`). Measured so far: `aldiv`/`alrem` (L-04), `dmul` (L-05) and, through `atof`, `ldexp` (L-06). Each entry point under `mutosemu.py` against Python's arithmetic (exact rationals for the floating ones), on edge values and many random ones, as `tests/mutos_as/float_coverage/libcatof.py` does for `atof`. Findings into `docs/MUTOS_ORIGINAL_BUGS.md`. |
| F3b | The corrected copy of `libc.a` (decision 14), member by member, for the findings of F3 that compute wrongly in ordinary programs: first a source reconstructed from the original member that `mutos_as` assembles to that member byte for byte (as `tests/mutos_as/libc_recon/ldexp.s` is for `ldexp.o`) - the proof of the reconstruction -, then the correction as a small change of that source (L-05 is one displacement, `2[di]` for `4[di]`). The sources in a directory of their own; the archive built from the original by replacing those members in place (`mutos_ar`, `mutos_ranlib`), so that order and every other member stay. Checked: each entry point exhaustively at the edges and on many random values against Python; the interface by linking the corpus programs the original compiler made (the goldens) with the copy and running them under `mutosemu.py` - the same values but where a bug is corrected; the `c1`/`as` re-enactments against the copy; on the hardware, a program linked by hand with `ld` against the copy before it replaces anything. Which library `ld` takes for `-lc` in which mode, and where the copy lives in the root, is settled with this step. |
| F4 | As the owner chooses: what the ports refuse or the original reports today - C1-07 first (a long constant shifted by a constant: common in real code), then C1-11, C1-09, C1-08, C1-04, C1-05, C1-10, and `c0`'s crashes. |
| F5 | The change of default (decision 13), in one change: the corrected mode, with the corrected `libc.a`, is what `cc` and `ld` do without a switch; the checks against the originals run in the original mode; the kernel built in the corrected mode, its differences from the original accounted for, booted on the hardware from a test disk. |

Not planned: the bugs of `cpp` and `c2` that need exotic input (CPP-12,
CPP-13, C2-04, ...); those of `ar`, `ranlib`, `make`, `nm`, `size`,
`strip`, `tsort`, `lorder`, `ed` and `em` (refused or harmless - and a
correction to `ed` or `em` would change what the kernel makefile's scripts
do); AS-03 to AS-14, which `mutos_as`, a re-implementation, does not
reproduce; the members of `libc.a` outside the runtime of F3, and the
kernel's `arith.a` (decision 14).

Each correction changes its entry in `docs/MUTOS_ORIGINAL_BUGS.md` ("This
project": corrected in the corrected mode) in the same change.

### 7.4 Stage V: 80186/V30 code

| Step | Rule | What |
|---|---|---|
| V0 | - | Part of F0. With `-1` passed through and no rule yet, `c1`'s and `c2`'s output with and without `-1` is the same. |
| V1 | V30-PUSH | In `c2`: `pushopt()` makes `mov r,x` / `push r` into `push x` already, but not for a constant `x` (one condition, `src/mutos_cc/c2port/c21.c`), since the 8086 cannot push one. Under `-1` the condition goes. The test that `r` is not read again is the original's: `regmask()` follows the branches and takes an op it does not know as reading and writing every register. `c2` counts `push *n.` as 4 bytes (it is 2 or 3): branch lengths stay safe. |
| V2 | V30-SHIFT | In `c1`: a shift by a constant count of 3 to 15 as `sal r,*n.` (and `sar`, `shr`), not `mov cx,*n.` / `sal r,cl` - the same result and the same flags, CX not loaded. A row of `c1`'s code tables, used only under `-1`. `c2` counts the form as 4 bytes (it is 3). |
| V3 | V30-IMUL | In `c1`, last: `imul ax,*n.` (or the form with a memory operand) for `mov cx,*n.` / `imul cx`. First `c2` must learn the form's length: `isreg()` reads only an operand's first two characters, so `c2` takes `imul ax,*30.` for a 2-byte instruction (it is 3 or 4) and could make a branch too short to reach. |
| V4 | - | On the V30: probes of each form with edge values, a program per rule that prints its values, the kernel built with `make CFLAGS="-c -O -K1024 -1"` and booted from a test disk; the gain measured (V30 clocks in `x86sim.py`, run times on the hardware). |

Then the list is closed. Left out, with the reasons:

- `enter`/`leave`: with the fixed frame layout no faster than the
  instructions they replace, and longer (`docs/devlog/m5_c2_v30.md`).
- `pusha`/`popa`: they save eight registers in their own order; a compiled
  function saves `bp`, `di` and `si` at fixed places. For hand-written
  assembler only.
- Two `sar r,*1` made into `sar r,*2`: slower on the V30 (2 clocks each
  against 7 + n) and the overflow flag undefined instead of 0 - `c1`
  branches on the flags of a shift's result.
- A pass after `c2`: `c2` keeps constants in registers. In the kernel's
  `amx.s`, after `mov cx,*30.` / `imul cx` it uses `cx` for 30 in two more
  `imul cx` and in an `add ax,cx` that `c1` wrote as `add ax,*30.`; rewriting
  the first pair afterwards would leave `cx` undefined there. 32 `imul cx` of
  the kernel's `c2` output have no `mov cx` right before them.
- NEC-only instructions: `-1` is the set the 80186 and the V30 share
  (section 4).

The gain, counted in the `c2` output of the kernel's 62 sources
(`tests/mutos_as/kernel_opt/`) - upper bounds, since whether the register
is used again was not checked; clocks from the NEC manual, V30, even
addresses:

| Today | With `-1` | Places | Gain each |
|---|---|---|---|
| `mov r,` constant / `push r` | `push` constant | 478 (389 of them 8-bit) | 1 or 2 bytes; `push imm8` 7 clocks for the `mov` and a `push` of 8 |
| `mov cx,*n.` / `sal r,cl` | `sal r,*n.` | 210 | 2 bytes and the `mov`; CX stays free |
| `mov cx,` constant / `imul cx` | `imul ax,` constant | 215 (203 of them 8-bit) | 1 or 2 bytes; 28-34 (8-bit) or 36-42 clocks for the `mov` and 41-47 |

Together at most 1,705 of the kernel's 50,496 bytes of text (3.4%).

A program compiled with `-1` must not run on an 8086: there 0x60-0x6F are
conditional jumps and 0xC0/0xC1 are `ret`, so it goes wrong without a
message. The manual pages will say so.

### 7.5 Order

F0 (with V0), F1, F2, F3, F3b, then V1 to V4, then F4 as the owner chooses,
then F5. Each step is a patch of its own, checked as every step before it
(`make test`, `make check-realbin`, `make check-docs`, a fresh clone).
