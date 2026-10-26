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

**Status (2026-10-26): steps 0 to 6 are done** (section 3). Step 5 -
the differential fuzzer for `mutos_as`, the switch `-1`, `-C` - measured the
re-implementation against the original; whether to port `mutos_as` is the
project owner's decision, with the measurements and a recommendation in
`STATUS.md` (Milestone 2). Step 6 - the ports of `ar` and `ranlib`, with
`bin/ar` and `bin/ranlib` in the root - was done on 2026-10-26. Step 7, the
port of `make`, is next. `STATUS.md`'s "Next up" follows this order.

## 1. Where things stand

| Tool | Today | Checked against |
|---|---|---|
| `mutos_cpp` | port of the original `cpp` (2026-10-22; until then a re-implementation without `# N "file"` lines or a default include directory) | the emulated original: every `.i` golden's source with and without `-P`, differential fuzzing |
| `mutos_c0` | port of the original `c0` (2026-10-18) | the emulated original: every golden, differential fuzzing |
| `mutos_c1` | port of the original `c1` (2026-10-13) | the emulated original at four depths, differential fuzzing |
| `mutos_c2` | port of the original `c2` (2026-10-25) | the emulated original: every `.s` golden three ways (`c2 in out`, `c2 - in out`, `c2 <in >out`), hand-written cases, differential fuzzing; `tests/mutos_as/kernel_opt/`'s nine real-hardware sources, also through `mutos_cc -O -S` |
| `mutos_as` | re-implementation (2026-10-25: measured against the original, step 5); `-g` (no effect), `-C` (leaves out the local `L` symbols) and `-1` (the 80186/V30 opcodes, off without it) as the original and the plan have them | 76 `.o` goldens, the floating conversion against `libc.a`'s own `atof`; the emulated original: every golden and compiler `.s`, differential fuzzing (`tests/realbin/ascmp.py`, `asfuzz.py`) |
| `mutos_ld` | port of the original `ld` (2026-10-22; until then a port of v7's `ld.c` with the MUTOS deltas known then), `-r`/`-d` an extension | the emulated original: every compiled golden linked with a matrix of options, hand-written cases, differential fuzzing (its own goldens `myhello`/`idhello` are missing from the tree); the extension by running programs linked through `-r` |
| `mutos_cc` | port of the original `cc` (2026-10-24) | the emulated original, its passes traced: a matrix of command lines, the corpus with the original passes run in both, the ported chain against the original chain and the goldens, differential fuzzing |
| `mutos_ar` | port of the original `ar` (2026-10-26) | the emulated original: hand-written command sequences, differential fuzzing (files and archives changed between the commands, damaged archives); libc.a rebuilt from its 167 objects |
| `mutos_ranlib` | port of the original `ranlib` (2026-10-26); runs `mutos_ar` through the root's `bin/ar` as the original runs `ar` through the shell | the emulated original `ranlib` and `ar`: hand-written cases, differential fuzzing; libc.a rebuilt from its 167 objects, `__.SYMDEF` included |
| `make`, `nm`, `size`, `strip`, `tsort`, `lorder`, `ed`, `em` | not ported yet (decision 10, steps 7 to 9); the originals in `mutos/mutos_bin/` since 2026-10-25 | - |

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
   decided then.
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
| 1 | Port of `cpp` | the emulated original on the 137 `.i` goldens with and without `-P`, differential fuzzing (macros, conditionals, includes, comments). **Done 2026-10-22**: `src/mutos_cpp/` is the original read from its binary, on an image of its data segment; `cppcmp.py goldens` 411/411 identical (with `-P`, without, into an output file), the differential fuzzing in `STATUS.md` (Milestone 3) without a difference; thirteen bugs of the original found (CPP-01 to CPP-13) |
| 2 | Port of `ld` | the emulated original: every compiled golden linked with `crt0.o` and `libc.a`, with `-i`, `-n`, `-s`, `-x`, `-X`, `-u`, `-e`, `-o`, archive search, messages and exit status; this replaces the missing `myhello`/`idhello` goldens. **Done 2026-10-22**: `src/mutos_ld/` is the original read from its binary, on an image of its data segment; `ldcmp.py goldens` 1,572/1,572 the same (131 goldens, twelve command lines each: messages, exit status, the files left), the hand-written cases and the differential fuzzing in `STATUS.md` (Milestone 1) without a difference; the extension `-r`/`-d` checked by running programs linked through it (`tests/mutos_ld/check_rflag.py`); eight bugs of the original found (LD-01 to LD-08) |
| 3 | Port of `cc` | the tracer: every pass's path and arguments, messages and exit status over a matrix of options, the original's against the port's; whole programs through the chain. **Done 2026-10-24**: `src/mutos_cc/ccport/` is the original read from its binary, on an image of its 64K segment (text, data, heap, `main()`'s frame); `cccmp.py` - the port's passes a stub that logs its arguments - 177 command lines traced (175 the same, 2 refused), the corpus with the original passes run in both 124/124, the ported chain against the original chain 124/124 (the same `a.out`) and against the goldens (`cc -S` 132/132, `cc -P` 132/132, `cc -O -S` 9/9 with the original `c2`), differential fuzzing in `STATUS.md` (Milestone 4) without a difference; six bugs of the original found (CC-01 to CC-06) |
| 4 | Port of `c2` | the emulated original on all 132 `.s` goldens and on `c1fuzz.py`'s programs; `tests/mutos_as/kernel_opt/`'s nine sources from `11_kernel` (the original gives them byte for byte). **Done 2026-10-25**: `src/mutos_cc/c2port/` is the original read from its binary, on an image of its data segment with libc.a's stdio in it; `c2cmp.py goldens` 396/396 the same (132 goldens three ways: into a file, with the statistics, through stdin and stdout), the nine `kernel_opt/` sources byte for byte - also through `mutos_cc -O -S` (`check_driver.py`, `cccmp.py goldens`) -, the hand-written cases and the differential fuzzing (the real compiler's output for `c1fuzz.py`'s and `fuzz_c.py`'s programs, assembly made for `c2`'s passes, both damaged) in `STATUS.md` (Milestone 5) without a difference; seven bugs of the original found (C2-01 to C2-07), two of them endless loops the port refuses |
| 5 | `mutos_as`: differential fuzzer, the `-1` switch | the emulated original; then the decision on a port. **Done 2026-10-25**: `tests/realbin/ascmp.py` (a comparator that sorts every run into a class) and `asfuzz.py` (differential fuzzing, five generators, reduced and clustered findings, the table of all 273 instruction words); `-1` and `-C` implemented (`tests/mutos_as/check_switches.sh`); the compiler's own output assembles to the same object as the original's in all 1,284 of the 1,409 compiled test programs the original accepts (the other 125 are the compiler's own invalid output) and in every `.s` of the tree it accepts; the fuzzer's findings that hand-written code could meet are fixed (list in `STATUS.md`), the rest measured and kept (the options `-e`/`-l`, the exit statuses, the messages, the 8087 words, leniency); eleven bugs of the original found (AS-04 to AS-14, besides AS-01 and AS-02 re-measured); the decision on a port is the project owner's - recommendation in `STATUS.md` ("Step 5 of the plan"): not now, after step 6 |
| 6 | Ports of `ar` and `ranlib`; `bin/` in the root for the new tools | the emulated original: archives made and changed with every key, `libc.a` rebuilt from its 167 objects against the original archive, `__.SYMDEF` included; differential fuzzing. **Done 2026-10-26**: `src/mutos_ar/` and `src/mutos_ranlib/` are the originals read from their binaries - none of the 33 functions of `ar.o` and the five of `ranlib.o` differs from v7's in what it does, the representation (26-byte headers, longs high word first, signed chars) and the libc.a underneath do (`perror`'s 41 texts, `mktemp`, `ctime`, the twenty stdio slots) -; `mutos_ranlib` starts `$MUTOS_ROOT/bin/ar`, the root's link to `mutos_ar`, where the original goes through `/bin/sh -c`; `bin/ar` and `bin/ranlib` in the root (`scripts/mutos_root.sh`, `check_root.py`); `arcmp.py cases` 366 runs (364 the same, 2 refused) and `ranlibcmp.py cases` 146 runs (139 the same, 6 refused, 1 endless), the differential fuzzing in `STATUS.md` (Milestone 6) without a difference; libc.a rebuilt from its 167 objects by `mutos_ar rc` and `mutos_ranlib` is the original archive byte for byte but for `__.SYMDEF`'s time (five seconds past the clock), group and mode - against the emulated originals (`ranlibcmp.py libc`) and in `make test` without an emulator (`tests/mutos_ar/check_libc.py`); twelve bugs of the original found (AR-01 to AR-05, RL-01 to RL-07) |
| 7 | Port of `make` | the emulated original, its `fork`/`exece`/`wait` faked as for `cc` and the commands it runs traced: the corpus's `Makefile.mutos` files, makefiles written for its rules, macros and options, differential fuzzing; then the corpus's makefiles through `mutos_make` and the ported tools against the goldens |
| 8 | Ports of `nm`, `size`, `strip`, `tsort`; `lorder` | the emulated original on every object and archive of the tree (the compiled goldens, `crt0.o`, `libc.a`) with each option; `lorder` and `tsort` on `libc.a`'s objects |
| 9 | Ports of `ed` and `em` | the emulated original on scripted editing sessions |

