# MUTOS 1700 Cross-Compiler Toolchain — Status

This document tracks the current functional status of the toolchain components. It is
meant to be re-verified, not trusted: per the project's core methodology, no fix or
feature is considered real until it has been rebuilt and diff-checked byte-for-byte
against a real hardware-linked golden reference file. Sections below are marked as
either **verified this session** (rebuilt and diffed against goldens as part of writing
this document) or **carried from prior session records** (not re-checked here — treat
with the same skepticism the project applies to any unverified claim).

Last updated: 2026-08-26.

---

## Milestone 1 — `mutos_ld` (linker)

**Status: COMPLETE** *(carried from prior session records — see caveat below)*

`mutos_ld.c` (`src/mutos_ld/`, ~1180 lines) reimplements the V7 `ld` linker with the
MUTOS-specific `a.out` deviations (see "MUTOS-specific deviations" below), including
full archive (`.a` / `-l`) support.

### Verified this session
- `mutos_ld.c` compiles cleanly (`cc -std=c11 -Wall -Wextra -O2`), zero warnings.
- The reference archive `tests/mutos1700_libc/libc.a` is present and intact (72,178
  bytes, matches the previously recorded size).

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

**Status: functionally complete for the real corpus; verified fresh this session.**

`mutos_as` (`src/mutos_as/`: `mutos_as.h`, `lexer.c`, `parser.c`, `pass2.c`,
`symtab.c`, `encode.c`, `objwrite.c`, `assemble.c`; secondary tools `main.c`
(→ `parse_dump`) and `classify_test.c`) is a two-pass cross-assembler producing real
MUTOS `a.out` relocatable object files for the 8086/80186/NEC V30.

### Verified this session

- **Clean rebuild** from source (`make clean && make all`), zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic`.
- **Full regression: 67/67 golden files byte-for-byte identical**, full file compare
  (header + text + data + text-reloc + data-reloc + symtab):
  - `tests/mutos_as/kernel_opt/`: 62/62 clean (includes `mch_insw_outsw.s`, a
    hardware-parity test added this session for the INSW/OUTSW fix below).
  - `tests/mutos_as/kernel_nonopt/`: 5/5 clean.
  - 0 diff-mismatches, 0 assembler-invocation errors, both directories.
- **AddressSanitizer + UBSan** (`-fsanitize=address,undefined`, `-O0 -g`): 0 errors
  across the entire 67-file corpus plus a dedicated smoke test for this session's new
  80186 instructions.
- Two real bugs found and fixed this session (see "Recent fixes" below), both
  confirmed via rebuild + full-corpus diff, not just inspection.

### Recent fixes (this session)

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

### Auxiliary deliverables (prior session records, not re-checked this session)
- `mutos_as.1` — English troff man page.
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

## `mutos_as` opcode coverage

This is the detailed breakdown requested for this document. "Confirmed real" means
verified byte-exact against an actual hardware-linked object file; "unconfirmed" means
implemented per the standard 8086/80186 ISA (and, where available, hand-checked via a
smoke test) but never exercised by any real sample. `Assembler_as.pdf` (this project's
only period-accurate reference manual) documents the **K1810WM86**, a 1:1 Soviet clone
of the plain 8086 — it does not cover any 80186/V30 opcode at all, confirmed by
grepping all 51 OCR'd pages for "80186"/"V30"/"V20". 80186/V30 coverage below is
therefore either backed by real corpus evidence or added speculatively toward the
project's stated goal of broad 80186/V30 support in the final version, with no
manual/corpus material to check it against.

### 80186/V30 opcodes — implemented

| Mnemonic | Opcode | Status |
|---|---|---|
| `pusha` | `0x60` | implemented — **no real corpus sample anywhere in the repo** |
| `popa` | `0x61` | implemented — no real corpus sample |
| `push #imm16` | `0x68 iw` | implemented — no real corpus sample |
| `push *imm8` | `0x6A ib` | implemented — no real corpus sample |
| `insb` | `0x6C` | implemented — no real corpus sample |
| `insw` | `0x6D` | implemented — **confirmed real** (`mch_insw_outsw.s`) |
| `outsb` | `0x6E` | implemented — no real corpus sample |
| `outsw` | `0x6F` | implemented — **confirmed real** (`mch_insw_outsw.s`) |
| shift/rotate, immediate count ≠1/CL | `C0`/`C1 /digit ib` | implemented (register + indirect-memory dest) — no real sample (every real sample uses count=1 or CL) |
| `leave` | `0xC9` | implemented — no real corpus sample |
| `enter framesize,nestlevel` | `0xC8 iw ib` | implemented — no real corpus sample |
| `bound reg,mem` | `0x62 /r` | implemented (indirect-memory operand only, matching this codebase's LDS/LES/LEA convention) — no real corpus sample |
| `imul dst,imm` (2-op) | `0x69`/`0x6B /r` | implemented — no real corpus sample |
| `imul dst,src,imm` (3-op) | `0x69`/`0x6B /r` | implemented — no real corpus sample |

Every 80186 addition covered in this project's working spec (register/flag behavior
aside, which is a CPU runtime distinction, not an assembler-encoding one) is now
implemented in at least best-effort form. **Only INSW/OUTSW carry real hardware
confirmation** — the other twelve entries are unconfirmed and should be the first
candidates for verification if real V30-targeted source/object pairs ever become
available.

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

Identified directly from `Assembler_as.pdf` Anlage A/B (the K1810WM86 base-chip
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
   active use by this project's stated final-version goal.
3. `esc`/`escb`, `ret`/`reti` with an immediate, and the dedicated `int 3` encoding
   are the three remaining 8086-level gaps with enough information in
   `Assembler_as.pdf` alone to implement without further real-hardware evidence.
