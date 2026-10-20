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

Last updated: 2026-10-20.

---

## Top-level build (verified 2026-10-20)

A repo-root `Makefile` builds every component — `mutos_ld` (compiled directly; it
has no sub-Makefile of its own yet) and, through each one's own
`src/mutos_<tool>/Makefile`, `mutos_as`, `mutos_cpp` and `mutos_c0`/`mutos_c1`.
`make test` runs every component's golden-diff suite from one place (`mutos_as`'s
`kernel_nonopt`/`kernel_opt`/`libc_recon`/`float_coverage` plus its `.float`/`.double`
checks - `check_floatdat.sh` and `float_coverage/fltmodel.py` - and the assembly of
every real compiler `.s` in `tests/mutos_cc`, `mutos_cpp`'s, and the compiler's).
`make check-realbin` and `make check-libcatof` need the Python module `unicorn` and
are not part of `make test`: the first checks the original binaries and the ports of
`c0` and `c1` against the emulated originals (Milestone 4), the second the
`.float`/`.double` conversion and `mutos_c1`'s constant texts against `libc.a`'s own
`atof`/`ecvt` (Milestones 2 and 4). `make check-docs` checks the Markdown files.

Verified this session: `make clean && make all` with zero warnings under
`-std=c11 -Wall -Wextra -Wpedantic`, then `make test` (results in the milestone
sections), `make check-realbin` and `make check-libcatof`.

Not built yet: the driver `mutos_cc`, `mutos_c2`, and the MUTOS root `mutos_root/`
(`crt0.o`, `libc.a` and the passes for the driver) - see
[`docs/TOOLCHAIN_PLAN.md`](docs/TOOLCHAIN_PLAN.md), step 0.

---

## Milestone 1 — `mutos_ld` (linker)

**Status: COMPLETE as a port of v7's `ld.c`** *(carried from prior session records -
its goldens are missing, see below)*; **to be ported from the original binary**
(`docs/TOOLCHAIN_PLAN.md`, decision 5 and step 2).

`mutos_ld.c` (`src/mutos_ld/`, ~1180 lines) reimplements the V7 `ld` linker with the
MUTOS-specific `a.out` deviations (see "Implementation summary" below), including
full archive (`.a` / `-l`) support.

### Verified 2026-10-20
- `mutos_ld.c` compiles cleanly (`cc -std=c11 -Wall -Wextra -Wpedantic -O2`), zero
  warnings.
- The reference archive is present and intact as `tests/mutos1700_libc/libc.a.base64.txt`
  (decodes to 72,178 bytes, 168 members: `__.SYMDEF` and the 167 objects).
- Linking assembled compiler goldens (`08_float`, `10_integ`, `04_funcs`) with the
  real `crt0.o` and `libc.a` works; with the archive's modification time newer
  than its `__.SYMDEF` time (any freshly decoded copy) `mutos_ld` warns
  `ld:libc.a(__.SYMDEF): out of date (warning)` and scans linearly - the output was
  the same as with the table on all 14 programs tried.
- **Against the original `ld`**, run under `tests/realbin/mutosemu.py` (first time):
  - `-lX` searches `/lib/libX.a`, then `/usr/lib/libX.a`, never the current
    directory. `mutos_ld` (`resolve_dash_l()`) searches `./libX.a`, then
    `/usr/lib/libX.a` - a deviation, removed by the port (`-l` then under
    `$MUTOS_ROOT`).
  - The original refuses `-r` and `-d` (`ld: -r and -d flags not supported`, exit
    status 4); `mutos_ld` implements both - kept as a documented extension.

### NOT verified this session
- **The golden reference binaries the "byte-for-byte identical" claim rests on
  (`myhello`/`idhello`, default-FMAGIC and `-i`/IMAGIC links) are not in this
  checkout**, nor is a `tests/mutos_ld/` directory. The
  linker's output has not been compared with real linked binaries since. Step 2 of
  the plan replaces those goldens with the emulated original `ld` as the oracle.

### Implementation summary (prior session records)
- Three archive code paths in `getfile()`: (1) plain archive, no `__.SYMDEF` → full
  linear scan; (2) up-to-date `__.SYMDEF` → fast `ldrand()`-driven symbol pull to a
  fixed point; (3) stale `__.SYMDEF` (archive mtime newer than the embedded ranlib
  timestamp — the common case for any freshly-uploaded archive) → warn + linear-scan
  fallback.
