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

Last updated: 2026-10-22.

---

## Top-level build (verified 2026-10-22)

A repo-root `Makefile` builds every component - through each one's own
`src/mutos_<tool>/Makefile` (`mutos_ld`'s since its port, 2026-10-22), `mutos_ld`,
`mutos_as`, `mutos_cpp` and `mutos_c0`/`mutos_c1` - and
makes the MUTOS root `mutos_root/` (`make root`, `scripts/mutos_root.sh`: `lib/crt0.o`,
`lib/libc.a` at its `__.SYMDEF` time, the passes and tools as symbolic links into
`src/`, `usr/lib/`, `usr/include/`, and since 2026-10-22 `tmp/`; `make clean` removes
it). Every component is
compiled with `-DMUTOS_ROOT_DEFAULT='"<tree>/mutos_root"'`, the default of
`src/h/mutos_root.h`'s lookup (`$MUTOS_ROOT` overrides it); `mutos_cpp` and `mutos_ld`
use it since 2026-10-22 (the default include directory; `-lX`'s libraries and the
temporary file), the port of `cc` will. A
`.gitignore` covers `mutos_root/` and the build
products, scoped so that no reference file can fall under it.
`make test` runs every component's golden-diff suite from one place (`mutos_as`'s
`kernel_nonopt`/`kernel_opt`/`libc_recon`/`float_coverage` plus its `.float`/`.double`
checks - `check_floatdat.sh` and `float_coverage/fltmodel.py` - and the assembly of
every real compiler `.s` in `tests/mutos_cc`, `mutos_cpp`'s, the compiler's, and
`tests/mutos_root/check_root.py` on the root and its lookup).
`make check-realbin` and `make check-libcatof` need the Python module `unicorn` and
are not part of `make test`: the first checks the original binaries and the ports of
`c0`, `c1`, `cpp` and `ld` against the emulated originals (and `mutos_ld`'s extension
`-r`/`-d`), the original `cc` traced through its
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
`make check-libcatof`, `make check-docs`.

Not built yet: the driver `mutos_cc` and `mutos_c2` (their links in `mutos_root/`
dangle until they are) - see [`docs/TOOLCHAIN_PLAN.md`](docs/TOOLCHAIN_PLAN.md),
steps 3 and 4.

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
byte-identical. "Provisional" because, per this project's core rule, no milestone is
ever declared permanently closed. Measured against the original `as` (2026-10-20)
it differs in its options and in what it accepts beyond the corpus (below); the
plan (`docs/TOOLCHAIN_PLAN.md`, decision 9) is not to port it now: accept `-g`/`-C`,
put the 80186/V30 instructions behind `-1`, and measure the rest with a
differential fuzzer before deciding on a port.

`mutos_as` (`src/mutos_as/`: `mutos_as.h`, `lexer.c`, `parser.c`, `pass2.c`,
`symtab.c`, `encode.c`, `objwrite.c`, `fltconst.c`, `assemble.c`; secondary tools
`main.c` (→ `parse_dump`) and `classify_test.c`) is a two-pass cross-assembler
producing real MUTOS `a.out` relocatable object files for the 8086 (and the 80186
instructions listed under "`mutos_as` opcode coverage").

### Verified 2026-10-20

- **Clean rebuild** (`make clean && make all`), zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **`-g`/`-C`**: `-gC`, `-g -C` and `-Cg -ox.o` give the object of no option
  (`10_integ/01_wordcount.s.golden`); `-gx` and `-L` are still the usage error.
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
  letter as the original reads them - and ignores them (plan decision 9): the
  original `cc`'s `as -gC -o x.o tmp` works. Every other option is still its usage
  error.
- **Output** (checked under the emulator, 2026-10-20): `mutos_as` writes a summary
  line (`x.s: text=... -> x.o`) to stderr on every run, the original nothing; it
  makes an object without undefined symbols executable (0755), the original leaves
  it as `creat` made it (0666 less the umask). For the driver (step 3) - which would
  show the line on every compile - and the differential fuzzer (step 5).
- **Mnemonics.** The original's table has the whole 8087 set (not needed here) and
  no 80186 mnemonic; an unknown mnemonic assembles silently to a word referring to an
  undefined external of that name (AS-02). Spellings `mutos_as` accepts that the
  original's table does not have: `scas`/`scasb`, `movsw`, `inb`/`outb`, `bloss`
  (see "Untested / unconfirmed opcodes" below).