Steps 6 to 9 in this order were proposed on 2026-10-25 (the archiver
first, since `make`'s library makefiles need it; the editors last, since
no makefile does). Later: the 80186/V30 decision for `c1`/`c2`, `make
install`, the manual pages.

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
  thirteen bugs (`docs/MUTOS_ORIGINAL_BUGS.md`, CPP-01 to CPP-13).

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
  recipe are the MUTOS tools - to be settled with step 7.
- Where the new tools sit in the MUTOS file system (`/bin` or
  `/usr/bin`), and so in the root: to be read from the binaries that
  name them - steps 7 to 9. (`make` names `/bin/sh`, and libc's
  `execvp` in it the search path `:/bin:/usr/bin` for a `PATH` not set.)
  Settled for `ar` and `ranlib` with step 6: `bin/ar` and `bin/ranlib`, next
  to `as`, `ld` and `cc` - `ranlib` names only "ar" (`ar rlb %s %s %s`,
  `ar rl %s %s`, through `/bin/sh -c`, so the shell's search path finds it).
- `lorder` is a shell script: its port is the script, run by the host's
  shell with the MUTOS `nm` - to be settled with step 8.
- What `ed` and `em` need of the terminal (`sgtty`, `ioctl`) under the
  emulator: to be read with step 9.