- MUTOS-specific `a.out`/relocation deviations from stock V7, applied throughout:
  1. Relocation word bit 15 = 1-byte target-shift flag (x86 is byte-oriented, unlike
     the word-aligned PDP-11 the original V7 `ld` targeted).
  2. DATA/BSS-relative values are pure segment-local offsets, not V7's "baked-in
     preceding segment size" convention.
  3. `R_EXT` relocation symbol-index field is 11 bits (bits 4–14), not 12.
  4. `nflag`/`iflag` 64-byte rounding is a separate zero-filled padding stage.
  5. `ar` archive header `long` fields use PDP-11 middle-endian encoding (high
     16-bit word first, each word itself little-endian).

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
  manual's `-L`, `-W`, `-D` and `-T` included. `mutos_as` has `-o`, `-W` and `-v`
  and rejects every other option with its usage message - so the original `cc`'s
  `as -gC` fails with it today.
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
- CLI: `-o output` (also `-ooutput`), `-W` (suppress diagnostics), `-v`, multi-file concatenation, stdin
  fallback, exec-bit on success, `asf` argv[0] alias. `-L` (control 'L'-prefixed
  compiler-internal label output) was implemented, found to break golden parity, and
  was **deliberately removed** per explicit prior user decision.
- Modified native-kernel `conf/Makefile` (`-S` instead of `-c`, retains `.s` files,
  copies output to `.o.golden`) — mechanism for generating more real hardware golden
  files from the kernel source tree in the future.

---

## Milestone 3 — `mutos_cpp` (C preprocessor)

**Status: COMPLETE for the real corpus; to be ported from the original binary**
(`docs/TOOLCHAIN_PLAN.md`, decision 4 and step 1).