- **Exit status**: the original exits with 2 on syntax errors and 4 on the floating
  overflow abort; `mutos_as` with 1 for both - a documented difference no test
  depends on.

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
- CLI: `-o output` (also `-ooutput`), `-W` (suppress diagnostics), `-v`, `-g` and `-C` (accepted, no
  effect - the original `cc` passes `-gC`), multi-file concatenation, stdin
  fallback, exec-bit on success, `asf` argv[0] alias. `-L` (control 'L'-prefixed
  compiler-internal label output) was implemented, found to break golden parity, and
  was **deliberately removed** per explicit prior user decision.
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
- **The original's bugs**: CPP-01 to CPP-13 in `docs/MUTOS_ORIGINAL_BUGS.md`, each with
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

## Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler) [CURRENT FOCUS]

**Status: the two compiler passes are done, the driver is not.** `mutos_c1` (since
2026-10-13) and `mutos_c0` (since 2026-10-18) are ports of the original `c1` and
`c0`, read from their binaries and checked against the originals running under an
8086 emulator. Every golden of `tests/mutos_cc` is reproduced at every stage
(`.i`, `.1`, `.2`, `.s`, messages and exit status): the 62-file construct corpus,
the nine real kernel files of `11_kernel/` and the 61 `fltprobe/` probes of fourteen
rounds - 132 files, `fltprobe/p19_open3` up to the real compiler's own invalid
output. The driver `mutos_cc` is not written; it is next, now that the ports of `cpp`
(Milestone 3) and `ld` (Milestone 1) are done (`docs/TOOLCHAIN_PLAN.md`, steps 1 to 3).
`src/mutos_cc/README.md` describes
both ports; the records of the hand-written `mutos_c0`/`mutos_c1` before them are in
[`docs/history/status_m4_compiler.md`](docs/history/status_m4_compiler.md).

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
hardware (`mutos_bin/`, commit `e8c7516`; see its `README.md`). They run on
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
   (CPP-01 to CPP-13). Checked by `tests/realbin/cppcmp.py` and `cppfuzz.py`.
2. ~~**Port of `ld`**~~ - **done 2026-10-22** (Milestone 1): `mutos_ld` is the
   original `ld`, read from its binary - `-l` under the root, the six bytes of
   "jmp $", the byte-shifted relocations, its bugs (LD-01 to LD-08); `-r`/`-d` kept as
   an extension. Checked by `tests/realbin/ldcmp.py` and `ldfuzz.py` (the emulated
   original in place of the missing `myhello`/`idhello` goldens) and
   `tests/mutos_ld/check_rflag.py`.
3. **The `mutos_cc` driver**, a port of the original `cc` - not of `v7/cc/cc.c`, from
   which it differs (`as -gC`, `-f`, `-o name.o`, its messages; see the plan's
   section 5). The `-P`/`-S` question `CLAUDE.md` kept open is answered by the
   original: `-P` stops after `cpp`, with or without `-S`; the port does the same.
   Its oracle is `cctrace.py`. `mutos_as`'s summary line on stderr (Milestone 2)
   must not reach the driver's output. **Next.**
4. **Port of `c2`** (Milestone 5), 8086 only.
5. **`mutos_as`**: differential fuzzer against the original, the `-1` switch for the
   80186 instructions; then the decision on a port.
6. **MUTOS makefiles**: ports of `ar` and `ranlib`, the `mutos_make` wrapper.

Smaller, not on the path: `tests/mutos_cc/fuzz/fuzz_c.py`'s generator still keeps
the limits it had for `c1_gen.c`'s sake (no constant divisor, no `x = y;`, simple
right operands, small frames); the ports refuse none of them, and
`tests/realbin/c1fuzz.py`'s generator covers those shapes against the original.

---

## Milestone 5 — Optimizer (`c2`) & NEC V30 (`-mv30`)

**Status: NOT STARTED - `mutos_c2` is to be a port of the original `c2`, for the
8086** (`docs/TOOLCHAIN_PLAN.md`, decision 7 and step 4). Its oracle is ready: the
original `c2` runs under `tests/realbin/mutosemu.py`, and on the nine `.s` goldens of
`tests/mutos_cc/11_kernel` it gives `tests/mutos_as/kernel_opt/`'s `delay.s`,
`prim.s`, `mem.s`, `pipe.s`, `nami.s`, `fio.s`, `v24.s`, `tty.s` and `amx.s` byte for
byte (verified 2026-10-20) - the optimized kernel sources are its output, so the
port has nine real-hardware goldens besides the emulated original. The whole
original chain gives them too: `cc -O -S` on the nine `11_kernel` sources, the
traced original `cc` running the original passes (`tests/realbin/cctrace.py
goldens`, 9/9, verified 2026-10-20).

