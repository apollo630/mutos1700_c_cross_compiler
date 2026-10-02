# MUTOS 1700 Cross-Compiler Toolchain — Status

This document tracks the current functional status of the toolchain components. It is
meant to be re-verified, not trusted: per the project's core methodology, no fix or
feature is considered real until it has been rebuilt and diff-checked byte-for-byte
against a real hardware-linked golden reference file. Sections below are marked as
either **verified this session** (rebuilt and diffed against goldens as part of writing
this document) or **carried from prior session records** (not re-checked here — treat
with the same skepticism the project applies to any unverified claim).

Last updated: 2026-10-02.

---

## Top-level build (verified this session)

A repo-root `Makefile` now exists, fulfilling `CLAUDE.md`'s standing Build
Requirement ("one top level Makefile to build all 4 components") — an open
item since Milestone 1, not specific to any single milestone. `make`/`make
all` builds `mutos_ld` (compiled directly; it has no sub-Makefile of its
own yet), and delegates to each of `mutos_as`/`mutos_cpp`/`mutos_cc`'s own
`src/mutos_<tool>/Makefile`. `make test` runs every component's own
golden-diff suite from one place (`mutos_as`'s `kernel_nonopt`/`kernel_opt`/
`libc_recon`/`float_coverage` plus its `.float`/`.double` checks - `check_floatdat.sh`
and `float_coverage/fltmodel.py`, the latter Python 3 - and the assembly of every real compiler
`.s` in `tests/mutos_cc`, `mutos_cpp`'s, and `mutos_cc`'s — see each
milestone's section below for current results). Verified this session via a
full `make clean && make all && make test`. `make check-libcatof` (not
part of `make test`: it needs the Python module `unicorn`) checks
`.float`/`.double` against `libc.a`'s own `atof` run under an 8086
emulator - see Milestone 2 - and `mutos_c1`'s constant and float-initializer
texts against `libc.a`'s own `ecvt` and `fstsp` (Milestone 4).

---

## Milestone 1 — `mutos_ld` (linker)

**Status: COMPLETE** *(carried from prior session records — see caveat below)*

`mutos_ld.c` (`src/mutos_ld/`, ~1180 lines) reimplements the V7 `ld` linker with the
MUTOS-specific `a.out` deviations (see "MUTOS-specific deviations" below), including
full archive (`.a` / `-l`) support.

### Verified this session
- `mutos_ld.c` compiles cleanly (`cc -std=c11 -Wall -Wextra -O2`), zero warnings.
- The reference archive is present and intact as `tests/mutos1700_libc/libc.a.base64.txt`
  (base64-encoded; decodes to 72,178 bytes, matching the previously recorded size — the
  raw `libc.a` binary itself was replaced by this base64 text file on 2026-09-27).

### NOT verified this session
- **The golden reference binaries this milestone's "byte-for-byte identical" claim
  is based on (`myhello`/`idhello`, default-FMAGIC and `-i`/IMAGIC separate-I&D links)
  are not present in this checkout**, nor is a `tests/mutos_ld/` directory (referenced
  by `CLAUDE.md` but currently absent from the repo). The linker could not be
  re-exercised end-to-end this session. Treat the "COMPLETE" status above as an
  **unverified carry-over claim** until those golden files are restored and a real
  link + diff is re-run.

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

**Status: PROVISIONALLY COMPLETE** for the real corpus; re-verified fresh this session
(rebuild + full regression + sanitizers, independent of the prior session's own
verification — see below). "Provisional" because, per this project's core rule, no
milestone is ever declared permanently closed: it stays open to correction the moment
new real hardware evidence (e.g. a V30-targeted golden `.o`) surfaces. Functionally,
this milestone's work is done and Milestone 3 has begun on top of it.

`mutos_as` (`src/mutos_as/`: `mutos_as.h`, `lexer.c`, `parser.c`, `pass2.c`,
`symtab.c`, `encode.c`, `objwrite.c`, `fltconst.c`, `assemble.c`; secondary tools
`main.c` (→ `parse_dump`) and `classify_test.c`) is a two-pass cross-assembler
producing real MUTOS `a.out` relocatable object files for the 8086/80186/NEC V30.

### Verified this session

- **Clean rebuild** from source (`make clean && make all`), zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **Full regression: 76/76 golden files byte-for-byte identical**, full file compare
  (header + text + data + text-reloc + data-reloc + symtab):
  - `tests/mutos_as/kernel_opt/`: 62/62 clean (includes `mch_insw_outsw.s`, the
    hardware-parity test for the INSW/OUTSW fix below).
  - `tests/mutos_as/kernel_nonopt/`: 5/5 clean.
  - `tests/mutos_as/libc_recon/`: 1/1 clean - `ldexp.s`: a source reconstructed
    from the real `libc.a` object `ldexp.o`, which is its golden (see below).
  - `tests/mutos_as/float_coverage/`: 8/8 clean - real-hardware objects from
    hand-written sources, among them zeros, doubles, inexact values and
    results outside the exponent range (`fltzero.s`, `fltdbl.s`, `fltopen.s`,
    `fltmode.s`, `fltmul.s`, and new this session `fltovf.s`, 7 of whose 9
    constants were refused before - see "Range: `ldexp` wraps, an overflow
    aborts" below).
  - 0 diff-mismatches, 0 assembler-invocation errors, all four directories.
- **`.float`/`.double` against real bytes**: `libc_recon/check_floatdat.sh` - 13/13
  comparisons byte-identical to the real `atof.o`/`ecvt.o` data: six constants,
  the zero against all six of its occurrences, and `ecvt.o`'s inexact 8-byte
  `0.03` over all 8 bytes (see below).
- **Conversion model**: `float_coverage/fltmodel.py survivors` - the 38
  known-source constants in the 8 goldens leave 2 of 80 combinations (the 64
  rounding-mode combinations plus `libc.a`'s own product for `dmul`), exactly
  the set `fltconst.c` uses; `fltmodel.py check` - 0 differences between
  `fltconst.c` and the independent Python model (2,107 texts in `make test`;
  150,107 in a one-off run).
- **Against the real `libc.a` `atof`** (`float_coverage/libcatof.py`, `make
  check-libcatof`): 34 of the 38 golden constants byte-identical to
  `libc.a`'s own `atof.o` run on `libc.a`'s own floating-point runtime under
  an 8086 emulator with the real assembler's zero handling, the other 4 the
  known exception (all-zero texts with decimal exponent 0, reported
  separately); every constant `fltconst_test` accepts among 82,607 texts
  (75,589 compared) identical to it, and every one of the 1,717 it refuses
  as `RANGE` raises `SIGFPE` in the emulated runtime (8,357 texts in `make
  check-libcatof`, 210 of them `RANGE`); the runtime's `dmul`/`ddiv`/`dadd`
  on 2,000 random operand pairs each identical to `fltmodel.py`'s
  arithmetic.
- **Every real compiler output assembles**: `tests/mutos_as/assemble_cc_goldens.sh`
  - 71/71 `tests/mutos_cc` `.s.golden` files (the 62-file corpus and `11_kernel`'s
  nine), up from 69 (the two `08_float` files). There are no reference objects for
  these; their bytes are checked only where the kernel and `libc_recon` goldens
  cover the same constructs.
- **AddressSanitizer + UBSan** (`-fsanitize=address,undefined`, `-O0 -g`,
  LeakSanitizer off - a Pass 1 error exits without freeing, as before): 0 errors
  over 156 `.s` inputs (every file above, `floatdat.s`, `float_open/fltsig.s`,
  the seven `v30_speculative` sources and the 71 compiler goldens) plus a probe
  of every new `.float`/`.double` acceptance and refusal path (wrapped
  results, `FLT_RANGE`, `FLT_UNKNOWN`), each object, diagnostic and exit status
  identical to the `-O2` build's; `fltconst_test` under both sanitizers over
  130,107 texts, output identical.
- **Old vs. new assembler** (this session's model against the previous
  commit's): of those 156 inputs, 155 give identical results; `fltovf.s`,
  which the previous build refused, now assembles to its golden. Over
  130,107 texts through both `fltconst_test`s, no text both accept has
  different bytes; 1,853 refused before (`FLT_RANGE`) are now written with
  the wrapped exponent byte, and 2 (LOGHUGE after a dropped digit) are now
  `FLT_UNKNOWN` instead of `FLT_RANGE`.

### Earlier fixes (prior session records; both still covered by the regression above)

1. **INSW/OUTSW mis-assembly** — `insb`/`insw`/`outsb`/`outsw` had no dispatch entry
   in `encode_instruction()` at all. An unrecognized zero-operand mnemonic fell into
   `assemble.c`'s bare-identifier fallback (meant for real symbol/pointer-table
   references, e.g. `tty.s`'s jump tables) and was silently mis-assembled as a 2-byte
   external-symbol placeholder + a spurious relocation entry instead of the correct
   1-byte opcode — desyncing the location counter for everything downstream, with no
   diagnostic. Fixed by adding real `0x6C`/`0x6D`/`0x6E`/`0x6F` dispatch entries;
   INSW/OUTSW confirmed byte-exact via the new `mch_insw_outsw.s` test (`mch.s` with
   its two `.byte /6d` / `.byte /6f` raw-byte workarounds replaced by plain
   `insw`/`outsw`).
2. **Missing "unknown opcode" safety net** — the same bare-identifier fallback would
   silently swallow *any* mnemonic-shaped token that failed to encode, not just
   genuinely unknown ones. Added `encode_is_known_mnemonic()`, which checks a token
   against every mnemonic name this assembler actually dispatches on (built by
   walking the same `GROUP1`/`GROUP1B`/`GROUP2`/`GROUP2B`/`REAL_JCC`/`LOOP_OPS`/
   `PSEUDO_BRANCHES`/`NOARG_TABLE` arrays `encode_instruction()` itself uses, plus a
   small explicit list for irregularly-dispatched mnemonics, so a new table entry is
   automatically covered with no second edit). `assemble.c` now only takes the
   bare-symbol fallback path when the token is *not* a known mnemonic; a recognized
   mnemonic used with an unsupported operand count/shape is now a hard compile error
   with a line number instead of silent mis-assembly. (A first version of this fix
   omitted the entire Group3 family — `mul`/`div`/`idiv`/`imul`/`neg`/`not` and their
   byte variants — from the known-mnemonic list; caught and fixed later the same
   session by regenerating the list programmatically from the source instead of by
   hand a second time.)

### `.float`, `lea <reg>,<label>`, no padding at a segment switch, byte-register `mov` (verified this session)

`mutos_c1`'s floating output (Milestone 4's `08_float`) now assembles, links with
the real `crt0.o`/`libc.a`, and runs. Full derivation in `docs/DEVLOG.md`'s section
"`mutos_as`: `.float`, `lea <reg>,<label>` - `08_float` assembles, links and runs".

- **`.float`** (`fltconst.c`): one 4-byte MUTOS float per operand (`L10000:<TAB>.float
  3.50000000000000000e+00`; the manual's `0f` prefix accepted too), converted with
  exact integer arithmetic - no host floating point. Only a value exactly
  representable in 24 significant bits is accepted; an inexact value is an
  explicit error (the real assembler's rounding is not known). At the time, zero
  (`bc a2 31 00` in `atof.o`/`ecvt.o`, the mantissa of 5\*\*17 with a zero
  exponent) and `.double` were explicit errors too - both are implemented now,
  from real-hardware goldens: see "Zero `.float`, `.double`" below.
- **`lea <reg>,<label>`**: `8D /r`, mod=00 rm=110 + a relocated disp16 (R_DATA /
  R_TEXT / R_BSS / R_EXT like any direct operand) - confirmed by `libc.a`'s machine
  code (`atof.o`/`ecvt.o` `8d 06 <disp16>` R_DATA; `ldexp.o`, `modf.o`,
  `doubles.o`, `singles.o`, `stkmath.o` with R_EXT `fac`). `lea` now also refuses a
  byte-register destination.
- **No padding at a `.text`/`.data` switch - a real bug, found by this work.**
  `mutos_as` padded the segment it left to an even size at every switch; the real
  assembler rounds each segment once, at the end. `mutos_c1` writes each floating
  constant's `.data` / `L10000: .float ...` / `.text` block between two
  instructions, so an odd location there put a `0x00` byte INTO the code (`add
  [bx+si],al` at run time). `atof.o`/`ecvt.o` show the lea directly after the
  previous instruction at odd offsets (text 71, 407; 201, 345). The earlier evidence
  (`malloc.o`, `mch.o`) was each file's last switch, where both rules give the same
  bytes - all 136 other `.s` inputs assemble to identical objects under the new rule.
- **`mov` with a byte register and a memory or register operand** (`mov al,x`,
  `mov x,al`, `mov al,bl`) got the WORD opcode (A1/A3/8B/89) unless written `movb` -
  now byte-sized, like the reg,imm form (`mov al,*0` in `mch.s`). Found reproducing
  `ldexp.o` (A0, A2, `8A 46 0A`); no corpus file writes one.
- **Regression tests**: `tests/mutos_as/libc_recon/` (`ldexp.s` reproduces the real
  `ldexp.o` byte for byte - `lea` with external and local labels at even and odd
  offsets, the byte `mov`s; `floatdat.s` + `check_floatdat.sh` for `.float`) and
  `tests/mutos_as/assemble_cc_goldens.sh`, all in the top-level `make test`. See
  that directory's `README.md` for why no compiled-C `libc.a` object can be
  reproduced whole yet: no `L` labels in their symbol tables (why the kernel
  goldens have them is open, see open item 6), and for `ecvt.o` also an inexact
  8-byte constant (`.03`). (A zero constant was a third reason until the zero
  `.float` below.)
- **End to end**: both `08_float` goldens, assembled and linked by `mutos_ld` with
  the real `crt0.o` and `libc.a`, return 7 and 3 - their C sources' values - when
  their `_main` runs under an 8086 emulator (Unicorn, a scratch harness, not part of
  `make test`), the real runtime's `flds`/`fadds`/`fmuls`/`fstsp`/`itof`/`fdivs`/
  `ftol`/`ftoi` reading the assembled constants. Ten more floating programs through
  `mutos_cpp`, `mutos_c0`, `mutos_c1`, `mutos_as`, `mutos_ld` and the same harness
  all return what the host C compiler's build returns.
- **`fltconst.c` checked**: 59,000 randomized exact and inexact texts (random 24-bit
  mantissas, exponents over the whole range, several spellings each) against an
  independent exact-fraction encoder in Python and `x86sim.py`'s `mbf_encode()`: 0
  differences; ASan/UBSan clean.

### Zero `.float`, `.double` (verified this session)

Both implemented from `tests/mutos_as/float_coverage/`'s real-hardware goldens
(former open item 7). Full derivation in `docs/DEVLOG.md`'s section
"`mutos_as`: zero `.float` and `.double` implemented". The restrictions below
(one zero spelling, exact values only) were superseded the same day - see "The
real conversion, re-enacted" next.

- **Zero `.float`**: `bc a2 31 00` - `fltzero.o.golden`'s bytes, identical to all
  six zero constants in `atof.o`/`ecvt.o` - but only for the spelling it is
  confirmed for, `0.00000000000000000e+00` (the `%.17e` text the real compiler
  writes: no minus sign, exactly 17 fraction digits, exponent 0). The mantissa is
  5\*\*17's, which fits a conversion that scales the digits by 5\*\*(fraction
  digits), so another spelling (`0.0`) may have other real bytes: every other
  zero is an explicit error naming the accepted spelling.
- **`.double`** (`fltconst.c`'s `flt_encode(FP_DOUBLE, ...)`, `assemble.c`'s
  `.float` branch): one 8-byte constant per operand, the float layout with 56
  significant bits (exponent byte 7, sign bit 7 of byte 6) - `fltdbl.o.golden`,
  `1 + 2**-32` → `00 00 80 00 00 00 00 81`, reproduced byte for byte. Exactly
  representable values only, as for `.float` (`ecvt.o`'s inexact `.03` is one
  rounded sample, not a rule); bare atof syntax or the manual's `0d` prefix with a
  `d`/`D` exponent; every zero `.double` refused (no real bytes for one exist).
  Previously `handle_directive()` refused every `.double` in Pass 1.
- **Checked**: `fltconst.c` against an independent exact-fraction encoder in
  Python on 136,818 random texts over both formats - 0 differences; the 69,296
  `.float` texts among them give the previous encoder's answers except for the
  seven accepted zero spellings; ASan/UBSan clean. End to end, five programs
  with `.double` constants (mantissa bits down to the lowest, a negative value,
  the `0d` spelling) run through the real runtime's
  `fldd`/`fsubd`/`fmuld`/`ftoi` - assembled by `mutos_as`, linked by `mutos_ld`
  with the real `crt0.o`/`libc.a`, under an 8086 emulator (scratch harness, not
  part of `make test`) - return the expected 1, 12345, -7, 23130, 1499.

### The real conversion, re-enacted (verified this session)

From `tests/mutos_as/float_coverage/fltopen.o.golden` (real hardware, 2026-09-28):
`.float 0.0` → `00 00 20 00`, `.float -0.00000000000000000e+00` → `bc a2 b1 00`,
`.double 0.00000000000000000e+00` → `00 00 c5 2e bc a2 31 00`, `.float
0.10000000000000000e+00` → `cc cc 4c 7d`. The first and third are the bytes
`docs/DEVLOG.md` had predicted before the run from v7 `atof()` (a zero dividend
keeps the divisor `flexp` = 5\*\*k's mantissa); the last is 0.1 **truncated** -
a `.float` is the high half of the double conversion. Full derivation in
`docs/DEVLOG.md`'s section "`mutos_as`: the real conversion, re-enacted".
Its rounding set (48 combinations) and the refusals and zero limits below
were narrowed the next day - see "Rounding pinned, zeros on both paths".

- **`fltconst.c` rewritten** to re-enact that conversion - v7 `atof()` on the
  56-bit double (digits accumulated while below 2\*\*56, `flexp` = 5\*\*k by
  repeated squaring, one divide or multiply, `ldexp`), `.float` = the double's
  high four bytes - instead of encoding the exact decimal value. How the real
  `dmul`/`dadd`/`ddiv` round is unknown except that `ddiv` does not truncate
  (`fltdbl.o.golden`), so the conversion runs under all 48 remaining
  combinations and a constant is accepted only if all give the same bytes
  (otherwise `FLT_ROUNDING`, which replaces `FLT_INEXACT`).
- **Newly accepted**: inexact values the unknown rounding cannot reach
  (`.float 0.1` → `cc cc 4c 7d`, `.double 0.1` → `cd cc cc cc cc cc 4c 7d`,
  `.double .03` → `ecvt.o`'s `c3 f5 28 5c 8f c2 75 7b`), and zeros of either
  size and sign with 1 to 24 more fraction digits than exponent (the mantissa of
  5 to that power, exponent byte 0).
- **Newly refused - a latent mismatch closed**: exact floats whose conversion
  rounds, e.g. the compiler's 18-digit `2.93572534179687500e+03` (its 18th digit
  makes `10*fl` inexact; under a truncating `dmul` the real bytes are one unit
  lower, `9a 7b 37 8c`). The previous encoder wrote the exact value for these
  without knowing it was right. About 6% of the `%.17e` constants `mutos_c1` can
  write fall here; no `.s` in the repository has one.
- **Tests**: `float_coverage/fltopen.s` (moved from `float_open/`, 5/5 there);
  `float_coverage/fltmodel.py`, an independent Python model, in `make test`
  (`survivors`: re-derives the 48 combinations from every golden in that
  directory and fails if `fltconst.c`'s set differs; `check`: `fltconst.c` via
  the new `src/mutos_as/fltconst_test` against the model); `floatdat.s` gains
  `ecvt.o`'s `.03`. One-off: 170,778 texts (random and targeted - compiler
  `%.17e` form, the 2\*\*56 accumulation boundary, zeros, prefixes) through
  `fltconst_test` and the model - 0 differences.
- **Next probe** (then): `fltmode.s` - four compiler-style `.double` constants
  chosen to split the 48 combinations into 32 classes, the refused
  `2.93572534179687500e+03` itself, and zeros on `atof`'s multiplication path.
  Run on real hardware; see "Rounding pinned, zeros on both paths" next.

### Rounding pinned, zeros on both paths (verified this session)

From `tests/mutos_as/float_coverage/fltmode.o.golden` (real hardware, committed
2026-09-29; moved from `float_open/` with its source). Its open question - which
product `dmul` rounds - and the k = 0 zero limits were settled the same day:
see "The real product, zeros at exponent 0" next. Full
derivation in `docs/DEVLOG.md`'s section "`mutos_as`: rounding pinned, zeros on
both paths, and `libc.a`'s own `atof` as a second oracle".

- **Rounding**: only 2 of the 64 rounding-mode combinations fit its ten
  constants and the nine before: `dmul` and `dadd` round to nearest, ties to
  even; `ddiv` to nearest (its tie rule never matters for `atof`). The
  compiler's `2.93572534179687500e+03` is the exact value, `9b 7b 37 8c`;
  `.double 0.1` and a negative zero `.double` are the predicted bytes.
- **`libc.a`'s own `atof`**, run on `libc.a`'s own floating-point runtime
  under an 8086 emulator (scratch harness), gives all 11 nonzero real
  constants byte for byte - the real assembler converts like it. Its zeros
  differ: `libc.a`'s `dmul`/`ddiv` clear the accumulator `fac` for a zero
  operand. Changed to clear only `fac`'s exponent byte, it reproduces all
  seven real zeros with a nonzero decimal exponent k.
- **Zero rule, now on both paths**: a zero gives the mantissa of `flexp` =
  5\*\*k with exponent byte 0 - the last multiplication's result, still in
  `fac` - for any k >= 1 on either path (`.float 0.0e+05` → `00 40 1c 00`,
  `.double 0.0000000000000000000000000` → the rounded 5\*\*25's mantissa),
  both directives and signs, unless `atof` gives up (a decimal exponent below
  -39 minus the digit count). For k = 0 `fac` holds what the digit loop left:
  only `Z0`'s shape is accepted (an unsigned `.float` of 18 digits,
  `ff ff ff 00`); other k = 0 zeros stay `FLT_ZERO`.
- **New open question - which product `dmul` rounds.** `libc.a`'s `dmul`
  is not exact: its partial-product routine takes the a0\*b2 term from b1's
  word (checked against the emulated routine on 20,000 random products: 0
  differences). No real constant so far needed a product where that
  matters, so `fltconst.c` keeps both candidates (`MODES_MUL`:
  `RM_NEAR_EVEN`, `RM_LIBC_MUL`; `MODES_ADD`: `RM_NEAR_EVEN`; `MODES_DIV`:
  `RM_NEAR_EVEN`, `RM_NEAR_AWAY` - replacing `MODES_MUL_ADD`) and refuses
  (`FLT_ROUNDING`) a constant whose bytes they decide: possible only for a
  positive decimal exponent of 4 or more (`%.17e` text from `e+20` up) and
  for k = 50..54 (`e-33` down). Every `%.17e` text from `e-32` to `e+19` is
  accepted - the 6% of exact floats refused before, `CF` among them,
  included. Over the whole exponent range 19% of random exact floats and 29%
  of exact doubles are refused (e.g. `.float 1.26765060022822940e+30`, 2\*\*100:
  `00 00 00 e5` or `ff ff 7f e4`), against 49%/93% under the previous 48
  combinations; 224 of 20,000 floats the previous model accepted are refused
  now - latent mismatches if the real `dmul` is `libc.a`'s.
- **Next probe** (then): `fltmul.s` - five constants whose bytes the two
  products decide, three zeros with k = 0 (digit count, `.double` low half,
  sign), the LOGHUGE path and the first negative nonzero constant.
- **Tests**: `float_coverage/fltmode.s` in `make test` (6/6 there);
  `fltmodel.py` extended the same way (`libc` as a fifth `dmul` candidate,
  the zero rule, 25 new edge cases).

### The real product, zeros at exponent 0 (verified this session)

From `tests/mutos_as/float_coverage/fltmul.o.golden` (real hardware, committed
2026-09-29; moved from `float_open/` with its source this session). Full
derivation in `docs/DEVLOG.md`'s section "`mutos_as`: the real product, zeros at
exponent 0, and `libcatof.py`".

- **The multiplication rounds `libc.a`'s product.** All five constants whose
  bytes depend on it have `libc.a`'s, none the exact product's - e.g. `.float
  1.26765060022822940e+30` (2\*\*100) is `ff ff 7f e4`, one unit below the
  value. `survivors`: 2 of 80 combinations left (`M=libc A=ne D=ne|na`);
  `fltconst.c`'s `MODES_MUL` is `RM_LIBC_MUL` alone. Every constant the model
  covers is now determined: `FLT_ROUNDING` cannot occur (only a division tie
  could raise it, and `atof` has none), and every `%.17e` constant `mutos_c1`
  writes from `e-37` up (`e-38` when `atof` drops the 18th digit) is
  accepted.
- **Zeros with decimal exponent 0**: `ff ff ff 00` for one digit (`K1`) as for
  18 (`Z0`), `00 00 00 00 ff ff ff 00` as a `.double` (`K2`), `ff ff 7f 00`
  negated (`K3`: `fneg` flips bit 7 of byte 6) - independent of the digit
  count and of the constant before. `mutos_as` accepts every such zero now.
  Why the digit loop leaves `ff ff ff` in `fac` is not known (`libc.a`'s
  runtime leaves -2\*\*56 there).
- **LOGHUGE** (nd - k < -39): `.double 1e-41` → eight 0 bytes, the `fac` rule
  again - `atof` sets `fl = 0`, exponent 0, and the multiplication by
  `flexp` = 1.0 keeps what `fac` held, `fl` = 1.0 from the digit loop's last
  `fadd`. Accepted when the text's last digit was accumulated (`fl`'s
  mantissa, exponent byte 0) or all digits are 0 (the zero above); after a
  dropped digit (`fac` holds `fcmp`'s difference, never observed) still
  `FLT_RANGE`. `FLT_ZERO` is gone - no zero is refused any more except where
  `flexp` overflows.
- **Still refused**: out of range at some step (`FLT_RANGE`), e.g. `.float
  1.00000000000000000e-38`, whose 5\*\*55 overflows although the value fits;
  `libc.a`'s runtime answers an overflow with `SIGFPE`, what the real
  assembler does is not observed. (Settled in the next section: `ldexp`
  wraps, an overflow inside the arithmetic aborts the real assembler.)
- **`libcatof.py`** (new, `tests/mutos_as/float_coverage/`, `make
  check-libcatof`): the emulator harness of the previous section as a tool -
  links `libc.a`'s `atof` and floating-point runtime from the base64 objects,
  runs it under Unicorn with the real assembler's zero handling (or
  unmodified, `--libc`), and checks the goldens, `fltconst_test`'s accepted
  output and the runtime's `dmul`/`ddiv`/`dadd` against `fltmodel.py`; it
  checks the linked image's code where it patches or relies on it. A build
  with the exact product instead of `libc.a`'s fails its `check`.
- **Tests**: `float_coverage/fltmul.s` in `make test` (7/7 there). Next
  probes: `float_open/fltovf.s` and `fltsig.s` (see open item 7) - run, see
  the next section.

### Range: `ldexp` wraps, an overflow aborts (verified this session)

From `tests/mutos_as/float_coverage/fltovf.o.golden` (real hardware, committed
2026-09-29; moved from `float_open/` with its source this session) and the real
assembler's refusal of `tests/mutos_as/float_open/fltsig.s`. Full derivation in
`docs/DEVLOG.md`'s section "`mutos_as`: out of range - `ldexp` wraps, an overflow
aborts".

- **A result only `ldexp` takes out of range wraps.** All nine constants of
  `fltovf.s` have exactly the bytes `libcatof.py` predicted: `libc.a`'s
  `ldexp` adds the exponent to `fac`'s exponent byte as a 16-bit sum and stores
  the low byte, so `.float 2.00000000000000000e+38` is `99 76 16 00` (exponent
  byte 256 → 0), `.double 1.0e-39` ends in `ff` (-1 → 255), `.double 1.0e+50`
  in `27` (295 → 39) - no diagnostic. The two in-range controls just inside
  both ends are as modelled. `fltconst.c` and `fltmodel.py` write the wrapped
  byte now; `mutos_as` reproduces `fltovf.o.golden` byte for byte.
- **An overflow inside the arithmetic aborts the real assembler.** `as -o
  fltsig.o fltsig.s` printed `***ERROR*** floating point over/under flow-
  assembly aborted` (followed by `W` and `?h), line 41`) twice, exit status 4,
  no object; line 41 is the first constant, `.float 1.00000000000000000e-38`
  (5\*\*55 overflows). Emulated, `libc.a`'s runtime raises `SIGFPE` twice for
  it - `__ovfl` in `dmul`, then `__div0` in the division by the zero left
  behind - which fits two messages if the real `as` catches the signal, prints
  and gives up after the constant. `mutos_as`'s refusal (`FLT_RANGE`, its
  message now quoting the real one) is the faithful behaviour; it exits with 1,
  the real `as` with 4 (unchanged). `fltsig.s` stays in `float_open/` as the
  evidence, without a golden.
- **What this means for the compiler's constants.** `mutos_as` accepts every
  `%.17e` constant `mutos_c1` writes from `e-37` up (`e-38` when `atof` drops
  the 18th digit), with the real bytes - among them the `%.17e` zero (`bc a2
  31 00`) and `.double 3.00000000000000000e-02` (`ecvt.o`'s `.03`), which the
  zero and non-float constants still refused in `mutos_c0`/`mutos_c1` will
  need (Milestone 4's "Next up"). Below that, 5\*\*55 overflows and the real
  assembler aborts, so a float or double constant below about 1e-37 cannot go
  through the real toolchain as `%.17e` text at all: `mutos_c1` writes it as
  the real compiler would, and both assemblers refuse it.
- **`FLT_UNKNOWN`** (new status): LOGHUGE after a dropped digit - `fac` holds
  `fcmp`'s difference, never observed - and a text of more than 100,000 digits,
  both `FLT_RANGE` before. `FLT_RANGE` now means only what the real assembler
  aborts on.
- **`libcatof.py`**: `__ovfl`/`__div0` stop the emulation (`atof` prints
  `SIGFPE`); `check` requires every text `fltconst_test` refuses as `RANGE` to
  raise it and no accepted one to. A build without the wrap (refusing those
  results) fails it with 57 `RANGE` texts that raise no `SIGFPE`; one with the
  exact product with 749 of 7,588 compared texts different.
- **Tests**: `float_coverage/fltovf.s` in `make test` (8/8 there);
  `float_open/` has no open probe.

### Auxiliary deliverables (prior session records, not re-checked this session)
- `mutos_as.1` — English troff man page (its `.float`/`.double` entries updated
  this session).
- `fltconst_test` — reads `f <text>`/`d <text>` lines and prints what
  `.float`/`.double` would store, or why it is refused (`SYNTAX`, `ROUNDING`,
  `RANGE`, `UNKNOWN`) (for `float_coverage/fltmodel.py check` and
  `libcatof.py check`).
- `float_coverage/libcatof.py` — new this session (verified this session, see
  above): `libc.a`'s own `atof` under an 8086 emulator.
- `run_goldens.sh` / `mk_goldenbase64.sh` — batch golden-diff test runner and base64
  golden-file generator (both used to run this session's regression above).
- CLI: `-o output`, `-W` (suppress diagnostics), multi-file concatenation, stdin
  fallback, exec-bit on success, `asf` argv[0] alias. `-L` (control 'L'-prefixed
  compiler-internal label output) was implemented, found to break golden parity, and
  was **deliberately removed** per explicit prior user decision.
- Modified native-kernel `conf/Makefile` (`-S` instead of `-c`, retains `.s` files,
  copies output to `.o.golden`) — mechanism for generating more real hardware golden
  files from the kernel source tree in the future.

---

## Milestone 3 — `mutos_cpp` (C preprocessor)

**Status: COMPLETE for the real corpus; verified fresh this session.**

`mutos_cpp` (`src/mutos_cpp/`: `mutos_cpp.h`, `util.c`, `source.c`, `macro.c`,
`ifexpr.c`, `directive.c`, `scan.c`, `main.c`) re-implements the observable behavior
of the real MUTOS 1700 / V7 "fast cpp" (`v7/cpp/cpp.c`, John F. Reiser, 1978) from
scratch in modern C11 — not a literal port of the original K&R, pointer-arithmetic
-heavy source, matching this project's established approach for `mutos_as`/`mutos_ld`.
See `src/mutos_cpp/README.md` for the full reverse-engineered behavioral
specification (K&R-era language quirks, output-line accounting rules, known
simplifications) this implementation follows.

### Verified this session

- **Clean build** (`make clean && make all`), zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **Full regression: 5/5 golden files byte-for-byte identical**
  (`tests/mutos_cpp/run_goldens.sh`), using the exact flags the golden references were
  generated with on real hardware (`cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE
  <name>.c`): `main.c`, `mch.c`, `sys.c`, `tty.c`, `v30ide.c`.
- **AddressSanitizer + UBSan** (`-fsanitize=address,undefined`, `-O0 -g`): 0 errors
  across all 5 golden files.
- Two real bugs found and fixed this session, both confirmed via rebuild + full-corpus
  diff (see "Bugs found this session" below).

### Bugs found this session

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

### Auxiliary deliverables (this session)

- `mutos_cpp.1` — English troff man page.
- `src/mutos_cpp/README.md` — full behavioral specification and source-layout guide.
- `tests/mutos_cpp/run_goldens.sh` — batch golden-diff test runner.

---

## Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler) [CURRENT FOCUS]

**Status: IN PROGRESS — `mutos_c0`/`mutos_c1` exist and are verified
byte-exact, end-to-end, for 62/62 of the full corpus - every file: `tests/mutos_cc/
00_smoke`'s 3 files plus all of `tests/mutos_cc/01_expr`: `01_intarith.c`,
`02_bitwise.c`, `03_rellogic.c`, `04_shift.c`, `05_incdec.c`,
`06_compasgn.c`, `07_ternary.c` and `08_castsize.c`, plus all 4 of
`02_long`: `01_addsub.c`, `02_muldiv.c`, `03_retval.c` and `04_params.c`,
plus all 7 of
`03_ctrlflow`: `01_ifelse.c`, `02_while.c`, `03_dowhile.c`, `04_for.c`,
`05_breakcont.c`, `06_switch.c` and `07_goto.c`, plus all 7 of
`04_funcs`: `01_call.c`, `02_manyargs.c`, `03_recfact.c`, `04_mutrec.c`,
`05_staticvar.c`, `06_regclass.c` and `07_funcptr.c`, plus all 7 of
`05_arrptr`: `01_arrbasic.c`, `02_array2d.c`, `03_ptrbasic.c`,
`04_ptrarreq.c`, `05_arrofptr.c`, `06_ptrptr.c` and `07_strlibc.c` (one- and
two-dimensional array subscripting, multi-level pointers, explicit `&`/`*`,
pointer/array-parameter equivalence, and string literals), plus all 7 of
`09_abiprobe` (`01_argvmain.c` and the six `0N_frameNNN.c` large-frame
probes - `char` element access), plus all 3 of `07_scope`:
`01_globstat.c`, `02_shadow.c` and `03_externdef.c` (file-scope variables
and block scope), plus all 9 of `06_struct` (structs, unions, nested
structs, arrays of structs, struct assignment, bit-fields, enums,
typedefs), plus all 5 of `10_integ`: `01_wordcount.c` (a file-scope
char array with a string initializer, a char compared with a constant),
`02_bubsort.c`, `03_linklist.c`, `04_strrev.c` and `05_matmul.c`
(evaluation-order codegen: a right operand computed first and spilled,
call arguments right to left, a relational's operands swapped by
degree, a store's right-hand side computed first), plus both of
`08_float`: `01_floatbas.c` and `02_dblconv.c` (`float`/`double`
locals, floating constants, `+ - * /`, int/long conversions - through
libc's software floating-point runtime) - see the sections
below. Beyond the corpus, all seventeen floating-point probes of
`tests/mutos_cc/fltprobe/`'s first three rounds (real-hardware goldens, not
counted in the 62) are byte-exact too, end-to-end: comparisons (a zero
included), comparisons as values and under `&&`, v7's operand order for
every floating operator (an element of a local array at degree 2),
constants of any value (`.double` with the MUTOS `ecvt()`'s own digits,
degree 0 where it decides an order; a zero kept), compound assignments
(`i += d` and `j /= e` into an int too), calls, float functions, the
register an int is converted in, file-scope and static floats with any
constant initializer, arrays (subscripted by a variable too), pointers and
members, `long`/`char` conversions; a fourth round of three probes
(`p14`..`p16`) waits for real hardware. ABI/
calling-convention research is done, the `c0`/`c1` process split is
confirmed as a deliberate design decision, the K&R test corpus now
has full-corpus goldens (all 62 files across all 11 categories) present in
this checkout, and its real-hardware golden-generation pipeline is confirmed
working end-to-end. A further, separate golden corpus - `11_kernel/`, nine
real MUTOS kernel driver source files with their own real-hardware-verified
goldens already present - was added this session; `mutos_c0`/`mutos_c1`
coverage against it is 0/9 so far (each refusal diagnosed, none silent),
tracked apart from the 62/62 figure above - see "Next up" below and
`docs/DEVLOG.md`'s Milestone 4 section for the initial assessment.**
`src/mutos_cc/` now exists — see `src/mutos_cc/README.md` for full detail.

### `mutos_c0`/`mutos_c1`: verified this session (the round-3 `fltprobe` goldens - 4/4 byte-exact; an element's degree, an int `*`'s left operand, `+ 0`, a value kept in AX, `j /= e`, a floating zero)

**62/62 corpus files and 17/17 `fltprobe` files byte-exact, all at every
stage.** Round 3 came back in commit `33ec944` with all four kinds of file,
round 2's four `.i` files in commit `34c9195` - byte-identical with
`mutos_cpp`'s, so `p6`..`p9` moved into category 1 unchanged and category 7
is empty. `p11_itof2` and `p12_init2` matched at once; `p10_elem` differed in
two places of its `.s`, and `mutos_c0` refused `p13_open`. Full derivation
in `docs/DEVLOG.md`'s "The round-3 fltprobe goldens".

- **An element of a local array has degree 2** (`p10`: `1.5 + arr[i]` loads
  the element first, then `lea ax,L10003` / `fadds`): a local's address is
  a computed `lea`, degree 1, not v7's link-time-constant -2 - which is also
  why `lea di,<arr>` comes before the index is scaled. `fdeg()`; a static or
  file-scope address stays -2.
- **An int `*` loads a dereferenced left operand first** (`p10`'s `ps[i].c *
  qs[i].g`: `mov di,*4.(di)`, the right element's address into SI, `mov
  ax,di` / `imul *12.(si)` - not spilled), as `+`/`-` do (`load_now()`).
- **The register an int is converted in** (`p13`): DI after a conversion
  and after an element (in DI context); in AX `i - j` is `mov ax,i` / `sub
  ax,j`, `i << 1` `mov ax,i` / `sal ax,*1`.
- **An int `+ 0` is dropped** - v7's `acommute()` toss: `(j + 0)` converted
  is `mov ax,j`. `mutos_c1` drops it everywhere (it emitted `add di,*0.`).
- **A value in AX plus a constant stays in AX** (`p13`'s `return r + (int) d
  - 30`: `call ftoi` / `add ax,*-42.(bp)` / `add ax,*-30.`; the kernel's
  `amx.s` the same for a product), `+ 1` / `- 1` as `inc ax` / `dec ax`.
- **`j /= e` into an int**: `FTOI(INT)`, `ASDIV(INT)` (`mutos_c0` accepts
  it now) and `call ftoi` / `mov cx,ax` / `mov ax,j` / `cwd` / `idiv cx` /
  `mov j,ax` - the golden returns 42, v7's `j / (int)e`.
- **A floating zero is an ordinary constant**: `d + 0.0` -> `.float
  0.00000000000000000e+00` / `flds` / `faddd d`, `d - 0.0` -> `fldd d` /
  `fsubs` (both refusals removed).
- **Confirmed as inferred**: every shape `p11`/`p12` and the rest of `p10`
  asked about - ints computed in DI or AX converted, `i += f()`, `float`,
  negated and `static float` initializers (truncated: `p12` returns 6),
  struct-array elements of 8 and 16 bytes, elements as second operands.
- **Compiled by inference** (round 4 asks): a value in AX plus a constant
  stored to memory or a register variable, `+ 0` in DI, `a[i] * b[j]` and
  `a[i] & b[j]` of int arrays, `x /= f()` / `y %= f()`, a chain ordered by
  an element's degree, a shift by more than one and a right shift converted
  in AX.
- **Still refused** (`p16_open2` asks): an int converted after `*p` or an
  assignment, a remainder converted, a negated zero, a constant converted
  to an int, `d += c` with a char, an `unsigned` converted; `%`, `?:`.
- **`tests/mutos_cc/fltprobe/` round 4**: `p14_axint.c`, `p15_fltinf.c`,
  `p16_open2.c` (`make -f Makefile.mutos round4`).

**Verification (this session):**

- `make test`: 79 files byte-exact at all four stages (62/62 corpus, 17/17
  `fltprobe`), category 7 empty, 0 mismatches, zero warnings; `mutos_as`
  76/76, `check_floatdat.sh` 13/13, 88/88 compiler goldens assemble,
  `mutos_cpp` 5/5.
- Against the previous build (`33ec944`), all 88 inputs (the 79 and the
  nine `11_kernel` refusals): output and diagnostics identical but for
  `p10` and `p13`.
- `fuzz_c.py` against `33ec944`: 8,000 programs (seeds 1, 7 `--scope`, 3
  scalars only): 0 WRONG, 0 BAD, 37 changed and still correct (the dropped
  `+ 0`, a value kept in AX); 500 more (seed 11) through ASan/UBSan builds:
  0 WRONG, 0 BAD.
- ASan/UBSan builds over the 88 inputs: no reports, the 79 byte-exact.
- `x86sim.py` runs all seventeen goldens (`p10` 21, `p11` 109, `p12` 6,
  `p13` 42); round 4 through the whole chain: `p14_axint` 129 and
  `p15_fltinf` 45, the host C compiler's values; `p16_open2` stops with
  four `mutos_c0` diagnostics.

### `mutos_c0`/`mutos_c1`: verified this session (the round-2 `fltprobe` goldens - 4/4 byte-exact; an 8-byte constant's degree, the register an int is converted in, elements by a variable, `i += d`, v7's `doinit()`)

**62/62 corpus files and 13/13 `fltprobe` files byte-exact** - the four of
round 2 (commit `4c668c2`) from `mutos_c0` on: their `.s`, `.1` and `.2`
came back, their `.i` did not, and `run_goldens.sh` skipped them silently.
It now checks such a file from `mutos_c0` on (`mutos_cpp`'s own `.i`
feeding it) and lists a match apart (category 7); bringing back `p6`..`p9`'s
`.i` files moves them into category 1 with no change (see "Next up").
`mutos_c0` matched `p6`/`p7` at once; `mutos_c1` refused both, `mutos_c0`
refused `p8`/`p9`. Full derivation in `docs/DEVLOG.md`'s "The round-2
fltprobe goldens".

- **An 8-byte constant has degree 0** (`p6_dblcon`), as a DOUBLE leaf:
  `d + 0.1` and `0.1 + d` keep their order, `0.1 * a` loads the float `a`
  first, `d < 0.1` is exchanged. Only a constant exactly a float (a
  `.float`) has degree 1.
- **The register an int is converted in** (`p7_itofreg`): AX after a
  computed `*`, `/` or a call, DI after a leaf, `+`, `-` or a negation; in
  AX `x + 1` is `mov ax,x` / `inc ax`, `x - 1` `dec ax`, `i + j` `mov ax,i` /
  `add ax,j`; in DI `i + j` is `mov di,i` / `add di,j` / `mov ax,di`.
- **`p8_misc`**: `ltof` with DI free goes through DI (the inference holds);
  `*p + 1.5` loads `*p` first (a STAR's degree, `max(1, degree(p))`); `i +=
  d` / `i -= d` - `FTOI`, then `ASPLUS(INT)` / `ASMINUS(INT)` in `mutos_c0`,
  `call ftoi` / `add *-44.(bp),ax` in `mutos_c1`; a double array subscripted
  by a variable - `lea di,arr` / `mov si,i` / `mov cx,*3.` / `sal si,cl` /
  `add di,si` / `lea ax,(di)`, as a target after the right-hand side.
- **`p9_init`: v7's `doinit()`** - a double variable's initializer is a
  `.double` whatever its value (`= 2`: `.double 2.0...`; `= 0.1`: the MUTOS
  `ecvt()`'s text), a float's the double truncated to a float (`float gy =
  0.1;` -> `.float 9.99999940395355225e-02`: `libc.a`'s `fstsp` keeps the
  high four bytes - confirmed under the emulator, `libcatof.py fecvt`), a
  negated one folded (`-1.5`); each initializer takes a `c1` label (the
  first code constant after five is `L10005`). `mutos_c0` accepts an int
  constant, a negated int and a negated floating literal as initializer;
  `mutos_c1`'s new `gen_finit()` writes them all.
- **Compiled by inference** (round 3 asks): an element subscripted by a
  variable as a second operand or next to another element (the first
  loaded before the second's address), any int computed in DI converted,
  a quotient converted, elements of any power-of-two size (`mov cx,*N.` /
  `sal si,cl` - struct arrays too), `i += f()`, `float`, negated and `static
  float` initializers.
- **Still refused** (`p13_open` asks): an int converted after a
  conversion, an element or an assignment, a difference or a shift for AX,
  `x + 0` for AX, `i /= d`, a floating zero added or subtracted, `%`.
- **`tests/mutos_cc/fltprobe/` round 3**: `p10_elem.c`, `p11_itof2.c`,
  `p12_init2.c`, `p13_open.c` (`make -f Makefile.mutos round3`).
- **Tooling**: `run_goldens.sh` category 7; `libcatof.py fecvt` and
  `fltdec_test -f` (in `make check-libcatof`). `x86sim.py` runs all four
  round-2 goldens unchanged (9, 62, 6, 6 - their programs' values).

**Verification (this session):**

- `make test`: 71 files byte-exact at all four stages (62/62 corpus, 9/9
  round-1 `fltprobe`) and the four round-2 files from `mutos_c0` on, 0
  mismatches, zero warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13,
  84/84 compiler goldens assemble, `mutos_cpp` 5/5. With scratch `.i`
  goldens from `mutos_cpp`, all 75 in category 1.
- Against the previous build (`4c668c2`), all 84 inputs (the 71, round 2,
  the nine `11_kernel` refusals): output and diagnostics identical but for
  round 2's four.
- `fuzz_c.py` against `4c668c2`: 30,000 programs (seeds 411, 421 `--scope`,
  431 scalars only): all identical, 0 WRONG, 0 BAD; 3,000 more (seed 441)
  through ASan/UBSan builds: 0 WRONG, 0 BAD.
- ASan/UBSan builds over the 84 inputs: no reports, output identical to
  the `-O2` build's.
- Seven hand-written programs (the round-3 probes among them) through the
  whole chain and `x86sim.py` against the host C compiler: equal, but for
  v7's `i -= d` and a truncated float initializer; ten refusal probes each
  stop with their diagnostic.
- `make check-libcatof`: `goldens`, `check`, `ops`, `ecvt` unchanged (0
  differences); `fecvt` 5,649 literals, 0 differences, every one `fstsp`'s
  high four bytes.

### `mutos_c0`/`mutos_c1`: verified this session (the `fltprobe` goldens - 9/9 byte-exact; v7's floating operand order; constant text from the MUTOS `ecvt()`)

**62/62 corpus files and 9/9 `fltprobe` files byte-exact.** The nine
programs came back from real hardware (commit `ac4ba38`); `mutos_c0` matched
every `.1`/`.2` golden it could produce at once, `mutos_c1`'s four
confirmation files differed in a few lines each, and the five open-question
goldens showed every shape `mutos_c1` had refused. Full derivation in
`docs/DEVLOG.md`'s "The fltprobe goldens".

- **The model corrections the confirmation files forced**: a floating
  constant that is exactly a float has **degree 1** (typed FLOAT - `a <
  1.5` loads 1.5 first, `e + 10` loads 10.0 first); `-1.5` is folded into
  the constant; an int `x - c` is `x + -c` (`add ax,*-48.`, v7's `optim()` -
  also in DI: `add di,*-5.`); an int converted to floating is loaded
  straight into AX after a computed `*` (`mov ax,c` / `call itof`), through
  DI otherwise; a double argument is `sub sp,*8` (no decimal point).
- **What the open-question files showed, now compiled**: v7's order for
  every floating operator - acommute() chains, the relational exchange,
  the left operand always first, a computed right operand combined on the
  runtime stack (`fldd d` / `itof` / `fsub` - no "reversed" entry points);
  a zero compared with `fcmp`, never tested; a comparison as a value and
  under `&&`; a constant's `.data` block where `cexpr()` prints it (ahead of
  a computed left operand); written constants numbered before converted
  ones; `-(-x)`; `+=`, `-=`, `/=` (target first) and `*=` (right operand
  first) with computed right operands and used values; `i *= e` (`call
  ftoi` / `mov ax,ax` / `imul i` - v7's `i * (int)e`); unused double
  results; float-returning functions; non-last computed and constant
  arguments; `long`/`char` to and from floating (`ltof`, `add sp,*4`); a
  register variable converted; in a function with a register variable in
  DI, SI for a constant argument and SI:DX for a long; file-scope
  `double`/`float` (`.comm`, `.bss`, initialized `.double`/`.float` data),
  `extern`, local `static`, arrays, pointers and members (`lea ax,16.+_ga`,
  `8.+_gs`, `mov di,p` / `lea ax,(di)`), `&` of each.
- **Constants that are not exactly a float**: an 8-byte `.double`, loaded
  with `fldd`; the text is the real `c1`'s `printf("%.17e")` - `libc.a`'s
  `ecvt()` of `libc.a`'s `atof()` on the 56-bit software double
  (`fltpr.o`'s `_pscien()` calls `_ecvt`): `0.1` -> `1.00000000000000000e-01`,
  `1e30` -> `1.00000000000000005e+30`. New `src/mutos_cc/c1_fltdec.c`
  reproduces it exactly - the value from `mutos_as`'s `fltconst.c`
  (compiled into `mutos_c1`), then v7's `cvt()` on exact 56-bit arithmetic -
  and `libcatof.py ecvt` (in `make check-libcatof`) compares it with the
  emulated `libc.a`.
- **`mutos_c1`'s floating code is planned**: every expression with a
  floating node goes through the evaluation-order planner, which rebuilds
  v7's order on the pre-scanned tree (`plan_fvalue()`, `fdeg()`,
  `plan_fchain()`, `fplan_constants()`), and the handlers generate each node
  in that order.
- **`mutos_c0`**: `+=`/`-=` into floats, `i *= e`, `e = (d *= e)`,
  char/floating conversions, float functions, floating struct members,
  local floating arrays/pointers/statics, file-scope floating variables,
  arrays, pointers and initializers (a new `INIT` opcode), file-scope struct
  variables, `(int) (<floating expression>)`.
- **Still refused** (each with its diagnostic, most asked by round 2): an
  8-byte constant where its degree decides an order (`d + 0.1`, `0.1 <
  d`), an int converted after a computed operand other than `+`/`-`/`*`,
  `x + 1`/`x - 1` converted in AX, a value read through a pointer as an
  operand of `+`/`*`/a comparison, a double array subscripted by a variable,
  `i += d`/`i -= d` into an int, a long converted with DI free (inferred:
  through DI - compiled, but no golden), initializers other than a floating
  literal that is exactly a float, `%`/`%=`, a floating zero added or
  subtracted, a floating truth test (the real `c1` itself fails on it).
- **`tests/mutos_cc/fltprobe/` round 2**: `p6_dblcon.c`, `p7_itofreg.c`,
  `p8_misc.c`, `p9_init.c` (`make -f Makefile.mutos round2`).
- **Tooling**: `dump_temp.py` decodes `INIT` and an empty symbol name;
  `x86sim.py` runs `ltof`, `.double` data and `N.+_sym` addresses (every
  round-1 golden runs: `p2_arith` returns 16 - v7's `i *= e` - its C value
  is 17); `libcatof.py ecvt`; `src/mutos_cc/fltdec_test`.

**Verification (this session):**

- `make test`: 62/62 corpus and 9/9 `fltprobe` byte-exact (71 in
  `run_goldens.sh`'s list), 0 genuine mismatches, zero warnings; `mutos_as`,
  `check_floatdat.sh`, the compiler goldens' assembly and `mutos_cpp`
  unchanged.
- `fuzz_c.py` against the previous build (`3c801a0`): 30,000 programs (seeds
  111, 121 `--scope`, 131 scalars only): 0 WRONG, 0 BAD, 398 changed but
  still correct (`x - c`, `p[i - 1]`), 1 formerly refused now correct;
  3,000 more through ASan/UBSan builds: 0 WRONG, 0 BAD.
- ASan/UBSan builds over the 71 golden inputs and the nine `11_kernel`
  refusals: clean.
- `libcatof.py ecvt`: 28,188 random C literals, `mutos_c1`'s text and size
  against the emulated `libc.a`: 0 differences; `goldens`, `check`, `ops`
  still pass.
- Seven more hand-written floating programs through the whole chain and
  `x86sim.py` against the host C compiler: all equal (but for v7's own
  `k *= a`).

### `mutos_c0`/`mutos_c1`: verified this session (floating shapes from `libc.a`'s compiled C - comparisons, int constants, `fneg`, `*=`/`/=`, double parameters, arguments and returns)

**Still 62/62 byte-exact; the new shapes come from real compiler output that
is not a golden.** Four `libc.a` objects are C compiled by the real MUTOS
1700 compiler - `atof.o`, `ecvt.o`, `gcvt.o`, `fltpr.o` - and their code
has most of what "Next up" item 1 listed as waiting for a golden. They were
compiled with `-O` (`atof.o` shows `c2`'s cross-jumping), so only what the
optimizer cannot have changed is taken. Full derivation - per-statement
code, the runtime's `fcmp`/`fadd`/`fneg`, how v7's `optim()`/`cexpr()`/
`acommute()` explain each shape - in `docs/DEVLOG.md`'s "Floating shapes
from libc.a's compiled C".

- **Comparisons** as conditions: both operands loaded (after v7's
  `degree()` exchange - `arg < 0` becomes `0 > arg`), `call fcmp`, `sahf`,
  the ordinary signed branch (`atof.o`'s `fl<big`, `ecvt.o`'s `arg<0`,
  `fi != 0`, `arg > 0`). `mutos_c0` types the node INT and converts an int
  operand to the floating one's type, as v7's `build()` does.
- **Int constants converted** to floating are `.float` constants, `%.17e`
  of the value - zero included, `.float 0.00000000000000000e+00`, whose
  real bytes `bc a2 31 00` are every zero constant in `atof.o`/`ecvt.o`; a
  written zero is accepted the same way.
- **Unary minus** (`mutos_c0` now writes `NEG`, typed like its operand):
  load, `call fneg`.
- **`*=`** loads the right operand and multiplies by the target in memory,
  **`/=`** loads the target (`atof.o`'s `flexp *= exp5`, `fl /= flexp`,
  `ecvt.o`'s `arg *= 10`) - the right operand a variable or a constant.
- **A computed left operand with an int right operand**: `call itof`, then
  `stkmath.o`'s stack-with-stack `fadd`/`fsub`/`fmul`/`fdiv` (`atof.o`'s
  `10*fl + (c-'0')`) - not the "reversed" `fsubrs`/`fdivrd`... the old
  "Next up" guessed; nothing compiled in `libc.a` uses those.
- **An assignment's value used**: `fstd`/`fsts`, no pop (`ecvt.o`).
- **Double and float parameters** (8 bytes; a float parameter is a
  double), **double arguments** (`sub sp,*8.` / `mov ax,sp` / `call
  fstdp`), **functions returning a double**: `lea ax,fac` / `call fstdp` /
  `lea ax,fac` before the return jump, `|RTYP 3`, and `call fldd` after
  the call in the caller.
- **Still refused**, each with a diagnostic: a zero compared as the right
  operand (a float variable or a computed value against 0 - v7 tests
  those, stkmath.o's `ftest`, never seen), truth tests, a comparison as a
  value, `&&`/`||`/`?:`, a constant right operand of a computed value,
  other computed right operands and `d + i`, `+=`/`-=`, `*=`/`/=` by a
  computed value or an int variable, non-float and 2\*\*24+ constants, a written constant
  after a converted one in one expression, constant or non-last computed
  call arguments, an unused double result, `-(-x)`, float-returning
  functions, floating globals/statics/arrays/pointers/members,
  `char`/`long` to and from floating.
- **`tests/mutos_cc/fltprobe/`** (new): nine programs for real hardware -
  four exercising only the new shapes (they become `08_float/03`..`06`
  once their goldens exist; `mutos_c1`'s output for each runs under
  `x86sim.py` and returns the host C compiler's value), five exercising the
  refused ones.
- **Tooling**: `dump_temp.py` decodes `NEG`; `x86sim.py` runs `fsts`/
  `fstd`, `fadd`...`fdiv`, `fneg`, `fcmp` + `sahf` and `fac`, and no longer
  divides eagerly (a floating multiplication by zero used to stop it with a
  Python `ZeroDivisionError`).

**Verification (this session):**

- `make test`: 62/62 byte-exact, 0 genuine mismatches, zero warnings;
  `mutos_as` 76/76, `check_floatdat.sh` 13/13, 71/71 compiler goldens
  assemble, `mutos_cpp` 5/5.
- Against the previous build (`3c801a0`), all 71 golden inputs:
  `mutos_c0` and `mutos_c1` alone byte-identical, diagnostics included.
- `fuzz_c.py` against the previous build: 42000 programs (18000 with
  arrays, seed 11; 18000 `--scope`, seed 21; 6000 scalars only, seed 31):
  all identical, 0 WRONG, 0 BAD; 4500 more (seeds 41, 51) through the
  ASan/UBSan builds: 0 WRONG, 0 BAD. The generator makes no floating
  code.
- 13 hand-written floating programs (the four `fltprobe` confirmation
  files among them) through `mutos_cpp`, both passes, `mutos_as` and
  `x86sim.py`, compared with the host C compiler: all equal; 27 refusal
  probes and the five `fltprobe` open-question files each stop with their
  diagnostic.
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 71 golden inputs and
  all of the above: clean, output identical to the `-O2` build's.
- `x86sim.py`: output unchanged on all 53 goldens it runs.

### `mutos_c0`/`mutos_c1`: verified this session (`08_float` - floating point through libc's software floating-point runtime)

**62/62 byte-exact end-to-end, up from 60** - both `08_float` files; the
62-file construct corpus is complete. Full derivation in `docs/DEVLOG.md`'s
section of the same name.

- **`mutos_c0`.** `float`/`double` locals (4- and 8-byte frame slots:
  `-8`/`-12`/`-16`, `-12`), floating constants - `FCON(DOUBLE, "3.5")`, the
  source text written with v7's `'F'` format (no `_`), never folded - and
  `+ - * /` typed `DOUBLE` whatever the operands (v7's "`if (t==FLOAT) t =
  DOUBLE`"; only a `NAME` keeps `FLOAT`); an assignment is `ASSIGN(DOUBLE)`
  even into a float. Conversions are v7's `cvtab[]`: `ITOF`/`LTOF` to
  floating (typed with the target's type - `d = i;` is `ITOF(DOUBLE)`,
  `a = i;` into a float `ITOF(FLOAT)` - or, in `+ - * /`, the floating
  operand's), `FTOI`/`FTOL` from it (`(int) c`, `(long) d`, `(int) d`);
  `return c;` in an int function converts as `doret()`'s assignment does
  (`FTOI RFORCE`, the golden's stream for `return (int) c;`). An integer
  LEFT operand of `+ - * /` is converted after its own bytes, ahead of the
  right operand's (the right operand is buffered - `arith_rhs_begin()`).
  The lexer keeps the constant's text, takes exponents and `.5`; `long
  float` is `double`, `sizeof` gives 4 and 8.
- **`mutos_c1`.** No FPU: every floating operation is a call into libc.a's
  runtime, which keeps a stack of doubles and takes an operand's ADDRESS in
  AX - `lea ax,<x>` / `call flds|fldd` (load), `call fadds|fmuls|fdivs`
  (top op memory), `call fstsp|fstdp` (store and pop), `mov di,<int>` /
  `mov ax,di` / `call itof`, `call ftoi` (-> AX), `call ftol` / `mov
  di,dx` / `mov si,ax`. A constant is written where it is read: `.data` /
  `L10000:<TAB>.float 3.50000000000000000e+00` / `.text` (`%.17e`, the
  label from c1's own counter); a file with floating code ends `.globl
  fltused`. The confirmed shapes are exactly the goldens'; `-` (`fsubs`/
  `fsubd`), a double right operand (`faddd`, ...), an int computed into DI
  converted, an int left operand of `+ - * /` and a long function returning
  a double follow the same pattern and the runtime's own entry points (no
  golden has one).
- **Evidence beyond the goldens** (all real MUTOS 1700 objects in
  `tests/mutos1700_libc/`): the runtime's entry points and calling
  convention (symbol tables and disassembly of `stacks.o`, `singles.o`,
  `doubles.o`, `convert.o`, `lconvert.o`); the floating format - the
  excess-128 exponent in the value's highest byte, the sign below it,
  v7's "0.1xxx" mantissa (`atof.o`'s `2**56` is `00 00 00 b9`, `ecvt.o`'s
  `10.0` is `00 00 20 84`); that only a constant exactly representable as
  a float is kept in 4 bytes (`ecvt.o`: `10.0` and `1.0` in 4 bytes, `.03`
  in 8); and `%e`'s two-digit, signed exponent (`fltpr.o`'s `_pscien`).
- **Refused (explicitly)**: floating comparisons and conditions, `%`,
  unary `-`, `!`, `~`, `++`/`--`, compound assignment, `&&`/`||`/`?:`/`,`
  around a floating value, a floating call argument, parameter, return
  type, global, static, array, pointer or struct member, a `char` mixed
  with a floating value; in `mutos_c1` a computed right operand (`d =
  (d + e) * (d + e);`, and so `a + i` with the int on the right), a
  constant right operand of a computed value, a constant converted to an
  integer or an int constant to floating, an int in AX (a call's result, a
  product) or a register variable converted, `LTOF`, an unused computed
  value, and every constant that is zero, not exactly a float, 2**24 or
  more, or longer than 18 significant digits.
- **`mutos_as`** could not assemble this output when this section was
  written (`.float` silently skipped, then an explicit error; `lea ax,L10000`
  an "unsupported instruction"). Done in a later session: it now assembles,
  links with the real `crt0.o`/`libc.a` and returns the C source's value
  when run - see Milestone 2's "`.float`, `lea <reg>,<label>`, ..." section.
- **`dump_temp.py`** decodes `FCON` (its text), `ITOF`, `FTOI`, `FTOL` and
  `LTOF`; **`x86sim.py`** runs floating code against a model of the runtime
  (host floats on a stack, `.float` data and stores in the MUTOS format).

**Verification (this session):**

- `make test` (clean build): 62/62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build (`c2c7a68`): `mutos_c0` on all 71 golden `.i`
  files changed the two `08_float` files (refusals -> their `.1`/`.2`
  goldens) and, for four `11_kernel` files, only the wording of the
  local-declaration diagnostic (it now lists `float`/`double`; their
  temp1 output is identical); `mutos_c1` alone on the 71 golden pairs
  changed the two `08_float` files only (refusals -> their `.s.golden`).
- `dump_temp.py` over all golden `.1`/`.2` files: output changed for the
  two `08_float` `.1.golden`s only; every construct-corpus golden now
  decodes to the end (only six `11_kernel` ones still stop).
- `fuzz_c.py` against the previous build: 42000 programs (18000 arrays and
  scalars, seed 11; 18000 `--scope`, seed 21; 6000 scalar-only, seed 31):
  0 WRONG, 0 BAD, all byte-identical; 4500 more through ASan/UBSan builds:
  0 WRONG, 0 BAD. The generator makes no floating code.
- 11 hand-written floating programs (float/double mixes, every operator,
  both precisions, `itof` from memory, from DI and from a file-scope int,
  `ftoi`/`ftol`, float code in loops and in called functions returning
  int and long) through `mutos_cpp`, both passes and `x86sim.py`, compared
  with the host C compiler: 10 match, one stops at its refusal (an int
  right operand); about 60 probes of refused constructs each stop with
  their diagnostic.
- `x86sim.py`: 53 of the 62 goldens run, up from 51 - both `08_float`
  ones (7 and 3).
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 71 golden inputs,
  the hand-written programs and the probes: clean.

### Test corpus: `11_kernel/` added (nine real kernel driver files, own real-hardware goldens); `mutos_c0`/`mutos_c1` coverage assessed at 0/9

Not a change to `mutos_c0`/`mutos_c1` themselves. `tests/mutos_cc/11_kernel/`
(commit `dd5dd73`) adds a second, separate golden corpus - nine real,
unmodified MUTOS kernel driver source files (`01_delay.c` … `09_amx.c`)
plus their header tree, with real-hardware-verified goldens already
present, mirroring what `tests/mutos_cpp/c/` already does for `mutos_cpp`.
Running `tests/mutos_cc/run_goldens.sh` against this session's build shows
all nine still refused at the `mutos_c0` stage, each with a diagnosed
error: one genuinely new gap confirmed with a minimal reproduction (a bare
expression-statement, e.g. `i++;` on its own - `01_delay.c` - which the
"Current scope" grammar has no production for at all, since every prior
`++`/`--` use in the corpus is inside an assignment); two hits on
already-documented gaps (`05_nami.c`'s untyped `register c;` local, and
`06_fio.c`'s file-scope struct/`unsigned` variable); and six
("external definition syntax", `02_prim.c`/`03_mem.c`/`04_pipe.c`/
`07_v24.c`/`08_tty.c`/`09_amx.c`) not yet root-caused to a specific
construct. See `docs/DEVLOG.md`'s Milestone 4 section for the full
per-file breakdown. The construct-coverage corpus's own 60/62 pass
fraction (below) is unaffected - `11_kernel` is tracked separately.

### `mutos_c0`/`mutos_c1`: verified this session (`06_struct` - structs, unions, bit-fields, enums, typedefs - and `10_integ/03_linklist`; `char` with an int operand, as a call argument, as a condition, stored into a file-scope array)

**60/62 byte-exact end-to-end, up from 50 of 62** - all nine `06_struct`
files and `10_integ/03_linklist`; only `08_float`'s two files are left.
Full derivation in `docs/DEVLOG.md`'s section of the same name.

- **Structs, unions, enums and typedefs (`mutos_c0`).** Declarations are
  laid out as v7's `c03.c` does (`align()`: a non-char member on a word
  boundary; bit-fields packed into words, a field that does not fit
  starting the next; a union's members all at offset 0; the size rounded
  to a word), and write nothing to temp1 themselves. A member reference
  is v7's `build()` - `a.b` as `(&a)->b`, `p->b` as `*(p + offset)` -
  including `setype()`/`disarray()`'s retyping of the whole
  NAME/AMPER/PLUS/STAR spine to the member's type (`p.x` is `NAME(p,
  INT) AMPER(8) CON 0 PLUS(8) STAR(0)`; a subscript's `ITOP` keeps its
  pointer-to-struct type), so a chain is collected and written once its
  last member is known. A bit-field's `STAR` is followed by the new
  `FSEL(UNSIGN, bitoffs, flen)` (opcode 10), a whole-struct assignment
  is `ASSIGN(4)` plus the new `STRASG(4, size)` (115). Enum constants
  are int `CON`s, a typedef name stands for its type, `sizeof` takes a
  type name, and a pointer cast of a call (`(struct node *) malloc(...)`)
  types the `CALL`. Unsigned arithmetic (a bit-field's value) is typed
  `UNSIGN`; `/`, `%`, `>>` and ordered comparisons on it are refused.
  All ten files' `.1`/`.2` goldens match through `mutos_c0` alone.
- **Struct code (`mutos_c1`).** A store through "pointer + constant" (a
  member other than the first) computes its right-hand side FIRST - a
  new evaluation-order plan - and then the pointer into the next free
  register: `mov di,*-6.(bp)` / `mov si,*-8.(bp)` / `mov *2.(si),di`
  (`cur->next = head;`); the first member keeps the push/`pop bx` shape.
  An address or pointer goes into DI, or SI while DI holds a value; a
  subscript index into DI, SI or - both taken - DX (`lea si,*-16.(bp)` /
  `mov dx,*-18.(bp)` / `sal dx,*1` / `sal dx,*1` / `add si,dx`). Member
  offsets become one displacement (`rp->botright.y` -> `*6.(di)`, `&*`
  pairs and `+0`s folded). A dereference that is the left operand of
  `+`/`-`/a comparison with code on the right is loaded first, and the
  right one then added from memory (`add di,*2.(si)`). `x - *p` with a
  computed left operand pushes `p` first (`push *4.(bp)` ... `pop bx` /
  `sub di,(bx)`), and an int `+` chain is reordered as v7's `acommute()`
  does (`sum + pts[i].x + pts[i].y` adds `sum` last). A bit-field is
  read as `mov` / `sar` / `and` and stored from a constant as `and`
  (complemented mask - in hex, `*/fff3`, `#/ff0f`, when the value is not
  0) plus `or`; a 4-byte struct is assigned as a long, a 2-byte one as an
  int. `x = y;` between two memory operands now goes through DI
  (`03_linklist`'s `head = cur;`), and `i * 2` is `sal di,*1`.
- **More `char` (`mutos_c0`/`mutos_c1`).** A char with one int leaf
  operand is computed in AX (`movb ax,...` / `cbw` / `add ax,*-48.` for
  `c - '0'`, `inc`/`dec` for 1, `imul`, shifts), a char AND a constant
  0..127 in DX without the `cbw` (`movb dx,#_amxscd(bx)` / `and dx,*12.`
  / `beq` - the flags of the AND tested directly), a char compared with an
  int leaf goes left and is widened into AX; a char object as a call
  argument is `movb ax,...` / `cbw` / `push ax`, as a condition a byte
  compare with 0; a store into a file-scope char array element pushes the
  index (`push *-8.(bp)` ... `pop bx` / `movb _t(bx),dx`). Evidence: the
  real compiler's non-optimized kernel output (`kernel_nonopt/amx.s`,
  `lp_AC.s`) and `kernel_opt/genio.s`, `ifss.s`, `cons_NOBIOS.s` - no
  golden has these shapes.
- **`dump_temp.py`** decodes `STRASG` and `FSEL`; **`x86sim.py`** takes
  the flags of `and`/`or`/`xor` (as `cmp <result>,0`), for a masked char
  or bit-field tested directly. **`mutos_c0`** no longer misreads the
  body of a refused function definition (a function returning a struct
  pointer) as further external definitions.
- **Refused (explicitly)**: file-scope struct/union/unsigned variables,
  functions returning a struct or union or a pointer to one, a struct
  parameter by value, `unsigned` variables, `long` members, char and
  union bit-fields, arrays of arrays as members, a struct larger than 4
  bytes assigned as a whole, a struct size that is not a power of two in
  a subscript, a computed value or an out-of-range constant stored into
  a bit-field, `&` of a bit-field, compound assignment or `++`/`--` on
  one, a char member reached through a pointer plus an offset, an address
  needed while DI and SI both hold values, and the shapes the
  register-occupancy guard stops (a store through a pointer from a
  `register` variable in DI, ...).

**Verification (this session):**

- `make test` (clean build): 60/62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build (`84d6548`): `mutos_c0` on the 62 golden
  `.i` files changed exactly the ten struct files (refusals -> their
  `.1`/`.2` goldens; 60 of 62 now match through `c0` alone); `mutos_c1`
  alone on the 62 golden pairs changed `02_stptr`, `03_starray`,
  `04_stassign`, `05_nestst`, `07_bitfield` and `03_linklist` (refusals
  -> their `.s.golden`; 60 of 62 now match through `c1` alone, up from
  54).
- `dump_temp.py` over all 124 golden `.1`/`.2` files: output changed for
  `04_stassign.1.golden` and `07_bitfield.1.golden` only (both decoded to
  the end now); only `08_float`'s two `.1` goldens still stop.
- `fuzz_c.py` against the previous build: 42000 programs (18000
  arrays and scalars, seed 11; 18000 `--scope`, seed 21; 6000 scalar-only,
  seed 31): 0 WRONG, 0 BAD; 324 changed and still correct, 1261 correct
  that the previous build refused, the rest byte-identical. The changes
  are the two intended ones: an AND/OR/XOR whose left operand is already
  in AX or DX (a product, a quotient, a call's result) is done there
  (`and ax,*10.` / `mov *-26.(bp),ax`, no `mov di,ax`), and a dereference
  on the left of `+`/`-` is loaded before the right operand's code. The
  generator makes no chars and no structs.
- 18 hand-written programs run through `mutos_cpp`, both passes,
  `mutos_as` and `x86sim.py`, compared with the host C compiler (`short`/
  `signed char`): ten `char` programs (a char with `+`/`-`/`&`/`|`/`^`/
  `*`/shifts and an int constant or variable, on either side where the
  operator commutes, relational operators against an int, chars as call
  arguments and
  conditions under `&&`/`||`/`!`/`?:`, masks tested directly, stores into
  a global array element) and eight struct programs (stores through a
  pointer at every offset from a char, a call, a comparison, a
  dereference and a subtraction; arrays of structs through `a[i]` and
  `p[i + 1]`; nested structs through a pointer, `&r.b` passed on;
  bit-fields of 1 to 8 bits set to 0, to all ones and in between, read in
  conditions and sums, through a pointer too; a linked list in a local
  array) - all correct, all assemble. Five refusal cases stop with their
  diagnostic.
- `x86sim.py`: 51 of the 62 goldens run, as before - all nine `06_struct`
  ones among them (`07_bitfield` 12, `05_nestst` 40).
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 62 golden inputs,
  the hand-written programs and 6000 fuzzed programs (a quarter
  `--scope`): clean.

### `mutos_c0`/`mutos_c1`: verified this session (`10_integ/01_wordcount` - a file-scope char array, character constants, a char compared with a constant)

**50 of 62 byte-exact end-to-end, up from 49 of 62** - `10_integ/
01_wordcount`, the last `10_integ` file that needs no structs. Full
derivation in `docs/DEVLOG.md`'s section of the same name.

- **A file-scope `char name[] = "..."` (`mutos_c0`).** v7's `extdef()`
  for an initialized declarator, confirmed byte-for-byte against
  `01_wordcount.1.golden`: `SYMDEF "_text"`, `DATA`, `NLABEL "_text"`,
  the string's `BDATA` runs (the label-less form of `putstr()`, in
  temp1: runs of 14, 15 and 16 values - the first golden with a string
  of 15 or more characters, so v7's run split is now confirmed directly),
  `EVEN` for the odd size (45). The string token uses up a label number
  (v7's lexer: `cval = isn++`), so `main()`'s labels start at 2. A
  `static` one writes no `SYMDEF` (v7; no golden). Elements are the
  ordinary subscript shape on `NAME(12, 1, "_text")`. Refused: a sized
  array, a brace list, no initializer, `extern` with one, a second
  declaration.
- **Character constants (`mutos_c0`)** were not parsed at all (`'\n'` -
  "expected an expression"): now an int `CON` (v7's `getcc()`).
- **A char compared with a constant (`mutos_c0`).** A char variable,
  element or dereference compared with a constant in 0..127 is not
  widened; the constant is typed char - `STAR(1) CON(1, 10) EQUAL(0)` -
  v7 `c12.c` `optim()`'s CHAR retyping, which the MUTOS front end does
  itself (`char_compare_rhs()`). Any other char comparison keeps its
  `ITOC`s (and stays refused by `mutos_c1`).
- **`mutos_c1`.** `DATA` renders `.data`, a temp1 `BDATA` run the
  string-literal renderer's `.byte` lines (`put_bdata_run()`, now shared
  with `gen_strings()`). `text[i]` is `VK_SYMIDX` until the `STAR`, which
  loads the index - `mov dx,*-6.(bp)` / `mov bx,dx` - and makes the
  element `_text(bx)`. A char compared with a char-typed constant is a
  byte compare (`VK_COND.cond_is_byte`): `cmpb _text(bx),*10.` (also for
  an addressable char - `cmpb *-8.(bp),*97.` in `kernel_nonopt/lp_AC.s`),
  and a computed element against 0 `movb dx,#_text(bx)` / `orb dx,dx` -
  v7's `cctab` `%n*,z` template; the `#` marks a symbol displacement
  loaded through v7's `#1` operand (`o_load()`, also used for a char
  element stored into a char: `movb dx,#_amxi_bu(bx)` in
  `kernel_nonopt/amx.s`). Refused: a local char array element with a
  runtime index in a comparison, a store into a file-scope array
  element, `&text[i]`, `text + i`, a constant index on a file-scope
  array.
- **Pre-existing silent wrong code fixed (refused):** `if (!(l > 0L))`
  compiled to `cmp *-8.(bp),<unmaterialized-long-const>` (a placeholder
  as operand text, one word compared), exit status 0 - `OP_EXCLA` built
  its inverted comparison without `cond_is_long`. It keeps the flag now
  (and the new byte flag), and `gen_long_cmp()` refuses the inverted
  operator like any other unconfirmed long comparison.
- **`dump_temp.py`** decodes `DATA` (`01_wordcount.1.golden` used to
  stop there); **`x86sim.py`** executes `.byte` data (a char array's, a
  string literal's), `_text(bx)`/`#_text(bx)` operands, `cmpb` and `orb`
  - 51 of the 62 goldens now run (up from 50), `01_wordcount` returning
  55, its C source's value.

**Verification (this session):**

- `make test` (clean build): 50 of 62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build: `mutos_c0` on the 62 golden `.i` files
  changed exactly `01_wordcount` (a refusal -> its `.1`/`.2` goldens); 50
  of 62 now match through `c0` alone. `mutos_c1` alone on the 62 golden
  pairs: only `01_wordcount` changed (a refusal at opcode 203 -> its
  `.s.golden`); 54 of 62 now match through `c1` alone.
- `dump_temp.py` over all 124 golden `.1`/`.2` files: output changed for
  `01_wordcount.1.golden` only.
- `fuzz_c.py` against the previous build: 18000 programs (seeds 11-16, two
  scalar-only) and 18000 with `--scope` (seeds 21-26, three scalar-only):
  all 36000 byte-identical, 0 WRONG, 0 BAD (the generator makes no chars
  and no negated long comparison).
- 10 hand-written programs run through `mutos_cpp`, both passes,
  `mutos_as` and `x86sim.py`, compared with the host C compiler (`short`/
  `signed char`): char locals and globals against constants with all six
  operators and against 0, vowel counting with `||`/`&&` over a global
  array, `c = s[i]` / `n = s[i]` / `return s[i]`, a `static` array with
  `else if`, comparisons as values and under `!`, three initializers
  (odd and even sizes, runs of 14 and 13) plus a literal walked by a
  `char *` against 0, `*p == 'h'`, character constants including `'\377'`,
  and `01_wordcount` itself: all 10 correct, all assemble. 17 refusal
  cases (a negative or larger constant, a constant on the left, two
  chars, a constant index, a store, `&s[i]`, `s + i`, a char index, a
  local array element, a char plus an int, a char condition, a sized, an
  uninitialized, an `extern`-initialized and a redeclared array, and
  `!(l > 0L)`) stop with their diagnostic.
- `x86sim.py` mutation check on `01_wordcount`: a flipped branch after a
  `cmpb` changes the result (55 -> 88); a broken `orb` test and a wrong
  base register are reported.
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 62 golden inputs,
  the hand-written programs and 3000 fuzzed programs (half `--scope`):
  clean.

### `mutos_c0`/`mutos_c1`: verified this session (`07_scope` - file-scope variables and block scope)

**49 of 62 byte-exact end-to-end, up from 46 of 62** - all three `07_scope`
files. Full derivation in `docs/DEVLOG.md`'s section of the same name.

- **File-scope variables (`mutos_c0`).** A top-level declarator without
  `(` is a variable, written where it is declared, as v7's `extdef()`
  writes it: `int counter;` -> `CSPACE("_counter", 2)`, `static int
  hidden;` -> `BSS`, `NLABEL("_hidden")`, `SSPACE(2)`, `extern int
  total;` -> nothing (`03_externdef`'s later `int total;` writes its
  `CSPACE` between `addto()` and `main()`). New MUTOS delta: no
  `SYMDEF("")` in front of a static's `BSS`, which v7 writes
  (`mutos_cc.h` delta 5). Every file-scope name is `SC_EXTERN` and is
  referenced as `NAME(12, type, "_counter")` - a second, file-wide symbol
  table searched after the function's own (`lookup_var()`), and
  `emit_name()` for the two `NAME` shapes. A compatible redeclaration is
  accepted; a file-scope pointer, array or initializer and a `static`
  function are explicit refusals.
- **Block scope (`mutos_c0`).** A nested compound statement has its own
  scope (`parse_nested_block()`, v7's LBRACE case with `blockhead()`/
  `blkend()`): a declaration may shadow an outer one (`02_shadow`: inner
  `x` at -8 below the outer one at -6, `SETSTK 8`; before: "'x'
  redeclared") and disappears at the `}`, where the allocation point and
  a claimed `register` slot are restored. Slot reuse by sibling blocks
  is v7's algorithm; no golden has one (before, each sibling block got
  fresh slots - a larger frame than the real compiler's, e.g. `sub
  sp,*14.` where v7 reserves 8 bytes).
- **`mutos_c1`.** Such a `NAME` is a `VK_STATIC` memory operand named by
  its symbol (new `Val.sym`, pooled by `intern_name()`), so a global
  takes exactly the code paths a local static takes (`mov di,_counter` /
  `inc di` / `mov _counter,di`), `&x` is `#_counter`; `CSPACE` renders
  `.comm\t_counter,2` (decimal, no `.`), `NLABEL` a `_hidden:` label
  glued onto the `.blkb` line. `++`/`--`, compound assignment and `long`
  on a global are refused, as on a local static.
- **Pre-existing silent wrong code fixed (refused):** `a = b;` with two
  local statics was `mov L4,L5` (no 8086 instruction - `mutos_as`
  rejects it), exit status 0. A static or global right-hand side into a
  memory target is now refused like a local one.
- **`dump_temp.py`** decodes `CSPACE` and `NLABEL` (both goldens used to
  stop there); **`x86sim.py`** executes fixed-address variables (`.comm`,
  `.blkb`), so 50 of the 62 goldens now run (up from 47), each returning
  its C source's value; **`fuzz_c.py --scope`** generates file-scope
  variables (plain, `static`, `extern` first) and shadowing blocks,
  checking the globals at their addresses.

**Verification (this session):**

- `make test` (clean build): 49 of 62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build: `mutos_c0` on the 62 golden `.i` files
  changed exactly the three `07_scope` files to their `.1`/`.2` goldens,
  plus partial output and messages of eleven files that still fail; the 46
  passing files are unchanged. `mutos_c1` alone on the 62 golden pairs:
  only `01_globstat`/`03_externdef` changed (refusal -> `.s.golden`) - 53
  of 62 now match through `c1` alone.
- `fuzz_c.py` against the previous build, 18000 programs (six seeds, two
  scalar-only): all 18000 identical. With `--scope`, 18000 more (six
  seeds, three scalar-only): 0 WRONG, 0 BAD, 9774 compiled and correct at
  every statement (the previous build refused them all), the rest
  refused as before. A mutation check (the last store into a global
  dropped from the output) was caught by the checker in 355 of 408
  programs - the other 53 dropped a store that changes nothing.
- 22 hand-written programs (globals across functions, `static` and
  `extern` forms, shadowing at two levels, a parameter shadowing a
  global, `return` inside a block, sibling blocks, `&global` through a
  pointer, a global loop counter, a `register` local next to globals,
  char globals copied through a char local, recursion counting calls in
  a global), executed with `x86sim.py` and compared with the host C
  compiler: all 22 correct, all assemble with `mutos_as`; 11 refusal
  cases behave as documented.
- ASan/UBSan builds of both passes over the 62 golden inputs, the
  hand-written programs and 3000 fuzzed programs: clean.

### `mutos_c0`/`mutos_c1`: verified this session (`char` element access - all six `09_abiprobe` frame files; call arguments right to left - `10_integ/04_strrev`; `10_integ/02_bubsort`)

**46 of 62 byte-exact end-to-end, up from 38 of 62** - eight more corpus
files: the six `09_abiprobe/0N_frameNNN` files, `10_integ/04_strrev` and
`10_integ/02_bubsort`. Full derivation in `docs/DEVLOG.md`'s section of
the same name.

- **`char` conversions (`mutos_c0`).** The MUTOS front end wraps chars in
  opcode 109 (`OP_ITOC`), its type the RESULT type - `ITOC(1)` for an
  int stored into a char (`buf[0] = 1;`), `ITOC(0)` for a char used as
  an int (`buf[0] + buf[79]`, both operands; `return buf[0];`); none
  for char = char (`t = *a;`). `mutos_c0` inserts them where v7's
  `build()` applies conversions: every operand of a binary arithmetic,
  shift, bitwise, relational or equality operator (`promote_char()`),
  every `=` (`convert_assign()`: also `CTOL` for a char into a long,
  and the existing `ITOL`), `return` in an int function, and a new
  `(int) c` cast. A char used where v7 converts nothing and no golden
  shows the MUTOS shape - a condition, an operand of `&&`/`||`/`!`/`~`/
  `?:`, a call argument, a comma operator's last operand - is an
  explicit "not yet supported". Also new: `&` of a subscripted element
  (`&s[lo]`) and the `if (...) return;` shortcut (`CBRANCH` straight to
  the return label). `mutos_c0` now reproduces all eight files'
  `.1`/`.2` goldens.
- **Byte operands (`mutos_c1`).** A char in memory is flagged (`Val.
  bytev`) and handed only to consumers that use a byte instruction (a
  char `ASSIGN`, `ITOC`, `CTOL`, `AMPER`); every other consumer refuses
  it. A char read as an int is a lazy `VK_CHARX`, loaded by its consumer
  as `movb ax,<mem>` / `cbw` - confirmed consumers only: int `+` of two
  chars (`... cbw` / `mov di,ax` / `movb ax,...` / `cbw` / `add di,ax`),
  `return`, and an int assignment (`mov <lhs>,ax`, from the real
  non-optimized kernel output). A char through a pointer variable goes
  through DX and BX (`mov dx,*4.(bp)` / `mov bx,dx` / `movb dx,(bx)`),
  a char store through DX (`movb *-6.(bp),dx`), a pushed char target is
  popped into BX as for int. A constant stored into a char folds to its
  sign-extended low byte (`movb *-84.(bp),*1.`).
- **Displacement marker.** A memory displacement outside -128..127 now
  takes `#` like an immediate (`movb #-132.(bp),*1.` - `03_frame128`
  ... `07_frame300`); it was always `*`.
- **Call arguments right to left.** A call with two or more arguments,
  any but the last of which has code of its own, is generated through
  the evaluation-order plan: each argument computed and pushed from the
  last to the first, v7's `comarg()` order (`reverse(s, lo + 1, hi -
  1)` -> `mov di,*8.(bp)` / `dec di` / `push di` / `mov di,*6.(bp)` /
  `inc di` / `push di` / `push *4.(bp)`). Before, the second computed
  argument overwrote the first (a register-guard refusal).
- **`02_bubsort`'s shapes.** A relational's operands are exchanged
  (operator mirrored) when a variable is compared with a computed value
  - v7 `optim()`'s swap by `degree()`: `i < n - 1` -> `cmp di,*-6.(bp)`
  / `ble`; a register left operand is compared directly with a memory
  right one (no more `mov di,<mem>` first); the left element of `a[j] >
  a[j + 1]` is loaded before the right one's address is computed (in SI);
  and `a[j + 1]` keeps the `+ 1` out of the index (v7's `distrib()`): it
  becomes the displacement `*2.(si)`, or a final `add di,*2.` for
  `&a[j + 1]`.
- **Pre-existing silent wrong code fixed or refused.** A plain `char`
  variable used as an int (`return c;`, `a + b` of two char locals) was
  read as a whole word, including the slot's unused high byte - no
  conversion from `mutos_c0`, a word `mov` from `mutos_c1`, exit status
  0 (fixed). A comparison as a call argument (`f(a < b)`) was written as
  `push <unmaterialized-cond>` (fixed), and `return &x;` returned `x`
  (fixed: an address is loaded with `lea`). Pointer arithmetic the
  fuzzer never generates: `q - 1` on an `int *` stepped one byte, `q - p`
  gave the byte difference, `i + p` added an unscaled `i`, `p < q`
  compared signed (v7 uses unsigned `LESSP`...), and `p == &x` compared
  with `x`'s contents - all now explicit refusals.
- **`x86sim.py`** executes `movb`/`cbw`, calls of functions in the same
  file (with `cret`), `chkstk` and `#` displacements: 47 of the 62
  goldens now run (up from 30), each returning its C source's value.

No new opcode and no argument-shape change, so `dump_temp.py` is
unaffected (it already decoded `ITOC`/`CTOL` with either type).

**Verification (this session):**

- `make test` (clean build): 46 of 62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build: `mutos_c0` on the 62 golden `.i` files
  changed exactly the eight files, each from a refusal to its golden
  `.1`/`.2`; `mutos_c1` alone on the 62 golden pairs changed eleven -
  nine from a refusal to their `.s.golden` (the eight plus `06_struct/
  06_union`, whose `c0` side still needs structs), and two struct files
  that still refuse, later, with identical output before that.
- `fuzz_c.py` against the previous build: 18000 programs (six seeds, two
  scalar-only), 0 WRONG, 0 BAD, 0 regressions; 1426 refused -> correct,
  292 changed and still correct - all of them only in the two comparison
  shapes above.
- 36 hand-written char/call/sort/pointer programs (the fuzzer generates
  none of these), executed with `x86sim.py` and compared with the host C
  compiler: all 26 that compile and return a value are correct (a 27th
  returns an address, checked by hand); 9 explicit refusals.
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 62 golden pairs,
  the hand-written programs and 2400 fuzzed programs: clean.

### `mutos_c1`: verified this session (conditional evaluation: `&&`, `||`, `?:`, `,` and postfix `++`/`--` in conditions - former open items 6 and 7 fixed)

**Both open wrong-code bugs the fuzzer found are fixed**, by generating
every conditionally evaluated operand where v7's code generator does -
inside its branch structure:

- **A postfix `++`/`--` in a condition** (`if (n++ > 9)`, `while (n--)`)
  now has its fixup emitted right after the operand is loaded, before the
  compare - the real non-optimized compiler's shape, found in
  `tests/mutos_as/kernel_nonopt/` (five instances in `lp_AC.s` and
  `sys1.s`): `mov R,n / dec n / or R,R / beq L` for a truth test,
  `... / or R,R / ble L` against 0, `mov R,n / dec n / cmp R,*2. / blt L`
  against 2. `or R,R` - not `cmp R,*0` - is how the real compiler tests a
  value it has just computed into a register (v7's `tst r` fallback);
  `mutos_c1` now emits it for a postfix operand's value. Before, the
  fixup waited for the next `EXPR`, inside the branch taken when the test
  held: `if (n++ > 9)` left `n` alone when false; `do ... while (n-- >
  0)` never terminated.
- **`&&`, `||`, `?:` and `,` are generated through the evaluation-order
  plan** (`plan_value()`/`plan_cbranch()` in `c1_gen.c`): each operand's
  temp1 range is replayed at its v7 position, between new plan steps
  that emit the branches, labels and 0/1 values (v7's `cexpr()` for
  value contexts, `cbranch()` for conditions). Before, all operands were
  computed ahead of the branches: `z = x ? y++ : 4;` always incremented
  `y`, `z = (x > 1) && (y++ > 0);` likewise, and `z = x ? y / x : 1;`
  divided by zero (on an 8086: a divide-error trap) when `x` was 0.
- **An `if`/`while`/`for` condition with `&&`/`||`/`!` is now jumping
  code**, never a materialized 0/1 that is then tested - the shape
  `10_integ/01_wordcount.s.golden` shows for its `||` (`cmp ... / beq
  L10000 / <second operand> / cmp ... / bne L9 / L10000:`), reproduced
  exactly by an `int` version of that condition (the golden itself still
  stops at an initializer opcode in `c1` alone).
- **Related sequence points**: a comma operator's left operand has its
  fixups done before the right operand (`z = (n++, n + 1)` was one too
  low), and a call's before the `call` (`f(n++)`: `push` / `inc` / `call`,
  as `kernel_nonopt/sys1.s`'s `clearseg(a++)` - which pushes the operand
  from memory where `c1` still goes through DI, a separate, pre-existing
  shape difference). Postfix fixups are tracked per conditional region,
  so one queued outside an `?:` arm or `&&` operand still happens on
  every path.

**Found on the way (pre-existing): a `long` comparison used as a value**
(`z = l > 0L;`) printed an operand placeholder as code (`cmp
*-8.(bp),<unmaterialized-long-const>`), exit status 0. It is now an
explicit refusal; as a condition (`if (l > 0L)`, the confirmed shape) it
compiles as before.

No `mutos_c0` or wire-format change, so `dump_temp.py` is unaffected.
For an operand without code of its own (every `&&`/`||`/`?:` in the
corpus) the output is byte-identical to before.

**Verification (this session):**

- `make test` (clean build): 38 of 62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- `mutos_c1` alone on the 62 golden `.1`/`.2` pairs: `.s`, stderr and
  exit status identical to the previous build for all 62.
- `fuzz_c.py` now generates both constructs by default (the two opt-in
  flags are gone). 9000 programs (six seeds, three scalar-only) against
  the previous build: 0 WRONG, 0 BAD, 0 regressions; 88 go from wrong to
  correct, 1 from BAD to correct, 339 from refused to correct (mostly
  register-guard refusals of operands that used to be computed ahead of
  the branches). Every one of the 499 programs whose output changed
  contains `&&`, `||`, `?:`, a comma operator or a postfix operator, and
  no program the previous build compiled is refused now.
- 25 hand-written cases the fuzzer cannot produce (`while`/`do`/`for`
  conditions, nested `?:`, `!` of `&&`, comma in a condition, a division
  in an untaken arm, ...), executed with `x86sim.py` (which now accepts
  flags set by `or r,r`) and checked variable by variable against the host
  C compiler on `short` locals: all 24 that compile are correct, the 25th
  is a correct register-guard refusal. The previous build: 10 wrong, 2
  non-terminating or trapping, 3 refused.
- ASan/UBSan builds of both passes over the 62 golden pairs and 1200
  fuzzed programs: clean.
- After the `long` guard above, re-run: `make test` and the 62 golden
  pairs unchanged, the hand-written cases unchanged, 3000 further fuzzed
  programs (two new seeds) 0 WRONG, 0 BAD, 0 regressions.

### `mutos_c0`/`mutos_c1`: verified this session (constant left operands; semantic fuzzing added)

**`mutos_c0` bug fixed: a constant LEFT operand was emitted on the right.**
`7 - x` compiled as `x - 7` - and likewise `/`, `%`, `<<`, `>>` and the
comparisons, since `mutos_c0`'s first commit. A constant is kept
unwritten so it can fold with a constant sibling, and was written only
when its operator was reached - after the right operand's bytes. Now,
while a constant left operand is pending, the right operand is parsed
into a memory buffer and the `CON` is written ahead of it
(`rhs_begin()`/`rhs_end()` in `c0_parser.c`, at all 13 binary-operator
sites). The same root cause had three more faces, fixed with it: `c ? 7 :
y` selected the wrong branch; a constant `?:` condition kept the
untaken branch's bytes in the stream (so `z = 0 ? x : 3;` failed in
`c1` with an internal error); and a constant last comma item came after
its `SEQNC` (`y + (x = 1, 5)` added 1, not 3). The stream is now v7's:
`c01.c`'s `fold()` folds only when both operands are constants (for
`?:`, all three parts) and never reorders, and `SEQNC` is never folded.

**`mutos_c1` now moves a constant operand to the right itself**, as v7's
`c1` does (`c12.c`'s `acommute()` for `+ * & | ^`; `optim()`'s `maprel[]`
swap for relationals: `7 < x` is `x > 7`). The old `c0` bug had been doing
this for it, so without it `7 | e`, `7 * x` or `7 == x` would have
regressed; a constant emits no code, so exchanging the two values in the
operator's handler gives exactly the previous output for those. `7 < x`
etc., previously wrong, now compile correctly. A non-commutative
`const - <compound>` that needs two registers is refused (no golden).

**Semantic fuzzing: `tests/mutos_cc/fuzz/`** (`make fuzz`; see its
README). `fuzz_c.py` generates random programs (2-D/1-D arrays, the
full scalar operator set, `?:`, comma lists, `++`/`--`), computes what
each must do under C semantics, runs it through the real pipeline and
executes the output with `x86sim.py` - checking every variable at every
statement boundary, not only at the end. With `--baseline` every
difference against an earlier build is classified (fixed / regression
/ still correct). `x86sim.py` reproduces the C value of all 30 goldens
it can execute, struct and bit-field programs included. It found two
more, pre-existing `mutos_c1` wrong-code bugs, recorded as "Open items"
6 and 7 and avoided by the fuzzer's default (each had an opt-in flag) -
both fixed later the same day, see the section above.

**Verification (this session):**

- `make test` (clean build): 38 of 62 byte-exact, 0 genuine mismatches,
  zero warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- `mutos_c1` alone on the 62 golden `.1`/`.2` pairs: identical to the
  previous build (`.s`, stderr, exit status) - real v7 streams never had
  a misplaced constant. `mutos_c0` on the 62 golden `.i` files: identical
  for every successful compile (one failing compile's partial output
  differs; same diagnostics).
- 6000 fuzzed programs against the previous build: 2356 compile, all
  correct at every statement; 0 BAD. 198 programs go from WRONG to
  correct and 9 from WRONG to an explicit refusal, 202 from refused to
  correct, and 7 from `c1`'s "internal: 2 unconsumed expression values"
  (the orphaned `?:` branch) to an explicit refusal. 51 changed but
  correct in both: re-run with up
  to 60 different initial values each, the new build was correct on
  every variant and the old one provably wrong on some for 31 of them;
  the other 20 cannot show the old operand order (`if (12 - i)` tests the
  same as `if (i - 12)`). 4 programs the old build compiled "correctly"
  are now refused: 3 computed a masked wrong value (`0 - a` with `a` 0; a
  truth-tested `7 - b`) in a `const - <compound>` shape `c1` now declines,
  and 1 is a constant `?:` condition (`~9 ? ... : ...`), now v7's real
  `QUEST` tree, which `c1` has no confirmed shape for.
- ASan/UBSan builds of both passes over 1500 fuzzed programs and the
  golden suite: clean.

### `mutos_c1`: verified this session (byte-exact, `10_integ/05_matmul` - evaluation order: right operand first, spilled)

**`sum = sum + a[i][k] * b[k][j];` now compiles exactly as the real
compiler does:** the right element is computed first and pushed (`... /
mov di,(di)` / `push di`), then the left one (`... / mov di,(di)`), then
`mov ax,di` / `pop cx` / `imul cx` - `v7/cc/table.s`'s `*` template
`%n,n` (`SS` / `F` / `mul (sp)+,R`: the right operand onto the stack
first). `mutos_c1` streams temp1 in postfix (left-first) order, so it now
pre-scans each expression (`plan_expression()`, reusing `scan_op_args()`'s
arity table), and when an operator must be evaluated right operand first
replays temp1's subtrees in that order through the ordinary opcode
handlers ("subtree replay", the plan `docs/DEVLOG.md` recorded last
session), with two value-stack steps in between: a spill (`push`, new
value kind `VK_STACKED`) and a swap. An expression with no such operator
gets no plan and streams exactly as before. No `mutos_c0` or wire-format
change, so `dump_temp.py` is unaffected.

**Only the golden-confirmed decision is taken:** int `*` whose two operands
are both complete 2-D element reads with plain-variable subscripts. By
v7's `degree()` a 1-D element (`a[i] * b[j]`) is just as hard, so the real
compiler very probably spills it too - but no golden shows it, so it stays
refused (register-occupancy guard, as before). `02_bubsort`'s swapped
relational and `03_linklist`'s right-hand-side-first store use the same
mechanism with other decisions; neither is taken yet.

**Found on the way (fixed the same day - see the section above):
`mutos_c0` emitted a constant LEFT operand of a binary operator on the
right** - `7 - x`
compiles as `x - 7` (likewise `/`, `%`, `<<`, `<`, ...). Silent wrong code;
invisible to every golden, since no corpus file has a constant left
operand. Found by this session's semantic check.

**Verification (this session):**

- `make test` (clean build): **38 of 62** byte-exact end-to-end (up from 37 of
  62), 0 genuine mismatches; `mutos_as` 67/67 and `mutos_cpp` 5/5
  unchanged. Zero warnings under `-Wall -Wextra -Wpedantic`.
- `mutos_c1` alone on all 62 golden `.1`/`.2` pairs, against the previous
  binary: `.s`, stderr and exit status identical for all 61 others;
  `05_matmul` now matches (42 of 62 via `c1` alone, up from 41). Every
  file that still stops is a byte-exact prefix of its golden except
  `02_bubsort` (unchanged - the relational swap).
- Semantic check (the one `docs/DEVLOG.md`'s process lessons asked for
  before any reordering): an interpreter for `c1`'s instruction subset,
  validated first against real goldens' results, runs fuzzed programs
  built around 2-D arrays and compares every scalar's and every array
  element's final value with the expected one. 3500 programs: every output
  that changed is a program with a product of two 2-D elements that the
  previous binary refused; every accepted output assembles with
  `mutos_as` and computes exactly the expected state (690 executed, 593
  through the new spill, 116 of those nested inside a pending indirect
  store). 1500 scalar-only programs: output identical to the previous
  binary.
- Hand-written edge cases (a `return`, a loop condition, a `for`
  increment, a call argument) correct; two such products in one statement,
  `a[i] * b[j]`, and a `register` subscript refused explicitly.
- ASan/UBSan build of `mutos_c1` over the 62 golden pairs and 800 fuzzed
  programs: clean.

### `mutos_c0`/`mutos_c1`: verified this session (byte-exact, string literals - `05_arrptr/05_arrofptr`, `07_strlibc`; `09_abiprobe/01_argvmain`; six older `c1` bugs)

**String literals, end to end.** `mutos_c0` writes each literal to temp2 -
`LABEL n`, `BDATA`, one `(1, byte)` pair per byte plus `(1, 0)` for the NUL,
a lone `0`, with a new run before every 15th byte (v7/cc/c00.c's
`putstr()`) - and references it in temp1 as `NAME(SC_STATIC, TY_CHAR, n)`
`AMPER(9)`; byte-exact against `05_arrofptr.1/.2.golden`, `07_strlibc.1/.2.
golden` and `10_integ/04_strrev.2.golden`. `mutos_c1` prints `.data` after
the code and then each literal as `L4:.byte<TAB>/6f,/6e,/65,/0`, lower-case
hex, at most 9 values per `.byte` line - a rule that, with the 15-byte
runs, reproduces the line layout of all 208 string literals in the real
compiler output in `tests/mutos_as/kernel_nonopt/` and `kernel_opt/`. A
literal's address is `#L4`: stored with one memory-immediate `mov`, passed
as an argument through DI (`mov di,#L4` / `push di`); any other use of it
(arithmetic, comparison, dereference) is refused. Escapes follow v7's
`mapch()` exactly (octal `\ddd`, `\f`, `\v`, backslash-newline).

**Declarations and calls this needed.** `char`/`long` locals and parameters
with `*` and `[N]` (`char buf[20]`, `char *s`, `char *names[3]`, parameter
`char *argv[]`), frame slots rounded to a word (v7's `rlength()`); array
decay by element type; prototypes with a pointer result and comma lists
(`char *strcpy();`, `int strlen(), strcmp();` - callee type 49 = "function
returning pointer to char"); a statement that is just a call. `for` and
`switch` now allocate their labels in v7's order (only observable with a
literal in the init/controlling expression). A constant index on a pointer
value compiles to a displacement (`argv[1]` -> `mov di,*6.(bp)` / `mov
di,*2.(di)`, `01_argvmain.s.golden`), and `OP_AMPER`'s eager-`lea` decision
now uses a real consumer lookahead (`scan_consumer()`) instead of "is the
next opcode a NAME?", which string arguments broke.

**Deliberately refused: `char` element access.** Reading or writing a
`char` (or `long`) through a subscript or pointer. The six `09_abiprobe`
frame goldens show the real front end wrapping such a value in opcode 109
with type `TY_INT` (char-to-int; so far only seen as `TY_CHAR`, int-to-char)
and a `movb`/`cbw` codegen shape - recorded in `docs/DEVLOG.md` for the next
session; without the refusal those six files would become `c0` mismatches.

**Older `mutos_c1` bugs found and fixed on the way** (all present in the
previous binary, none in a golden - full table in `docs/DEVLOG.md`):
`if (*p)`/`return *p;` emitted `<unpopped-ind-pending>` text (exit 0);
`a = v[1];`/`f(v[2])` used `&v[k]` instead of `v[k]`; comparisons emitted
`cmp *3.,...` and memory-memory `cmp`s that only `mutos_as` rejected;
`*a = *b;` emitted `mov (bx),(di)`; `*b = t;` was refused although
`02_bubsort.s.golden` shows its shape (now implemented from it); a function
address argument was pushed as an immediate. A compound assignment through a
subscript (`a[i] += 2;`) is now refused instead of mis-compiled.

**Verification (this session):**

- `make test`: 37 of 62 byte-exact end-to-end (up from 34 of 62:
  `05_arrofptr`, `07_strlibc`, `09_abiprobe/01_argvmain`), 0 genuine
  mismatches; `mutos_as`/`mutos_cpp` suites unchanged. Zero warnings under
  `-Wall -Wextra -Wpedantic`.
- `mutos_c1` alone on all 62 golden `.1`/`.2` pairs: 41 match their
  `.s.golden` (up from 38); of the rest, every partial output is a
  byte-exact prefix of its golden except `02_bubsort` and `05_matmul`
  (unchanged - the evaluation-order work in "Next up").
- Differential testing against the previous binaries: 1500 random programs
  in the previous grammar - `c0` output identical for all; every changed
  `.s` or new refusal replaces output that was garbage, unassemblable, or
  (7 programs) `&v[k]` for `v[k]`. 1500 random programs using the new
  features: every accepted `.s` assembles with `mutos_as`.
- ASan/UBSan builds over the corpus plus 1200 random programs: clean.
- `dump_temp.py` decodes BDATA; the three `.2.golden` files with literals
  now dump completely (Workflow Guideline 8).

### `mutos_c0`/`mutos_c1`: verified this session (byte-exact, `05_arrptr/02_array2d` - 2-D arrays; constant-shift threshold fix)

**2-D arrays (`int m[N][M]`), confirmed against `02_array2d.1/.2/.s.golden`
and, for the front end, `10_integ/05_matmul.1/.2.golden`.** `mutos_c0`
declares a 2-D local as one `N*M*2`-byte block (`SymEntry.dim2` = M) and
emits `m[i][j]` as two ordinary subscript steps, the row re-decayed in
between: `NAME AMPER(8) <i> CON(2M) ITOP(104) PLUS(8) STAR(0) AMPER(8) <j>
CON(2) ITOP(8) PLUS(8) STAR(0)` - byte-identical to both goldens. The outer
`ITOP`'s type 104 ("pointer to array of int"), recorded last session as only
empirically pinned, is now derived: `v7/cc/c01.c`'s `disarray()` calls
`setype()`, which retypes the node chain it walks (STAR → PLUS → AMPER →
NAME, always via the left operand) down to the element level but never
visits the `ITOP` on `PLUS`'s right, so that one node keeps the original
pointer-to-row type. `mutos_c1` cancels the row's `STAR`/`AMPER` pair
(`v7/cc/c12.c` `optim()`'s first rule, `&*x → x`), keeps both scaled
indices symbolic (new `VK_SCALED`/`VK_ROWADDR` value kinds, which
`pop_val()` refuses to hand to any other consumer) and emits the address
the way `v7/cc/c12.c`'s `distrib()` factors `i*8 + j*2` into `(i*4 + j)*2`:
`lea di,&m / mov si,i / sal si,*1 / sal si,*1 / add si,j / sal si,*1 / add
di,si` - byte-identical to `02_array2d.s.golden` and to every runtime-index
subscript in `05_matmul.s.golden` (row/element ratio 2 there); both
goldens' all-constant subscripts (`a[0][1] = 2;`) fold to one bp-relative
operand exactly like 1-D `v[0]`. Refused explicitly (no golden): three or
more dimensions, a bare or half-subscripted 2-D array, `&` on any array
(v7/cc would emit `NAME`(array type)/`AMPER`(pointer-to-array), not what
`c0` emitted before), a constant index next to a runtime one, a
non-plain-variable index, and a row size that is not a power-of-two
multiple (≥ 2) of the element size.

**Two further `c1` shapes confirmed by `02_array2d.s.golden`.** A constant,
non-power-of-two multiplier: `i * 10` → `mov ax,i / mov cx,*10. / imul cx`
(previously refused; 0, 1 and powers of two stay refused - `v7/cc`
strength-reduces them to shapes no golden shows for `OP_TIMES`). An
AX-resident value plus a memory operand: `add ax,mem` in place, never
`mov di,ax` first (`05_matmul.s.golden` shows the same with the product as
the right operand) - this changes the previously unconfirmed output for
e.g. `f(a) + b` accordingly.

**Bug fix - constant shift counts of 3 or more.** The real non-optimized
compiler output in `tests/mutos_as/kernel_nonopt/*.s` never repeats a
single-bit shift more than twice; a count of 3 or more is `mov cx,*N.` /
`sar reg,cl` (e.g. `amx.s`: counts 3, 7 and 11), while 2 is two repeated
shifts (`04_shift.s.golden`, and a run of two in `amx.s`). `c1` used to
repeat `sal`/`sar` N times for any N - silently wrong from 3 up. Register
shifts now follow the confirmed rule (`MCC_SHIFT_REPEAT_MAX` = 2 in
`c1_gen.c`); `<<=`/`>>=` by more than 2 and `*=` by 8 or more on a memory
operand, whose shape no golden shows, are now refused instead of
extrapolated.

**Also fixed (pre-existing, found by this session's ASan/UBSan sweep):**
`mutos_c0` leaked every string-literal token's text (no grammar rule
consumes one yet, so `advance()` now frees it), and folded `(0 - 7) << 3`
through a left shift of a negative value (undefined behavior; now an
unsigned intermediate, and a constant shift count outside 0..15 is an
explicit error). `dump_temp.py` now renders a derived type as its full
chain, outermost first - `PTR.ARRAY.TY_INT(104)`, `FUNC.TY_INT(16)`,
`PTR.FUNC.TY_INT(72)` - instead of a misleading "`TY_INT ptr×13`".

**Verification (this session):**

- `make test`: **34 of 62** byte-exact end-to-end (up from 33 of 62), 0
  genuine mismatches; `mutos_as`/`mutos_cpp` suites unchanged. Zero
  warnings under `-Wall -Wextra -Wpedantic`.
- `mutos_c1` run directly on all 62 golden `.1`/`.2` pairs, old vs. new
  binary: 58 identical; 4 changed, all expected (`02_array2d` now matches
  its `.s.golden`; `06_struct/03_starray`, `06_struct/05_nestst` and
  `10_integ/05_matmul` still refused, with a different diagnostic). 38 of
  the 62 now match their `.s.golden` via `c1` alone.
- `10_integ/05_matmul`: `c0` byte-exact; `c1` stops only at `a[i][k] *
  b[k][j]`, where the real compiler evaluates the RIGHT operand first and
  pushes it (`push di` ... `pop cx` / `imul cx`) - the evaluation-order/
  spill codegen the register-occupancy guard already refuses.
- Differential testing, old vs. new `c0`+`c1`, on 3000 random scalar K&R
  programs: `c0` wire output identical for all; of the programs both
  accept, every `c1` output difference is one of the two intended changes
  (141 AX-add, 100 shift-by-CL, 50 both); every new refusal is one of the
  two memory-operand shift cases. 3000 random 2-D-array programs: every
  accepted output assembles with `mutos_as`, sampled outputs checked by
  hand; every refusal is one of the explicit diagnostics above.
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the full corpus plus both
  3000-program random sets: clean.

### `mutos_c1`: verified this session (`c1_gen.c` emission-layer refactor, register-occupancy guard, `SETSTK` threshold)

A code review of `src/mutos_cc/c1_gen.c` (full write-up in `docs/DEVLOG.md`'s
Milestone 4 section) led to three changes, all confined to that one file - no
`mutos_c0` change and no wire-format change, so `dump_temp.py` is unaffected:

1. **Emission layer (refactor, no output change).** Every line of assembly
   text now goes through one small layer: typed operands (`o_reg()`,
   `o_val()`, `o_imm()`, `o_lab()`, `o_ind()`, ...), `ins0()`/`ins1()`/
   `ins2()` for instructions, `put_label()` for the no-newline `L<n>:`
   convention, `put_line()` for `|`-comments - replacing 146 scattered
   `fprintf()` calls and their scratch buffers. Where a table fits, it is
   one: relational branch mnemonics/inversions (`RELOPS`), opcode →
   mnemonic/diagnostic name (`ALUOPS`), and the confirmed fixed idioms as
   `const Insn` sequences (`SEQ_PROLOGUE`, `SEQ_DXAX_TO_DISI`, ...).
2. **Register-occupancy guard (bug fix).** `c1` tracks where every pending
   value lives but never checked whether a register it was about to
   overwrite still held one. That silently miscompiled, among others,
   `f(a) + a * b` (the `imul` overwrote `f()`'s result in `AX`),
   `v[a + 1] = 5;` (the array base in `DI` was overwritten before use),
   `if (a + b < c)` (compiled to `cmp di,di`), `f(a) + g(b)`,
   `g(a + b, 5)`, `a / (b % c)`, `a - *p`, `(a < b) + (c < d)`, and any
   use of `DI` as scratch while a `register` local lived there. A central
   check in the emission layer (an `INSN_FX` table of each mnemonic's
   register effects, plus explicit checks where a handler holds a popped
   operand) now turns every such collision into an explicit "not yet
   supported" diagnostic. It does not invent spill code - no golden yet
   confirms the real compiler's shape for these.
3. **`SETSTK` threshold (bug fix).** The `09_abiprobe` goldens pin both
   frame-allocation shapes: 80 bytes → `sub sp,*80.`; 128/176/224/256/300
   bytes → `mov ax,#N.` / `call chkstk`. The threshold is therefore in
   `(80,128]`. `c1` previously refused 77..256 and, above 256, emitted
   `mov ax,*N.` (wrong size marker - silently wrong); it now emits both
   confirmed shapes byte-exactly, and refuses only 81..127.

**Verification (this session):**

- `make test`: 33 of 62 byte-exact end-to-end, unchanged; 0 genuine
  mismatches; `mutos_as`/`mutos_cpp` suites unchanged. Zero warnings under
  `-Wall -Wextra -Wpedantic`; clean under ASan/UBSan.
- `mutos_c1` run directly on all 62 golden `.1`/`.2` pairs (bypassing
  `mutos_c0`): output and exit status byte-identical to the pre-change
  binary for all 62 (37 of the 62 match their `.s.golden` via `c1` alone -
  `06_struct/01_stbasic`, `08_enum`, `09_typedef` and `07_scope/02_shadow`
  are blocked only by `mutos_c0`). The only stderr change is the
  unsupported-opcode message's wording (it no longer claims coverage is
  "limited to 00_smoke/01_intarith").
- Differential testing against the pre-change binary over 3000 random
  K&R programs (2060 accepted by `mutos_c0`): the refactor alone changed
  no byte of output, stderr or exit status; with the guard, 426 are
  identical and 1634 are now refused, 0 unexpected - each refusal checked
  to be the guard's own diagnostic with the new output an exact byte-prefix
  of the old one, and a manual sample of refused cases confirmed genuine
  register clobbers in the old output. Hand-assembled `temp1` streams
  covered the two pointer-`PLUS` fallback paths no C source reaches, and
  the `SETSTK` value of every `09_abiprobe` golden (all six now match
  their `.s.golden` frame epilogue; before, five were refused and one was
  wrong).

### `mutos_c0`/`mutos_c1`: verified this session (byte-exact, 4/7 of `05_arrptr`)

`05_arrptr` is the first category needing real type-system work beyond a
flat "2 bytes, maybe a single pointer degree" model (see the prior
session's own "Next up" below) - single-dimension array subscripting,
multi-level pointers, explicit `&`/`*` as general (not just statement-
special-cased) unary operators, and pointer/array-parameter equivalence are
now confirmed; 2-dimensional arrays and string literals (`05_arrofptr.c`/
`07_strlibc.c`) are not - see "Next up".

**General pointer-degree chaining, not a flat `TY_INT|010`.** `c0_parser.c`
gained `ty_incref_tag()`/`ty_ptr_of()`/`ty_decref()`, transcribing
`v7/cc/c04.c`'s own `incref()`/`decref()` formula exactly (`TYPE=7`, the
base-type mask; `TYLEN=2`; `PTR=010`): `new_type = ((t & ~7) << 2) |
(t & 7) | tag`. This is NOT "add 8 per pointer level" (the assumption
`TY_PTR_FUNC_INT=72`'s own hardcoded derivation already hinted at, but
which nothing before this session needed to generalize): confirmed via
`06_ptrptr.1.golden`'s `int **pp;` - every `NAME`/`AMPER`/`ASSIGN` touching
`pp` uses type **40**, not a naive "16" (`TY_PTR_INT + 8`). `pp = &p;`
(`AMPER` of an already-pointer-typed operand) and `**pp = 6;` (two chained
`STAR`s, decreffing 40→8→0) both confirmed byte-for-byte with this formula
and no other change to the wire format.

**Single-dimension array subscripting (`a[i]`), confirmed against
`01_arrbasic.1.golden`/`.s.golden`.** `a[i]` compiles to exactly
`*(&a + i*sizeof(elem))`, reusing the array-decay `AMPER` and pointer-
scaling `ITOP`/`PLUS` shapes `05_incdec.c` already established - a new
`emit_subscript()` in `c0_parser.c` emits this for both an array
(`AMPER`-decayed first) and a plain pointer variable/parameter (used
directly, no decay - see `04_ptrarreq.c` below), reached from
`parse_primary()` (rvalue subscript) and a new subscript branch in
`parse_assign_stmt()` (lvalue target). The real, non-obvious part was
`c1`'s codegen for scaling a **non-constant** index by a constant size -
previously `OP_ITOP` only ever folded two compile-time constants (the
literal "1" in `++`/`--`). Confirmed shape: the index is loaded into DI
(`mov di,i` / `sal di,*1` for a size-2 element - repeated-shift strength
reduction, the same style `06_compasgn`'s `*=2` already established) -
UNLESS DI is already holding a live base address from a preceding `AMPER`
(a real array, not a bare pointer parameter), in which case SI is used
instead (`mov si,i`/`sal si,*1`) so the two can be combined afterward
(`add di,si`) without clobbering. Which register ends up as the final
combined address is decided the same way in `OP_PLUS`'s new pointer-
arithmetic case: whichever operand is already resident in a register (DI
preferred) becomes the destination, the other is added in via its own
rendered operand text directly (a plain memory or immediate right-hand side
is legal for `ADD` on the 8086, no extra load needed) - confirmed against
both `01_arrbasic.s.golden`'s `"lea di,&a" / "add di,si"` (base in DI) and
`04_ptrarreq.s.golden`'s `"mov di,i;sal di,*1" / "add di,*4.(bp)"` (index
in DI, the pointer parameter added straight from memory, never loaded into
a register at all).

**A dereferenced value consumed by further arithmetic must be
force-materialized, not left lazy - confirmed against `01_arrbasic.s.
golden`'s `"sum = sum + a[i];"` and `04_ptrarreq.s.golden`'s `"s = s +
*(a + i);"`.** `OP_STAR`'s result (a lazy `"(di)"` operand, `VK_IND`) is
fine left un-materialized when it becomes an `ASSIGN`'s own lhs (the
existing, already-confirmed shape), but when it instead feeds a further
`OP_PLUS`, it must be resolved first via `"mov di,(di)"` (in place, same
register) before the other operand is added in - otherwise the final `ADD`
would need two memory operands, which the 8086 cannot do. New logic in the
plain-`TY_INT` `OP_PLUS`/`OP_MINUS` case (`PLUS` only - commutative, so
either operand order works; not generalized to `MINUS`, unconfirmed by any
golden) handles this before the pre-existing register-class-variable
special case, which a naively-materialized `VK_IND` would otherwise
wrongly trigger.

**General `&`/`*` as ordinary unary operators (not just the
statement-level `"*p = ...;"` special case), confirmed against
`03_ptrbasic.1.golden`/`.s.golden`.** `parse_unary()` gained real `'&'`
(address-of a plain variable - reuses the array-decay `NAME`+`AMPER` shape,
generalized via `ty_ptr_of()`) and `'*'` (dereference, recursing through
`parse_unary()` itself so a chain of leading `'*'`s decrefs one degree at a
time - `parse_star_assign_stmt()`'s own lvalue form was generalized the
same way, from a single hardcoded `TY_INT` `STAR` to a loop of `ty_decref()`
steps, for `**pp = 6;`). `parse_add()` gained a pointer-plus-int case
(pointer left operand, int right - the CON/`ITOP` scaling tail
`emit_subscript()` already established) so `*(a + i)` - a parenthesized
general expression, not just a bare identifier - resolves its operand's
type correctly.

**A genuinely surprising `c1` discovery: an indirect-assignment target
whose right-hand side needs its own working registers gets its address
PUSHED (real hardware stack) rather than left resident in a register -
confirmed against both `01_arrbasic.s.golden`'s `"a[i] = i * i;"` (`push
di` / ... / `pop bx` / `mov (bx),ax`) and `03_ptrbasic.s.golden`'s
`"*p = *p + 1;"` (`push *-10.(bp)` - the plain pointer's own memory
operand, pushed WITHOUT first loading into any register - `mov di,ptr`
happens as part of the right-hand side's own `*p`, moments later).** A
bare-constant right-hand side (`"*p = 20;"`) does NOT push, confirmed by
`05_incdec.s.golden` (unchanged, still passing). The discriminator: whether
the wire immediately following the completed address (this `OP_STAR`'s own
position in the stream) is exactly `CON` then `ASSIGN` (nothing else) -
checked via one-to-few opcodes of lookahead, implemented as a save/restore
of `temp1`'s own file position (`ftell`/`fseek` around ordinary
`c1_read_op()`/`c1_read_num()` calls - a plain seekable `FILE*`, so this
changes no other opcode's behavior; ADDING this lookahead capability to
`c1_gen.c`, which had none before this session, is itself new - see
`docs/DEVLOG.md`). A parallel discovery for `OP_AMPER`: an array's address-
of is deferred (a new `VK_MEM_DIRECT`) rather than eagerly emitting `"lea"`
whenever the immediately-following opcode is NOT `NAME` - covering two
distinct needs: (1) a compile-time-constant-index subscript (`"v[0] = 1;"`)
folds the WHOLE address into a single already-known bp-relative offset at
`OP_PLUS`, with no `lea`/`add` of any kind (`"mov *-12.(bp),*1."` alone -
confirmed against `04_ptrarreq.s.golden`), and (2) a bare array-decayed call
argument (`"sumarr(v, 4);"`) must not commit to DI until `gen_call()`
actually pushes it (right-to-left), since eagerly computing it first would
let a later sibling argument's own code clobber DI before `v`'s turn -
confirmed against the same golden's `"mov di,*4.;push di" / "lea di,
*-12.(bp);push di"` ordering. `OP_ASSIGN` materializes a still-deferred
`VK_MEM_DIRECT` rhs the same way for a bare `"p = &x;"`/`"pp = &p;"`
(unaffected in output - confirmed unchanged against `05_incdec.s.golden`
and `06_ptrptr.s.golden`, both still passing).

**Array-parameter decay (`"int a[];"`), confirmed against
`04_ptrarreq.1.golden`/`.s.golden`.** `parse_param_decls()` now accepts a
trailing `'[' ... ']'` on an `int` parameter (any size between the
brackets is parsed and discarded, matching real K&R semantics), marking it
`pptr` - identical in every respect to a plain `"int *a"` parameter, matching
K&R's own array-decays-to-pointer rule and confirmed by `sumarr(a, n)`'s own
`*(a + i)` and `a[i]`-shaped subscript codegen.

**Not yet supported, deliberately** *(since superseded for `02_array2d.c`,
which is now done - see the `02_array2d` section above)*:
`05_arrptr/02_array2d.c` (multi-
dimensional arrays) - the wire shape for `m[i][j]` is understood (an outer
`ITOP` node carries a distinctly different, empirically-confirmed type
value - see `docs/DEVLOG.md` for the full derivation) but `c1`'s actual
codegen combines both dimensions' scaling into a single register-pair
computation (`si = i*4 + j; si = si*2; di = &m + si`) rather than two
independent scaled adds, which needs dedicated pattern-recognition codegen
not yet written. `05_arrptr/05_arrofptr.c` and `07_strlibc.c` (string
literals, arrays of `"char *"`) need an entirely new data-segment/string-
constant emission subsystem (`v7/cc/c00.c`'s `putstr()` equivalent) that
does not exist at all yet - `mutos_cc.h`'s already-transcribed
`OP_BDATA`/`OP_WDATA`/`OP_DATA` pseudo-ops are presumably involved but their
exact argument shape has not been reverse-engineered from any golden. See
`src/mutos_cc/README.md`'s "Next steps" for the dependency-ordered plan.

**Full-corpus regression** (`tests/mutos_cc/run_goldens.sh`, run via
`make test`): 33 of 62 byte-exact end-to-end (up from 29), 0 genuine
mismatches anywhere, confirmed via a full `make clean && make all && make
test` from a clean checkout with zero compiler warnings under `-Wall
-Wextra -Wpedantic`.

### `mutos_c0`/`mutos_c1`: verified this session (byte-exact, `02_long/03_retval` + `04_funcs/06_regclass`)

The two items the prior session's "Next up" left open — a `long`-returning
function's `DX:AX` return-value convention, and real `register`-variable
allocation — are both now implemented, closing out `02_long` (4/4) and
`04_funcs` (7/7) completely.

**`02_long/03_retval.c` (`long addlong(a, b) long a, b; { return a + b; }`,
called from `main` and assigned to a `long`)**: `parse_extdef()`/
`parse_top_prototype()` were unified into one function, since the existing
grammar could only parse a TYPED top-level declaration as an empty-parens
prototype (`"long addlong();"`), never as an actual definition with a body -
`04_mutrec.c`'s `"int iseven();"` prototype-only path is unchanged (still
requires an explicit type, an empty parameter list, and a trailing `';'`
with no body), but anything else with an explicit type prefix now flows into
`cfunc()` exactly like an implicit-`int` definition, just carrying its own
`ret_type`. Each function's return type is now tracked in a new
`functypes[]` array parallel to the existing `funcnames[]` (via
`register_func()`/a new `lookup_func_type()`), consulted by `parse_call()`
(so a call to a `long`-returning function emits `OP_CALL` with `TY_LONG`,
not the previously-hardcoded `TY_INT`, and the callee's own `NAME` leaf's
type is `ret_type | 020` — confirmed via `03_retval.1.golden`'s `_addlong`
callee using type 22, i.e. `TY_LONG(6) | 020(16)`, not `TY_FUNC_INT`'s
16) and by `do_return_stmt()`/`cfunc()` (a new `p->cur_ret_type`, set once
per `cfunc()`, drives `OP_RFORCE`'s and `OP_RETRN`'s type argument instead
of the previous hardcoded `TY_INT`). `c1_gen.c`: `OP_CALL`/`OP_RFORCE` gained
`TY_LONG` branches — a `long`-returning call's result comes back in `DX:AX`
and is moved into the `DI(high):SI(low)` convention every other `long`-value
producer here uses (`gen_call()`'s new `is_long_ret` parameter), and
`OP_RFORCE`'s `TY_LONG` case does the reverse move (`materialize_long()`
then `mov ax,si` / `mov dx,di`) — confirmed against `"return a + b;"`'s
`"mov ax,si\nmov dx,di"`. `OP_RETRN` itself needed no change at all — its
`"|RTYP n"` rendering was already generic over `n`. One more real gap
surfaced along the way: `gen_call()`'s `VK_LCON` (a `long` constant call
argument) turned out to have the SAME two-shape split `materialize_long()`
already established for an assignment target — a genuinely 32-bit value
still direct-splits (`02_long/04_params.c`'s already-confirmed `90000L`
shape), but an int-range value merely carrying an `L` suffix (`addlong`'s
own `5L` argument) instead uses the CWD sign-extension idiom (`"mov ax,*5.
/ cwd / push ax / push dx"`) — the prior session's implementation only had
the direct-split shape, since no earlier golden's `long` constant argument
happened to be int-range.

**`04_funcs/06_regclass.c` (`register int i;` as a `for`-loop induction
variable)**: contrary to the source file's own comment ("a K&R compiler is
free to ignore [`register`]"), the real MUTOS 1700 compiler does NOT ignore
it — `i` is allocated a real physical register (`di`) for the function's
entire body, with no stack slot at all. Reverse-engineered byte-for-byte
from `06_regclass.1.golden`/`.s.golden` (extending `dump_temp.py` with the
previously-unconfirmed `RNAME`(216) opcode to decode the full stream):
- **Allocation** (`c0_parser.c`): a new `p->regvar` (v7/cc's own global of
  the same name), reset to `MCC_INIT_REGVAR`(4) at the start of each
  `cfunc()`. `parse_decl()` now accepts a leading `'register'` keyword; for
  a plain (non-pointer, non-array) `int` declarator, a new
  `try_claim_register()` mirrors `v7/cc/c03.c`'s `goodreg()` exactly (fails
  once `regvar < 3`, else returns `--regvar`) — MUTOS has exactly 2
  claimable "working" registers (`di`/`si`) vs. v7's larger PDP-11 set, so
  `MCC_INIT_REGVAR=4` yields 2 available slots (3→`di`, confirmed; 2→`si`,
  the structurally next slot this same algorithm hands out but not itself
  golden-confirmed). `register` on a pointer/array/`char`/`long` declarator,
  or once no slots remain, silently falls back to an ordinary `AUTO` local -
  the same `goodreg()`-fails-so-`skw=AUTO` fallback v7 itself uses, and
  exactly what the source comment describes. A claimed variable gets a new
  `symtab_declare_reg()` entry (`hclass=SC_REG`, `offset=`the register slot
  number, not a stack offset) and emits `SETREG(newregvar)` **immediately
  before** its own `RNAME(name, newregvar)` — confirmed via
  `06_regclass.1.golden`'s exact `SETREG 3, RNAME("_i", 3)` order (SETREG
  strictly precedes RNAME, not batched at the end of all declarations the
  way `v7/cc/c02.c`'s `blockhead()` reads on paper). At the end of the
  function's own top-level compound statement, `cfunc()` now emits a
  RESTORE `SETREG(MCC_INIT_REGVAR)` if `p->regvar` changed — mirrors
  `v7/cc`'s `statement()` LBRACE-block-exit restore — confirmed via the
  golden's trailing `"SETREG 4"` right before `LABEL`/`RETRN`.
- **`mutos_c1`'s `|NREG n` comment** (previously always silent — no grammar
  coverage had ever produced a changing `regvar`): a function's FIRST
  `SETREG` (the unconditional one from `cfunc()`) renders nothing; every
  SUBSEQUENT one renders `"|NREG %d\n"` with `n = regvar - 1` — confirmed
  against BOTH the golden's `"|NREG 2"` (regvar=3, `i` claimed) and its
  `"|NREG 3"` (regvar=4, the end-of-function restore) — the restore's own
  value is numerically identical to the silent initial `SETREG`, so only
  POSITION (first-in-function vs. not), never value, distinguishes the two.
  A new `RNAME` opcode handler renders `"| name=<reg>\n"` (the actual
  register name, no trailing `"."` — unlike `ANAME`'s `"| name=offset.\n"`).
- **Codegen (`c1_gen.c`)**: `OP_NAME`'s new `SC_REG` case pushes
  `val_reg(<physical-register-name>)` (a new `regvar_name()` maps slot
  3→`"di"`/2→`"si"`, `gen_fatal`-ing on anything else) — because DI/SI are
  already this codebase's generic "working registers" for any intermediate
  value, most existing codegen (comparisons, `CBRANCH`, `ASSIGN`'s rhs
  materialization) worked completely unchanged once fed a `VK_REG("di")`.
  Three genuinely new shapes did surface: (1) `i + 1`'s `"+1"`-specific
  `INC` optimization applies for free (`load_into_di()` is already a no-op
  for a value already in `di`); (2) `sum = sum + i` loads the OTHER operand
  into `SI` instead of `DI` when the right operand is already `di` (loading
  the left into `di` as usual would clobber the live register variable
  before it's read) — confirmed via `"mov si,*-6.(bp) / add si,di"`; (3)
  `i = i + 1`'s `ASSIGN` emits NO instruction at all when its lhs and rhs
  resolve to the identical register (the `+1` already wrote the new value
  in place) — confirmed via `"inc di"` having no trailing `"mov di,di"`.
  A fourth, more far-reaching finding: once a `register`-class local
  occupies `DI` for the rest of the function, `DI` becomes unavailable as
  the GENERIC scratch register elsewhere too — confirmed via `"return
  sum;"` (an ordinary `AUTO` local, unrelated to `i`) rendering as `"mov
  si,*-6.(bp) / mov ax,si"`, not the usual DI-then-AX shape every other
  `RFORCE` in this corpus uses. A new `GenState.di_reserved` flag (set by
  `OP_RNAME` when it claims `di`, cleared at each `OP_SAVE`) drives a new
  `load_into_si()` fallback in `OP_RFORCE`'s default path — the ONLY site
  this golden confirms needs it; every other "go through DI" site
  (`OP_TIMES`'s `IMUL`, etc.) is left unchanged since none is exercised
  with a live register variable by any golden yet.
- All of this is deliberately scoped to the single confirmed shape (one
  `int` register variable, physical register `di`): a second live register
  variable, a `register` value of any other type, or any codegen combining
  two simultaneously-live register variables at once remains an explicit
  `gen_fatal("...not yet supported")` rather than a guess.

**Full-corpus regression** (`tests/mutos_cc/run_goldens.sh`, run via
`make test`): 29 of 62 byte-exact end-to-end (up from 27), 0 genuine
mismatches anywhere, confirmed via a full `make clean && make all && make
test` from a clean checkout with zero compiler warnings under `-Wall
-Wextra -Wpedantic`.

### `mutos_c0`/`mutos_c1`: verified this session (byte-exact, `04_funcs`'s `01_call` + `02_manyargs` + `03_recfact` + `04_mutrec` + `05_staticvar` + `07_funcptr`, plus `02_long/04_params`)

Function definitions with K&R-style parameters (`bp+4, bp+6, ...` offsets),
direct and indirect (through a function-pointer variable) calls, local
`static` variables, and function pointers are now implemented in
`src/mutos_cc/` — a from-scratch, modern-C11 reimplementation per this
project's established approach (`v7/cc/` used as an algorithmic reference
only, per CLAUDE.md Workflow Guideline 3).

**Verification: the real, unmodified pipeline — `<n>.c` → real `mutos_cpp
-P` → `mutos_c0` → `mutos_c1` → `<n>.s` — was run for all 6 of `04_funcs`'s
files that have goldens (`01_call.c`, `02_manyargs.c`, `03_recfact.c`,
`04_mutrec.c`, `05_staticvar.c`, `07_funcptr.c`) and for `02_long/
04_params.c`, and every intermediate artifact (`.i`, `.1`, `.2`, `.s`)
matches its real-hardware golden byte-for-byte.** Re-run via
`tests/mutos_cc/run_goldens.sh` (or `make test` from the repo root).
Current full-corpus result: 27 byte-exact end-to-end, 35 "not yet
supported" (expected), **0 genuine mismatches anywhere**.

Findings, each cited against exact byte offsets in `docs/DEVLOG.md`'s
Milestone 4 section:

1. **Parameter offsets** (`docs/MUTOS_C_ABI.md` sect. 1.3): a parameter is
   `hclass SC_AUTO` — the exact same AUTO-with-a-numeric-offset
   representation as a body local, differing only in the sign/magnitude of
   the offset — with offsets `MCC_STARG=4, 6, 8, ...` in K&R parameter-LIST
   order (not necessarily the order their own type-declaration statements
   happen to appear in, though every confirmed golden's order matches
   both). Confirmed via `01_call.1.golden`'s `_a=4`/`_b=6` and
   `02_manyargs.1.golden`'s six parameters up to `_f=14`.
2. **Parameter `ANAME`s are emitted between `SETREG` and the body's own
   `BRANCH`/`LABEL` pair** — a genuine, confirmed ordering difference from
   a body-local's own `ANAME` (which comes AFTER that `LABEL`, inside the
   compound-statement's own decl loop) — matching v7/cc/c02.c's `cfunc()`
   structurally (`funchead()`, which emits each parameter's declaration,
   runs before `branch(sloc)`/`label(sloc+1)` there too).
3. **A direct call's callee is `NAME(SC_EXTERN, TY_INT|FUNC=16, name)`** —
   K&R's implicit "extern function returning int" declaration; no prior
   declaration or prototype is required, and the callee is never looked up
   in the local (AUTO) symbol table. `TY_INT|FUNC` matches v7/cc/c04.c's
   `incref()`/`FUNC` derived-type tag exactly (`FUNC=020` octal = 16
   decimal, distinct from `PTR=010`=8's already-confirmed one-pointer-
   degree tag).
4. **Call arguments are built as a left-associative chain of
   `COMMA`(`TY_INT`) nodes** (wire opcode `9` — the *token* value K&R
   reuses as a tree operator, entirely distinct from the already-confirmed
   comma-*operator*'s own `SEQNC`=97), one per argument beyond the first;
   a single argument uses no `COMMA` at all, and zero arguments emits a
   single `NULLOP`(`218`) leaf — v7/cc/c04.c's `treeout(NULL)` shape.
   Confirmed via `02_manyargs.1.golden`'s six-`CON`/five-`COMMA` chain,
   `03_recfact.1.golden`'s single-`CON` no-`COMMA` call, and
   `05_staticvar.1.golden`'s zero-argument `counter()` call.
5. **Every argument is pushed right-to-left** (`docs/MUTOS_C_ABI.md` sect.
   1.1), caller-cleanup via `add sp,N` (`N` = 2 bytes per argument WORD,
   not per argument — a `long` argument occupies two words). An ordinary
   argument is pushed AS-IS whenever 8086's `PUSH` can take it directly (a
   register or any addressable memory operand); only a genuine immediate
   needs `DI` first (`PUSH` has no immediate form). Confirmed via
   `01_call.s.golden`'s `mov di,*4./push di` (immediate) vs.
   `07_funcptr.s.golden`'s `push *-6.(bp)` (a plain local, pushed with no
   preceding `mov` at all). A `long` constant argument instead pushes its
   own two words directly (low word first, then high) — confirmed via
   `02_long/04_params.s.golden`'s `myseek(3, 90000L, 1)`.
6. **A call's result is always in `AX`** (sect. 1.5); `RFORCE` now skips
   its usual `mov di,.../mov ax,di` pair when the value is *already* `AX`
   (a call result, or an `OP_TIMES`/`OP_DIVIDE` quotient) — a real,
   confirmed compiler optimization (mirroring v7/cc/c10.c's own
   `rcexpr()`/`movreg()` "already in the target register" skip), not
   something this project invented. When the AX-resident value instead
   becomes an *operand* of a further `TIMES` (`03_recfact`'s `n *
   fact(n - 1)`), that operator's own existing "load left into AX" codegen
   now checks: whichever side is already `AX` KEEPS `AX` (even a redundant
   `mov ax,ax` self-move, matching this project's no-peephole-optimization
   ethos exactly) and the other side becomes the `IMUL` operand —
   regardless of source left/right position.
7. **The confirmed `+1`→`INC` codegen delta now has a confirmed symmetric
   `-1`→`DEC` case too** (`03_recfact`'s/`04_mutrec`'s `n - 1`) — no
   longer left unconfirmed as prior sessions' notes had it.
8. **`(int)` (`LTOI`) applied to a `long` PARAMETER must NOT eagerly
   materialize into a register** the way it does as a plain assignment's
   rhs (`08_castsize`'s already-confirmed `mov di,*-8.(bp)` shape) —
   confirmed against `02_long/04_params.s.golden`'s `fd + (int) offset`
   rendering as `add di,*8.(bp)` (the converted low-word memory reference
   used directly as `PLUS`'s operand, no separate `mov` at all). Both
   shapes are real; the difference is which opcode CONSUMES the converted
   value (`OP_ASSIGN`'s plain-type case still needs a register, since 8086
   `MOV` cannot take two memory operands — an ordinary, still-unconfirmed
   `"x = y;"` stays rejected either way).
9. **A local `static` variable lives in its own dedicated `.bss` block**,
   tagged by a fresh intermediate-code label rather than a stack offset —
   `BSS`, `LABEL`(fresh), `SSPACE`(size), `PROG` opens it; `SNAME`(name,
   same label) declares it (`hclass SC_STATIC`); every later reference
   reuses that same label number as its "offset". Renders as `.bss` /
   `L<n>:.blkb N.` / `.text` / `| name=L<n>` and a bare `L<n>` operand
   wherever referenced (`mov di,L4`, `mov L4,di`) — confirmed against
   `05_staticvar.s.golden`. Its value genuinely persists across calls, for
   free, since nothing about it is stack-relative.
10. **A function pointer's declared type is `TY_INT|FUNC|PTR = 72`**
    (v7/cc's `incref(FUNC) = ((FUNC & ~TYPE) << TYLEN) | (FUNC & TYPE) |
    PTR`, `TYPE=7`/`TYLEN=2`/`PTR=8` — confirmed: `incref(16) = 72`),
    declared as `'(' '*' IDENT ')' '(' ')'` for both a local variable
    (`int (*fp)();`) and a parameter (`int (*f)();`). A bare function name
    used as a *value* (not called — `fp = square;`) is the same
    `NAME(SC_EXTERN, TY_INT|FUNC)` a direct call's callee uses, wrapped in
    `AMPER`(72) — but, since a function's address is a link-time constant,
    **no code at all is emitted for that `AMPER`** (unlike the already-
    confirmed array-decay `AMPER`, which needs a real `lea` for its
    genuinely runtime bp-relative address): the assignment goes straight
    to a memory-immediate `mov *-6.(bp),#_square`. An indirect call
    (`(*f)(x)`) dereferences `f` via `STAR`(`TY_INT|FUNC`=16) — likewise a
    PURE TYPE-LEVEL operation emitting no code of its own, leaving `f`'s
    own plain memory reference untouched for `CALL` to render as `call
    @*4.(bp)` (`@` = mutos_as's indirect-call marker). Confirmed against
    `07_funcptr.s.golden` in full.

**Current grammar/opcode scope, `04_funcs` additions (deliberately narrow —
see `src/mutos_cc/README.md`):** K&R-style function definitions with
parameters (`name(a, b) int a, b; { ... }`), a top-level function
prototype (`int name();`, entirely discarded — no wire output of its own),
direct and indirect function calls with up to `MCC_MAXPARAMS`/
`MCC_MAXCALLARGS` (16) arguments, local `static` `int`/`char`/`long`
variables (plain-`IDENT` declarator only), and a plain function-pointer
local/parameter (`int (*name)();`, zero-argument callee signature only).
Everything else — the `register` storage-class hint actually changing
codegen (`06_regclass.c` — parsing `register` at all is not yet
implemented; real register-variable allocation would be a substantial,
separate feature, not attempted this session), a `long`-returning
function's `DX:AX` return-value convention and `long` parameters'
interaction with it (`02_long/03_retval.c`; `04_params.c`'s mixed int/long
parameters themselves ARE now supported — see above), a function pointer
with a non-empty parameter signature, a function-pointer array or struct
member, block-scoped declarations, or a function with a non-`int` return
type — remains an explicit "not yet supported" error, not silently-wrong
output.

Build: `cd src/mutos_cc && make`, or `make`/`make test` from the repo root.
Clean build, zero warnings under `-Wall -Wextra -Wpedantic`.
`mutos_cc` (the driver chaining `cpp|c0|c1|as|ld`) is not yet written —
see `src/mutos_cc/README.md`'s "Next steps" for the full plan (`long`
return values, `06_regclass`'s register variables, `09_abiprobe`'s
`chkstk` threshold, then the driver).

### `mutos_c0`/`mutos_c1`: verified in an earlier session (byte-exact, `00_smoke` + `01_intarith` + `02_bitwise` + `03_rellogic` + `04_shift` + `05_incdec` + `06_compasgn` + `07_ternary` + `08_castsize` + `02_long/01_addsub`/`02_muldiv` + `03_ctrlflow`)

Built `mutos_c0` (lexer, diagnostics, the `temp1`/`temp2` stream writer, a
symbol table, and a front-end driver) and `mutos_c1` (the stream reader and
a code generator with an operand-kind-aware value stack) in `src/mutos_cc/`
— a from-scratch, modern-C11 reimplementation per this project's
established approach (`/v7/cc/` used as an algorithmic reference only, per
CLAUDE.md Workflow Guideline 3, not copied wholesale).

**Verification: the real, unmodified pipeline — `<n>.c` → real
`mutos_cpp -P` → `mutos_c0` → `mutos_c1` → `<n>.s` — was run for all 3
`tests/mutos_cc/00_smoke/` files plus all of `01_expr`: `01_intarith.c`,
`02_bitwise.c`, `03_rellogic.c`, `04_shift.c`, `05_incdec.c`,
`06_compasgn.c`, `07_ternary.c` and `08_castsize.c`, plus
`02_long/01_addsub.c` and `02_muldiv.c`, plus all 7 of `03_ctrlflow`, and
every
intermediate artifact (`.i`,
`.1`, `.2`, `.s`)
matches its real-hardware golden byte-for-byte.** Re-run via
`tests/mutos_cc/run_goldens.sh` (also wired into the top-level `Makefile`'s
`test` target), which covers the full 62-file corpus and reports every file
outside current grammar/opcode coverage as an explicit, expected "not yet
supported" diagnostic (nonzero exit, clear message) — distinct from a
genuine byte mismatch, so the script's pass count is an honest,
non-inflated measure of verified coverage. Current full-corpus result: 20
byte-exact end-to-end, 42 "not yet supported" (expected), **0 genuine
mismatches anywhere** (`.i`, `.1`, `.2`, or `.s`).

Reverse-engineering the real `temp1`/`temp2` wire format (byte-level, via
`od`/hexdump, against the `00_smoke`, `01_intarith`, `02_bitwise`,
`03_rellogic`, `04_shift`, `05_incdec`, `06_compasgn`, `07_ternary` and
`08_castsize`
goldens) surfaced **four confirmed MUTOS-1700-specific deltas from vanilla
V7 `cc`** (found against `00_smoke`), the full local-variable wire format
(found against `01_intarith`), the bitwise-operator/immediate-marker
findings below (found against `02_bitwise`), the deferred-comparison/
short-circuit codegen findings below (found against `03_rellogic`), the
shift-codegen findings below (found against `04_shift`), the
increment/decrement/pointer/array findings below (found against
`05_incdec`), the compound-assignment findings below (found against
`06_compasgn`), the ternary/comma-operator findings below (found
against `07_ternary`), and the char/long/cast/sizeof findings below
(found against `08_castsize`), each
cited against exact byte
offsets in `docs/DEVLOG.md`'s Milestone 4 section:

1. `STAUTO = -4`, not V7 PDP-11's `-6` (MUTOS's prologue saves 2 registers,
   not 3 — see `docs/MUTOS_C_ABI.md` sect. 1.2/1.4).
2. `cfunc()`'s header sequence emits an extra `EVEN` between `PROG` and
   `RLABEL` (8086 entry-point alignment, presumably).
3. `RETRN` carries an extra numeric argument (return type), rendered by
   `c1` as a `|RTYP n` comment.
4. The initial `SETREG` register-variable budget is `4`, not V7's `5`.
5. `ANAME` (`outcode("BSN", ANAME, name, offset)`) declares each `AUTO`
   local right after the function's body-entry label, rendered by `c1` as a
   `"| name=offset."` comment; `NAME` (`outcode("BNNN", NAME, hclass, type,
   hoffset)`) references one. Offset assignment matches `v7/cc/c03.c`'s
   declarator loop exactly, using MUTOS's own `STAUTO=-4` as the starting
   subtrahend (delta #1) — confirmed via `a`/`b`/`c` landing at `-6`/`-8`/
   `-10`.
6. The `SETSTK`-vs-`.s` local-frame threshold was implemented in that
   session as a plain `"sub sp,*N."` up to 76 bytes (confirmed via
   `01_intarith`'s `extra=6` case) and `"mov ax,*N. / call chkstk"` above
   256 bytes (unconfirmed at the time), with the sizes in between refused.
   **Superseded** - the `09_abiprobe` goldens have since pinned both
   shapes down (the `chkstk` form's immediate turned out to take the `#`
   marker); see the `c1_gen.c` emission-layer section above.
7. `mutos_as`'s `*`/`#` immediate-operand size markers (byte- vs.
   word-sized) are a real, confirmed source-text convention: `*value.`
   when the value fits a signed byte (`-128..127`), `#value.` otherwise —
   confirmed both via `man/mutos_as.1`'s documented semantics and by
   actually assembling+disassembling both forms with the real `mutos_as`
   (identical machine code either way for plain `MOV`, since 8086's `MOV`
   has no byte-immediate form — the marker choice is purely a source-text
   convention here, not something affecting `02_bitwise`'s generated
   bytes). `AND`/`OR`/`EXOR`/unary `COMPL` (`~` on a non-constant operand
   — the first confirmed non-constant *unary* operator) follow the same
   `DI`-working-register shape as `+`/`-`.
8. Relational (`< <= > >=`), equality (`== !=`), logical (`&& ||`) and
   unary `!` all confirmed against `03_rellogic`. `c0`'s tree emission is
   uniform — every one of these is a plain "BN" (tag + type) node, exactly
   like `+`/`&`/etc.; there is **no `CBRANCH` involved** even though V7's
   `c04.c` reference shows `CBRANCH` used elsewhere (`if`-statement
   condition compilation, not yet implemented) — materializing a
   comparison's 0/1 value or fusing `&&`/`||` into a branch is entirely
   `c1`'s job. `c1`'s codegen (byte-level derivation in `docs/DEVLOG.md`):
   a standalone comparison compiles to `cmp` (right operand loaded into
   `di` first if it's memory, since 8086 `CMP` can't take two memory
   operands; used directly if it's an immediate) + a direct conditional
   branch (`blt`/`ble`/`bgt`/`bge`/`beq`/`bne`) to a fresh label, then
   `mov di,*0./jmp/Ltrue:mov di,*1./Lend:`. `&&`/`||` never materialize
   their operands separately — they fuse two comparisons into classic
   short-circuit "jumping code": `&&`'s first operand branches on its
   **inverted** condition straight to a false label (skipping the second
   operand entirely when short-circuiting), its second branches on its
   direct condition to the true label, and the false-label code is the
   fallthrough; `||` mirrors this with both operands branching direct to
   a shared true label and the false case as a pure fallthrough (no
   separate false label needed). Unary `!x` reuses this via a "negate the
   condition, defer materialization" step — no code is emitted at `!`
   itself, confirmed by `r = !r;`'s golden compiling to a single
   `cmp r,*0 / beq ...` (the negation of `NEQUAL`), never a separate
   negation instruction. `c1`'s own internal label counter (distinct from
   `temp1`'s own label numbers, which start at 1) starts at a confirmed
   `10000`. One byte-exact rendering quirk: `CMP`'s immediate right-hand
   operand omits the trailing `.` decimal-terminator that every other
   immediate rendering uses (`cmp *-6.(bp),*0`, not `*0.`) — per
   `man/mutos_as.1` the period is purely stylistic with zero effect on
   the assembled value, so this is a source-text quirk of the real `CMP`
   codegen path specifically, not a semantic difference.
9. Shift `<<`/`>>` confirmed against `04_shift`. `c0` again emits a plain
   "BN" `LSHIFT`/`RSHIFT` node, no special shape. `c1`'s codegen splits on
   whether the shift count is a compile-time constant or not: a
   **variable** count loads into a *second* working register, `CX`
   (confirmed via `"mov\tcx,*-8.(bp)"` immediately before `"sal\tdi,cl"`
   — the 8086's "shift by register" opcode only accepts `CL`, and this is
   the first construct in this grammar scope to need any register other
   than `DI`/`AX`/`DX`); the value being shifted still loads into `DI` as
   usual. A **constant** count uses no immediate-count opcode at all —
   plain 8086 has none (that's an 80186-only extension; `04_shift.c`'s
   own header comment confirms this compiler targets plain 8086 only) —
   so the real compiler instead repeats the single-bit-shift form N
   times: confirmed via `"r = r >> 2;"` emitting two consecutive
   `"sar\tdi,*1"` lines and `"r = a << 1;"` emitting exactly one
   `"sal\tdi,*1"` line. `SAL`/`SAR` (not `SHL`) are the real compiler's
   chosen mnemonics for `<<`/`>>` respectively. The immediate `*1`
   confirms the `CMP`-immediate no-trailing-period rendering quirk
   (delta #8 above) is not `CMP`-specific — it applies to `SAL`/`SAR`'s
   immediate operand too, so `c1_gen.c`'s renderer for it was renamed
   `render_bare_imm()` (from `render_cmp_imm()`) to reflect the broader
   confirmed scope.
10. Postfix/prefix `++`/`--` (`INCAFT`/`INCBEF`/`DECAFT`/`DECBEF`), pointer
    declarations (`int *p;`), single-dimension array declarations
    (`int a[N];`), array-to-pointer decay, and pointer dereference (`*p`)
    all confirmed against `05_incdec`. `c0`'s tree shape for `i++`/`++i`/
    etc.: the lvalue `NAME` first, then a literal `CON(1)` (the syntactic
    "1"), then — only for a pointer operand — `CON(MCC_SZINT)` and an
    `ITOP` node that scales the two into a real byte count, then the
    `INCAFT`/`INCBEF`/`DECAFT`/`DECBEF` tag itself; `c1` folds the
    `CON`/`ITOP` subtree into a plain immediate at compile time rather
    than emitting a runtime multiply (confirmed via `"add *-18.(bp),*2."`,
    never an `imul`). **Postfix vs. prefix is a codegen-ordering
    distinction, not a wire-format one**: `c1` commits a *prefix*
    op's `inc`/`dec` (or `add`/`sub`, for the scaled-pointer case)
    immediately, then loads the new value into `DI`; a *postfix* op loads
    the OLD value into `DI` first and defers the fixup instruction until
    the enclosing statement's `EXPR` node — confirmed via `"j = i++;"`
    emitting `"mov di,*-6.(bp)" / "mov *-8.(bp),di" / "inc *-6.(bp)"` in
    that exact order (the `inc` after the assignment's own store, not
    before). A bare array name used as an rvalue (`"p = a;"`) decays via
    `NAME` (the array's base element type/offset) followed by `AMPER`
    (address-of), which `c1` renders as a real `lea` — confirmed via
    `"lea di,*-16.(bp)"`. `ASSIGN`'s own type argument is the lvalue's
    real type (`TY_INT` or, newly, `TY_PTR_INT = TY_INT|010`, confirmed
    via `"p = a;"`'s `ASSIGN` node), not hardcoded `TY_INT` as every prior
    grammar increment happened to have. A dereferenced pointer used as an
    assignment target (`"*p++ = 1;"`/`"*++p = 2;"`) is a new statement
    form (`c0_parser.c`'s `parse_star_assign_stmt()`, dispatched on a
    leading `*` token) whose tree is the pointer sub-expression (with its
    own optional postfix/prefix `++`/`--`) followed by `STAR` — `c1`
    represents `STAR`'s result as a new operand kind, an indirect `(di)`
    addressing mode (`VK_IND`), confirmed via `"mov (di),*1."`. Only
    pointer-to-`int` and single-dimension `int` arrays are supported —
    multi-level pointers, multi-dimensional arrays, array subscripting
    (`a[i]`), and explicit `&`/`*` in general (rather than array decay or
    this one dereference-assignment shape) are not yet — see
    `src/mutos_cc/README.md`.
11. All ten compound-assignment operators (`+= -= *= /= %= <<= >>= &= |=
    ^=`) confirmed against `06_compasgn`. `c0`'s wire shape is
    pleasantly uniform: the real `ASPLUS`/`ASMINUS`/`ASTIMES`/`ASDIV`/
    `ASMOD`/`ASLSH`/`ASRSH`/`ASSAND`/`ASOR`/`ASXOR` opcode (`v7/cc/c0.h`
    already had dedicated values for each) is emitted directly, in the
    exact same `NAME`/rhs-expr/`<op-tag>`/`EXPR` shape as plain `=` —
    never a synthesized `a = a + 5`-style tree. `c1`'s codegen is where
    the interesting decisions are: `+= -= &= |= ^=` with a constant
    right-hand side each compile to a *single* in-place
    `"<mnem> <lvalue>,<imm>"` instruction — never routed through `DI`
    the way a non-compound binary operator is, since there is no
    separate `ASSIGN` node to do the memory write afterward. `*=` by a
    constant is a genuine strength-reduction confirmation, not a guess:
    `"a *= 2;"` compiles to `"sal *-6.(bp),*1"`, never an `imul` — 8086
    `IMUL` cannot take an immediate operand directly (the same
    restriction `TIMES`'s own codegen already enforces), so the real
    compiler substitutes a shift for a power-of-two constant multiply.
    `/=`/`%=` need an extra step neither `+=` nor `*=` do: since `IDIV`
    can't take an immediate either, the constant is loaded into `CX`
    first (`"mov cx,*4."` before `"idiv cx"`, confirmed distinct from
    the shift operators' own `CX`-loading helper, which skips loading
    when the value is already there — an immediate literal never is),
    and — because `IDIV`'s result lands in `AX`/`DX`, never directly in
    memory — an explicit `"mov <lvalue>,ax"` (or `dx` for `%=`)
    store-back follows, the only compound-assignment op needing one.
    Finding this also surfaced a **real, pre-existing bug** in
    `c0_lex.c`'s tokenizer, unrelated to any of the above:
    `skip_space_and_comments()`'s handling of a `/` that turns out not to start a
    comment needs to push back *two* characters (the `/` itself, plus
    the character peeked to rule out `/* ... */`), but the lexer's
    pushback buffer (`Lexer.peek`) only ever had room for one — a
    second `lex_ungetc()` call silently overwrote the first, dropping a
    character. This was invisible for a lone `/` (only a harmless
    trailing space got lost) but silently ate the `=` in `/=`, so
    `06_compasgn.c` was the first construct in this grammar scope to
    actually exercise (and expose) it. Fixed with a proper 2-slot LIFO
    pushback stack (`Lexer.peek[2]`/`npeek`), not a scope-specific
    workaround — a general lexer-correctness fix, not a `c0_parser.c`/
    `c1_gen.c` change. Only a constant (`CON`) right-hand side is
    confirmed/supported for any of the ten operators; a variable
    right-hand side, and `*=`/`/=`/`%=` by anything other than a
    positive power of two (for `*=`) is an explicit "not yet supported"
    — no golden confirms the register-operand sequence a real compiler
    would need there.
12. The `?:` conditional operator and the `,` comma operator confirmed
    against `07_ternary`. `c0`'s tree shape for `a > b ? a : b` is the
    condition (`GREAT`, already-established comparison codegen) followed
    by the two branches wrapped in a `COLON` node (`tr1`=true, `tr2`=false),
    followed by `QUEST` — matching real K&R `cc`'s tree shape exactly (a
    `QUEST` node's `tr1` is the condition, `tr2` is the `COLON` node). `c1`'s
    codegen is a **new branch polarity**, not a reuse of the existing
    bare-comparison-as-0/1-value pattern (`materialize_cond()`, confirmed
    against `03_rellogic`): `"a > b ? a : b"` compiles to
    `"cmp a,b" / "ble Lfalse"` — the **inverted** condition branches to a
    false label, and the **true** branch's code sits inline at the
    fallthrough (never behind its own jump target), unlike
    `materialize_cond()`'s branch-to-true-label/fallthrough-false shape.
    Each branch loads its value into `DI` unconditionally inside its own
    arm — confirmed by the false branch re-loading `b` into `DI` via
    `"mov di,*-8.(bp)"` even though the comparison's own right-hand-side
    setup had already loaded `b` into `DI` moments earlier (no
    cross-branch value tracking). The comma operator emits **no code of
    its own**: `SEQNC` is a pure value-discard — the left operand's side
    effects (if any) were already emitted by whichever opcode produced it,
    and its value is simply dropped, unmaterialized. This surfaced that a
    parenthesized comma-list can contain an **embedded plain assignment**
    (`"a = a + 1"` as one comma-item) — not otherwise reachable from
    expression context, since `assign-stmt` (handling `=` and the ten
    compound-assignment operators) is a distinct, statement-level-only
    production. Only a bare `=` is supported there, not any compound-
    assignment operator (unconfirmed in that position). Supporting this
    required `c0_parser.c` to gain one token of lookahead (`peek2_kind()`)
    to distinguish `"IDENT = expr"` from a bare `IDENT` starting a larger
    expression, both of which begin identically. It also surfaced that
    **`ASSIGN`'s own result value now has a real consumer**: an assignment
    used as a discarded comma-operand needs something on `c1`'s value
    stack for `SEQNC` to pop, so `OP_ASSIGN`'s handler was changed to push
    the assigned value (previously it pushed nothing, since no prior
    golden ever consumed an assignment's value) — which in turn required
    `OP_EXPR` to discard that now-leftover value for every *plain*
    top-level assignment statement (`gen_fatal`-guarded to at most one
    leftover value, to catch a real stack-imbalance bug rather than mask
    one). The ten compound-assignment operators deliberately do **not**
    push a result — `c0`'s grammar structure makes a compound-assignment
    expression unreachable from anywhere but a statement's sole top-level
    operator, so nothing would ever consume such a value; this is a
    grammar-structural guarantee; not merely an untested convention.
    Separately, `"a = a + 1;"` surfaced a confirmed `+1`-specific codegen
    shape: `"a + 1"` compiles to a plain `INC`, never `"add di,*1."` — an
    `OP_PLUS` special case for an immediate right-hand side of exactly 1.
    `OP_MINUS` is deliberately left unchanged (still `"sub di,*N."`
    unconditionally, including by 1) since no golden yet shows whether
    `"x - 1"` gets the symmetric `DEC` treatment.
13. `char`/`long` locals, casts between `int`/`char`/`long`, and `sizeof`
    confirmed against `08_castsize` — a genuine type-system extension, not
    just a new operator. `TY_CHAR=1`/`TY_LONG=6`/`TY_UNSIGN=7` already
    existed as named constants in `mutos_cc.h` (from earlier sessions'
    opcode-table transcription) but were unused until now. **A `long`'s
    wire/memory layout matches `docs/MUTOS_C_ABI.md` §1.6's already-
    documented "high word at the lower address" convention exactly**, down
    to the constant-encoding level, not just stack layout — confirmed via
    `"l = 70000;"` (`0x00011170`) splitting into an `LCON` node whose two
    fields decode to `1` (high) then `4464` (low), and via every
    long-producing/consuming opcode moving the low word through `SI` and
    the high word through `DI`, storing low-then-high. A `char` local
    occupies a full `MCC_SZINT`-sized (2-byte) stack slot despite its real
    1-byte value — confirmed by `"long l;"` (offset `-10`) immediately
    followed by `"char c;"` landing at `-12`, a 2-byte gap, not 1 — this
    target always word-aligns an AUTO local's slot, even for a byte value.
    **A previously-undocumented opcode, `107`, sits between the already-
    named `OP_SETREG=105` and `OP_ITOC=109`** — confirmed absent from
    vanilla V7's `c0.h` too (nothing is defined at 106/107/108 there
    either), so this is a genuine MUTOS-1700-specific addition, not
    something earlier opcode-table transcription simply missed. Named
    `OP_CTOL` (char-to-long) by the same `XTOY` convention as every other
    conversion opcode here — confirmed via `"l = (long) c;"`'s tree
    (`NAME(c)`, `CTOL`, `ASSIGN`), and its codegen is a textbook 8086
    sign-extension idiom: `"movb ax,<mem>"` (the char, loaded into `AX`
    specifically — `CBW`/`CWD` are fixed-register instructions, `AL`/`AX`
    only, so this is a hardware necessity, not a style choice) then `CBW`
    then `CWD` then moved into the `DI`(high)`:SI`(low) convention. Casts
    are narrowly scoped to a bare-variable operand
    (`'(' ('int'|'char'|'long') ')' IDENT`, not a general unary-expr) and
    compile to one of three confirmed conversion opcodes chosen by
    (source-type, target-type): `LTOI` (long→int, truncation — reads only
    the long's low word, discarding the high word entirely: `"mov
    di,*-8.(bp)"` where `l`'s own base offset is `-10`), `ITOC` (int→char,
    truncation — loads into `DX` specifically, not the usual `DI`
    "working register," since the subsequent `movb` store needs a
    byte-addressable register and `DI`/`SI` have none on the 8086), and
    `CTOL` as above. **`sizeof` never emits a wire opcode of its own at
    all** — every `sizeof(...)` in `08_castsize.c`, including `sizeof(i)`
    (a variable, not a type name), folds directly to `CON(TY_UNSIGN,
    <size>)` at parse time — for `sizeof(i)`, `i`'s own `NAME` is never
    even emitted, confirming `sizeof`'s operand is genuinely never
    evaluated, only its type inspected, matching real C semantics exactly.
    Large integer literals (`70000`) are automatically promoted to `long`
    (standard K&R/C89 constant promotion — K&R2 §A2.5.1: a decimal
    constant too big for `int` becomes `long`), threaded through via a new
    `is_long` flag on `c0_parser.c`'s `ExprVal`, never propagated through
    any arithmetic combinator (no golden exercises `long` arithmetic —
    only a bare literal, directly and immediately assigned). Only these
    three (source, target) cast pairs, a bare-`IDENT`/bare-type-keyword
    `sizeof` operand, and a memory (not register-pair) `long`/`char`
    operand for every new opcode are supported — anything else (a fourth
    cast combination, `long` arithmetic, `sizeof` on an array or a general
    expression, a `long`/`char` pointer or array) is an explicit "not yet
    supported."

**Current grammar/opcode scope (deliberately narrow, by design — see
`src/mutos_cc/README.md`):** function definitions with no parameters; a
body of `int`/`int *`/single-dimension `int[N]`/`char`/`long` local
declarations (no
initializers; `char`/`long` support only a plain-`IDENT` declarator, no
pointer/array form) followed by `name <assign-op> expr;` assignment (`=` or any
of the ten compound-assignment operators `+= -= *= /= %= <<= >>= &= |=
^=`), `*<ptr-expr> = expr;`
dereferenced-pointer assignment, and/or `return` statements; expressions
over `+ - * / % & | ^ ~ < <= > >= == != && || ! << >> ?:` (unary/binary as
applicable), postfix/prefix `++`/`--` (pointer-scaled where applicable),
a narrowly-scoped cast (`'(' ('int'|'char'|'long') ')' IDENT`) and
`sizeof` (`sizeof '(' ('int'|'char'|'long'|IDENT) ')'`), parens
(including a parenthesized comma-list, optionally containing a
plain `=` assignment per comma-item), integer
constants (automatically `long`-promoted per K&R/C89 rules when too large
for a plain `int`), and variable references (with array-to-pointer decay on an
array name used as an rvalue), constant-folded at parse time exactly
like real K&R `cc`'s own per-operation `build()`-time folding, with a real
`NAME`/operator tree emitted the moment a variable is involved. **`01_expr`
is now fully covered.** Everything
else (a compound-assignment operator's right-hand side being anything
other than a compile-time constant, `*=`/`/=`/`%=` by a non-power-of-two
or non-constant, a `?:` branch that is itself a bare relational
comparison, a compound-assignment operator inside a comma-list, a cast
combination other than the three confirmed ones (long→int, int→char,
char→long), `sizeof` on an array or a general expression, `long`
arithmetic, a `char`/`long` pointer or array, any other
element type, array
subscripting, multi-level pointers/multi-dimensional arrays, explicit `&`
outside array decay, function parameters, control flow, memory-to-memory
assignment, an immediate `IMUL`/`IDIV` operand outside a confirmed
compound-assignment shape) is an explicit "not yet
supported" error, not silently-wrong output — confirmed via a direct test
against `02_long/01_addsub.c` (correctly rejected with a clear message,
not a crash or bad `.s`).

Build: `cd src/mutos_cc && make`, or `make`/`make test` from the repo root
(see "Top-level build" above); clean build, zero warnings under `-Wall
-Wextra -Wpedantic`. `mutos_cc` (the driver chaining `cpp|c0|c1|
as|ld`) is not yet written — see `src/mutos_cc/README.md`'s "Next steps"
for the full, dependency-ordered expansion plan (the rest of `01_expr`,
`long`/arrays/pointers/structs, control flow, function calls, the
`chkstk` threshold, then the driver).

This is the next milestone after `mutos_cpp`.
Scope (from `CLAUDE.md`'s roadmap): port the V7 `cc` frontend/`c0`/`c1` pipeline,
emit x86-16 code in `mutos_as` syntax, and enforce PDP-11 middle-endian encoding for
compiled `long` variables (see `CLAUDE.md`'s "PDP-11 Middle-Endian" rule, which
explicitly calls out that this is where the rule will actually start mattering — now
confirmed true, see below).

### `mutos_c0`/`mutos_c1`: verified this session (`02_long/02_muldiv`)

**`long` `*`/`/`/`%` confirmed against `02_long/02_muldiv.c`** — the first
construct in `02_long`'s own scope (`long` arithmetic; `long` locals/casts/
constants were already done, from `08_castsize`). Two real gaps had to be
fixed, both found by byte-decoding `02_muldiv.1.golden`/`.s.golden` rather
than by guessing from `docs/MUTOS_C_ABI.md` §1.8's prose alone:

1. **A genuine `mutos_c0` lexer/parser bug, unrelated to `long` arithmetic
   itself**: `parse_primary()` never had a case for `T_LCON` (an integer
   literal with an explicit `l`/`L` suffix) — only a bare oversized literal
   (e.g. `70000`, auto-promoted per K&R/C89 magnitude rules) reached the
   existing `LCON`-emission path. A small literal with an explicit suffix
   (`37L`) fell through to "expected an expression, found long constant".
   Fixed by giving `T_LCON` its own `parse_primary()` case that always
   returns a `long`-typed constant, regardless of magnitude.
2. **`long`-typed arithmetic needed real type propagation through `c0`,
   not just a bigger opcode table in `c1`.** `ExprVal` previously tracked
   `is_long` only for the constant-folding path, never for an
   already-emitted (dynamic) value like a `NAME` reference — every binary
   operator's wire-format type argument was hardcoded `TY_INT`. Added a
   `type` field (set from a `NAME`'s own declared `sym->type`, or `TY_INT`/
   `TY_LONG` for a constant) that `parse_mul()`'s `*`/`/`/`%` now consult to
   pick `OP_TIMES`/`OP_DIVIDE`/`OP_MOD`'s `TY_LONG` vs. `TY_INT` operand —
   confirmed byte-for-byte against `02_muldiv.1.golden`'s `"c = a * b;"`
   (`a`, `b`, `c` all `long`) tree. Deliberately **not** propagated into
   `parse_add()`'s `+`/`-` yet — no golden for `long` `+`/`-` can be
   re-verified this session (`02_long/01_addsub.c` also needs an `if`
   statement, which is `03_ctrlflow` scope, not yet implemented — see
   "Next up" below), so guessing its `c1` codegen shape is out of scope.

**`c1`'s actual confirmed calling convention for `long` `*`/`/`/`%`
differs from `docs/MUTOS_C_ABI.md` §1.8's own prose** (which describes
`almul`/`aldiv`/`alrem` using a pointer/lvalue-and-result convention,
explicitly hedged as "presumably" how the code generator uses them).
`02_muldiv.s.golden`'s actual generated code instead calls plain `lmul`/
`ldiv`/`lrem` (the *integer counterparts* §1.8 also names) with both
`long` operands passed **flat**, as two ordinary two-word `long`s per
§1.6, right-to-left per §1.1 — i.e. push order is `r_low, r_high, l_low,
l_high` (the right/second operand's own low-then-high pair, then the
left/first operand's) — never a pointer. Result comes back in `DX:AX`
per §1.5's ordinary `long`-return convention, then moved into the
`DI`(high)`:SI`(low) convention every other confirmed `long`-value
producer (`OP_LCON`/`OP_CTOL`) already uses, so `OP_ASSIGN`'s existing
`TY_LONG` case (no changes needed there) stores it correctly. Implemented
as a single shared `gen_long_binop_call()` helper in `c1_gen.c` used by
all three operators; only a plain memory (`NAME`) operand is supported so
far for either side (matches `02_muldiv.c`'s only confirmed shape — both
operands are bare locals, never an immediate or a nested long expression).

**`OP_LCON`'s `c1` codegen has two real, distinct confirmed shapes**, not
one — found only because `02_muldiv.c`'s `"b = 37L;"` (small magnitude,
explicit suffix) sits right next to `"a = 123456L;"` (genuinely 32-bit) in
the same golden:
- When the 32-bit value's high word is **not** the sign-extension of its
  low word (`hi != (lo < 0 ? -1 : 0)`, e.g. `123456L`, or `08_castsize`'s
  `70000`): direct split, `mov si,<lo> / mov di,<hi>` (unchanged from the
  prior implementation).
- When it **is** (e.g. `37L`, an ordinary int-range value that merely
  carries a `long` suffix/type): `mov ax,<lo> / cwd / mov di,dx / mov
  si,ax` — the same sign-extension idiom `OP_CTOL` already uses starting
  from a `movb`, just starting from a plain immediate here instead.
  Confirmed via `02_muldiv.s.golden`'s `"b = 37L;"` using this shape, not
  the direct-split one, even though `37L`'s explicit suffix takes the same
  `T_LCON`/`LCON` wire path as `123456L`.

`02_long/01_addsub.c`, `03_retval.c` and `04_params.c` remain "not yet
supported" — confirmed to be blocked on `if`/function-call/parameter
support (`03_ctrlflow`/`04_funcs` scope), not on anything `long`-arithmetic
-specific; re-run of the full golden suite after this session's changes
shows their same pre-existing diagnostics, unchanged, and zero regressions
anywhere else in the 62-file corpus.

Build: `cd src/mutos_cc && make`; clean build, zero warnings under `-Wall
-Wextra -Wpedantic`.

### `mutos_c0`/`mutos_c1`: verified this session (all of `03_ctrlflow`, plus `02_long/01_addsub`)

**All 7 of `03_ctrlflow` are now confirmed** - `if`/`else`, `while`,
`do`/`while`, `for`, nested `break`/`continue`, `switch`/`case`/`default`,
and `goto`/labels. `mutos_c0` gained a real recursive-descent statement
dispatcher (`parse_statement()`) in place of the prior flat assign/return-
only loop; every construct reuses the existing `CBRANCH`/`BRANCH`/`LABEL`
opcodes and `p->isn` as the same function-wide label counter `cfunc()`
already seeded (`sloc`/`sloc+1`/`retlab`) - no new opcodes were needed for
`if`/`while`/`do`/`for`/`break`/`continue`/`goto` (only `switch` added one:
`SWIT`). Byte-decoded every `.1.golden`/`.s.golden` in the category via
`od` before writing any code, per this project's standing methodology.

**`if`/`else` (`01_ifelse`)**: `CBRANCH(false_lab, cond=0)` skips the
true-branch on a false condition; an `else` additionally allocates a second
label (`end_lab`) only once actually seen, and the true-branch `BRANCH`es
past the else-branch to it - confirmed against a 2-level `else if` chain
allocating its labels in the same left-to-right, two-per-level order
(4,5 outer; 6,7 inner). `CBRANCH`'s own `line` argument is confirmed to be
the line of whatever token follows the condition's `)` - not the `if`'s own
line - a direct, for-free consequence of v7/cc's one-token lookahead there
(done to check for the shortcut below), which this parser reproduces simply
because `p->cur` already sits on that next token once `)` is consumed.

**`while`/`do`-`while` (`02_while`/`03_dowhile`)**: `while` is the textbook
2-label shape (`LABEL(top)` doubling as `continue`'s target, `CBRANCH(end,
cond=0)`, body, `BRANCH(top)`, `LABEL(end)` as `break`'s target). `do`-
`while` allocates 3 labels up front in a fixed order - `contlab`, `brklab`,
then the loop's own top-of-body label - but only the top label is placed
immediately; `contlab` isn't placed until after the body (right before the
condition test), and the trailing `CBRANCH` branches back to the top label
on a TRUE condition (`cond=1`), falling through to `brklab` otherwise.
Neither construct's `CBRANCH` line shifts the way `if`'s does - no extra
lookahead happens for either, so `line` is simply the condition's own
closing `)`.

**`for` (`04_for`)**: matches `v7/cc/c02.c`'s `forstmt()` exactly, including
its most surprising piece - the increment clause is PARSED before the body
(to keep the token stream in order) but its EMITTED CODE is deferred until
after the body, and its own `EXPR` opcode keeps the source line where it
was originally written (the `for`-header's own line), not wherever body-
parsing left off - confirmed via `04_for`'s increment (`i = i + 1`, on the
header's line 9) carrying `EXPR(9)` despite appearing in the byte stream
after the body (whose own line is 10), and equivalently for both the outer
and inner loops of `05_breakcont`'s nested case. Since `mutos_c0` streams
wire bytes as it parses (no AST to re-emit later the way real `cc`'s
`rcexpr(st)` does), the increment's bytes are buffered via
`open_memstream()` and flushed verbatim after the body. `continue` inside
the body targets a NEW label placed right before this deferred increment,
reassigned only once the increment's presence is known - matching v7's
`l = contlab; contlab = isn++;` exactly.

**`break`/`continue`, including nested loops (`05_breakcont`)**: `p->brklab`/
`p->contlab` are plain ints, saved locally and restored by each loop/switch
parser function around its own body - nesting falls entirely out of C's own
call stack, with no explicit stack structure needed, confirmed against the
nested-`for` case's label numbering (outer allocates 4,5,6; inner,
recursed into from the outer's own body-parsing call, continues from 7,8,9).

**The `if (cond) goto X;`/`break;`/`continue;` "simpif" shortcut** (confirmed
in both `05_breakcont` and `07_goto`): when an `if`'s body is EXACTLY a bare
`goto label;`/`break;`/`continue;`, it compiles to a single direct
`CBRANCH(target, cond=1)` - no extra label at all - rather than the general
two-label shape. `target` is the `goto`'s own label (allocated now if this
is a forward reference), or the enclosing `brklab`/`contlab`. This is v7/
cc's own "simpif" optimization (`c02.c`'s `statement()`, the `IF` case's
inner `switch`), reproduced deliberately rather than incidentally - without
it, `if (j == 3) break;` would emit a different, non-matching shape.

**`goto`/labels (`07_goto`)**: a small function-scoped label-name table
(`name → intermediate-code label number`, allocated on first mention -
whichever comes first, a `name:` definition or a `goto name;` reference)
handles forward references cleanly; `"loop:"` (defined before any reference)
and `"done:"` (referenced by a forward `goto` before its own definition)
both resolve to the correct, single label number either way.

**`switch`/`case`/`default` (`06_switch`)**: the deepest single addition
this session. Matches v7/cc's `pswitch()` precisely: the controlling
expression is `RFORCE`'d (the exact same "force into the return-value
convention" wrapper `return` itself uses) and emitted as its own
expression-statement (loading it into `AX`), then a `BRANCH` jumps PAST the
whole body to a fresh dispatch label; the body is parsed inline, with
`case`/`default` just placing a label and recording a `(label, value)`
pair (or, for `default`, recording the label separately as `deflab`); once
the body is done, the dispatch label gets a genuine JUMP TABLE:
```
sub ax,#/1          ; normalize to a 0-based index (min case value = 1) -
                     ; the ONE confirmed immediate rendered in HEX
                     ; (man/mutos_as.1's leading-'/' literal), not this
                     ; project's usual decimal '*N.'/'#N.'
cmp ax,*3.           ; range check (max-min), ordinary decimal
bhi L10              ; out of range (unsigned-above) -> default's label
shl ax,#1            ; scale index to a word offset
xchg bx,ax
seg cs
jmp @L10001(bx)      ; indirect jump through the table
L10001:L6            ; the table: one case-body label per word, ascending
L7                   ; by case value
L8
L9
```
Only a DENSE, CONTIGUOUS run of case values (no gaps) is implemented - a
genuinely sparse `switch` would need a linear compare-chain instead, which
no golden confirms, so that stays an explicit "not yet supported" rather
than a guess. The table's own internal label is confirmed to burn one
extra label first (`L10001`, not `L10000`, even though this switch is the
only internal-label consumer in the file) - not derivable from this one
example, so it's reproduced as an observed constant rather than explained.
`OP_SWIT`'s own wire format matches v7 exactly too: `deflab`, then a source
line (the body's own closing `}`, requiring a new `p->prev_line` - "the
line of the token most recently consumed" - since a nested
`parse_statement()` call had already moved `p->cur` past it by the time
control returns), then a run of `(label, value)` pairs, then a single LONE
zero word as the table's terminator (never a `(0,0)` pair).

**`02_long/01_addsub` also fell out of this session** - it was blocked only
on `if` (per the prior session's notes), but implementing `if` surfaced
three more real, confirmed gaps needed to actually complete it:
- **Implicit `int`→`long` widening (`ITOL`, a new opcode)**: an int-range
  constant assigned to a `long` lvalue with no explicit `L` suffix (e.g.
  `"b = 23456;"`) takes the ordinary `CON` path, not `LCON`'s - `c0_parser.c`
  now wraps it in `OP_ITOL(TY_LONG)` before `ASSIGN`; `c1`'s codegen is the
  same `CWD` sign-extension idiom `OP_CTOL`/`materialize_long()` already
  use, just starting from a plain immediate.
- **`long` `+`/`-`**: `OP_PLUS`/`OP_MINUS` now carry `TY_LONG` when either
  operand is `long` (same rule `02_muldiv` already confirmed for `*`/`/`/
  `%`). Codegen has TWO confirmed shapes depending on the right operand: a
  plain memory operand loads the left into `DI:SI` then `ADD`/`ADC` (or
  `SUB`/`SBB`) read the right straight out of memory; an in-range `long`
  CONSTANT right operand is sign-extended into `DX:AX`, pushed to the
  stack (low then high) to free those registers, has the left operand
  loaded into `SI:DI`, then popped back into `BX`(high)`:CX`(low) before
  the same `ADD`/`ADC` pair - confirmed down to a literal real-hardware
  asymmetry: the second `pop` renders as `"pop cx"` with a plain space, not
  a tab, unlike every other instruction here.
- **`long` relational comparison against a constant**: genuinely different
  from a 16-bit comparison - a 32-bit signed comparison on a 16-bit ALU
  compares HIGH words SIGNED first (which alone decides the answer whenever
  they differ), only falling through to an UNSIGNED low-word compare when
  the high words are equal. Confirmed only for `"if (c > 0L)"` (`OP_GREAT`
  against a literal `0L`, at a `CBRANCH` "branch if false" site) - any other
  operator, non-zero/non-constant operand, or "branch if true" site is an
  explicit "not yet supported" rather than a guess. This also surfaced that
  a comparison's own `type` wire argument is ALWAYS plain `TY_INT`,
  regardless of operand type (a comparison's RESULT is always `int`, per
  ordinary C semantics) - `long`-ness is detected instead from the
  operands themselves, which in turn required redesigning `OP_LCON` to be
  LAZILY materialized: a raw, unmaterialized `(hi, lo)` pair (`VK_LCON`) is
  pushed with no code emitted at all, since a `long` constant used as a
  comparison operand never loads into registers in the real output - it
  folds straight into a bare `cmp` immediate. Confirmed backward-compatible
  with every already-passing golden (`02_muldiv`, `08_castsize`), since
  `ASSIGN` was already always the very next opcode after `LCON` in every
  one of those cases, with nothing in between to observe the difference.

**A real, pre-existing bug was also found and fixed along the way**: `CMP`'s
immediate right-hand side does NOT universally omit the trailing `"."`
decimal-terminator the way a prior session's `03_rellogic`-only derivation
concluded - every `CMP` immediate confirmed there happened to be value `0`
(`"cmp *-6.(bp),*0"`), which is genuinely the one exception; `03_ctrlflow`'s
non-zero cases (`"cmp *-6.(bp),*10."`, `"*5."`, `"*3."`) confirm the
period IS present otherwise, matching `render_operand()`'s ordinary
convention. `render_bare_imm()` (still correct, and still used, for
`SAL`/`SAR`'s own confirmed no-period shift-count operand) was split from a
new, correctly-scoped `render_cmp_imm()`.

Full-corpus regression (`tests/mutos_cc/run_goldens.sh`) confirms zero
regressions anywhere: 20 of 62 byte-exact (up from 12), 0 genuine mismatches.
`02_long/03_retval.c` and `04_params.c` were re-checked and confirmed to
still fail with their same pre-existing diagnostics - both need function
calls/parameters (`04_funcs` scope), not more control-flow or `long`-
arithmetic work.

Build: `cd src/mutos_cc && make`; clean build, zero warnings under `-Wall
-Wextra -Wpedantic`.

### ABI research (carried from prior session records — 2026-09-06)

Before writing any `mutos_c1` code generation logic, the real MUTOS 1700 function
calling convention and C runtime startup/cleanup behavior were reverse-engineered
byte-for-byte from real hardware-linked objects: `tests/mutos1700_crt0/crt0.o.base64.txt` and
~15 selected files from `tests/mutos1700_libc/`'s 167 real linked objects (the
load-bearing evidence), plus `tests/mutos_as/kernel_opt/mch.s`'s hand-written source
as supplementary/corroborating evidence — **not**, as an earlier version of this
note incorrectly stated, compiler output: `mch.c` is explicitly marked
`Assemblerteil` ("the assembly portion") in its own header comment and is never
passed through `c0`/`c1`/`c2`, only `cpp`+`as` (correction found via review; see
`docs/DEVLOG.md`'s Milestone 4 section and `docs/MUTOS_C_ABI.md` §1.10 for the full
story, prompted by `mch.s`'s own `_outb`/`_out`/`_in`/`_inb`/`_hdio` using `bx`, not
`bp`, as a lighter-weight hand-written frame convention). Full findings, each cited
against a real disassembled example, are in
**[`docs/MUTOS_C_ABI.md`](./docs/MUTOS_C_ABI.md)**;
condensed summary also in `docs/DEVLOG.md`'s Milestone 4 section. Headline points:
pure stack-based argument passing (right-to-left push, caller cleanup); a completely
fixed, unconditional `push bp/mov bp,sp/push di/push si ... jmp cret` prologue/
epilogue used by every compiled function regardless of actual register/local usage;
fixed parameter (`bp+4,+6,...`) and local (`bp-6,-8,...`) frame offsets; `AX`/`DX:AX`
return convention; `long` values confirmed to use PDP-11 middle-endian word order
(high word at the lower address) everywhere — locals, by-reference operands, and
by-value parameters alike; a separate, internal-only extended-prologue ABI used
solely by the compiler's own `almul`/`aldiv`/`alrem` long-arithmetic runtime helpers;
a `chkstk` stack-overflow guard for large local frames (threshold then only
bounded to between 76 and 256 bytes by `libc.a`; since narrowed by the
`09_abiprobe` goldens - see the Milestone 4 section above); and the real `crt0` →
`_main` → `exit()` → `_cleanup()` cleanup chain (crt0 calls `exit()`, which flushes
stdio via a `_cleanup()` hook, before the raw `_exit()` syscall — confirmed via both
halves' disassembly). This is prep/documentation only — no `mutos_c1` code exists
yet to validate against these findings.

### `c0`/`c1` process split and K&R test corpus (verified this session)

Two design questions were resolved ahead of writing any `mutos_c0`/`mutos_c1`
code:

- **`c0`/`c1` split: keep it, as two separate executables**, matching
  `CLAUDE.md`'s already-documented "Target Executables" list. Reading the real
  `v7/cc` source (`c04.c`'s `outcode()` / `c11.c`'s `getree()`) showed the
  `temp1`/`temp2` intermediate format described in `v7/cc/cc.c`'s own usage
  comment (`c0 source temp1 temp2`) is not a raw memory/pointer dump but a
  small, fully-specified tagged byte stream — which makes the split's
  testability upside (an inspectable IR boundary, verifiable before any x86
  code generator exists) real rather than a leftover of the PDP-11's 64K
  address-space limit. Decision, rationale, and the table-driven-matcher
  (`table.s`/`cctab`/`efftab`/`regtab`/`sptab`) analysis that grounds it are in
  `docs/DEVLOG.md`'s Milestone 4 section. This does **not** reduce the actual
  engineering cost of Milestone 4 — porting the PDP-11-specific register/
  addressing-mode assumptions throughout `c10`–`c12` to the 8086's much more
  irregular register model is identical work whether `c0`/`c1` are one process
  or two — it only buys an earlier, cheaper diagnostic checkpoint.
- **Test corpus**: `tests/mutos_cc/` now holds 62 small K&R C files across 11
  numbered categories (`00_smoke` … `10_integ`), plus a `Makefile` and
  `README.md` — source-only, no `*.s.golden` references yet. Every identifier
  was checked against this toolchain's real significant-character limits (8
  internal / 7 external, see `CLAUDE.md`'s "Identifier length limits" rule);
  two were found over the 7-character external limit and renamed (`factorial`
  → `fact`, `swapchar` → `swapch`). Every file/directory name was checked
  against MUTOS 1700's real `DIRSIZ`=14 filename limit (`tests/mutos_cpp/h/
  dir.h`, `h/param.h`) and shortened accordingly (e.g. `01_wordcount.c`,
  several others sit exactly at 14). A `09_abiprobe/frame080.c` …
  `frame300.c` sub-series specifically targets this document's own open
  `chkstk` threshold question below.
- **Golden generation: confirmed working end-to-end this session.** Real
  hardware produces `.s`/`.i`/`.1`/`.2` for the corpus correctly (verified for
  `00_smoke`); those get turned into `*.s.golden`/`*.i.golden`/`*.1.golden`/
  `*.2.golden` (+ base64 companions for the binary `.1`/`.2`) on the modern
  host via `tests/mutos_cc/Makefile`'s `goldens` target. Getting there
  required finding and fixing several real bugs in this project's own
  tooling — see the next subsection. Not yet present in this checkout as of
  this writing (goldens are generated and committed by the person running
  the real hardware, not by this session directly).

### MUTOS 1700 host-tooling findings (verified this session)

Building and running the corpus on real MUTOS 1700 hardware surfaced several
concrete, previously-undocumented facts about the toolchain itself — not
about `mutos_cc` design, but about the real V7-heritage tools this project's
own scripts and Makefiles have to work within. Full narratives, each with the
exact error text and root cause, are in `docs/DEVLOG.md`'s Milestone 4
section; headline findings:

- **MUTOS 1700 `make(1)` is not GNU Make and has real capacity limits**,
  confirmed against its own manpage: no `%.o: %.c` pattern rules, no
  `$(wildcard)`/`$(dir)`/`$(notdir)` functions, no `:=` — only plain `=`
  macros and two-suffix rules (`.c.o:`). Beyond syntax, it also has a fixed
  internal capacity: a single makefile covering all 62 test files (124
  targets, ~490 lines) failed twice in different ways — first
  `Make: line too long. Stop.` (one ~2950-character backslash-continued
  dependency line), then, even after that was fixed, `Make: out of memory.
  Stop.` partway through the very first category. The working fix was
  structural, not cosmetic: one small `Makefile.mutos` per category
  directory (2–9 files each) instead of one covering all 62.
  `tests/mutos_cc/`'s top-level `Makefile` (GNU-only) stays for the modern
  host; each category directory has its own real-`make`-compatible
  `Makefile.mutos`; `gen_mutos.sh` is a `make`-independent `/bin/sh`
  fallback covering the same ground.
- **Real `cc`'s `-P` and `-S` cannot be combined.** `v7/cc/cc.c`'s own
  control flow — not just its flag-parsing switch — makes `-P` stop `cc`
  dead right after `cpp` (`if (pflag) { cflag++; continue; }`), for every
  source file, before the code checking `-S`'s `sflag` is ever reached. An
  early version of this corpus's `.s`-generating recipes used `cc -P -S`
  and silently produced `.i` files but never `.s` — found only once real
  `.s`/`.i`/`.1`/`.2` output was inspected on real hardware. Fixed to plain
  `cc -S` (no `-P`) everywhere `.s` is generated; the separate direct
  `cpp -P` calls used for `.i`/`.1`/`.2` are unaffected (not routed through
  the `cc` driver, so this control-flow interaction doesn't apply to them).
- **`goldens` must never depend on `all`/`intermediates`.** An earlier
  version of the top-level `Makefile` had `goldens: all intermediates`,
  which made a plain `make goldens` on the modern host re-check `.s`/`.1`
  freshness against `.c`/`.i` by mtime — and after a fresh `git checkout` or
  a file transfer from MUTOS, those timestamps routinely don't land in the
  order Make expects, so it decided already-generated files were stale and
  tried to rebuild them with `$(CC)`/`$(CPP)`/`$(C0)`, none of which exist
  on the modern host (`/lib/c0: not found`). `goldens` now has no
  prerequisites at all — it only packages whatever `.s`/`.i`/`.1`/`.2`
  already exist on disk, skipping (not failing on) anything missing.
- **`find(1)`/`expr(1)`/`basename(1)`**, verified against their real
  manpages for `gen_mutos.sh`: this `find`'s predicates (`-name` included)
  are pure tests with no implicit default action, so a bare
  `find . -name '*.c'` silently produces zero output — an explicit
  `-print` is required (GNU `find` defaults to it; this one doesn't).
  `expr`/`basename` usage was already correct, confirmed against the
  manpages' own example usages.

### Next up

Grow `mutos_c0`/`mutos_c1`'s grammar/opcode coverage category by category,
per `tests/mutos_cc/`'s own increasing-difficulty ordering — every category
is now fully covered (62/62). In order:

1. **Run `tests/mutos_cc/fltprobe/`'s round 4 on real hardware** (`make
   -f Makefile.mutos round4` there, then `make goldens` in
   `tests/mutos_cc` on the modern host - all four kinds of file):
   `p14_axint.c` (int shapes round 3 raised: a value in AX plus a
   constant stored, `+ 0` in DI, products and `&` of two int elements,
   `x /= f()`, `y %= f()`), `p15_fltinf.c` (a chain ordered by an
   element's degree, shifts converted in AX, a call then an element),
   `p16_open2.c` (the floating shapes still refused). Rounds 1 to 3 are
   byte-exact - see the sections above and
   `tests/mutos_cc/fltprobe/README.md`.
2. **Globals beyond `07_scope`**, each with kernel evidence (see
   `docs/DEVLOG.md`'s `07_scope` section): `++`/`--` and compound
   assignment on a global or local static (`inc _amxslee`, `orb
   _amxscd(bx),*4.` in `kernel_nonopt/amx.s`); `x = y;` between two
   memory operands goes through DI now (`03_linklist`'s `head = cur;`),
   but in a function with a register variable in DI the kernel uses DX
   (`mov dx,*-6.(bp)` / `mov _cfreeli,dx`, a function with two) - which
   register with one still needs a golden.
3. **Two shape follow-ups from the conditional-evaluation work** (both
   currently correct code, just not the real compiler's bytes; see
   `docs/DEVLOG.md`'s "Conditional evaluation" section): a call result
   tested as a condition is `cmp ax,*0` where `kernel_nonopt/sys1.s`
   (four instances) has `or ax,ax` - v7's `tst r` fallback, which `c1`
   emits so far only for a postfix operand; and a postfix call argument is
   `mov di,a / push di / inc a` where the kernel pushes the memory operand
   directly (`push *-56.(bp) / inc *-56.(bp) / call _clearse`).
4. **Struct shapes still refused** (see `src/mutos_cc/README.md`'s
   "Structs, unions, bit-fields, enums and typedefs"): an int member of a
   file-scope struct and an element of a file-scope int array (`mutos_c0`
   accepts the declarations now - `p3_global` needed them for floats -
   `mutos_c1` refuses the access), functions returning a struct or a struct
   pointer, block
   copies, a computed value stored into a bit-field, a struct size that is
   not a power of two in a subscript - each needs evidence of the real
   compiler's shape first. Then the remaining 81..127-byte `chkstk` gap.
5. **`tests/mutos_cc/11_kernel`** (9 real kernel driver files, currently
   0/9): grow coverage into it once the categories above are done. One
   concrete, confirmed gap to start from - a bare expression-statement
   (`i++;` on its own, `01_delay.c`) has no grammar production yet; the
   six files refusing with "external definition syntax" still need their
   specific construct isolated first. See `docs/DEVLOG.md`'s Milestone 4
   section for the full per-file breakdown.
6. **The `mutos_cc` driver itself.**

The register-occupancy guard's refusals (see the `c1_gen.c` review section
above) mark where further spill/reordering codegen - and an SI-scratch
generalization for functions with a `register` local - will be needed; each
needs its own golden before it can be implemented. The largest group in the
fuzzer's refusals is now a 2-D subscript while DI holds another value
(`x + m[i][j]`), which needs the base address in a register other than DI.
See `src/mutos_cc/README.md`'s "Next steps" for the full plan.

---

## Milestone 5 — Optimizer (`c2`) & NEC V30 (`-mv30`)

**Status: NOT STARTED — no `c2` work, but one design question is resolved.**
Confirmed constraint: `-mv30`-compiled code must stay link-compatible with the real,
unmodified `libc.a`/`crt0.o` — no separate `-mv30`-only runtime is planned, so the
ABI-visible frame layout from Milestone 4's ABI research can never change based on
`-mv30`, regardless of any performance case. Under that constraint, real NEC
µPD70116(V30) timing data (from the newly added
`docs/V20_V30_Users_Manual_Oct86.pdf` — note: NEC calls `ENTER`/`LEAVE` `PREPARE`/
`DISPOSE`) shows the only layout-compatible use of `PREPARE`/`DISPOSE` is an exact
cycle-for-cycle tie against the current discrete-instruction prologue/epilogue, with
a small code-size regression — so `mutos_c1` should **not** use `PREPARE`/`DISPOSE`
for `-mv30`. `PUSHA`/`POPA` and multi-bit shift-by-immediate remain confirmed,
ABI-safe wins for later `-mv30` work. Full numbers and derivation in
`docs/DEVLOG.md`'s Milestone 5 section.

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
| `insw` | `0x6D` | implemented — **confirmed real** (`mch_insw_outsw.s`) |
| `outsb` | `0x6E` | implemented — no real corpus sample; cross-checked¹ |
| `outsw` | `0x6F` | implemented — **confirmed real** (`mch_insw_outsw.s`) |
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

### 80186/V30 speculative-opcode test files (this session)

Added `tests/mutos_as/v30_speculative/` — seven hand-written test files (one per
natural instruction family, same pairing convention as `mch_insw_outsw.s`) covering
all twelve "no real corpus sample" rows in the table above, plus `nasm_crosscheck.asm`
and a `README.md` documenting the full methodology, exact commands, and results.

Verified this session:
- All seven files assemble cleanly with the current `mutos_as` (zero errors) and
  remain clean under `-fsanitize=address,undefined -O0 -g`.
- The pre-existing 67/67 golden regression is unaffected (new files only, no `src/`
  changes).
- Every instruction in every file was independently decoded with `objdump -D -b
  binary -m i8086` and manually checked against the intended mnemonic/operand/
  addressing-mode; a representative subset covering all seven families was
  independently re-encoded with NASM and matched byte-for-byte wherever the encoding
  is unambiguous (see footnote ¹ above for the one case where it is not).
- The real kernel corpus (`kernel_opt`+`kernel_nonopt`) was re-searched for any
  further hand-encoded `.byte` opcode workarounds of the kind that made INSW/OUTSW
  "confirmed real" (see Milestone 2's "Recent fixes" above) — **none found** for any
  of the other twelve opcodes; INSW/OUTSW remain the only such case in the corpus.
- `x86dis` (`libdisasm` 0.23, no real code change since Dec 2013) was evaluated as an
  alternative to `objdump` and rejected for this purpose: its `-e <offset>`
  (forward-trace) mode silently stops decoding at the first `ret` byte with no error
  or warning (reproduced directly: it decoded only 5 of 48 real text bytes from
  `pusha_popa_test.o`), and its 16-bit (`-L`) mode prints 32-bit register names for
  `rep movs`. `objdump` showed neither issue. Full detail in the README above.

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

### 8086 opcodes still missing

Identified directly from `MUTOS1700_Assembler_as.pdf` Anlage A/B (the K1810WM86 base-chip
manual) — these are documented there but have no dispatch entry in `encode.c`:

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

1. Restore or re-locate `tests/mutos_ld/`'s golden binaries (`myhello`/`idhello`) so
   Milestone 1's "COMPLETE" status can be re-verified rather than carried forward
   unchecked.
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
4. Finish Milestone 4 (`mutos_cc`/`mutos_c0`/`mutos_c1`, 62/62 of the corpus
   byte-exact): the remaining `chkstk` gap, growing coverage
   into the new `tests/mutos_cc/11_kernel` real-kernel corpus (0/9 so
   far), and the `mutos_cc` driver - see its "Next up".
   `v7/cc/` is the reference source tree.
5. If a real MUTOS source file ever surfaces that exercises one of `mutos_cpp`'s
   documented simplifications (a formal parameter embedded in a macro-body string
   literal, a function-like macro name not immediately followed by `(`, or a macro
   call whose argument list spans multiple physical lines), re-check that specific
   behavior against it — see `src/mutos_cpp/README.md`'s "Known, documented
   simplifications" section for exactly which three cases these are.
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