`mutos_cpp` (`src/mutos_cpp/`: `mutos_cpp.h`, `util.c`, `source.c`, `macro.c`,
`ifexpr.c`, `directive.c`, `scan.c`, `main.c`) re-implements the observable behavior
of the real MUTOS 1700 / V7 "fast cpp" (`v7/cpp/cpp.c`, John F. Reiser, 1978; the
MUTOS binary is that program's SCCS version `cpp.c 1.6 12/12/86`) from scratch in
modern C11 — not a literal port of the original source. See `src/mutos_cpp/README.md`
for the full reverse-engineered behavioral specification (K&R-era language quirks,
output-line accounting rules, known simplifications) this implementation follows.

### Verified 2026-10-20

- **Clean build** (`make clean && make all`), zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **Full regression: 5/5 golden files byte-for-byte identical**
  (`tests/mutos_cpp/run_goldens.sh`), using the exact flags the golden references were
  generated with on real hardware (`cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE
  <name>.c`): `main.c`, `mch.c`, `sys.c`, `tty.c`, `v30ide.c`. Through the compiler
  goldens, `mutos_cpp` also gives all 132 `.i` goldens of `tests/mutos_cc`.
- **Against the original `cpp`** (`tests/realbin/`): without `-P` - the way the
  original `cc` runs it for every compile - the original writes `# N "file"` lines
  and the original `c0` takes its messages' file name and line number from them
  (`e.c:3: y undefined; func. main`); `mutos_cpp` writes none, so the same program
  through `mutos_cpp` and `mutos_c0` says `4: y undefined; func. main`. The port
  removes the difference; until then `mutos_cpp`'s output equals the original's
  only with `-P`.

### Carried from the session that completed it (not re-run this session)

- **AddressSanitizer + UBSan** (`-fsanitize=address,undefined`, `-O0 -g`): 0 errors
  across all 5 golden files.
- Two real bugs found and fixed in that session, both confirmed via rebuild +
  full-corpus diff (see "Bugs found in that session" below).

### Bugs found in that session

1. **Global (rather than per-source) pushback stack.** A character peeked (via
   `source_peekc`) while reading from one source — e.g. checking what follows a macro
   name — was pushed back onto a *single global* ungetc buffer. If that peek happened
   immediately before a macro expansion pushed a *new* source (the expansion's
   substituted text) onto the stack, the stale peeked character would incorrectly be
   served *before* the new source's own content once reading resumed, corrupting
   output (e.g. `x[NOFILE]` → `x[]20` instead of `x[20]`). Fixed by moving the
   pushback buffer into each `Source` frame, so a peeked character stays correctly
   scoped to whichever source was on top at the time of the peek.
2. **Comment-embedded newlines silently dropped inside a false `#ifdef` body.** The
   reference `cpp.c`'s comment-skipping loop calls `putc('\n', fout)` directly for
   each newline inside a `/* ... */` comment, **unconditionally** — bypassing the
   normal `flslvl`-gated output-suppression path entirely. This means a multi-line
   comment's *newlines* (not its other text) still reach the output even inside an
   otherwise-fully-suppressed false `#ifdef`/`#ifndef`/`#if` body. Confirmed against
   `tests/mutos_cpp/c/mch.c`'s `#ifdef M1834` blocks (M1834 undefined in this
   project's golden fixtures), which contain multi-line comments whose internal
   newlines do appear in the golden output. Fixed by making comment-embedded-newline
   output unconditional everywhere comments are skipped (`scan.c`, `macro.c`,
   `directive.c`), rather than gated on the enclosing context's active/inactive state.

### Output-line accounting rule (the hard part of this milestone)

The reference cpp preserves source line numbers by mapping (almost) every input line
to exactly one output line — directives and comments become blank lines, and a false
`#ifdef`/`#ifndef`/`#if` body outputs *nothing at all*, not even blank placeholders
(explicitly documented in `v7/cpp/README`'s "Stylistic choice" section). Reproducing
this exactly (rather than the more obvious "one line in, one line out always") took
real reverse-engineering against the golden files — see `src/mutos_cpp/directive.c`'s
file header comment for the precise rule (a directive line contributes a blank output
line iff the conditional-active state was true *immediately before* that directive's
own effect is applied), confirmed against `main.c`'s two `#ifdef MMU ... #endif MMU`
blocks (3 and 5 source lines respectively, both collapsing to exactly one blank golden
output line) and cross-checked against `mch.c`'s deeper, multi-level nesting.

### Known, documented simplifications (not exercised by the current corpus)

See `src/mutos_cpp/README.md`'s "Known, documented simplifications" section in full;
briefly: formal parameters embedded inside a string/char literal *within a macro's
own definition* are not substituted (the real cpp has a special quote-aware scan for
this); a function-like macro name not followed by `(` is left unexpanded rather than
expanded-with-empty-args-plus-a-warning as the reference does; a multi-line macro
*call*'s embedded newlines are captured into whichever actual argument they fall
within rather than independently emitted; and no default system include directory is
built in (only `-I` dirs and the including file's own directory are searched — every
golden fixture's includes resolve fine without one). None of these are exercised by
the real MUTOS kernel source this project validates against.

### Auxiliary deliverables

- `mutos_cpp.1` — English troff man page (to be rewritten at the end of the
  project, `CLAUDE.md`'s man-page rule).
- `src/mutos_cpp/README.md` — full behavioral specification and source-layout guide.
- `tests/mutos_cpp/run_goldens.sh` — batch golden-diff test runner.

---

## Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler) [CURRENT FOCUS]

**Status: the two compiler passes are done, the driver is not.** `mutos_c1` (since
2026-10-13) and `mutos_c0` (since 2026-10-18) are ports of the original `c1` and
`c0`, read from their binaries and checked against the originals running under an
8086 emulator. Every golden of `tests/mutos_cc` is reproduced at every stage
(`.i`, `.1`, `.2`, `.s`, messages and exit status): the 62-file construct corpus,
the nine real kernel files of `11_kernel/` and the 61 `fltprobe/` probes of fourteen
rounds - 132 files, `fltprobe/p19_open3` up to the real compiler's own invalid
output. The driver `mutos_cc` is not written; it comes after the ports of `cpp` and
`ld` (`docs/TOOLCHAIN_PLAN.md`, steps 1 to 3). `src/mutos_cc/README.md` describes
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
  port or not finished by the original, as specified.
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

0. **Infrastructure**: `tests/realbin/mutosemu.py` serves `fork`/`exece`/`wait`
   (faked), `fstat`, `chmod`, `umask` and `link`, and a tracer for the original `cc`
   joins `tests/realbin/`; `mutos_root/` (`lib/crt0.o`, `lib/libc.a` with its
   `__.SYMDEF` time, the passes as symbolic links) and the `MUTOS_ROOT` lookup; a
   `.gitignore` (there is none yet) for `mutos_root/` and the build products; `mutos_as`
   accepts `-g`/`-C`.
1. **Port of `cpp`** (Milestone 3): `# N "file"` lines, `/usr/include` under the
   root, no simplifications.
2. **Port of `ld`** (Milestone 1): `-l` under the root; `-r`/`-d` kept as an
   extension; the emulated original replaces the missing `myhello`/`idhello`
   goldens.
3. **The `mutos_cc` driver**, a port of the original `cc` - not of `v7/cc/cc.c`, from
   which it differs (`as -gC`, `-f`, `-o name.o`, its messages; see the plan's
   section 5). The `-P`/`-S` question `CLAUDE.md` kept open is answered by the
   original: `-P` stops after `cpp`, with or without `-S`; the port does the same.
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
port has nine real-hardware goldens besides the emulated original.

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

1. Milestone 1's golden binaries (`myhello`/`idhello`) are missing from the tree, so
   its "COMPLETE" is carried forward unchecked. Step 2 of `docs/TOOLCHAIN_PLAN.md`
   checks the port of `ld` against the emulated original `ld` instead; the goldens
   would still be welcome as real-hardware evidence.
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
   `docs/C0_PORT_PLAN.md`); the `mutos_cc` driver follows the ports of `cpp` and
   `ld`, as a port of the original `cc` - see "Next up" and
   `docs/TOOLCHAIN_PLAN.md`.
5. If a real MUTOS source file ever surfaces that exercises one of `mutos_cpp`'s
   documented simplifications (a formal parameter embedded in a macro-body string
   literal, a function-like macro name not immediately followed by `(`, or a macro
   call whose argument list spans multiple physical lines), re-check that specific
   behavior against it — see `src/mutos_cpp/README.md`'s "Known, documented
   simplifications" section for exactly which three cases these are. The port of
   `cpp` (plan step 1) replaces all three with the original's behaviour.
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