**80186/V30 code generation** in `mutos_c1` and `mutos_c2` is deferred: the decision
whether and how it comes is taken once the 8086 toolchain works completely
(decision 8). One design question was settled ahead of it and stands: code built
for the V30 must stay link-compatible with the real, unmodified `libc.a`/`crt0.o` -
no separate runtime is planned, so the ABI-visible frame layout from Milestone 4's
ABI research can never change for it. Under that constraint, real NEC
µPD70116 (V30) timing data (`docs/V20_V30_Users_Manual_Oct86.pdf` - NEC calls
`ENTER`/`LEAVE` `PREPARE`/`DISPOSE`) shows the only layout-compatible use of
`PREPARE`/`DISPOSE` is an exact cycle-for-cycle tie against the discrete-instruction
prologue/epilogue, with a small code-size regression - so not those; `PUSHA`/`POPA`
and multi-bit shift-by-immediate remain ABI-safe candidates. Full numbers and
derivation in `docs/devlog/m5_c2_v30.md`.

---

## Further tools — `ar`, `ranlib`, MUTOS makefiles

**Status: NOT STARTED** (`docs/TOOLCHAIN_PLAN.md`, decision 10 and step 6).
MUTOS-compatible makefiles are to run on the host through a wrapper `mutos_make`
around the host's `make` (built-in rules off, the MUTOS `make`'s own rules and
macros, `$MUTOS_ROOT/bin` first in `PATH`); `ar` and `ranlib` are to be ported from
their binaries. Those binaries - `make`, `ar`, `ranlib`, and `lorder`, `tsort`, `nm`,
`size`, `strip` as needed - and the MUTOS `make` manual page are to come from the
hardware (`mutos_bin/`, `docs/mutos_man/`). What the real `make` is known to do and
not do is in `docs/devlog/m4_foundations.md`, "MUTOS 1700 host-tooling findings".

---

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

**Planned (2026-10-20, `docs/TOOLCHAIN_PLAN.md` section 4): behind the switch
`-1`.** Today `mutos_as` always accepts these instructions; the original `as` has
none of them and assembles each such word, silently, as a reference to an undefined
external (AS-02). With step 5 of the plan they need `-1`, and without it
`mutos_as` does what the original does; `mch_insw_outsw.s` and
`tests/mutos_as/v30_speculative/` (seven test files covering the twelve rows
without a real sample, cross-checked with NASM and objdump - its `README.md`; the
session that added them is recorded in
[`docs/history/status_m2_as_v30.md`](docs/history/status_m2_as_v30.md)) get `-1`.

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

Of these spellings, `bloss`, `scas`/`scasb`, `movsw` and `inb`/`outb` are not in the
original `as`'s mnemonic table (read from its binary, 2026-10-20): the original
assembles each as a word referring to an undefined external. The differential
fuzzer (`docs/TOOLCHAIN_PLAN.md`, step 5) decides what to do with them.

### 8086 opcodes still missing

Identified directly from `MUTOS1700_Assembler_as.pdf` Anlage A/B (the K1810WM86 base-chip
manual) — these are documented there but have no dispatch entry in `encode.c`. Measured
against the original binary instead (its mnemonic table, 2026-10-20), `esc`/`escb` and
the `lods`/`stos` spelling are not missing - the original has neither; whether it
encodes `ret N`, `int 3`, `xchg reg,mem` and Group2 with a direct address is for the
differential fuzzer:

- **`esc`/`escb`** (`0xD8`–`0xDF`, coprocessor escape) — Anlage A documents
  `esc 2 U(063) A+W`; no implementation exists.
- **`ret`/`reti` with an immediate operand** (`0xC2 iw` / `0xCA iw`, "return and pop N
  bytes") — Anlage A lists this as a separate row from the bare 0-operand form; only
  the bare form (`0xC3`/`0xCB`) is implemented.
- **`int 3` dedicated 1-byte encoding** (`0xCC`) — Anlage A lists `int 1 3` separately
  from the general `int 1 * U` form; the current code always emits the general 2-byte
  `CD ib` form, even for a literal `3`.
- **`xchg` general `reg,mem` ModRM form** (`0x86`/`0x87` with a memory operand) — only
  register/register and the AX-shortcut forms are implemented.
- **Shift/rotate (Group2) with a DIRECT (bare absolute-address) destination** — Group2
  only accepts REGISTER or INDIRECT destinations; Group1/Group3/TEST all support
  DIRECT, making this a Group2-specific asymmetry.
- `lods`/`stos` under their literal Anlage-A spelling — functionally reachable today
  via the confirmed-real, toolchain-diverging names `lodb`/`lodw`/`stob`/`stow`, but
  the documented spelling itself is not accepted.

Already-known, deliberately-not-implemented gap (unchanged from prior sessions):

- **CALLI/JMPI direct `d:s`** (segment:offset literal) form — needs a new
  colon-separated operand syntax neither the lexer nor the operand classifier parse;
  zero real corpus evidence of use. The indirect (`@reg`/`@disp(reg)`) form of both is
  fully implemented.

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
   1,572 links of the compiled goldens and the fuzzing without a difference; the
   goldens would still be welcome as real-hardware evidence (a link on the hardware
   to compare with the emulated original's).
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
3. `esc`/`escb`, `ret`/`reti` with an immediate, and the dedicated `int 3` encoding
   are the three remaining 8086-level gaps with enough information in
   `MUTOS1700_Assembler_as.pdf` alone to implement without further real-hardware evidence.
   The original binary has no `esc`/`escb` (see the opcode coverage); what it does with
   `ret N` and `int 3` is for the differential fuzzer (plan step 5).
4. Finish Milestone 4 (`mutos_cc`/`mutos_c0`/`mutos_c1`, 62/62 of the corpus
   byte-exact, `tests/mutos_cc/11_kernel` 9/9): `mutos_c1` and `mutos_c0`
   are ports of the real `c1` and `c0` (`docs/C1_PORT_PLAN.md`,
   `docs/C0_PORT_PLAN.md`); the `mutos_cc` driver is next (the ports of `cpp` and
   `ld` are done), as a port of the original `cc` - see "Next up" and
   `docs/TOOLCHAIN_PLAN.md`.
5. ~~`mutos_cpp`'s documented simplifications~~ - **closed 2026-10-22**: the port of
   `cpp` does what the original does in each (a formal inside a string of a macro body,
   a macro with parameters named without `(` - CPP-11 -, a macro call across lines -
   CPP-10), checked against the emulated original; the old list is in
   [`docs/history/status_m3_cpp.md`](docs/history/status_m3_cpp.md).
6. `mutos_as`'s `-L` deviation (see `CLAUDE.md` rule 3): **the "-L was used
   for the kernel build" hypothesis is now RULED OUT by real hardware**
   (2026-09-28). `tests/mutos_as/kernel_opt/Makefile.mutos`/
   `kernel_nonopt/Makefile.mutos` were written to assemble every real kernel
   `*.s` with the real `as`, both with and without `-L`, but the first
   attempt already settled the flag question without needing a full
   diff: `as -L -o v30ide.oL v30ide.s` printed `Unknown option L ignored`
   and proceeded — **the real MUTOS 1700 `as` binary does not implement
   `-L` at all**, despite `docs/MUTOS1700_Assembler_as.pdf` sect. 3.1
   documenting it (a manual/binary mismatch, not a MUTOS 1700
   toolchain-version question this project can resolve). Since the flag
   is silently ignored, "-L" and no-"-L" invocations are necessarily
   byte-identical on this hardware, so no `.oL` goldens were generated —
   there would be nothing to compare. This still leaves the original
   discrepancy genuinely open (none of `libc.a`'s 167 objects — compiled
   C such as `atof.o`/`ecvt.o` included — has an `L`-number label, while
   every kernel golden does, e.g. `malloc.o`: 14) but rules out a
   per-invocation flag as the cause: real `as` has exactly one behavior
   here, unconditionally. The next angle to try is source-level, not
   flag-level — whether `L`-labels are emitted by the *compiler* only
   under certain conditions (so kernel `.s` sources contain them in the
   text and libc.a's lost sources never did), not by the assembler's
   symbol-table policy; `mutos_as`'s own always-emit-`L`-labels behavior
   remains the pragmatic choice for golden parity either way (see
   `CLAUDE.md` rule 3 and `tests/mutos_as/README.md`).
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
   `.03` included), only the `L` labels (open item 6) keep them from being
   `libc_recon/` goldens.
