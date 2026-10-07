# MUTOS 1700 Cross-Compiler Toolchain — Development Log

This document is the durable, version-controlled home for the detailed technical
findings, debugging methodology, and session-by-session history that previously lived
only in Claude's conversation memory (which is project-scoped, background-sync-delayed,
and capped at 30 entries). `STATUS.md` remains the authoritative *current-state*
tracker (what's verified, right now); this file is the *reference manual and history*
behind it — the "why" and "how we found out" for the facts `STATUS.md` and `CLAUDE.md`
assert. When the two disagree, `STATUS.md` wins for current state; this file is not
expected to be re-verified every session the way `STATUS.md` is.

---

## Table of contents

1. [Milestone 1 — `mutos_ld` (linker)](#milestone-1--mutos_ld-linker)
2. [Milestone 2 — `mutos_as` (cross-assembler)](#milestone-2--mutos_as-cross-assembler)
   - [CPU reference (8086/80186/V30/80188)](#cpu-reference-808680186v3080188)
   - [Assembly syntax](#assembly-syntax)
   - [Confirmed encoding facts](#confirmed-encoding-facts)
   - [Object file / relocation table format](#object-file--relocation-table-format)
   - [Implementation history](#implementation-history)
   - [Bug-fix history](#bug-fix-history)
   - [Opcode coverage audit](#opcode-coverage-audit)
   - [CLI reference](#cli-reference)
   - [Auxiliary deliverables](#auxiliary-deliverables)
   - [Debugging methodology (reusable)](#debugging-methodology-reusable)
3. [Milestone 3 — `mutos_cpp` (C preprocessor)](#milestone-3--mutos_cpp-c-preprocessor)
4. [Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler)](#milestone-4--mutos_ccmutos_c0mutos_c1-c-compiler)
5. [Milestone 5 — Optimizer (`c2`) & NEC V30 (`-mv30`)](#milestone-5--optimizer-c2--nec-v30--mv30)
6. [Recurring process lessons](#recurring-process-lessons)

---

## Milestone 1 — `mutos_ld` (linker)

**Status:** complete per prior verification, but **not re-verified recently** — the
golden reference binaries (`myhello`, `idhello`) and `tests/mutos_ld/` are currently
missing from the checkout. See `STATUS.md` for current status; treat this section as
historical record of what was verified when the goldens were last present.

Verified byte-for-byte identical against three real hardware-linked binaries:
`myhello` (default FMAGIC), `idhello` (`-i`/IMAGIC, separate I&D), and end-to-end
against the real `libc.a` for archive/`-l` support. Archive/library support (`-l`,
`.a` files) is part of Milestone 1, not a later milestone.

### Archive / `-l` implementation

Three code paths in `getfile()`/archive handling:

1. **Plain archives without `__.SYMDEF`** — full linear scan via `step()`.
2. **Up-to-date ranlib `__.SYMDEF`** table of contents — fast `ldrand()`-driven symbol
   pull, repeated to a fixed point for transitive dependencies.
3. **Stale `__.SYMDEF`** (archive mtime newer than the embedded ranlib timestamp) —
   warns and falls back to linear scan. Freshly-uploaded archives almost always hit
   this path, since the host mtime becomes "now" on upload.

Fixed a use-after-free bug in `getfile()`: `cur_filname` must own stable storage
(`strdup`) since `archive_load1()`'s `stat()`/`error()` calls happen after the `-l`
path-resolution buffer would otherwise already be freed.

Reference test archive: real MUTOS `libc.a`, 72178 bytes, 167 object members,
`__.SYMDEF` table 264 entries / 3168 bytes, embedded ranlib timestamp decodes to
~1989 (always triggers the stale-fallback path on modern hosts).

### MUTOS-specific `a.out`/`ld` deviations from stock V7

(shared with `mutos_as` — the object format is common to both tools)

1. Relocation word bit 15 = 1-byte target-shift flag (x86 is byte-oriented, unlike
   the word-aligned PDP-11 the original V7 `ld` targeted) — the actual relocated word
   may sit 1 byte off from the reloc table's nominal 2-byte grid.
2. DATA/BSS-relative values (symbol table + content words) are pure segment-local
   offsets, **not** V7's "baked-in preceding segment size" convention.
3. `R_EXT` relocation symbol-index field is **11 bits** (bits 4–14), not 12 bits as
   in stock V7.
4. `nflag`/`iflag`'s additional stock-V7 64-byte rounding is a separate padding stage
   filled with zero bytes.
5. `ar` archive header's `long` fields (`atime`, `asize`) use **PDP-11 middle-endian**
   encoding (high 16-bit word first, each word itself little-endian) — unlike the
   pure-16-bit-LE `a.out` exec header, which has no `long` field at all and is not
   middle-endian (see `mutos_aout.h`'s own top-of-file comment).

---

## Milestone 2 — `mutos_as` (cross-assembler)

**Status:** provisionally complete for the real corpus — 76/76 golden object files
byte-for-byte identical (full file: header + text + data + trel + drel + symtab: the 67
kernel files, `tests/mutos_as/libc_recon/ldexp.s`, a source reconstructed from a
real `libc.a` object, and the eight `tests/mutos_as/float_coverage/` objects - zeros,
doubles, inexact values, results outside the exponent range and the probes
`fltmode.s`, `fltmul.s` and `fltovf.s` among them), 0
AddressSanitizer/UBSan errors. See `STATUS.md` for the current, re-verified opcode
coverage tables (implemented/unconfirmed/missing) — those tables are kept there, not
duplicated here, since they need re-verification every session per Workflow
Guideline 6.

### CPU reference (8086/80186/V30/80188)

Confirmed via the AP-186 app note (Appendix A/G/H/I) against user-supplied specs, all
consistent with `docs/210973-001_AP-186_Introduction_to_the_80186_Microprocessor_Mar83.pdf`:

- **PCB** (Peripheral Control Block) is 256 bytes, relocatable to any 256-byte
  boundary (reset default `FF00h`, relocation register at PCB offset `FEh`); all PCB
  accesses must be **word** accesses (a byte read from an odd PCB address is
  undefined). Timer register accesses incur 1 wait state, others 0.
- `PUSHA` order: `AX, CX, DX, BX, SP(pre-push value), BP, SI, DI`; `POPA` is the
  reverse, skipping the `SP` slot.
- Shift/rotate immediate count is masked mod 32 (`AND 1Fh`) on the 80186, unmasked
  on the 8086 — usable as a runtime 8086-vs-80186 detection trick.
- **`PUSHF` does NOT distinguish the 8086 from the 80186** — a claim worth stating
  explicitly because it keeps resurfacing verbatim across independent sessions
  (2026-09, at least twice within the same month) as a user-supplied "reference
  spec" asserting the 80186 pushes flag bits 12-15 as `0` in real mode vs. the
  8086's `1`. Checked directly against the AP-186 app note: both chips push bits
  12-15 as `1` in real mode — there is no encoding or runtime difference here at
  all. The shift-count-masking trick immediately above is the genuine, confirmed
  8086-vs-80186 runtime detection method; `PUSHF` is not a substitute for it and
  any future spec/prompt claiming otherwise should be treated as wrong on this
  point regardless of how confidently or precisely it's phrased. **Root cause of
  the recurrence** (identified 2026-09-16): the spec tends to arrive as a generic
  "act as an x86 expert" persona-priming message before any MUTOS task is named,
  which let it bypass this project's session-start protocol (read `CLAUDE.md` /
  `STATUS.md` / `docs/DEVLOG.md` first) in at least one session. `CLAUDE.md` now
  carries a top-of-file callout plus an explicit persona-section rule to close that
  gap — see "Recurring process lessons" below.
- 80186-vs-8086 execution differences relevant to future `mutos_cc` codegen and libc
  (not assembler-encoding differences — pure CPU runtime behavior):
  - IDIV quotient range extended by 1 on the 80186 to include `8000h`/`80h` (the most
    negative two's-complement values) without a divide error; the 8086 traps on
    these.
  - Segment-wraparound behavior differs (80186 can fault on mid-segment-boundary
    cases the 8086 doesn't).
  - Interrupted `REP` string-move resume/prefix-repush behavior differs.
  - `LOCK` prefix timing differs (not codegen-relevant).
- 80188 vs. 80186: identical instruction set and PCB; only external bus width
  differs (4-byte vs. 6-byte prefetch queue, 8-bit vs. 16-bit external data bus) —
  relevant to hardware timing only, not code generation.

### Assembly syntax

Native MUTOS 1700 `as` syntax (not `as86`/Intel syntax). `-mv30` adds V30/80186
mnemonics; it is not a separate assembler mode.

- **Statement**: `[label].[prefix]mnemonic[operand].[comment]`.
- **Assignment**: `[label].name = expr`. Separators are newline or `;`.
- **Labels**: name-labels (`name:`) and numeric local labels (`0`–`9` + `:`, referenced
  as `<n>b`/`<n>f`); real `c1` output uses symbolic labels (`L4:`, `L20001:`) — the
  parser supports both forms.
- **Comments**: lines starting with `|` are pure compiler bookkeeping (`|NREG n` =
  register-count hint, `|RTYP n` = register-type hint for `c1`'s optimizer) — always
  comment-to-EOL, zero semantic effect, safely ignored by `mutos_as`.
- **Numbers**: default base is **decimal** (a major correction — see below); leading
  `/` = hex prefix. `*` = byte-size marker, `#` = word-size marker (both also work as
  genuine infix multiply/divide operators, disambiguated by lexer position).
- **Directives**: `.comm` for *all* uninitialized globals regardless of size (no
  separate `.bss` directive in most corpus files — see the real `.bss` segment
  support added later for `v30opt.s`); `.globl`, `.text`/`.data`, `.even` (word
  alignment).
- **K&R 8-character external-symbol truncation is real** and causes genuine
  collisions (e.g. `global_char`/`global_int` both truncate to `_global_`, emitted as
  two redundant `.comm` lines) — `mutos_as`/`mutos_cc` must replicate this exactly for
  compatibility.
- **New syntax discovered during corpus work**:
  - `@reg` — register-indirect jump/call target (distinct from data-indirect
    `(reg)`); confirmed real (`call @si`, `jmp @bx` in `mch.s`).
  - `@disp(reg)` — indirect call/jmp/br through a pointer stored at `disp+reg` (e.g.
    `tty.s`'s `call @8.+_cdevsw(bx)` device-switch-table dispatch). A `*`/`#` marker
    may appear right after `@` before the displacement and must be skipped.
  - A bare identifier statement (no label, no colon, no operands — e.g. a lone
    `L10003` on its own line, or `_ttbl:_t0`) is an **implicit `.word <ident>`** data
    value, used to build jump/pointer tables (`tty.s`). Resolved as a fallback in
    `assemble.c`: try `encode_instruction()` first; only on failure with zero
    operands, reinterpret the token as a bare data-value symbol reference.
  - `.asciz *<string>*` uses `*` as a **string delimiter** — the one exception to
    `*`=byte-marker — requiring the tokenizer to bypass normal scanning and rescan
    the raw source for this directive specifically.
  - `".=.+N"` bare location-counter assignment (reserve-and-zero-fill `N` bytes) —
    found in `mch.s`'s crt0. Must actually zero-fill the reserved span in the Pass-2
    output buffer, not just advance the counter (see Bug-fix history).

### Confirmed encoding facts

All confirmed via real hardware-linked object-code disassembly (`malloc.o`, `mch.o`,
and later goldens) unless marked *unconfirmed*. Standard 8086 ModRM throughout.

**Number literals & markers**
- Default base is **decimal**, not octal as `MUTOS1700_Assembler_as.pdf`'s V7/PDP-11 wording
  might suggest — proven via multiple real symbol values (e.g. `.comm _canonb,256`
  links to 256/`0x100`, not octal-256=174). A trailing `.` (which `c1` always emits)
  is a redundant stylistic habit, not semantically required. True octal literals (if
  they exist at all) remain unconfirmed.
- Leading `/` = hex, disambiguated from divide by lexer-tracked "last significant
  token type": `/` is the hex-constant marker (prefix position) *unless* the
  immediately preceding token was a `NUMBER`, `)`, local-label-ref, or `.` — a bare
  `IDENT` before `/` is still treated as prefix/hex (mnemonics are far more often
  followed by a hex operand, e.g. `in /C2`, than by any confirmed divide case).
- `*`=byte-size marker, `#`=word-size marker on immediates/prefixes; both also work
  as genuine infix operators (confirmed: `mov ax,/01*4+2`).
- **Marker behavior is asymmetric** between immediates and displacements: for an
  **immediate**, the marker is honored literally regardless of value magnitude; for
  an indirect-addressing **displacement**, `mod01`-vs-`mod10` is instead chosen by
  whether the *actual resolved value* fits a signed byte, ignoring the marker
  entirely — *unless* the displacement contains **any symbol reference**, which
  **unconditionally forces `mod=10`** (16-bit disp) regardless of marker or apparent
  value, since a symbol's true value isn't fixed until link time.

**MOV**
- `B8+reg iw` (reg,imm — marker-independent for word registers).
- `B0+reg ib` (byte registers — infer byte-ness from *either* the `movb` mnemonic
  suffix *or* `reg_class==REG_BYTE`; this "check both signals" pattern recurs for
  group1-immediate, TEST, and group1 load/store `+1`/`+3` opcodes too).
- `C6`/`C7` mem,imm — byte-vs-word selection depends **only** on `movb`
  (the mnemonic), **never** on the immediate's own marker.
- `89`/`8B` reg↔mem/reg↔reg.
- `A0`–`A3` short accumulator-direct-address forms — **AL/AX only**; a general
  register always uses `8B`/`89` + `mod00`/`rm110`/disp16. (`A1` inferred by
  symmetry, not directly observed; the other three are directly confirmed.)
- `8E`/`8C` for segment-register MOV — a separate opcode family, not `8B`/`89`.
- The reg↔mem and reg↔reg forms (`A0`–`A3`, `88`–`8B`) are byte-sized for `movb`
  **or** for a byte-register operand under plain `mov` — the same two-signal rule as
  `B0+reg`. Until 2026-09-27 only `movb` selected them, so `mov al,x` silently
  assembled as a load of AX; the kernel corpus never writes one (`c1` writes `movb`
  with word register names), `libc.a`'s `ldexp.o` has three (`A0`, `A2`, `8A 46 0A`
  — see the `mutos_as` `.float` section under Milestone 4).

**Group1 arithmetic (`add`/`or`/`adc`/`sbb`/`and`/`sub`/`xor`/`cmp`)**
- `AND`/`OR`/`XOR` have **no sign-extend (`s`) bit** in their real CPU encoding
  (confirmed by reading MUTOS1700_Assembler_as.pdf Anlage C directly: their encoding is the
  fixed `1000 000:w`, unlike ADD/ADC/SUB/SBB/CMP's variable `1000 00:s:w`) — these
  three **never** use `0x83`, always the full `0x80`(byte)/`0x81`(word) form,
  regardless of the immediate's marker, value, or expression shape.
- `ADD`/`ADC`/`SUB`/`SBB`/`CMP` use `0x83`/ib (sign-extended byte immediate, register
  dest, any immediate) or `0x81`/iw only for a `#`-marked word immediate; `0x80` for a
  genuinely byte-sized destination.
- Short accumulator forms (`opidx*8+4` AL / `opidx*8+5` AX) take priority when dest is
  the accumulator.
- `+1`/`+3` opcode selection (load vs. store direction) needs the same byte-vs-word
  check via `reg_class`, not just the mnemonic suffix.
- `TEST` has its own accumulator shortcut (`A8`/`A9`), separate from group1's.

**Shift/rotate, INC/DEC, XCHG, PUSH**
- Shift/rotate: `count==1` **always** uses short `D0`/`D1` (2 bytes), never the
  80186's `C0`/`C1`-with-imm=1 form.
- INC/DEC: short `40`–`4F`/`48`–`4F` only exists for **word** registers — a byte
  register (e.g. `dec bl`) must use the general `FE`/`FF` ModRM form.
- XCHG: the word form with AX as either operand **always** uses the `0x90+reg`
  shortcut, never the general `87` ModRM; byte `xchgb` always uses general `86`.
- PUSH: register→`50+reg`; segment registers have their own dedicated opcodes; a
  bare/unmarked DIRECT expression operand means "push the **word stored at** that
  address" (`FF /6`, dereference), **not** "push this address as an immediate" —
  consistent with the general DIRECT-mode "unmarked = dereference" convention seen
  throughout (MOV, etc.); a marker-prefixed immediate uses `0x68`/`0x6A`.

**Jumps/branches**
- `j <label>` and `jmp <label>` are **not aliases**: `j` is always short `EB rel8`
  (2 bytes, including the classic `j .` self-loop); `jmp` is always long `E9 rel16`
  (3 bytes) — no size-based auto-selection either way. `br` (Anlage B, unconditional
  long branch — no condition to negate) dispatches to the exact same encoder as
  `jmp`.
- Pseudo-branches (`beq`/`bne`/`blt`/`bge`/`ble`/`bgt`/`bhi`/`blos`/`bhis`/`blo`/
  `bloss`) **unconditionally** expand to "negated-Jcc(skip 3)+`E9 rel16`" (5 bytes)
  regardless of actual target distance — no short-branch peephole. Real (non-pseudo)
  Jcc mnemonics are plain, un-expanded 2-byte `0x7x rel8`.
- `reti` (`0xCB`, RETF/far-return-only) and `iret` (`0xCF`, true IRET) are **not**
  aliases despite similar spelling.

**Misc confirmed opcodes**
`LEA`=`8D /r`, with an indirect operand or — since 2026-09-27, `mutos_c1`'s
`lea ax,L10000` — a direct one (mod=00 rm=110 + relocated disp16, confirmed by
`libc.a`'s own machine code: `8d 06 <disp16>` + R_DATA in `atof.o`/`ecvt.o`, R_EXT
`fac` in `ldexp.o`/`modf.o`/`doubles.o`); `MUL`/`NEG`/`DIV`/`IMUL`/`NOT`/`IDIV` = group3 `F6`/`F7` `/4`/`/3`/`/6`/
`/5`/`/2`/`/7` (register-only, byte-ness inferred from `REG_BYTE`); `WAIT`=`9B`;
`INW`/`OUTW`=`ED`/`EF`; `MOVSB`/`MOVSW`=`A4`/`A5`; `STOB`/`STOW`/`LODB`/`LODW`=`AA`/
`AB`/`AC`/`AD`; `rep` and `seg <sr>` are each their own 1-byte prefix
pseudo-instruction (`F3`; `26`/`2E`/`36`/`3E` for es/cs/ss/ds) written on their own
source line before the following real instruction. **`imul`/`imulb`** (base, no
`-mv30`) is the classic **single-operand** 8086 group3 form (`F6`/`F7 /5`) — *not*
the 80186 three-operand extension (that needs `0x69`/`0x6B`, added separately under
`-mv30` — see Opcode coverage audit).

### Object file / relocation table format

Relocation table (trel/drel), fully resolved via `mch.o` (327 non-zero entries) +
`malloc.o` + later goldens:

- One 2-byte entry per text/data word.
- **Bit 15** = 1-byte-shift flag (set when the actual field to relocate starts at an
  odd byte offset relative to the table's even-byte grid).
- **Bits 4–14** (11 bits) = external symbol index, meaningful **only** when bit 3
  (`R_EXT`=`0x08`) is set; for local `R_TEXT`(`0x02`)/`R_DATA`(`0x04`)/`R_BSS`(`0x06`)
  relocations this field is always 0.
- **Bit 0** (`0x01`) = **PC-relative flag** (undocumented in `mutos_aout.h`, discovered
  via disassembly) — set exactly for `E8 CALL rel16`/`E9 JMP rel16` to an *external*
  symbol.
- So: low nibble `0x9` = R_EXT+PCREL (external call/jmp); `0x8` = R_EXT absolute;
  `0x2`/`0x4`/`0x6` = local R_TEXT/R_DATA/R_BSS. `R_ABS`(`0x00`) is essentially never
  emitted (absolute non-symbolic pointer constants compile as plain immediates with
  no relocation at all).

**Text-segment layout in linked executables**: a fixed 128-byte crt0 vector block at
text start (symbol `start0`=128, filled with `EB 7E` jump-over + repeated `EB FE`
traps + zero bytes); each linked object module's own code ends with a fixed 6-byte
tail of exactly three `EB FE` trap instructions; a single stray `0x00` filler byte
appears whenever a label needs even (`.even`) alignment but the preceding instruction
ended at an odd offset, and each module's text and data segments are rounded up to an
even size at the module's **end** — not at a `.text`↔`.data` switch in the middle of
the source (corrected 2026-09-27: `mutos_as` padded at every switch, which the corpus
could not tell apart because its evidence was each file's last switch; `atof.o`/
`ecvt.o` show no pad where `c1` switches to `.data` for a floating constant between
two instructions — see the `mutos_as` `.float` section under Milestone 4).

**`.bss` segment** (first seen in `v30opt.s`): `.bss` directive switches to a third
segment (own location counter, no output byte stream — implicitly all-zero, only the
LC advances; the accumulated total becomes the object header's `a_bss` field).
`.blkb <N>` reserves `N` zero-filled bytes at a label. A `.bss`-segment symbol is
written as a true `N_BSS`(`0x04`) type with a genuine segment-relative address —
distinct from `.comm`, which is written as `N_UNDF|N_EXT` with *value=size* (not an
address).

**`.comm`**: creates an `N_UNDF|N_EXT` symbol with value = requested size (not an
address — must never be used as one when resolving expressions, must be treated like
a true external). A genuinely undefined-but-referenced symbol (never locally defined)
must always get `N_EXT` set when written, regardless of whether it was ever
`.globl`'d, or the linker has no way to resolve it.

**Symbol table ordering**: the real vendor `as` writes symbols in **creation order**
(order of first appearance during Pass 1), **not** hash-bucket/chain order — this was
initially assumed to be an unmatchable vendor convention but turned out to be a
simple, exactly-discoverable rule (see Debugging methodology).

### Implementation history

Architecture (final): `mutos_as.h` (shared types) + `lexer.c` + `parser.c` (Step 1:
tokenizer/statement parser) + `pass2.h`/`pass2.c` (Step 2: expression evaluator with
precedence + operand addressing-mode classifier: REGISTER / INDIRECT / IMMEDIATE /
DIRECT) + `symtab.h`/`symtab.c` (chained-hash symbol table with a creation-order
linked list for deterministic output) + `encode.h`/`encode.c` (Step 3–7: per-mnemonic
8086/80186 opcode encoder, two-pass-safe via a `resolve` bool) + `objwrite.h`/
`objwrite.c` (serializes to a genuine `mutos_aout.h`-format `.o` file) +
`assemble.c` (two-pass driver + `main`). `classify_test.c` and `main.c`→`parse_dump`
are secondary debug tools, not real assemblers.

Built and validated incrementally against the real `c1`-generated `.s` corpus
(`malloc.s`, `reloctest.s`, `opcodetest.s`, `mch.s` — 1466 statements combined, zero
parse errors at the parser stage; 1489 operands, zero classification failures at the
operand-classifier stage).

### Bug-fix history

Chronological, most important first for future reference:

1. **Symbol table ordering bug** (`symtab.c`/`symtab.h`) — was hash-bucket order,
   needed creation order. Diagnostic method: instrument `symtab_find_or_create()` to
   dump a numbered creation-order log, compare directly against the golden `.o`'s
   symbol sequence — matched 100% on the first try. Fix: an `order_next` singly-linked
   list populated at creation time, walked instead of the bucket array at output
   time.
2. **Missing relocation entries** (`encode.c`/`assemble.c`/`encode.h`) — every word
   encoding a TEXT/DATA-segment address had the *correct value* but no accompanying
   relocation-table entry except the 3 pre-existing `E8`/`E9`-to-external-symbol
   PCREL cases. Diagnostic signal: text/data segments matched byte-for-byte while
   trel/drel did not. Fix: `classify_word_reloc()` walks an expression tree,
   accumulating a **signed coefficient per relocation base** (TEXT base, DATA base,
   BSS base, and each distinct external/`.comm` symbol *separately* — a naive
   single-symbol accumulator wrongly flags a same-segment label *difference* like
   `#t_trap-ivsstep-3` as needing relocation, when it's actually a
   link-base-invariant constant). `EX_LOCCTR` (`.`) must itself contribute +1 to
   whichever segment is currently being assembled. Threaded through every
   word-emission call site via a new `RelocList*` parameter.
3. **`AND`/`OR`/`XOR` sign-extend bit** — see Confirmed encoding facts. A wrong
   intermediate hypothesis (bare-literal vs. expression) was tested and disproven by
   a real counter-example before the correct per-opcode-table explanation was found;
   lesson: read the actual encoding table rather than pattern-matching from a
   handful of samples.
4. **`emit_modrm_indirect`'s mod=2 branch only resolved symbols when `resolve==true`**
   — a symbol referenced *only* from within such a displacement (e.g. `tty.s`'s
   `_cdevsw`) never got registered during Pass 1, then got created fresh during
   Pass 2 *after* `symtab_finalize()` had already assigned every `out_index`, leaving
   it at the invalid default and corrupting the symbol-table write (heap-buffer
   overflow, found via AddressSanitizer). Fix: always call `resolve_expr()` there
   regardless of the `resolve` flag, matching the established "always resolve, even
   in Pass 1, to register symbols as a side effect" pattern used everywhere else.
5. **`INSB`/`INSW`/`OUTSB`/`OUTSW` had zero dispatch entries** — an unrecognized
   zero-operand mnemonic silently fell into the bare-identifier data-value fallback
   (meant for real jump-table references), mis-assembling as a spurious external
   symbol + relocation instead of the correct 1-byte opcode, desyncing the location
   counter downstream with no diagnostic. Caught by a dedicated new test file
   (`mch_insw_outsw.s`) built specifically to exercise this path. This prompted the
   general safety net below.
6. **General "unknown opcode" safety net**: `encode_is_known_mnemonic()` checks a
   token against every mnemonic this assembler actually dispatches on, built by
   walking the *same* lookup tables `encode_instruction()` itself uses (so a new
   table entry is automatically covered) plus an explicit `IRREGULAR_MNEMONICS[]`
   list for `strncmp`-dispatched mnemonics. The bare-identifier fallback now only
   triggers for genuinely unknown tokens; a *recognized* mnemonic with an
   unsupported operand shape is now a hard compile error instead of silent
   mis-assembly. Self-caught bug: the first version of `IRREGULAR_MNEMONICS[]`
   omitted the entire Group3 family — fixed by regenerating the list
   *programmatically* (`grep` every `strncmp` dispatch site) instead of hand-typing
   it a second time; do this again if it ever needs updating.
7. **`".=.+N"` zero-fill omission** — advanced the location counter but didn't
   actually zero-fill the reserved span in the Pass-2 output buffer, silently
   desyncing the buffer with no symbol-address symptom (the hardest class of bug to
   find this way — see Debugging methodology's LC-delta-vs-buffer-growth technique).
8. **Padding at every segment switch** (2026-09-27) — an odd location counter at a
   `.data`/`.text` switch got a `0x00` pad byte in the segment being left, a rule
   generalized from `malloc.o`/`mch.o`, where the switch was the file's last and
   end-of-file rounding gives the same bytes. `mutos_c1`'s floating constants
   (`.data` / `L10000: .float ...` / `.text` between two instructions) made it real:
   the pad landed inside the code. Fixed by rounding each segment once, at the end;
   all 136 previously assembling `.s` inputs give identical objects.
9. **Plain `mov` with a byte register and a memory/register operand got the word
   opcode** (2026-09-27) — see Confirmed encoding facts (MOV). Found reproducing
   `libc.a`'s `ldexp.o`.

### Opcode coverage audit

A full systematic audit (not just corpus-reactive fixing) was done by reading
`MUTOS1700_Assembler_as.pdf` directly (a genuine text-layer PDF, 51 pages — like
`docs/V20_V30_Users_Manual_Oct86.pdf`, `pdftotext` extracts the full Anlage A/B/C
tables straight out of it, no unzipping needed) and cross-checking every documented
mnemonic against `encode.c`'s dispatch table one by one. Found and fixed ~30 real gaps in one pass, including `not`/`notb`, `idiv`/
`idivb`, all the simple fixed single/double-byte no-operand instructions (`clc`,
`hlt`, `lahf`, `daa`, `aam`, etc.), `cmps`/`scas` families, `repe`/`repne` synonyms,
the remaining `jo`/`jno`/`jnae`-style Jcc aliases, `inb`/`outb` synonyms, `lds`/`les`,
indirect `calli`/`jmpi` (the *direct* `d:s` segment:offset form remains a deliberate,
documented gap — needs new colon-operand syntax, zero real corpus evidence of use),
and byte-suffixed Group2 shift/rotate forms (`rolb`, `shrb`, etc. — a *real*
functional gap, since byte-sized *memory* destinations have no register to infer
byte-ness from). All ~30 were spot-checked via hand-assembled smoke tests against the
standard 8086 ISA encoding, not just "doesn't break existing goldens." See
`STATUS.md` for the current, re-verified confirmed/unconfirmed/missing tables — those
need re-checking every session and are deliberately not duplicated here.

A separate audit specifically for 80186/V30 coverage (prompted by "K1810WM86 is a
Soviet 1:1 clone of the *base* 8086 without V30/80186 opcodes — `MUTOS1700_Assembler_as.pdf`
only documents the base chip, but `mutos_as`'s final version should support as many
80186/V30 opcodes as possible anyway") added `LEAVE`, `ENTER framesize,nestlevel`,
`BOUND reg,mem`, and the two/three-operand `IMUL` immediate forms — all hand-verified
against the standard 80186 ISA byte encoding, since neither the manual nor the real
corpus covers any of them. Corrected a prior wrong inference: `PUSHA`/`POPA` were
assumed "confirmed real" but a fresh repo-wide grep found zero actual usage anywhere
— they're unconfirmed like most other 80186 additions; only `INSW`/`OUTSW` carry
genuine hardware confirmation (via `mch_insw_outsw.s`).

### CLI reference

Based on the real `as_1.d` man page (a flattened/rendered nroff page). Implemented:
`-o output` (both `-o file` and attached `-ofile`; default without `-o` is `a.out` in
the cwd); `-W` (suppress diagnostics, errors still tracked/still block success);
multiple file arguments concatenated as one continuous source (like `cat`), with a
defensive inserted newline between files not already ending in one; no files = read
stdin; success with zero unresolved symbols → `chmod 0755`, otherwise `0644`; `asf`
recognized as an alternate program name (via `argv[0]` basename) with currently no
functional difference (no project material documents concrete 8087 coprocessor
opcodes to gate behind it).

**Deliberate, documented deviation**: `-L` (control over whether compiler-internal
`L`-prefixed labels appear in the symbol table) is **not implemented** — it was
implemented once, found to break byte-for-byte golden parity (every real golden this
project validates against has those labels present *unconditionally*, with no flag
involved in how they were produced), and was then **removed entirely** per explicit
project decision. Matching the golden files takes priority over literal
switch-for-switch parity with the man page here. **Counter-evidence found
2026-09-27**: none of `libc.a`'s 167 objects has an `L`-number symbol — not even
compiled C such as `atof.o`/`ecvt.o`, which branch to `L` labels — i.e. they show the
documented default. The kernel goldens may simply have been assembled with `-L`;
unresolved, see `STATUS.md`'s open items and `tests/mutos_as/libc_recon/README.md`.

### Auxiliary deliverables

- `mutos_as.1` — English troff man page, hand-verified for macro correctness with a
  custom Python troff-subset interpreter (no groff/nroff/mandoc available in the
  sandbox).
- `run_goldens.sh` — assembles every `*.s` in the current directory, diffs against
  `<n>.o.golden`, reports clean/diff-mismatch/invocation-error counts.
- The **native MUTOS 1700 kernel build Makefile** (`conf/Makefile`, the actual OS's
  own build — not this project's own Makefile) was modified: `CFLAGS` changed from
  `-c` to `-S` so every compile step leaves a `NAME.s` file behind; a new `AS`
  variable performs the assembly step; every compile recipe additionally copies the
  final `.o` to a sibling `NAME.o.golden`. This is the mechanism for generating *more*
  real hardware/native-toolchain golden files from the kernel source tree in future
  sessions.

### Debugging methodology (reusable)

Techniques proven useful across many sessions of encoder/assembler discrepancy
hunting — apply these to future `mutos_cc`/`c0`/`c1`/`c2` work too:

1. **Symbol-diff**: dump every TEXT/DATA symbol's address from the real object file
   vs. this assembler's own Pass-1 table, sort both by address, find the first
   divergence — pinpoints the statement(s) responsible for shifting a later address.
2. **LC-delta-vs-buffer-growth**: for bugs that *don't* shift any symbol address (a
   single statement's Pass-2 output falls short of its own implied size with no
   downstream propagation), symbol-diff is blind. Instead instrument the *real*
   driver directly with a per-statement location-counter-delta vs.
   actual-buffer-growth mismatch check, gated behind an env var, removed once the
   culprit is found.
3. **Verify comparison-script freshness** before concluding a fix "didn't work" —
   several apparent non-fixes across this project's history were actually stale
   `.bin`/`.o` output files being re-read.
4. **AddressSanitizer** is effective for heap-buffer-overflow-class bugs (e.g. a
   symbol created after `symtab_finalize()` got an invalid `out_index`).
5. **Recursive, unconditional symbol registration**: a symbol referenced only from
   *within* a larger expression (e.g. `_clknumb+0`, top node `EX_ADD` not `EX_SYM`)
   needs recursive registration through the whole expression tree even when only
   sizing (`resolve=false`) — an early-return-before-recursing optimization silently
   skips nested symbol registration.
6. **Don't assume an unknown vendor convention is unmatchable** — instrument the real
   driver to dump the exact order/values it produces and diff directly against the
   golden file. Both the symtab-ordering and relocation-coverage "scope limitations"
   in this project turned out to be simple, exactly-discoverable rules on first real
   test against ground truth, not permanent unknowns.

---

## Milestone 3 — `mutos_cpp` (C preprocessor)

**Status:** complete for the real corpus — 5/5 golden files byte-for-byte identical,
0 ASan/UBSan errors. Full behavioral specification, architecture, and known
documented simplifications live in **`src/mutos_cpp/README.md`** (not duplicated
here). Man page: `man/mutos_cpp.1`. Test runner: `tests/mutos_cpp/run_goldens.sh`.

**Golden generation command** (critical, easy-to-lose detail): the reference `.i`
files were generated on real MUTOS 1700 hardware via exactly:

```
cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE <name>.c
```

These specific defines are required to reproduce byte-exact output — `mch.c`'s
conditional branches resolve differently without them.

### The two bugs worth remembering

1. **Pushback/`ungetc` must be scoped per-source-frame, not global.** A character
   peeked while reading from one source (e.g. checking what follows a macro name)
   was pushed onto a single global buffer; if that peek happened immediately before
   a macro expansion pushed a *new* source (the substituted text) onto the stack,
   the stale peeked character leaked ahead of the new source's own content once
   reading resumed. Symptom: a bracket/paren appears *before* a macro's expansion
   instead of after (`x[NOFILE]` → `x[]20` instead of `x[20]`). Fixed by moving the
   pushback buffer into each `Source` frame.
2. **Comment-embedded newlines are always individually emitted, even inside a false
   `#ifdef`/`#ifndef`/`#if` body** — the reference `cpp.c`'s comment-skipping loop
   calls `putc('\n', fout)` **unconditionally**, bypassing the normal
   `flslvl`-gated output-suppression path entirely. Confirmed via `mch.c`'s
   `#ifdef M1834` blocks (M1834 undefined in this project's golden fixtures)
   containing multi-line commented-out code, whose internal newlines still appear
   in the golden output.

### Core output-line-accounting rule

A directive line (`#define`/`#undef`/`#include`/`#ifdef`/`#ifndef`/`#if`/`#else`/
`#endif`/`#line`) contributes exactly one blank output line **iff the
conditional-active state was true immediately *before* that directive's own effect
is applied** (its "pre-state") — this single rule correctly collapses a false
`#ifdef ... #endif` block of *any* body size to exactly one blank output line, and
correctly handles `#else` transitions in both directions. See
`src/mutos_cpp/directive.c`'s file header comment for the full derivation.

---

## Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler)

**Status:** IN PROGRESS — `mutos_c0`/`mutos_c1` now exist in `src/mutos_cc/` and are
verified byte-exact, end-to-end, for 20 of 62 of the full corpus (`00_smoke`'s 3 files
plus all of `01_expr`: `01_intarith`/`02_bitwise`/`03_rellogic`/`04_shift`/`05_incdec`/`06_compasgn`/`07_ternary`/`08_castsize`, plus `02_long/01_addsub`/`02_muldiv`, plus all 7 of `03_ctrlflow`); see `STATUS.md` for the
current, re-verified count and grammar/opcode scope. This section starts with the
groundwork done ahead of any code: ABI/calling-convention research, the `c0`/`c1`
process-split decision, and the K&R test corpus — so `mutos_c1`'s code generator had
a byte-level-accurate target from day one instead of guessing generic
8086-C-compiler conventions, and so a verification path was lined up before it
existed — then continues below with the byte-level wire-format derivation and
host-tooling findings from actually building against it. Full ABI detail, with every rule cited against a
real disassembled `.o`, lives in **`docs/MUTOS_C_ABI.md`** (not duplicated here in
full — the ABI subsection below is the condensed "why/how we found out" pointer
into it, matching this file's usual role).

### Method

Real hardware-linked evidence, primarily two sources: `tests/mutos1700_crt0/crt0.o.base64.txt`
(the real startup object) and ~15 hand-picked files out of `tests/mutos1700_libc/`'s
167 real linked objects (chosen to cover: a trivial 1-arg function, 2-arg functions,
`long`-returning/`long`-parameter functions, the compiler's own long-arithmetic
runtime helpers, a large-local-frame function, and the `exit`/cleanup chain) — this
is the **load-bearing** evidence for the calling convention itself.
`tests/mutos_as/kernel_opt/mch.s` was used as a **third, supplementary** source, but
**not** as compiler output: a mid-session correction (prompted by a reviewer noticing
`mch.s`'s own `_outb`/`_out`/`_in`/`_inb`/`_hdio` use `bx`, not `bp`, as their frame
pointer — see item 9 below) established that `mch.c`
(`tests/mutos_cpp/c/mch.c`), unlike the kernel's other four `mutos_cpp` corpus C
files, opens with the comment `Assemblerteil MUTOS 1700/1834` ("the assembly
portion") and is 100% hand-written MUTOS-assembler source from its first line, using
a `.c` filename only so it flows through the same `cpp` build step (`#include`/
`#ifdef`) as genuine C files — it is **never** passed through `c0`/`c1`/`c2`. It
remains useful because it's the one place `cret`'s exact implementation is available
as literal source, and it contains two hand-written-but-explicitly-`C-Callable`
helper functions (`_co`, `_ci`) that a human deliberately conformed to the real
compiler ABI for interop — corroborating, not independently proving, the same
convention `libc.a`'s genuine compiler output establishes. Text segments were
extracted from each `.o`'s already-understood `mutos_aout.h` header layout and
disassembled with `objdump -D -b binary -m i386 -M intel,i8086`; relocation entries
were decoded with the project's already-established rules (shift-flag/symidx/type
bits) to identify call targets by name.

### Headline findings (see `docs/MUTOS_C_ABI.md` for full derivation and citations)

1. **Pure stack ABI, no register arguments.** Right-to-left push order, caller
   cleans up (`add sp,N`) after every call — confirmed at every single call site
   examined, no exceptions.
2. **Fixed, unconditional prologue/epilogue** for every compiler-generated function,
   regardless of actual register/local usage: `push bp / mov bp,sp / push di /
   push si` … `jmp cret`. `cret` itself — quoted verbatim from `mch.s`'s
   hand-written source (see "Method" above) — is `lea sp,#-4(bp) / pop si / pop di /
   pop bp / ret`; the `lea`-relative-to-`bp` trick is why one shared routine can
   serve every function regardless of local-frame size. Confirmed unconditional via
   genuine compiler output in `libc.a`: `_abs` (uses `di` but not `si`) and
   `_strlen` (uses both) still save/restore both registers unconditionally.
   `mch.s`'s hand-written-but-`C-Callable` `_ci` (zero params, uses neither `di` nor
   `si` internally) independently shows the same shape, as corroborating rather than
   primary evidence (see "Method").
3. **Frame layout is fixed too**: parameters at `bp+4, bp+6, ...`; locals always
   start at `bp-6` (the `bp-2`/`bp-4` slots are permanently reserved for saved
   `di`/`si`, whether or not a given function has any real locals there). Confirmed
   via 2-word (`atol.o`) and 4-word (`sprintf.o`) local-block examples.
4. **`long` = 2 words, high word at the lower address, everywhere** — locals,
   register-pair returns, by-reference operands, and by-value parameters alike.
   This is the exact same PDP-11 "middle-endian" word order already established for
   `ar` archive `long` fields (see Milestone 1's entry), now directly confirmed to
   also govern compiled C `long` values — resolving `CLAUDE.md`'s note that this
   "hasn't been relevant yet." Confirmed three independent ways (a `long` local in
   `atol.o`, a by-reference `long` in `almul.o`/`aldiv.o`, and the by-value `long`
   parameter of `_lseek`'s `offset`).
5. **Return values**: `AX` for 16-bit scalars, `DX:AX` (`DX`=high) for `long` —
   confirmed via `_atol`'s and `aldiv`/`ldiv`'s final `mov ax,.. / mov dx,..` before
   their epilogues.
6. **A second, *different* internal-only ABI** for the compiler's own
   `long`-arithmetic runtime helpers (`almul`/`aldiv`/`alrem`, `lmul`/`ldiv`/`lrem` —
   note: no leading `_`, i.e. not user-callable C symbols). These use an *extended*
   prologue (`push bp / push bx / mov bp,sp / push di / push si`, shifting params to
   start at `bp+6`) and a matching custom epilogue (not `jmp cret`, since `cret`
   doesn't know about the extra saved `bx`). First parameter is a pointer to a `long`
   that's both an input operand and the in-place result destination; this is a
   private contract between `mutos_c1`'s own codegen and its own runtime support
   library, **not** part of the general C function ABI.
7. **Large stack frames get a guard**: above some threshold, `sub sp,N` is replaced
   by `mov ax,N / call chkstk`, which checks the new `sp` against a stack-bottom
   limit, attempts to grow the stack via a far call through a runtime-initialized
   trampoline pointer on failure, and ultimately does `_kill(_getpid(), SIGSEG)` +
   `__exit` if growth genuinely fails — a real stack overflow terminates with
   `SIGSEGV`-equivalent behavior. Empirically bounded via corpus scan: largest plain
   `sub sp,N` seen is `N=76`; smallest `call chkstk` seen is `N=256` — exact real
   cutoff not pinned down further by this corpus.
8. **`crt0` startup**, fully annotated in `docs/MUTOS_C_ABI.md`: reads `argc`/`argv`
   off the kernel-provided initial `sp`, scans for the `argv[]` NULL terminator
   (**correction worth flagging**: the scan uses `test WORD PTR [bx],0xffff` /
   `jne`, which is an AND-with-all-ones zero-test, i.e. an ordinary NULL check — an
   initial fast read of the raw bytes mis-suggested a `0xFFFF` sentinel value; always
   trace the actual flag semantics, not just the literal immediate, when reading
   `TEST`), sets the global `_environ`, calls `_main(argc, argv, envp)` per the
   standard ABI (§1 above), and — critically — calls **`exit()`** (asm symbol
   `_exit`, defined in `cuexit.o`) after `main` returns, **not** the raw
   `_exit()`/`__exit` syscall wrapper (`exit.o`). `exit()` calls a `_cleanup()` hook
   (asm symbol `__cleanu` — `_cleanup` mangled + K&R-8-char-truncated) before the raw
   syscall; a classic two-object linker trick (`fakcu.o`'s no-op stub vs. the real
   flush routine bundled inside `flsbuf.o`) means a program only pays for stdio
   flush-on-exit if it actually uses stdio.
9. **Not every function-shaped routine is compiler output**, and hand-written
   assembly uses more than one internal style — documented explicitly in
   `docs/MUTOS_C_ABI.md` §1.10 (expanded this session) so a future session doesn't
   mistake any of these for a second valid *compiler* convention:
   - Trivial 1:1 syscall stubs (`libc.a`: `access.o`, …) — `mov ax,N / jmp sysNa`.
   - **`bx`-as-frame-pointer leaf routines, no `bp` at all** — `mov bx,sp` directly,
     params at `[bx+2]`, `[bx+4]`, … This is in fact the **prevailing** style in
     `mch.s` itself (25 occurrences, several with an explicit human comment like
     `| bx is frame pointer`, vs. only 6 using the full `push bp` C-ABI prologue) —
     confirmed via real, `.globl`'d, C-linkage examples `_in`/`_inb`/`_out`/`_outb`/
     `_hdio`. The same idiom, inferred from disassembly alone before these `mch.s`
     examples confirmed the pattern's intent, is also used by a handful of `libc.a`
     syscall wrappers (`dup.o`/`execv.o`/`shutdn.o`/`signal.o`). **This is the
     pattern a reviewer flagged** (correctly) as looking like a contradiction to
     item 2's `bp`-based convention — it isn't a contradiction, but it did expose
     that this document's first version mischaracterized `mch.s` as compiler output
     (see "Method" above); the underlying `bp`-frame facts in items 1–8 were
     unaffected, only the `mch.s` attribution needed correcting.
   - `bp`-based but reordered and not using `cret` (`libc.a`'s `_lseek`): pushes
     `si` before `di` (opposite of item 2) with its own matching inline epilogue —
     internally consistent, just independent of the shared `cret` routine.

### Open item

The exact `chkstk` size threshold (bounded to between 76 and 256 bytes by `libc.a`
above) is not pinned down further by the `libc.a` corpus; if a real object file with a local frame in that
range surfaces later (e.g. from generating more goldens off the kernel source tree
per Milestone 2's `conf/Makefile` mechanism, or from a userland `.c` file not yet in
this checkout), re-check it against this bracket. **Update:** `tests/mutos_cc/
09_abiprobe/frame080.c` … `frame300.c` now exists specifically to resolve this —
six otherwise-identical files with local buffers of 80/128/176/224/256/300 bytes,
bisecting the gap. Once compiled on real hardware, whichever ones emit `call
chkstk` vs. a plain `sub sp,N` pin the real cutoff down for the first time.
**Resolved (2026-09-23)**: the goldens are in - `02_frame080` uses `sub sp,*80.`,
all of `03_frame128` … `07_frame300` use `mov ax,#N.` / `call chkstk`, so the
real threshold is in `(80,128]`; see "`c1_gen.c` review" below for the details
and what `mutos_c1` now does with it. **Settled (2026-10-07)**: the
`fltprobe` goldens of round 7 (`p27_frame`: 90 bytes `sub sp`, 100 the
guarded call) and round 8 (`p29_frame2`: 92, 94, 96 and 98 bytes all `sub
sp,N`) leave no size open - a frame is even, so `sub sp,N` up to 98 bytes,
`mov ax,N` / `call chkstk` from 100: `(98,100]` (see "The round-8 fltprobe
goldens").

### `c0`/`c1` process split: decision and rationale

**Decision: keep `c0` and `c1` as two separate executables**, communicating
through a `temp1`/`temp2` file pair exactly as V7's own `cc` driver does (see
`v7/cc/cc.c`'s pipeline comment: `c0 source temp1 temp2` / `c1 temp1 temp2
assembly.s`). This also matches `CLAUDE.md`'s "Target Executables" list, which
already named `mutos_c0`/`mutos_c1` as separate deliverables before this
question was raised — the analysis below is the reasoning behind confirming
that choice deliberately rather than by default.

**The question.** The historic split exists because V7's `cc` ran on a PDP-11
with a 64K address space per process: parsing/semantic-analysis (`c0`) and code
generation (`c1`) each needed the full 64K to themselves, so they had to be
separate processes. That constraint doesn't exist on a modern 64-bit host, which
raises the obvious question: keep the two-phase split, or merge `c0`+`c1` into a
single `mutos_cc` binary? The concern driving the question was testability of
the final generated assembly, not implementation convenience.

**What reading the real `v7/cc` source settled it.** The `temp1`/`temp2`
intermediate format is not a raw memory/pointer dump (which would make the split
purely an artifact of the 64K limit, with no inherent value once that limit is
gone) — it is a small, fully-specified, tagged byte stream:

- `outcode()` in `c04.c` (~70 lines) is the *only* place `c0` writes to `temp1`/
  `temp2`. Format characters: `'B'` (opcode) writes `(opcode_low_byte, 0376)` —
  the fixed sentinel byte `0376` marks "this is an opcode", letting the reader
  distinguish an opcode from an ordinary data word (a legitimate 16-bit number
  never has high byte `0376`); `'N'` is a little-endian 16-bit word; `'S'`/`'F'`
  are a nul-terminated symbol name / float ASCII string; `'1'`/`'0'` are
  shorthands for the constants 1 and 0.
- `getree()` in `c11.c` (~270 lines) is the exact inverse: reads that stream,
  rebuilds an expression tree (`struct tnode`) for `EXPR`/`CBRANCH`/`C3BRANCH`
  nodes, and emits PDP-11 text directly for the many administrative pseudo-ops
  (`PROG`/`DATA`/`BSS`/`.globl`/`SETSTK`/`SNAME`/`ANAME`/`RNAME`/…).

**Why this changes the calculus.** Because the format is this well-defined and
small, preserving it (nearly as-is) buys real, cheap benefits independent of
whether `c0`/`c1` are one process or two:

1. An inspectable IR boundary that can be unit-tested on its own — `mutos_c0`'s
   front end (parsing, type promotion, tree shape, scope/storage-class handling)
   can be fully verified against hand-written expected IR *before* a single line
   of x86 code generator exists, by adding an optional human-readable dump mode
   to `mutos_c0` (not present in the original V7 `cc`, but cheap to add on top of
   the same encoding).
2. A clean separation between front-end and back-end bugs during the hardest
   part of this milestone: the code generator itself. `v7/cc`'s back end
   (`c10`–`c13` + `table.s`) is a tree-pattern matcher (`match()`/`cexpr()`/
   `reorder()` in `c10.c`) driven by PDP-11 instruction-template tables
   (`cctab`/`efftab`/`regtab`/`sptab`, defined in `table.s` as literal PDP-11
   assembly-template strings like `%a,n`). This is fundamentally tied to the
   PDP-11's register model (6 orthogonal general registers R0–R5, symmetric
   `mov`-style two-operand instructions, hardware autoincrement/autodecrement
   mapped 1:1 onto C's `++`/`--`). The 8086 has none of that regularity (`AX`
   required for `MUL`/`DIV`, `CX` required for shift counts, only `BX`/`BP`+
   `SI`/`DI` usable for addressing) — porting this is a genuine redesign, not a
   table refresh, **and it costs exactly the same whether `c0`/`c1` are merged
   or split**. The split doesn't shrink that work; it only lets bugs in it be
   isolated from front-end bugs without waiting for a complete `.s` file every
   time.

**What does *not* change.** Final verification of generated assembly is
identical either way: byte-diffing `mutos_c1`'s `.s` output against real
hardware-linked goldens, exactly the methodology already validated for
Milestone 3 (`mutos_cpp`) and now being extended to Milestone 4 via
`tests/mutos_cc/`. The split is a development-time diagnostic aid, not a
verification mechanism in itself — the golden-diffing process would be
identical if `c0`+`c1` were merged into one binary.

**Concrete follow-on**: `tests/mutos_cc/` (62 K&R C files across 11 categories,
`Makefile`, `README.md`) now exists to seed that golden-diffing process once
`mutos_c1` exists; `*.s.golden` references are being generated on real MUTOS
1700 hardware next, category by category. Two identifiers in the corpus
(`factorial`, `swapchar`) turned out to exceed this toolchain's real 7-character
external-identifier limit (see `CLAUDE.md`'s new "Identifier length limits"
rule, derived from `v7/cc/c0.h`'s `NCPS 8` plus the mandatory leading `_` seen
in `outcode()`) and were renamed (`fact`, `swapch`); every file/directory name
in the corpus was also checked against MUTOS 1700's real `DIRSIZ`=14 filename
limit (`tests/mutos_cpp/h/dir.h`, `h/param.h`) and shortened where needed.

### MUTOS 1700 host-tooling findings

Getting the corpus's golden-generation pipeline actually running on real
hardware (rather than just designed against manpages and `v7/cc` source
reading) surfaced several real bugs in this project's own scripts and
Makefiles — none of them about `mutos_cc` design, all of them about the real
V7-heritage tools those scripts have to run correctly under. Recorded here in
the order they were found, since each fix only surfaced the next problem.

**1. `make -f Makefile.mutos`: `Must be a separator on rules line 35. Stop.`**
Turned out to be user error, not a tooling bug — `gen_mutos.sh` (a shell
script) had been passed to `make -f` by mistake, which tried to parse shell
syntax as makefile rules and choked on the `for` loop's bare `do` (no `:`, no
leading tab, so `make` expected a rule separator and found none). Fixed by
making the distinction impossible to miss: a loud warning at the very top of
`gen_mutos.sh` itself, and the two invocations visually separated in
`tests/mutos_cc/README.md`'s workflow section. Real `make(1)`'s own manpage
was checked at this point (person-supplied `basename_1.d`/`expr_1.d`/
`find_1.d`/`make_1.d` troff source) and confirmed: no `%.o: %.c` pattern
rules, no `$(wildcard)`/`$(dir)`/`$(notdir)` functions, no `:=` — only plain
`=` macros and two-suffix rules like `.c.o:`. Also confirmed from the
manpage's own *Fehlerquellen* section: shell state (notably `cd`) does not
carry across separate recipe lines, since each line runs in its own
subshell — every recipe in this project's `Makefile.mutos` files is
deliberately written as one `;`-joined shell line because of this.

**2. `make -f ./Makefile.mutos`: `Make: line too long. Stop.`** The
single top-level `Makefile.mutos` (covering all 62 test files) built its
`all:` target from one backslash-continued dependency line listing all 124
`.s`/`.1` outputs — joined, ~2946 characters across 63 physical lines. The
longest individual recipe line in the same file was only 110 characters, so
this pinned the fault precisely on that one aggregate line, not on any
per-file command. Fixed by replacing the flat list with a chain of small
targets (`chk01: file1.s file1.1`, `chk02: chk01 file2.s file2.1`, …,
`all: chk62`), so no logical line ever exceeds ~110 characters regardless of
how many files the corpus grows to.

**3. Same file, next run: three `Warning: CC/CPP/C0 changed after being
used` warnings, then `Make: out of memory. Stop.`**, partway through the
very *first* category (`00_smoke`, 3 files) despite the chain fix above.
This ruled out per-line length as the remaining cause (already fixed) and
pointed instead at total makefile size: ~490 lines, 62 chain links plus 124
real per-file targets, apparently exceeding some fixed-size internal
table/arena this `make(1)` allocates once for the whole run — a genuinely
different failure mode from #2, not the same bug resurfacing. The warnings
are presumed to be a symptom of the same resource exhaustion (this `make`'s
built-in default macros for `CC` etc. getting corrupted/reused once table
space ran low), not a separate, independent bug. Fixed structurally: one
small `Makefile.mutos` per category directory (2–9 files, 42–84 lines each)
instead of one covering all 62, run from inside each directory (`cd
00_smoke && make -f Makefile.mutos`) so no single invocation's makefile gets
anywhere near whatever the real limit is. `tests/mutos_cc/README.md`'s
workflow section and `CLAUDE.md`'s `/tests/mutos_cc/` bullet were updated to
match; the top-level `tests/mutos_cc/Makefile.mutos` was deleted outright
(no longer represents anything real).

**4. `make -f Makefile.mutos` (per-category, this time): ran clean, but
produced no `.s` files at all** — `.i`/`.1`/`.2` present and correct, `.s`
silently missing for every file. Root cause: `cc`'s `-P` and `-S` cannot be
combined on this compiler. `v7/cc/cc.c`'s own control flow — not just its
flag-parsing `switch`, which just sets `pflag`/`sflag` independently and
gives no hint of an interaction — makes this explicit:

```c
av[1] = tmp4;
tsp = savetsp;
av[0]= "c0";
if (pflag) {
	cflag++;
	continue;          /* <-- for every source file, unconditionally */
}
...
if (sflag)
	assource = tmp3 = setsuf(clist[i], 's');
```

`-P` (`pflag`) makes `cc` `continue` to the next source file immediately
after `cpp` runs, before c0, c1, or the `sflag`-driven `.s`-naming logic a
few lines further down is ever reached. So `cc -P -S foo.c` only ever
produces `foo.i` (cpp's output, since `pflag` also redirects `tmp4` to
`setsuf(clist[i], 'i')` a few lines earlier) — `-S` never gets a chance to
matter. This directly explains why Milestone 3's own established convention
(`cc -P -DM7100 ... file.c`, no `-S`, used to generate the `.i.golden`
corpus) was always correct: for a preprocess-only run, `-P` alone is exactly
the right, and only meaningfully possible, flag. The mistake was assuming
`-P` plus `-S` would compose (both effects together) when designing this
corpus's own `.s`-generating recipes — they don't compose, `-P` simply wins
by exiting first. Fixed by dropping `-P` from every `cc -S` invocation
across `tests/mutos_cc/Makefile`, all 11 `Makefile.mutos` files, and
`gen_mutos.sh`'s final `cc` call; the separate direct `cpp -P` calls used
to produce `.i`/`.1`/`.2` are unaffected, since they don't go through
`cc`'s driver logic and so never hit this interaction at all. Confirmed
this doesn't threaten byte-parity with the rest of the golden set: `c0`'s
own intermediate-code opcode set (`v7/cc/c0.h`) has no line/file-tracking
opcode at all, so whether `cpp` inserted `# N "file"` line markers (the
actual, sole difference `-P` makes to `cpp` itself) has no path into the
resulting `.s` text.

**5. `make goldens` on the modern host (Linux), after transferring the
now-correct `.s`/`.i`/`.1`/`.2` back: `/bin/sh: 1: /lib/c0: not found`,
`make: *** [Makefile:114: 00_smoke/01_emptymain.1] Error 127`.** The
top-level `Makefile`'s `goldens` target depended on `all intermediates`,
which made Make re-check `.s`/`.1` freshness against `.c`/`.i` by mtime
before packaging anything — standard Make behavior, but wrong for this
specific workflow, where the actual generation happens on a *different*
machine (MUTOS) than the packaging step (Linux). A fresh `git checkout`, or
just the mechanics of transferring files off MUTOS, routinely doesn't
preserve the "each stage newer than its input" timestamp ordering Make
assumes; when it doesn't, Make decides an already-correct `.s`/`.1` is stale
and tries to rebuild it via `$(CC)`/`$(CPP)`/`$(C0)` — paths to binaries
that exist only on real MUTOS hardware, not on the packaging host. Fixed by
making `goldens` prerequisite-free: it now only packages whatever
`.s`/`.i`/`.1`/`.2` files already exist on disk (skipping, not failing on,
any that are missing), and never attempts to (re)create anything itself.
Verified against the exact failure scenario (a `.c` file's mtime forced
newer than already-generated `.s`/`.1` siblings, replicating what a fresh
`git checkout` does): `make -n goldens` now shows only `cp`/`base64`
commands, never `cc`/`cpp`/`/lib/c0`.

**End state, confirmed working:** with fixes #1–5 applied, `make -f
Makefile.mutos` inside a category directory on real MUTOS 1700 hardware
correctly produces `.s`/`.i`/`.1`/`.2` for every file in that category
(confirmed for `00_smoke`), and `make goldens` on the modern host, after
transferring those files back, correctly packages them into
`*.s.golden`/`*.i.golden`/`*.1.golden`/`*.2.golden` (+ base64 companions)
without attempting to invoke any MUTOS-only tool. The golden-generation
pipeline designed in the previous subsection is now empirically validated,
not just designed.

### `temp1`/`temp2` wire format: byte-level derivation (`mutos_c0`/`mutos_c1` built and verified this session)

With full-corpus goldens now present in this checkout, `mutos_c0` (front
end) and `mutos_c1` (back end) were built in `src/mutos_cc/` and verified
byte-exact, end-to-end, against `tests/mutos_cc/00_smoke`'s 3 files — see
`STATUS.md` for the verification summary and `src/mutos_cc/README.md` for
architecture/scope. This subsection is the full byte-level derivation
behind that work: exactly how `v7/cc/c04.c`'s `outcode()` format and
`v7/cc/c02.c`'s `cfunc()`/`funchead()` algorithm shape were confirmed (and,
in four places, found to diverge) against the real MUTOS 1700
`00_smoke/*.1.golden`/`*.s.golden` bytes.

**Method.** `od -A d -t x1z` on each `00_smoke/*.1.golden` gave the raw
byte stream; each byte pair was matched against `v7/cc/c0.h`'s manifest
"operator" constants (e.g. `SYMDEF`=207=`0xCF`, `PROG`=202=`0xCA`) per
`outcode()`'s documented format (`'B'`: `(value, 0xFE)`; `'N'`:
little-endian word; `'S'`: `'_'` + up to `NCPS`(8) chars + `NUL`), then
cross-checked against which `v7/cc/c0*.c` function call site could have
produced that exact byte sequence, and finally against the matching
`*.s.golden` text to confirm the *meaning* of each opcode via `mutos_c1`'s
(not-yet-written, at the time) rendering of it.

**Worked example — `01_emptymain.1.golden` (`main(){}`), full 56 bytes:**

```
cf fe 5f 6d 61 69 6e 00   SYMDEF + "_main\0"        (extdef(): outcode("BS", SYMDEF, name))
ca fe                     PROG                       (cfunc(), MUTOS variant - see delta #2 below)
d2 fe                     EVEN                       (cfunc(), MUTOS variant - see delta #2 below)
72 fe 5f 6d 61 69 6e 00   RLABEL + "_main\0"          (cfunc(): outcode("B..S", PROG, EVEN, RLABEL, name))
d0 fe                     SAVE                        (cfunc(): outcode("B", SAVE))
69 fe 04 00               SETREG 4                     (funchead(): outcode("BN", SETREG, regvar) - MUTOS variant, delta #4)
6f fe 01 00               BRANCH 1                      (cfunc(): branch(sloc), sloc=1)
70 fe 02 00               LABEL 2                        (cfunc(): label(sloc+1))
70 fe 03 00               LABEL 3                         (cfunc(): outcode("BNBN", LABEL, retlab, ...), retlab=3 - empty body, no code between L2/L3)
d1 fe 00 00               RETRN 0                          (... RETRN, TY_INT) - MUTOS variant, delta #3
70 fe 01 00               LABEL 1                           (cfunc(): label(sloc))
db fe 04 00               SETSTK 4                          (cfunc(): outcode("BN", SETSTK, -maxauto) - MUTOS variant, delta #1
6f fe 02 00               BRANCH 2                           (cfunc(): branch(sloc+1))
00 fe                     EOFC                                (c00.c main(): outcode("B", EOFC))
```

`02_retconst.1.golden`/`03_retexpr.1.golden` (`return 42;`/`return 6*7;`)
insert exactly this extra sequence between `LABEL 2` and `LABEL 3` (in
place of the "no code between L2/L3" case above):

```
15 fe 00 00 2a 00   CON, type=TY_INT(0), value=42        (treeout(): outcode("BNN", CON, type, value))
6e fe 00 00           RFORCE, type=TY_INT(0)                (doret(): build(RFORCE); treeout() emits the wrapper)
d6 fe NN 00             EXPR, line=NN                         (rcexpr(): outcode("BN", EXPR, line))
6f fe 03 00               BRANCH 3                              (doret(): branch(retlab))
```

`line` is `09 00` (9) for `02_retconst.c` (`return 42;` is on line 9 of
that file) and `0a 00` (10) for `03_retexpr.c` (`return 6 * 7;` is on line
10) — confirmed against each file's actual physical line count, including
`mutos_cpp`'s comment-to-blank-line preservation (see Milestone 3), which
keeps `.i`'s line numbers identical to the original `.c`'s. `03_retexpr`'s
`CON` value is `42` (`0x2a`), not two separate `CON(6)`/`CON(7)` leaves
with a `TIMES` opcode between them — direct confirmation that real K&R
`cc`'s constant folding happens in the front end (`c0`, at `build()` time),
not deferred to the code generator.

**The four confirmed MUTOS-1700-specific deltas from vanilla `v7/cc`**
(each is a deliberate divergence in the *real* compiler that generated
these goldens, not a bug in this project's understanding of `v7/cc` —
`/v7/cc/` is used here strictly as v7/cc/c0.h's numeric constants plus an
algorithmic reference per CLAUDE.md Workflow Guideline 3, not assumed to be
byte-identical in every call sequence):

1. **`STAUTO = -4`, not V7 PDP-11's `-6`.** `cfunc()` sets
   `maxauto = STAUTO` and, for a function with zero real locals (every
   `00_smoke` case), never changes it, so `SETSTK`'s emitted argument
   (`-maxauto`) directly reveals `STAUTO`. Observed `SETSTK 4` (not 6) in
   all three `00_smoke` goldens. This tracks directly with
   `docs/MUTOS_C_ABI.md` sect. 1.2/1.4: MUTOS's fixed prologue only
   callee-saves 2 registers (`di`, `si`), vs V7 PDP-11's 3 — one fewer
   saved word than V7 reserves. This does **not** move the first local
   slot's own address: `STAUTO` is the *starting subtrahend*
   (`c03.c`: `autolen =- rlength(dsym)`), not the first-local offset
   directly, so a 2-byte first local at `STAUTO(-4) - 2` still lands at
   `-6`, matching the ABI doc's confirmed `bp-6` exactly — only the
   *gap* between the last saved register (`si` at `bp-4`) and the first
   local (`bp-6`) changes versus V7's tighter packing. (For a function with real
   locals, `c1`'s `SETSTK` handler currently treats any `extra > 0`
   beyond this fixed 4-byte reservation as "not yet supported" rather
   than guessing — see `src/mutos_cc/README.md`.)
2. **`cfunc()`'s header sequence is `PROG, EVEN, RLABEL, name`, not V7's
   `PROG, RLABEL, name`.** The extra `EVEN` (`0xD2`) tag sits, byte-exact,
   between `PROG`'s (`0xCA`) and `RLABEL`'s (`0x72`) in all three
   `00_smoke` goldens — confirmed not to originate from any *other*
   `outcode(..., EVEN, ...)` call site in `v7/cc/c02.c`/`c03.c` (those are
   all struct-initializer/local-`static`-declaration paths, unreachable
   for any of these three trivial functions). Most likely explanation:
   an 8086-specific addition ensuring a function's entry point is
   word-aligned, which PDP-11 didn't need the same way.
3. **`RETRN` carries one extra numeric argument (the function's return
   type)**, rendered by `c1` as a `|RTYP n` comment immediately before the
   `jmp cret` epilogue tail-jump — confirmed via the `00 00` word
   following `RETRN`'s tag byte in every `00_smoke` golden, with no
   further opcode tag (`0xFE`-terminated pair) immediately after it, i.e.
   it reads as a plain data word belonging to `RETRN`, not a new opcode.
   V7's own `outcode("BNB", LABEL, retlab, RETRN)` has no such trailing
   argument.
4. **The initial `regvar` (`SETREG`'s first emitted value, from
   `funchead()`) is `4`, not V7's `5`.** Confirmed directly: `SETREG 4`
   in every `00_smoke` golden, for functions with zero register-class
   parameters (which would otherwise have decremented it further). Likely
   reflects the 8086 having fewer freely-allocatable "register variable"
   candidates than the PDP-11's `r5`-downward scheme assumed.

**`mutos_c1`'s text-rendering rules** (the inverse direction: opcode →
`.s` text) were derived the same way, via `od -c` on the matching
`*.s.golden` files rather than reading `v7/cc/c11.c`/`c12.c` line-by-line
(not necessary once the target text was byte-exactly known) — see
`src/mutos_cc/c1_gen.c`'s file header comment for the resulting rules
(notably: `LABEL` emits `"Ln:"` with **no** trailing newline, so two
adjacent labels with no code between them — e.g. an empty function body's
`"L2:L3:"` — concatenate correctly onto one physical line, while `RLABEL`
always emits `"name:\n"` **with** one, since a global entry symbol always
starts a fresh line; and the `"jmp L1" ... "L1: <maybe more code> jmp L2"`
shape is a deferred-prologue-completion trick: `SETSTK`'s value — and thus
whether any `sub sp,N` is needed — is only known *after* the whole
function body has already been walked, since `cfunc()` emits it last).

### Local variables: byte-level derivation (`01_expr/01_intarith` — `mutos_c0`/`mutos_c1` extended and verified this session)

With `mutos_c0`/`mutos_c1` verified against `00_smoke`'s constant-only
programs, the natural next step (per `tests/mutos_cc/`'s own
increasing-difficulty ordering) is `01_expr/01_intarith.c`: `int a, b, c;`
plus `a = 17; b = 5; c = a + b; c = a - b; c = a * b; c = a / b; c = a % b;
return c;`. This is the first construct needing a symbol table and real
(non-folded) expression codegen — see `src/mutos_cc/c0_sym.c` (the symbol
table) and the `ExprVal` fold-or-emit representation in `c0_parser.c`'s
file header comment. Same method as before: `od -A d -t x1z` on
`01_intarith.1.golden`, matched byte-by-byte against `v7/cc/c0.h`'s
constants and cross-checked against `01_intarith.s.golden`'s text.

**`ANAME`** (`outcode("BSN", ANAME, name, offset)` — `#define ANAME 217` =
`0xD9`) is emitted once per declared local, immediately after the `L2`
body-entry label and before any statement code:

```
d9 fe 5f 61 00 fa ff   ANAME "_a" offset=-6    (declares `a`)
d9 fe 5f 62 00 f8 ff   ANAME "_b" offset=-8    (declares `b`)
d9 fe 5f 63 00 f6 ff   ANAME "_c" offset=-10   (declares `c`)
```

`mutos_c1` renders each as a `"| name=offset."` comment — confirmed
byte-for-byte against `01_intarith.s.golden`'s `"L2:| _a=-6.\n| _b=-8.\n|
_c=-10."` (three comments back-to-back, since `LABEL`'s no-trailing-
newline rule from the `00_smoke` derivation above still applies — `L2:`
just runs straight into the first comment).

**`NAME`** (`outcode("BNNN", NAME, hclass, type, hoffset)` for a non-
`EXTERN` name — `#define NAME 20` = `0x14`) is emitted for every variable
*reference*. Worked example, `"c = a + b;"` (bytes decoded from
`01_intarith.1.golden`, offsets 99–134):

```
14 fe 0b 00 00 00 f6 ff   NAME hclass=AUTO(11) type=INT(0) offset=-10  (c, the assignment's lvalue)
14 fe 0b 00 00 00 fa ff   NAME hclass=AUTO(11) type=INT(0) offset=-6   (a)
14 fe 0b 00 00 00 f8 ff   NAME hclass=AUTO(11) type=INT(0) offset=-8   (b)
28 fe 00 00               PLUS type=INT(0)                             (#define PLUS 40 = 0x28)
50 fe 00 00               ASSIGN type=INT(0)                           (#define ASSIGN 80 = 0x50)
d6 fe 0c 00                EXPR line=12                                 (statement wrapper)
```

i.e. `treeout()`'s generic postorder walk applied to `ASSIGN(NAME(c),
PLUS(NAME(a), NAME(b)))`: the lvalue first, then the right-hand subtree
(itself postorder: `a`, `b`, then `PLUS`), then `ASSIGN` itself, then the
statement-level `EXPR` wrapper — exactly matching a real assignment
expression's `tr1`/`tr2` shape from `treeout()`'s `default:` case (`c04.c`).
Offset assignment matches `v7/cc/c03.c`'s declarator loop exactly, using
`STAUTO=-4` (the MUTOS-specific delta above) as the *starting subtrahend*,
not a first-local offset directly: `autolen -= size; hoffset = autolen`,
so `a` (first, 2 bytes) lands at `-4-2=-6`, `b` at `-6-2=-8`, `c` at
`-8-2=-10` — confirmed exactly.

`"c = a * b;"`/`"c = a / b;"`/`"c = a % b;"` follow the identical NAME/
NAME/NAME/`op`/`ASSIGN`/`EXPR` shape with `TIMES`(`0x2a`)/`DIVIDE`(`0x2b`)/
`MOD`(`0x2c`) in place of `PLUS` — confirmed for all five operators (`+ -
* / %`) across the file's five statements.

**Per-operator `.s` instruction shapes** (`od -c` on `01_intarith.s.golden`,
matched against each opcode's position in the decoded stream above):

```
c = a + b;   mov di,*-6.(bp)  / add  di,*-8.(bp)  / mov *-10.(bp),di
c = a - b;   mov di,*-6.(bp)  / sub  di,*-8.(bp)  / mov *-10.(bp),di
c = a * b;   mov ax,*-6.(bp)  / imul *-8.(bp)     / mov *-10.(bp),ax
c = a / b;   mov ax,*-6.(bp)  / cwd  / idiv *-8.(bp) / mov *-10.(bp),ax
c = a % b;   mov ax,*-6.(bp)  / cwd  / idiv *-8.(bp) / mov *-10.(bp),dx
return c;    mov di,*-10.(bp) / mov ax,di
```

Notable findings, all implemented in `c1_gen.c`'s `Val`/value-stack
handlers: `DI` is `+`/`-`'s working register (matching `00_smoke`'s
`RFORCE` handler, which already used `DI` as the generic "materialize a
return value" register — the same register, not a coincidence); `*`/`/`/
`%` instead use `AX` (required by the 8086's single-operand `IMUL`/`IDIV`
encoding, which always operates on `AX`/`DX:AX`); `/` and `%` compile to
the *literal same* `mov ax,.. / cwd / idiv ..` sequence, differing only in
which register the following `ASSIGN` takes its value from afterward (`ax`
= quotient, `dx` = remainder) — and the golden genuinely repeats this
whole sequence for both statements rather than sharing it, confirming
`mutos_c1` is deliberately unoptimized (peephole sharing is `c2`'s job,
Milestone 5).

**`SETSTK`'s local-frame-size handling is now confirmed with a real
nonzero case**, not just the `extra==0` case from `00_smoke`:
`01_intarith` has three 2-byte locals reserved via three `ANAME`s, giving
`SETSTK 10` (tag `db fe` = `0xDB` = `SETSTK` = 219, value `0a 00` = 10) →
`extra = 10 - 4 = 6` bytes beyond the fixed register-save area, rendered
as `"L1:sub\tsp,*6.\njmp\tL2"` — confirmed byte-for-byte, and `extra=6`
sits comfortably under `docs/MUTOS_C_ABI.md` sect. 1.9's confirmed
real-hardware bound (largest plain `sub sp,N` seen in `libc.a`: `N=76`),
so no ambiguity with the (then still unconfirmed) `chkstk` gap applies here.

### Bitwise operators and the `*`/`#` immediate marker: byte-level derivation (`01_expr/02_bitwise` — `mutos_c0`/`mutos_c1` extended and verified this session)

Next in `tests/mutos_cc/`'s difficulty ordering: `02_bitwise.c` (`a & b`,
`a | b`, `a ^ b`, `~a`). Same method as before, `od -A d -t x1z` on
`02_bitwise.1.golden`.

**`AND`(`#define AND 47` = `0x2F`)/`OR`(`48` = `0x30`)/`EXOR`(`49` =
`0x31`)** follow the identical `NAME`/`NAME`/`op`/`ASSIGN`/`EXPR` shape
already confirmed for `+ - * / %` — e.g. `"c = a & b;"` decodes to `NAME(c)
NAME(a) NAME(b) AND(type) ASSIGN(type) EXPR(line)`, byte-for-byte parallel
to `01_intarith`'s `"c = a + b;"` with `PLUS` swapped for `AND`.

**`COMPL`(`38` = `0x26`) is the first confirmed *unary* operator on a
non-constant operand.** `"c = ~a;"` decodes to `NAME(c) NAME(a) COMPL(type)
ASSIGN(type) EXPR(line)` — only one operand between the lvalue's `NAME`
and the operator tag, confirming `treeout()`'s single-child walk for a
non-`BINARY`-flagged op (no second `treeout()` call, unlike `AND`/`OR`/
`EXOR`/`PLUS`/etc., which each have a `NAME`/`CON` pair before their own
tag).

**`.s` instruction shapes** (`od -c` on `02_bitwise.s.golden`): `AND`/`OR`/
`EXOR` each load the left operand into `DI` then `and`/`or`/`xor` the
right operand in place — literally the same shape as `+`/`-`, just a
different mnemonic. `COMPL` loads its one operand into `DI` then `not\tdi`
in place (no second operand, matching a true unary instruction). All four
use `DI` as their working register, extending (not contradicting) the
pattern already established: `DI` appears to be `mutos_c1`'s single
generic two-or-one-operand working register for this whole class of
integer operators, at least at the single-operand-per-operand-slot
complexity this grammar scope currently reaches.

**A genuinely new finding: `mutos_as`'s `*`/`#` immediate-operand size
markers are a real, meaningful distinction, not interchangeable
punctuation.** `"a = 0xF0;"` (240) renders as `"mov\t*-6.(bp),#240."`
while `"b = 0x0F;"` (15) renders as `"mov\t*-8.(bp),*15."` — confirmed via
`od -c` down to the exact byte (`#`=`0x23` vs. `*`=`0x2A` immediately
before the digits). Two independent checks pinned down the rule:

1. `man/mutos_as.1`'s "Operand size markers" section (read directly,
   since `mutos_as` is a complete, independently-verified Milestone 2
   component whose own documentation is authoritative for its own input
   syntax) states plainly: `*` marks an operand byte-sized, `#` marks it
   word-sized, and "for an immediate operand the marker is honored
   literally, regardless of the value's actual magnitude" — i.e. this is
   the code generator's *choice*, not the assembler inferring anything.
2. Actually assembling both lines with the real, already-built `mutos_as`
   (`./mutos_as -o /tmp/bw.o 02_bitwise.s.golden` — exit 0, no errors) and
   disassembling the result (`objdump -D -b binary -m i386 -M intel,
   i8086` against the extracted text segment, same method
   `docs/MUTOS_C_ABI.md` uses) shows **both lines assemble to the
   identical 5-byte `C7 /0 iw` encoding** (`c7 46 fa f0 00` and `c7 46 f8
   0f 00` respectively) — plain 8086 `MOV r/m16,imm` has no byte-immediate
   form at all, so the `*`/`#` choice cannot be encoding-driven for `MOV`
   specifically.

Putting the two together: the real compiler's code generator picks `*`
when a value fits a **signed byte** (`-128..127` — 15 does, 240 does not:
sign-extending `0xF0` from a byte would corrupt it to `-16`) and `#`
otherwise, applying that choice **uniformly** to every immediate it emits
regardless of which instruction it ends up in — for `MOV` the choice is
cosmetic (both encode identically), but the same generator presumably
also feeds instructions that *do* have a real 3-byte `imm8`-sign-extended
encoding (`ADD`/`SUB`/`AND`/`OR`/`XOR`/`CMP r/m16,imm8`, opcode `83`),
where the choice would matter for real. `c1_gen.c`'s `render_operand()`
now implements this for every immediate; the `127` upper bound is directly
confirmed, while the symmetric `-128` lower bound is the natural
completion of "fits in a sign-extended byte" but not yet independently
confirmed by a golden with a large-magnitude negative constant. Memory-
operand displacements are unaffected either way (confirmed via the same
man page: "for a displacement... the marker has no effect") and keep
using `*` unconditionally, matching every golden's uniform
`*offset.(bp)` regardless of the offset's own magnitude.

### Relational/equality/logical operators and short-circuit codegen: byte-level derivation (`01_expr/03_rellogic` — `mutos_c0`/`mutos_c1` extended and verified this session)

Next in difficulty order: `03_rellogic.c` (`< <= > >= == != && || !`).
Same method, `od -c`/`od -t x1z` on `03_rellogic.1.golden` and
`.s.golden`.

**`c0`'s tree emission is completely uniform across this whole
operator class.** `LESS`(`63`=`0x3F`)/`LESSEQ`(`62`)/`GREAT`(`65`)/
`GREATEQ`(`64`)/`EQUAL`(`60`)/`NEQUAL`(`61`)/`LOGAND`(`53`)/`LOGOR`
(`54`)/`EXCLA`(`34`) all decode as the identical `NAME`/`NAME`/`op`
"BN" (tag + type) shape already confirmed for `+ - * / % & | ^ ~` —
e.g. `"r = a < b;"` decodes to `NAME(r) NAME(a) NAME(b) LESS(type)
ASSIGN(type) EXPR(line)`, byte-for-byte parallel to `01_intarith`'s
`"c = a + b;"`. This was the single most load-bearing finding of this
session: it means `CBRANCH` (`103`=`0x67`, confirmed present in `v7/
cc/c04.c`'s `doif()` — `outcode("BNNN", CBRANCH, lbl, cond, line)`)
is **not** used at all by any code this grammar scope currently
produces. `CBRANCH` bakes a condition directly into an `if`-statement
branch; nothing here is an `if`, so `c0` never emits it, and a
comparison used as a plain *value* (`r = a < b;`) is just another
operator-tree leaf, identical in shape to `PLUS`/`AND`/etc. Turning
that tree into 0/1 or into a branch is entirely `c1`'s problem — not
something `c0`'s wire format needs to distinguish.

**`c1`'s codegen for a standalone comparison** (`.s.golden`'s first
six blocks, one per operator on `"r = a OP b;"`):
```
mov   di,*-8.(bp)   | load right operand (b) into DI - only when
                    | it's itself memory; an immediate right operand
                    | (see LOGAND/LOGOR below) needs no load at all,
                    | since 8086 CMP allows one memory + one
                    | immediate/register operand but not two memory
cmp   *-6.(bp),di   | left operand (a) stays a direct memory operand
b<cc> L<true>       | direct condition: blt/ble/bgt/bge/beq/bne
mov   di,*0.
jmp   L<end>
L<true>:mov di,*1.
L<end>:             | (no trailing newline - glues to the next
                    | instruction on the same source line, same
                    | style already established for LABEL)
```
Each of the six standalone comparisons in the golden consumes exactly
2 of `c1`'s own internal labels (confirmed starting value `L10000`,
strictly separate from `temp1`'s own label numbers which start at 1
per-function) — `LESS`→`L10000`/`L10001`, `LESSEQ`→`L10002`/`L10003`,
… `NEQUAL`→`L10010`/`L10011`.

**`LOGAND`/`LOGOR` never materialize their two comparison operands
separately — they fuse them into one short-circuit branch sequence**,
confirmed against `"r = (a < b) && (b > 0);"`:
```
mov   di,*-8.(bp)
cmp   *-6.(bp),di
bge   L10013        | INVERTED first condition (blt -> bge) jumps
                    | straight to the FALSE label, skipping the
                    | second comparison entirely when short-circuited
cmp   *-8.(bp),*0
bgt   L10012        | DIRECT second condition jumps to the TRUE label
L10013:mov di,*0.   | false label doubles as the fallthrough target
jmp   L10014
L10012:mov di,*1.
L10014:
```
and `"r = (a < 0) || (b > 0);"`:
```
cmp   *-6.(bp),*0
blt   L10015         | DIRECT condition, both operands -> same TRUE label
cmp   *-8.(bp),*0
bgt   L10015
mov   di,*0.         | pure fallthrough - no separate false label needed,
jmp   L10016         | since nothing ever jumps here
L10015:mov di,*1.
L10016:
```
i.e. exactly the classic "jumping code" technique: an `&&` chain's
non-final operands branch on their *inverted* condition to a shared
false label (falling through on success), its final operand branches
direct to the true label; an `||` chain's operands all branch direct
to a shared true label, falling through together to the false case.
Label allocation order is `true, false, end` for `LOGAND` (`L10012`,
`L10013`, `L10014`) and `true, end` for `LOGOR` (`L10015`, `L10016`,
no false label at all) — deduced from which label number each branch
target names, not from the labels' left-to-right textual position in
`.s` (the false-branch target is printed before its own definition,
an ordinary forward reference).

**`EXCLA` (`!`) defers exactly like a comparison — it emits *no* code
of its own**, confirmed via `"r = !r;"`'s golden: nothing appears
between the second `NAME(r)` and a single `cmp *-10.(bp),*0 / beq
L10017 / ...` block (labels `L10017`/`L10018`) — the *negation* of
`NEQUAL 0` (i.e. `EQUAL 0`), not a separate `not`/`xor` instruction.
This confirms `!x` is implemented as "take x's condition (synthesizing
an implicit `x != 0` truth test if x isn't already a comparison),
invert its branch sense, and defer" — materialization happens later,
at the same `ASSIGN` that would have materialized a bare comparison.

**One byte-exact rendering quirk, otherwise easy to miss**: `CMP`'s
immediate right-hand operand drops the trailing `.` decimal-terminator
used everywhere else (`"cmp\t*-6.(bp),*0"`, never `"...,*0."`), while
every `MOV` immediate in the same file still carries it
(`"mov\tdi,*0."`). Confirmed via `od -c` — no trailing `2e` (`.`) byte
after the `0` in any `cmp` line, present after every `mov` line's `0`
or `1`. Per `man/mutos_as.1`, the trailing period is "a stylistic
decimal terminator" with "no effect on the value", so this is a
source-text quirk specific to the real compiler's `CMP`-immediate
rendering path (a different internal format string than the one used
for `MOV`), not a semantic difference `mutos_as` itself cares about.
Only the value `0` is golden-confirmed; the `*N`/`#N` byte-vs-word
marker threshold for other magnitudes in this position is extrapolated
from the already-confirmed `MOV` threshold (delta #7 above), not
independently confirmed here.

`c1_gen.c` implements all of the above via a new `VK_COND` value-stack
kind: `LESS`/`LESSEQ`/`GREAT`/`GREATEQ`/`EQUAL`/`NEQUAL` push a
deferred "left `<op>` right" descriptor instead of emitting anything;
`LOGAND`/`LOGOR` pop two such descriptors (synthesizing an implicit
`!= 0` one via `as_cond()` for any operand that isn't already a
comparison — not exercised by any golden yet, since both of
`03_rellogic`'s `&&`/`||` operands are always direct comparisons, but
the natural zero-risk generalization) and fuse them per the branch
patterns above; `EXCLA` inverts and re-defers; every other operator
(`ASSIGN`, `RFORCE`, and defensively `PLUS`/`MINUS`/`TIMES`/`DIVIDE`/
`MOD`/`AND`/`OR`/`EXOR`/`COMPL`) materializes any `VK_COND` it pops
before using it, via the single standalone-comparison code path above.

### Shift operators: byte-level derivation (`01_expr/04_shift` — `mutos_c0`/`mutos_c1` extended and verified this session)

Next in difficulty order: `04_shift.c` (`a << n` with a variable count,
`r >> 2` and `a << 1` with constant counts). Same method, `od -c`/`od -t
x1z` on `04_shift.1.golden` and `.s.golden`.

**`c0`'s tree emission is, once again, completely uniform.**
`LSHIFT`(`46`=`0x2E`)/`RSHIFT`(`45`=`0x2D`) both decode as the identical
`NAME`/`NAME`/`op` "BN" (tag + type) shape as every other binary op
confirmed so far — `"r = a << n;"` decodes to `NAME(r) NAME(a) NAME(n)
LSHIFT(type) ASSIGN(type) EXPR(line)`. `c0` does not care whether the
right operand is a variable or a constant; that distinction is entirely
`c1`'s problem.

**`c1`'s codegen splits hard on whether the shift count is a compile-time
constant**, confirmed against all three shift expressions in
`04_shift.s.golden`:

Variable count (`"r = a << n;"`):
```
mov  di,*-6.(bp)   | value being shifted -> DI, as usual
mov  cx,*-8.(bp)   | shift COUNT -> CX specifically - the first construct
                   | in this grammar scope needing any working register
                   | other than DI/AX/DX
sal  di,cl         | 8086's "shift by CL" opcode shape only accepts CL
mov  *-10.(bp),di
```

Constant count (`"r = r >> 2;"` and `"r = a << 1;"`):
```
mov  di,*-10.(bp)
sar  di,*1         | repeated N times - see below
sar  di,*1
mov  *-10.(bp),di
```
```
mov  di,*-6.(bp)
sal  di,*1         | N=1, so exactly one repetition
mov  *-10.(bp),di
```
**No shift-by-immediate-count opcode is used at all for the constant
case** — plain 8086 doesn't have one (`C0`/`C1 /digit ib`, documented in
this session's earlier 80186/80188-spec exchange, is an 80186-only
extension; `04_shift.c`'s own header comment states explicitly that this
compiler targets plain 8086 only). Instead the real compiler repeats the
one-bit-shift form (`D1 /4` for `SAL`, `D1 /7` for `SAR` — count
implicitly 1, no count operand encoded at all) exactly N times. This is
the historically correct K&R/pre-80186-era C compiler technique for a
constant shift count, not a "naive" or suboptimal choice on this
project's part — it's what the real hardware golden actually does, so
it's what `mutos_c1` reproduces.

**Mnemonic choice**: `SAL` for `<<`, `SAR` for `>>` — not `SHL`. `SAL`
and `SHL` are the identical opcode (arithmetic and logical left shift are
the same operation; the 8086 only distinguishes them by mnemonic
convention), so this is a pure source-text choice by the real compiler,
confirmed directly from the golden rather than assumed.

**The `CMP`-immediate no-trailing-period rendering quirk (previous
session, `03_rellogic`) is confirmed NOT `CMP`-specific**: the repeated
`"sar\tdi,*1"`/`"sal\tdi,*1"` lines use the same bare `*1` (no trailing
`.`) as `CMP`'s immediate operand, never `*1.` the way every `MOV`
immediate in this same file renders (e.g. `"mov\t*-6.(bp),*1."` for the
`a = 1;` initialization at the top of the function). `c1_gen.c`'s
`render_cmp_imm()` was renamed to `render_bare_imm()` to reflect this
broader confirmed scope — it's the generic "immediate operand of
anything other than `MOV`" renderer, not a `CMP`-only one.

`c1_gen.c` implements the variable-count path via a new `load_into_cx()`
helper (mirroring the existing `load_into_di()`: loads a value into `CX`,
skipping the no-op `"mov cx,cx"` case exactly like `load_into_di()` does
for `DI`) and the constant-count path via a simple counted loop emitting
`render_bare_imm()`'s rendering of the literal `1`, `r.imm` times.

### Increment/decrement, pointers and arrays: byte-level derivation (`01_expr/05_incdec` — `mutos_c0`/`mutos_c1` extended and verified this session)

Next in difficulty order: `05_incdec.c` — `i++`/`++i`/`i--`/`--i` on a
plain `int`, then `p = a;` (array-to-pointer decay), `*p++ = 1;` and
`*++p = 2;` (pointer arithmetic + dereferenced-pointer assignment). Same
method, `od -A d -t u1` on `05_incdec.1.golden`, cross-referenced against
`05_incdec.s.golden`'s text.

**`c0`'s tree shape for a plain-int increment/decrement**, decoded from
`"j = i++;"` (`NAME(j) NAME(i) CON(1) INCAFT(0) ASSIGN(0) EXPR(15)`) and
the three siblings (`"j = ++i;"` → `INCBEF`, `"j = i--;"` → `DECAFT`,
`"j = --i;"` → `DECBEF`): the lvalue's `NAME`, then a bare `CON(1)` (the
literal "1" every `++`/`--` means), then the operator tag itself
(`INCBEF`=`30`=`0x1E`, `DECBEF`=`31`=`0x1F`, `INCAFT`=`32`=`0x20`,
`DECAFT`=`33`=`0x21`). No `ASPLUS`/`ASMINUS` anywhere, unlike vanilla
V7 `cc`'s `c00.c` (`case ASSIGN: if (andflg==0 && PLUS<=*op && *op<=EXOR)
o = *op-- + ASPLUS - PLUS;` — that rule only fires for a real
compound-assignment token like `+=`, not for the already-distinct
`INCBEF`/`INCAFT`/`DECBEF`/`DECAFT` tokens the lexer produces for `++`/
`--`, so MUTOS's `cc` keeps them as their own dedicated opcodes all the
way to `temp1` rather than folding them into `ASPLUS`/`ASMINUS` the way
`v7/cc/c10.c`'s codegen tables suggested they might).

**The pointer case adds an explicit scaling subtree.** `"*p++ = 1;"`
decodes to `NAME(p,type=8) CON(1) CON(2) ITOP(8) INCAFT(8) STAR(0) CON(1)
ASSIGN(0) EXPR(21)` — i.e. the same `NAME`/`CON(1)`/`<op>` shape as the
plain-int case, but with `CON(2)` (`MCC_SZINT`) and an `ITOP` node
(`13`=`0x0D`) spliced in between the `CON(1)` and the `INCAFT` tag. Both
operands feeding `ITOP` are always compile-time constants in this
grammar scope (the literal "1" every `++`/`--` means, and the pointee's
fixed size), so `mutos_c1`'s `OP_ITOP` handler just folds them
(`amt.imm * size.imm`) into a single immediate rather than emitting a
real 8086 multiply — confirmed by `05_incdec.s.golden` never containing
an `imul` for either pointer statement, only `"add *-18.(bp),*2."`. Type
value `8` = `TY_INT | 010` (`TY_PTR_INT` in both `c0_parser.c` and
`c1_gen.c`) — one pointer-degree bit set per `mutos_cc.h`'s `XTYPE`
comment, cross-referenced directly against `v7/cc/c0.h`'s own `TYPE`/
`XTYPE` bit-layout comments (the only available reference for this bit
packing - no MUTOS-specific manpage covers the compiler's internal type
encoding).

**Postfix vs. prefix is a `c1`-side codegen-ordering decision, not a
different wire shape** — `INCBEF`/`DECBEF` (prefix) and `INCAFT`/
`DECAFT` (postfix) share the identical tree shape above; only `c1`'s
`gen_incdec()` treats them differently:

Prefix (`"j = ++i;"`, commits immediately then loads the NEW value):
```
inc  *-6.(bp)      | side effect FIRST
mov  di,*-6.(bp)   | then load the (now-updated) value
mov  *-8.(bp),di   | ASSIGN
```

Postfix (`"j = i++;"`, loads the OLD value first and DEFERS the side effect):
```
mov  di,*-6.(bp)   | load the value BEFORE the side effect
mov  *-8.(bp),di   | ASSIGN
inc  *-6.(bp)      | side effect LAST - strictly after the assignment's own store
```

This is the classic K&R postfix-side-effect-deferred-to-end-of-full-
expression rule, made concrete: `c1_gen.c`'s `GenState` gained a small
`deferred[]` instruction-text queue (`queue_deferred()`/
`flush_deferred()`), and `OP_EXPR`'s handler — previously a pure no-op,
just consuming the source-line number — now flushes it. Confirmed this
queue is flushed exactly once per statement (never straddling two
statements) since `05_incdec.c` never nests two postfix ops in one
expression; a real multi-postfix expression would need `DEFERRED_MAX`
(currently 4, comfortably above the 1 this file ever needs) raised if it
overflowed, which `gen_fatal()` would catch loudly rather than silently
dropping a fixup.

**Array-to-pointer decay** (`"p = a;"`) decodes to `NAME(a,type=0,
offset=-16) AMPER(8)` — the array's `NAME` node uses its *base element
type* (`TY_INT`, not some distinct "array" type value — this
reimplementation's minimal grammar scope never encodes array-ness on the
wire at all, only in `c0`'s own `SymEntry.is_array` bookkeeping) and its
first element's offset, then `AMPER` (`35`=`0x23`) takes its address.
`c1`'s `OP_AMPER` handler renders this as a real `lea`, confirmed via
`"lea di,*-16.(bp)"`. This is also what surfaced that **`ASSIGN`'s type
argument is the LVALUE's real type**, not always `TY_INT` as every prior
grammar increment happened to have (each was a plain-`int`-to-`int`
assignment, so the distinction was invisible): `"p = a;"`'s `ASSIGN` node
uses type `8` (`TY_PTR_INT`), confirmed at byte offset 242-243 of
`05_incdec.1.golden`. `c0_parser.c`'s `parse_assign_stmt()` was changed
from a hardcoded `outcode(t1, "BN", OP_ASSIGN, TY_INT)` to `sym->type`.

**Dereferencing a pointer as an assignment target** (`"*p++ = 1;"`/
`"*++p = 2;"`) is a new statement form, not an expression-level
construct reused from `assign-stmt` — `c0_parser.c`'s
`parse_star_assign_stmt()`, dispatched off a leading `*` token at
statement level. Its tree is exactly the pointer sub-expression (with
its own optional postfix/prefix `++`/`--`, reusing `emit_incdec()`)
followed by `STAR` (`36`=`0x24`, always type `0`/`TY_INT` — only
pointer-to-`int` is in scope, so the dereferenced type never varies),
then the ordinary rhs/`ASSIGN`/`EXPR` shape. `c1`'s `OP_STAR` handler
loads the pointer value into `DI` (a no-op when it's already there,
e.g. straight off the preceding `INCAFT`/`INCBEF` — see
`load_into_di()`'s existing skip-if-already-there check) and produces a
new operand kind, `VK_IND` (an indirect `"(di)"` addressing mode) rather
than `VK_MEM`'s `"*N.(bp)"` — confirmed via `"mov (di),*1."`.
`OP_ASSIGN`'s lvalue-kind check was widened from `lhs.kind != VK_MEM` to
also accept `VK_IND`.

**Confirmed byte-exact, full pipeline, zero mismatches**: `01_expr/
05_incdec.c` → real `mutos_cpp -P` → `mutos_c0` → `mutos_c1` matches
`05_incdec.i.golden`/`.1.golden`/`.2.golden`/`.s.golden` byte-for-byte on
the first complete implementation attempt (no golden-mismatch iteration
was needed this session — the byte-level `od` derivation above was done
*before* writing any `c0_parser.c`/`c1_gen.c` code, the same
derive-then-implement order every prior Milestone 4 increment used).
Full-corpus regression (`tests/mutos_cc/run_goldens.sh`) confirms zero
regressions elsewhere: one more file byte-exact than the prior
session's count, one fewer "not yet
supported" than before (`05_incdec` moved from that bucket to
the pass bucket), 0 genuine mismatches.

### Compound assignment operators, and a real lexer bug: byte-level derivation (`01_expr/06_compasgn` — `mutos_c0`/`mutos_c1` extended and verified this session)

Next in difficulty order: `06_compasgn.c` — all ten compound-assignment
operators (`+= -= *= /= %= <<= >>= &= |= ^=`) on a plain `int`. Same
method, `od -A d -t u1` on `06_compasgn.1.golden`, cross-referenced
against `06_compasgn.s.golden`'s text.

**`c0`'s tree shape is pleasantly uniform, and confirms a real design
choice already visible in `v7/cc/c0.h`'s opcode table** (`ASPLUS=70`,
`ASMINUS=71`, `ASTIMES=72`, `ASDIV=73`, `ASMOD=74`, `ASRSH=75`,
`ASLSH=76`, `ASSAND=77`, `ASOR=78`, `ASXOR=79` — ten dedicated,
consecutive opcode values): every compound-assignment statement decodes
to exactly the same shape as plain `=` (`NAME(lvalue) <rhs-expr>
<op-tag>(type) EXPR(line)`), just with the matching `AS*` tag in place
of `ASSIGN` — e.g. `"a += 5;"` → `NAME(a) CON(5) ASPLUS(0) EXPR(11)`.
There is **no synthesized `NAME(a) NAME(a) CON(5) PLUS ASSIGN`-style
tree** — the lvalue's `NAME` is emitted exactly once, doing double duty
as both the read-location the operator implicitly reads from and the
write-target it implicitly stores back into, confirming these are
genuine read-modify-write nodes at the `c0` level, not front-end sugar
for a regular assignment of a regular binary expression.

**`c1`'s codegen is where the real decisions are**, confirmed against
all ten statements in `06_compasgn.s.golden`:

`+= -= &= |= ^=` (`"a += 5;"`, `"a -= 3;"`, `"a &= 0x0F;"`,
`"a |= 0x30;"`, `"a ^= 0x11;"`) each compile to a **single** in-place
instruction directly on the memory operand:
```
add  *-6.(bp),*5.
sub  *-6.(bp),*3.
and  *-6.(bp),*15.
or   *-6.(bp),*48.
xor  *-6.(bp),*17.
```
Never routed through `DI` the way a non-compound binary operator is
(`OP_PLUS`/`OP_MINUS`/`OP_AND`/`OP_OR`/`OP_EXOR` above all load into `DI`
first) — there is no separate `ASSIGN` node following these in `temp1`
to do the memory write, so the compound-assignment node's own codegen
has to be the one writing back, and it does so by operating on the
`bp`-relative operand directly. `render_operand()`'s existing
`*`/`#`-marker immediate rendering (delta #7, `02_bitwise` session)
needed no changes — `5`/`3`/`15`(`0x0F`)/`48`(`0x30`)/`17`(`0x11`) all
fit a signed byte, so all five render with the `*` marker and trailing
`.`, exactly matching the golden text.

`*=` (`"a *= 2;"`) is a **genuine strength-reduction confirmation, not
a guess**:
```
sal  *-6.(bp),*1
```
**Never an `imul`.** 8086 `IMUL` cannot take an immediate operand
directly — the exact restriction `OP_TIMES`'s own codegen already
enforces (`gen_fatal("multiplying by an immediate is not yet
supported...")`, confirmed in the `01_intarith` session) — so the real
compiler substitutes a shift for a power-of-two constant multiply
instead of the "load the immediate into a register, then `imul`
reg"-style sequence one might otherwise guess at. `mutos_c1`'s
`OP_ASTIMES` handler generalizes this to any power-of-two ≥ 2 via the
same N-times-repeat-the-single-bit-shift reasoning `LSHIFT`/`RSHIFT`
already established (`04_shift` session) — only the `*2` case (one
repetition) is itself golden-confirmed; a non-power-of-two constant
(or 0 or 1) falls back to the same explicit "not yet supported" rather
than guessing the real compiler's actual strength-reduction
thresholds (e.g. does it use `LEA` tricks for `*3`? Unconfirmed, not
implemented).

`/=`/`%=` (`"a /= 4;"`, `"a %= 3;"`) need **an extra step neither of
the above do**:
```
mov  ax,*-6.(bp)
cwd
mov  cx,*4.        | the immediate has to be loaded into a register
                   | first - IDIV can't take one directly either
                   | (the same restriction OP_DIVIDE/OP_MOD already
                   | enforce for their own right operand)
idiv cx
mov  *-6.(bp),ax   | explicit store-back - IDIV's result lands in
                   | AX/DX, never directly in memory
```
and for `%=`, identical except the final store reads `dx` instead of
`ax` — matching `OP_DIVIDE`/`OP_MOD`'s already-confirmed quotient-in-
`AX`-vs-remainder-in-`DX` convention exactly (`01_intarith` session).
This is the only pair of compound-assignment operators needing an
explicit store-back instruction, since every other one above operates
on the memory operand in place.

**This session also found and fixed a real, pre-existing bug in
`c0_lex.c`'s tokenizer**, unrelated to any grammar/opcode work above:
`/=` was lexing as plain `T_SLASH`, silently dropping the `=`, which
made `parse_assign_stmt()`'s new operator-token switch fail with
`"expected '=' or a compound-assignment operator, found token"` at
`"a /= 4;"` — the *third* compound-assignment statement below `*=` to
be parsed, ruling out a simple "first attempt, first bug" explanation.
Root cause: `skip_space_and_comments()` peeks one character past a `/`
to decide whether it starts a `/* ... */` comment; when it doesn't
(`c2 != '*'`), it needs to push *both* characters back (`c2`, then `c`
itself) so the real tokenizer can read them fresh from `/`. But the
`Lexer` struct's pushback buffer (`int peek`, sentineled by
`LEX_NOPEEK`) only ever had room for **one** character — the second of
the two back-to-back `lex_ungetc()` calls silently overwrote the
first, permanently losing whichever character was pushed back first
(`c2`). For a lone `/` (plain division, e.g. `"a / b"` with a space
after), the lost character was just that following space — harmless,
since whitespace insignificance meant the bug was completely invisible
through every prior session's goldens, including `01_intarith`'s own
division test. For `/=` specifically, the lost character is the `=`
itself: `skip_space_and_comments()` peeks `=` as `c2`, determines it's
not `*` (not a comment), then loses it in the double-`ungetc`, leaving
only `/` to be read back — the main tokenizer's `case '/':` then peeks
the *next* real character (now the space after the already-consumed-
and-lost `=`) to decide `T_SLASH` vs. `T_SLASHEQ`, finds a space (not
`=`), and returns plain `T_SLASH`. `06_compasgn.c` is the first
construct in this grammar scope's test corpus to put a `/` directly
before a `=` with nothing in between, which is exactly why this bug
had stayed dormant through five prior sessions of goldens despite
being a straightforward, general correctness defect (not something
`06_compasgn`-specific). Fixed generally: `Lexer.peek` became a
2-slot LIFO stack (`int peek[2]; int npeek;`), not a `/=`-specific
workaround, since `skip_space_and_comments()`'s own logic (and
potentially other call sites, though none of the rest turned out to
need more than 1-deep pushback) legitimately needs up to 2-deep
pushback by design. `lex_ungetc()` now rejects (`c0_error_at`, not a
silent drop) a third consecutive pushback rather than resurrecting the
same silent-data-loss failure mode at a new depth.

**Confirmed byte-exact, full pipeline, zero mismatches**: `01_expr/
06_compasgn.c` → real `mutos_cpp -P` → `mutos_c0` → `mutos_c1` matches
`06_compasgn.i.golden`/`.1.golden`/`.2.golden`/`.s.golden` byte-for-byte
once the lexer fix landed (the byte-level `od` derivation above was, as
usual, done *before* writing the `c0_parser.c`/`c1_gen.c` codegen -
the lexer bug was an unrelated pre-existing defect the new grammar
coverage happened to be the first to exercise, not a flaw in that
derive-then-implement process itself). Full-corpus regression confirms
zero regressions elsewhere: one more file byte-exact than the prior
session's count, one fewer "not yet supported" than before —
`06_compasgn` moved from that bucket to the pass bucket, 0 genuine
mismatches.

### The `?:` conditional and `,` comma operator, plus a confirmed `ASSIGN`-value-consumer: byte-level derivation (`01_expr/07_ternary` — `mutos_c0`/`mutos_c1` extended and verified this session)

Next in difficulty order: `07_ternary.c` — `m = a > b ? a : b;` then
`m = (a = a + 1, b = b + 1, a + b);`. Same method, `od -A d -t u1` on
`07_ternary.1.golden`, cross-referenced against `07_ternary.s.golden`'s
text.

**`c0`'s tree shape for `?:` matches real K&R `cc` exactly** —
confirmed via the decode: `NAME(a) NAME(b) GREAT(0) NAME(a) NAME(b)
COLON(0) QUEST(0)`. `GREAT` is the condition (nothing new — the same
comparison codegen `03_rellogic` already established). `COLON`
(opcode `8`) then packages the two already-emitted branch values
(`tr1`=true, `tr2`=false in real `cc`'s tree terms), and `QUEST`
(opcode `90`) combines the condition with the `COLON` pair — a
`QUEST` node's `tr1` is the condition, `tr2` is the `COLON` node,
confirmed purely from emission order (condition emitted first, then
both branches, then `COLON`, then `QUEST` last).

**`c1`'s `?:` codegen is a *new* branch polarity**, not a reuse of
`materialize_cond()`'s existing bare-comparison-as-0/1-value pattern
(confirmed in the `03_rellogic` session):
```
mov  di,*-8.(bp)   | b -> di, setting up the cmp's rhs
cmp  *-6.(bp),di
ble  L10000          | INVERTED condition (a<=b) branches to the FALSE label
mov  di,*-6.(bp)       | TRUE branch: inline at the fallthrough, no jump target of its own
jmp  L10001
L10000:mov di,*-8.(bp)   | FALSE branch
L10001:                    | end: di now holds the selected value
```
Contrast with `materialize_cond()`'s shape for a bare `"r = a < b;"`
(confirmed in `03_rellogic`): `blt Ltrue` (the ORIGINAL, not inverted,
condition branches to a true label), fallthrough sets `di=0`, the true
label sets `di=1`. The difference makes sense once the value being
produced is considered: `materialize_cond()` only ever needs to
produce one of exactly two fixed constants (0 or 1), so it can afford
a shared "set di=1" instruction behind a jump target; `?:` selects
between two *arbitrary* values, so each branch's own load has to live
inline in its own arm rather than behind a shared instruction — the
true branch gets the cheaper "no jump target" treatment since it's the
fallthrough, and the false branch gets its own label. Confirmed the
false branch re-loads `b` into `DI` (`"mov di,*-8.(bp)"`) even though
the comparison's own right-hand-side setup already loaded `b` into
`DI` moments earlier — no cross-branch value tracking, a plain,
unconditional reload in each arm. Label allocation order is
false-then-end (`L10000`/`L10001`) — only two labels, unlike
`gen_logand()`'s three (`03_rellogic`), since there's no separate
"true" label to jump to.

**The comma operator emits *no* code of its own.** Decoding
`"(a = a + 1, b = b + 1, a + b)"`:
```
NAME(a) NAME(a) CON(1) PLUS(0) ASSIGN(0)   | "a = a + 1"
NAME(b) NAME(b) CON(1) PLUS(0) ASSIGN(0)   | "b = b + 1"
SEQNC(0)                                    | combines the two ASSIGNs
NAME(a) NAME(b) PLUS(0)                       | "a + b"
SEQNC(0)                                        | combines with the running result
```
`SEQNC` (opcode `97`) is a pure value-discard: the left operand's side
effects were already emitted by whichever opcode produced it (here,
`ASSIGN`'s own `mov`), so `SEQNC` itself corresponds to **zero**
instructions in the `.s` output — confirmed by there being no
instruction anywhere in the golden that could plausibly correspond to
either `SEQNC` node; the `.s` output for this whole statement is just
the two assignments' own code followed directly by `"a + b"`'s code.

**This surfaced a real gap in the existing grammar**: an embedded
plain assignment (`"a = a + 1"`) used as a comma-item is not reachable
from `parse_expr()`'s precedence chain at all — `assign-stmt`
(handling `=` and the ten compound-assignment operators) is a
distinct, statement-level-only production, only ever invoked from the
top-level statement dispatcher. Supporting `07_ternary.c`'s actual
syntax required a new grammar rule,
`comma-item := (IDENT '=' expr) | expr`, reachable only from inside a
parenthesized list. Telling the two comma-item shapes apart needs to
happen *before* committing to either parse path (an `IDENT` starts
both), so `c0_parser.c`'s `Parser` gained a genuine one-token
lookahead (`peek2_kind()`, backed by a small `Token la; int have_la;`
pair) rather than relying on speculative emission — `NAME`'s wire
encoding happens to be identical whether the identifier turns out to
be an rvalue reference or an assignment's lvalue, so speculative
emission would have worked here, but committing to a real lookahead
mechanism is more honest about what the grammar is actually doing and
generalizes better than relying on that coincidence.

**This also surfaced that `ASSIGN`'s own result value now has a real
consumer, for the first time.** Every prior session's `ASSIGN` handler
in `c1_gen.c` pushed nothing back onto the value stack, with a comment
stating plainly that nothing in this grammar scope ever consumed an
assignment expression's value. `07_ternary.c` breaks that: `"a = a +
1"` as a comma-item needs *something* real on the stack for `SEQNC`
to pop and discard. Fixed by having `OP_ASSIGN` push the already-
computed `rhs` value back (free — it costs no extra instructions,
since `rhs` is already sitting in a register or is a plain immediate,
and it is never actually read again in any confirmed case, only
discarded). This immediately raised a bookkeeping question: does a
*plain* top-level `"a = 4;"` (no comma involved) now leak a value onto
the stack forever, since nothing was previously popping anything at
`EXPR` time? Yes — so `OP_EXPR`'s handler, previously a pure no-op
beyond flushing the postfix-`++`/`--` deferred queue, now discards
exactly one leftover value if present (`gen_fatal` if more than one is
found, to catch a genuine stack-imbalance bug rather than silently
eating it). This correctly generalizes across every statement form
currently supported: `RFORCE` (return-stmt) already consumes its
value without repushing, so there is nothing left for `EXPR` to
discard there; the ten compound-assignment operators still push
nothing, so nothing is left after those either — and c0's own grammar
structure *guarantees* a compound-assignment expression can never
appear anywhere but a statement's sole top-level operator (it is only
reachable from the statement dispatcher, never from `parse_expr()`'s
chain or `parse_comma_item()`), so there is no way for a future test
to need one to push a value the way plain `ASSIGN` now does.

**Separately, `"a = a + 1;"` surfaced a confirmed `+1`-specific
codegen shape**, unrelated to the ternary/comma work above:
```
mov di,*-6.(bp)
inc di            | NOT "add di,*1."
mov *-6.(bp),di
```
`OP_PLUS`'s handler now special-cases an immediate right-hand side of
exactly `1`, emitting a plain `INC` instead of the general `"add
di,<imm>"` shape. `OP_MINUS` is deliberately left untouched (still
`"sub di,*N."` unconditionally, including by 1) — no golden yet shows
`"x - 1"`, so whether the real compiler gives it the symmetric `DEC`
treatment remains unconfirmed rather than guessed.

**Confirmed byte-exact, full pipeline, zero mismatches**: `01_expr/
07_ternary.c` → real `mutos_cpp -P` → `mutos_c0` → `mutos_c1` matches
`07_ternary.i.golden`/`.1.golden`/`.2.golden`/`.s.golden` byte-for-byte
on the first complete implementation attempt (the byte-level `od`
derivation above was, as usual, done *before* writing any
`c0_parser.c`/`c1_gen.c` code). Full-corpus regression confirms zero
regressions elsewhere despite the `ASSIGN`/`EXPR` stack-balance change
touching every assignment statement in the whole corpus: one more file
byte-exact than the prior session's count, one fewer "not yet
supported" than before — `07_ternary` moved from that bucket to the
pass bucket, 0 genuine mismatches.

### `char`/`long`, casts, `sizeof`, and a new MUTOS-specific opcode: byte-level derivation (`01_expr/08_castsize` — `mutos_c0`/`mutos_c1` extended and verified this session)

Last item of `01_expr`: `08_castsize.c` — `long l; char c;` locals,
`i = (int) l; c = (char) i; l = (long) c;`, then `sizeof(int)`/
`sizeof(char)`/`sizeof(long)`/`sizeof(i)`. Same method, `od -A d -t u1`
on `08_castsize.1.golden`, cross-referenced against
`08_castsize.s.golden`'s text — but a much bigger jump than any prior
session: this is a genuine type-system extension (`TY_CHAR=1`/
`TY_LONG=6`/`TY_UNSIGN=7`, already present as named constants in
`mutos_cc.h` from earlier opcode-table transcription but unused until
now), not just a new operator over the existing `int`-only model.

**Frame layout confirms `long` is 4 bytes and `char` still occupies a
full 2-byte slot.** `i=-6` (int, 2B), `l=-10` (long, 4B - matches the
4-byte gap from `i`), `c=-12` (char - only a 2-byte gap from `l`, not
1). This target always word-aligns an AUTO local's stack slot,
matching `int`'s own slot size, even for a byte-sized value — the
1-byte VALUE is read/written within that 2-byte slot via a dedicated
byte-move instruction (`movb`, see below), not by shrinking the slot
itself.

**`long`'s wire/memory layout matches `docs/MUTOS_C_ABI.md` §1.6's
already-documented "high word at the lower address" convention
exactly, down to the constant-encoding level, not just stack
layout.** `"l = 70000;"` (`0x00011170`) decodes to a new opcode,
`LCON` (already named in `mutos_cc.h`, `= 25`, just unused before now)
carrying two 16-bit fields that split as `1` (high) then `4464` (low)
— confirmed against the `.s` output: `"mov si,#4464." / "mov
di,*1."` loads low into `SI`, high into `DI`, then `"mov
*-8.(bp),si" / "mov *-10.(bp),di"` stores low (at `l`'s base offset +
2) before high (at `l`'s own base offset, `-10`) — exactly the ABI's
own "high word at lower address" rule, reproduced at the register/
constant level, not just where the final bytes land in memory.

**A previously-undocumented opcode, `107`, sits between the
already-named `OP_SETREG=105` and `OP_ITOC=109`.** Confirmed absent
from vanilla V7's `c0.h` too (checked directly: nothing is defined at
106/107/108 there either) — a genuine MUTOS-1700-specific addition,
not something earlier opcode-table transcription simply missed.
Decoded from `"l = (long) c;"`'s tree (`NAME(c) <107>(type=6)
ASSIGN(6)`) and named `OP_CTOL` (char-to-long) by the same `XTOY`
convention as every other conversion opcode already in the table
(`ITOL`, `LTOI`, `ITOC`, `ITOF`, `FTOI`, `LTOF`, `FTOL`). Its codegen
is a textbook 8086 sign-extension idiom:
```
movb ax,*-12.(bp)   | the char, loaded into AX specifically
cbw                   | sign-extend AL -> AX
cwd                    | sign-extend AX -> DX:AX
mov  di,dx               | high word -> DI
mov  si,ax                | low word -> SI
```
`AX` (not `DX`/`CX`) is forced here — `CBW`/`CWD` are fixed-register
8086 instructions, `AL`/`AX` only, so this is a hardware necessity,
not a style choice, unlike `ITOC`'s own register pick below.

**Casts decode to one of three confirmed conversion opcodes**, chosen
by (source type, target type), each a single new node between the
operand's `NAME` and the enclosing `ASSIGN`:
- `"i = (int) l;"` → `NAME(l,type=6) LTOI(type=0)`. Codegen: `"mov
  di,*-8.(bp)"` — reads ONLY the low word (base offset `-10`, plus
  `MCC_SZINT`), discarding the high word entirely. Truncation is
  simply "don't read the other word," no masking instruction needed.
- `"c = (char) i;"` → `NAME(i,type=0) ITOC(type=1)`. Codegen: `"mov
  dx,*-6.(bp)"` — loads into `DX` specifically, not the usual `DI`
  "working register" everywhere else. `DI`/`SI` have no
  byte-addressable half on the 8086, and the following `ASSIGN`'s
  `movb` needs one, so this could never have been `DI` regardless —
  but `DX` over `AX`/`CX` isn't itself hardware-forced the way
  `CTOL`'s `AX` is; the golden simply confirms `DX`, so `DX` is what
  `mutos_c1` now hardcodes, without inventing a broader "byte
  operations always use DX" theory beyond what's shown.
- `"l = (long) c;"` → `CTOL` as above.

Real C's cast-expression grammar allows a full unary-expr operand;
`08_castsize.c`'s three casts are all bare variables, so
`c0_parser.c`'s `cast-expr` production is narrowed to exactly that
(`'(' ('int'|'char'|'long') ')' IDENT`) rather than guessing how a
more complex operand would need to flow through these new opcodes.
Disambiguating a cast's leading `'('` from the existing
parenthesized-comma-list `'('` (from the `07_ternary` session) reuses
that session's `peek2_kind()` lookahead: a type keyword immediately
after `'('` means cast, anything else means comma-list/grouping —
resolved before emitting anything, no backtracking needed.

**`OP_ASSIGN` gained two more type-dispatched shapes.** `TY_CHAR`
reuses the existing single-`mov`-instruction path but with `"movb"` in
place of `"mov"` — confirmed via `"c = (char) i;"`'s `"movb
*-12.(bp),dx"`. `TY_LONG` is a genuinely different shape, handled
first and separately: the right-hand side must be the new `VK_LONG`
marker (a value sitting in `DI`(high)`:SI`(low) — the fixed
convention both `LCON` and `CTOL` produce), and the left-hand side
gets TWO stores, low word (`offset+MCC_SZINT`) before high word (own
base offset) — confirmed identical in both `"l = 70000;"` and `"l =
(long) c;"`.

**`sizeof(...)` never emits a wire opcode of its own at all — not even
`OP_SIZEOF` (already named in `mutos_cc.h`, unused).** All four
`sizeof(...)` calls in `08_castsize.c` — `sizeof(int)`, `sizeof(char)`,
`sizeof(long)`, and `sizeof(i)` (a *variable*, not a type name) — fold
directly to `CON(TY_UNSIGN, <size>)` at parse time: `2`, `1`, `4`, and
`2` respectively. For `sizeof(i)`, `i`'s own `NAME` is never emitted
anywhere on the wire — confirming `sizeof`'s operand is genuinely
never evaluated, only its type inspected, exactly matching real C
semantics (`sizeof` is famously one of the few C constructs that
doesn't evaluate its operand). `CON`'s type field being `TY_UNSIGN`
rather than `TY_INT` here (`sizeof` yields an unsigned type in real C)
required widening `OP_CON`'s own type check in `c1_gen.c` to accept
`TY_UNSIGN` alongside `TY_INT` — numerically identical codegen either
way in this scope, the constant just flows through as a plain 2-byte
immediate; the enclosing `ASSIGN`'s own type argument (following the
*lvalue*, an ordinary `int`, per the convention established back in
the `05_incdec` session) is what actually governs the store
instruction, not `CON`'s own type tag. Since `c0_parser.c`'s `ExprVal`
carries no type tag at all (only `is_const`/`is_long`/`value`),
`sizeof` is parsed as an *immediate, unconditional* emission
(`outcode()` called directly, returning `ev_dynamic()`) rather than
deferred as a further-foldable constant the way every other constant
expression in this grammar is — the simplest correct choice given
no golden exercises combining a `sizeof(...)` result with anything
else at parse time (e.g. `sizeof(int) + 1` would not itself constant-
fold here, though it would compile: the `CON(TY_UNSIGN,2)` gets
emitted immediately, then `PLUS` combines it with a materialized
`CON(TY_INT,1)` at runtime like any other non-constant addition).

**Large integer literals are automatically promoted to `long`**,
standard K&R/C89 constant promotion (K&R2 §A2.5.1: a decimal constant
too large for `int` becomes `long`) — confirmed via `"l = 70000;"`
using `LCON` rather than a truncated `CON`. `c0_parser.c`'s `ExprVal`
gained an `is_long` flag for exactly this: `parse_primary()`'s
`T_ICON` handling checks the raw (pre-`trunc16()`) literal value
against `32767` (the largest value representable in a 16-bit signed
`int`) and keeps the full, untruncated value when it doesn't fit;
`emit_materialize()` checks the flag and emits `LCON`'s two-word split
instead of a plain `CON`. This flag is never propagated through any
arithmetic combinator (`PLUS`, `MINUS`, etc.) — no golden exercises
`long` arithmetic, only a bare literal, directly and immediately
assigned, so a long-typed intermediate combined with another operator
remains explicitly unsupported territory (`02_long` proper, next).

**Confirmed byte-exact, full pipeline, zero mismatches**: `01_expr/
08_castsize.c` → real `mutos_cpp -P` → `mutos_c0` → `mutos_c1` matches
`08_castsize.i.golden`/`.1.golden`/`.2.golden`/`.s.golden` byte-for-byte
on the first complete implementation attempt (the byte-level `od`
derivation above was, as usual, done *before* writing any
`c0_parser.c`/`c1_gen.c` code). Full-corpus regression confirms zero
regressions elsewhere: one more file byte-exact than the prior
session's count, one fewer "not yet supported" than before —
`08_castsize` moved from that bucket to the pass bucket, 0 genuine
mismatches. **This completes `01_expr` (all 9 files) end-to-end.**

### `02_long/02_muldiv` — `long` `*`/`/`/`%` via runtime helpers

First construct in `02_long`'s own scope (`long` locals/casts/constants
already existed, from `08_castsize`). As always, byte-decoded
`02_muldiv.1.golden`/`.s.golden` (`od`) *before* writing any
`c0_parser.c`/`c1_gen.c` code — and doing so this time turned up a real
divergence from `docs/MUTOS_C_ABI.md` sect. 1.8's own prose, not just new
opcode plumbing.

**Two real gaps, found before any codegen work even started, since
`mutos_c0` itself rejected the file:**
1. A pre-existing lexer/parser bug, orthogonal to `long` arithmetic:
   `parse_primary()` had no case for `T_LCON` — an integer literal with an
   explicit `l`/`L` suffix. `08_castsize`'s prior session only ever
   exercised `long`-promotion via *magnitude* (`70000`, too big for `int`,
   auto-promoted per K&R/C89), which lexes as `T_ICON` with `is_long`
   unset at the token level (the promotion decision happens later, in
   `parse_primary()` itself). `37L` — small enough to fit a plain `int`,
   but carrying an explicit suffix — lexes as a genuinely different token
   kind, `T_LCON`, which nothing in `parse_primary()` handled at all:
   `"expected an expression, found long constant"`. Fixed with a
   dedicated `T_LCON` case that always returns a `long`-typed constant
   regardless of magnitude, reusing the same `ev_const_long()`/
   `emit_materialize()` path the magnitude-promotion case already used.
2. `ExprVal` tracked `is_long` only for the constant-folding path (per
   the `08_castsize` entry above, explicitly flagged there as "never
   propagated through any arithmetic combinator"). A `NAME` reference to
   a `long` local — a fully dynamic, already-emitted value — carried no
   type information at all, so `parse_mul()`'s `*`/`/`/`%` always
   hardcoded `OP_TIMES`/`OP_DIVIDE`/`OP_MOD`'s type argument to `TY_INT`.
   Added a `type` field to `ExprVal` (set to `TY_LONG`/`TY_INT`
   consistently for both the const and dynamic cases — a `NAME`'s `type`
   field comes straight from its own `SymEntry.type`), and had
   `parse_mul()` pick `TY_LONG` whenever either operand's `type` is
   `TY_LONG`. Confirmed against `02_muldiv.1.golden`'s `"c = a * b;"`
   tree (`a`, `b`, `c` all `long`) — the wire shape itself
   (`NAME`/`NAME`/`TIMES`/`ASSIGN`/`EXPR`) needed zero new opcodes, only
   the correct type argument on `TIMES`. Deliberately *not* extended to
   `parse_add()`'s `+`/`-` — `02_long/01_addsub.c` (the file that would
   confirm it) also needs `if`, `03_ctrlflow` scope, not yet built, so
   there is no golden to verify a `long` `+`/`-` codegen shape against
   this session; guessing it would violate this project's own
   verification rule.

**The actual confirmed `c1` calling convention differs from
`docs/MUTOS_C_ABI.md` sect. 1.8's own prose** — worth flagging explicitly
since that section is written with some hedging ("presumably... both
patterns are present") rather than as a flat confirmed fact. Real
generated code for `"c = a * b;"` (and `/`, `%`) calls plain `lmul`/
`ldiv`/`lrem` — never `almul`/`aldiv`/`alrem` — with **both operands
passed flat**, as two ordinary two-word `long`s per sect. 1.6, right-to-
left per sect. 1.1: push `b`'s low word, `b`'s high word, `a`'s low word,
`a`'s high word (4 words, 8 bytes — matching the golden's `"add
sp,*8."`), `call lmul`, then the result comes back in `DX:AX` per sect.
1.5's ordinary `long`-return convention — never through a pointer/
lvalue-and-result parameter the way sect. 1.8's prose speculates library
helpers of this shape might work. `DX:AX` is then moved into `DI`(high)`:
SI`(low) (`mov di,dx` / `mov si,ax`), the same convention `OP_LCON`/
`OP_CTOL` already produce, so `OP_ASSIGN`'s existing `TY_LONG` case
(unchanged) stores it correctly with no new logic needed there.
Implemented as one shared `gen_long_binop_call()` helper in `c1_gen.c`,
parameterized only by the helper name (`"lmul"`/`"ldiv"`/`"lrem"`) —
`MOD`'s result comes back the same `DX:AX` → `DI:SI` way as `TIMES`/
`DIVIDE`, unlike the plain-`int` `DIVIDE`/`MOD` split just above it in
the same file (quotient in `AX`, remainder in `DX`, no helper call at
all) — there is only one long helper call per operator here, not a
shared call whose result register differs by operator. Only a plain
memory (`NAME`) operand is confirmed for either side — `02_muldiv.c`
never nests a long expression or uses an immediate operand here.

**`OP_LCON`'s `c1` codegen turned out to have two real, distinct
confirmed shapes, not one** — invisible in the `08_castsize` session
because its only two `LCON` producers (`"l = 70000;"` and the `CTOL`
sign-extension path) both happened to need the direct split. This
session's `"b = 37L;"` sits right next to `"a = 123456L;"` in the same
golden and takes a visibly different shape:
- `123456L` (`hi=1, lo=-7616` — `hi` is *not* the sign-extension of
  `lo`, since `lo` is negative and `hi` isn't `-1`): direct split, `mov
  si,<lo> / mov di,<hi>` (the pre-existing, unchanged shape).
- `37L` (`hi=0, lo=37` — `hi` *is* `lo`'s sign-extension, an ordinary
  int-range value that merely carries a `long` suffix/type): `mov
  ax,<lo> / cwd / mov di,dx / mov si,ax` — reusing the exact
  sign-extension idiom `OP_CTOL` already established for `char`→`long`,
  just starting from a plain immediate load into `AX` instead of a
  `movb`.

  The dispatch condition implemented in `c1_gen.c`: `hi == (lo < 0 ? -1
  : 0)` picks the sign-extension shape, else the direct split. Confirmed
  the `08_castsize` cases still take their original (direct-split) path
  unchanged (`70000`'s `hi=1` doesn't equal `0`, so no behavior change
  there) — full-corpus regression run confirmed this explicitly, not
  just assumed from the logic.

**Confirmed byte-exact, full pipeline, zero mismatches**: `02_long/
02_muldiv.c` → real `mutos_cpp -P` → `mutos_c0` → `mutos_c1` matches
`02_muldiv.i.golden`/`.1.golden`/`.2.golden`/`.s.golden` byte-for-byte —
this one *did* need one golden-mismatch iteration (the `OP_LCON`
sign-extension-vs-direct-split distinction above was only found by
diffing the first codegen attempt's `.s` output against the golden line
by line; the `T_LCON` parser fix and the `TIMES`/`DIVIDE`/`MOD`
long-helper-call codegen were both correct on the first attempt, derived
from the `od` dump beforehand as usual). Full-corpus regression
(`tests/mutos_cc/run_goldens.sh`) confirms zero regressions elsewhere:
one more file byte-exact than the prior session's count, one
fewer "not yet supported" than before (`02_long/02_muldiv` moved from
that bucket to the pass bucket), 0 genuine mismatches anywhere.
`02_long/01_addsub.c`, `03_retval.c` and `04_params.c` were re-checked
and confirmed to still fail with their same pre-existing diagnostics
(unrelated to this session's changes — blocked on `03_ctrlflow`/
`04_funcs`, not on `long` arithmetic).

### `03_ctrlflow` (all 7 files) — a real recursive-descent statement dispatcher, plus `02_long/01_addsub` falls out for free

The biggest single jump in grammar coverage so far: `mutos_c0`'s prior
statement handling was a flat `while` loop recognizing exactly three
shapes (`return`, plain assignment, `*`-assignment). Every construct in
this category needed real recursive-descent — a statement can itself
contain statements — so `parse_compound_stmt()`'s inline dispatch became
a proper `parse_statement()` function, called recursively by every
construct that has a body. As always, every `.1.golden`/`.s.golden` in
the category was byte-decoded via `od` before writing any `c0_parser.c`/
`c1_gen.c` code.

**`if`/`else` (`01_ifelse`).** `v7/cc/c02.c`'s `statement()` IF case
translates directly: `CBRANCH(false_lab, cond=0)` right after the
condition skips the true-branch when it's false; `false_lab` is
allocated immediately (`o1 = isn++`), but a second label (`end_lab`) is
only allocated if an `else` actually follows (`o2 = isn++` inside the
`if (...==ELSE)` check) — confirmed against a 2-level `else if` chain
allocating exactly this way (4,5 for the outer if; 6,7 for the inner,
recursed into as the outer's own else-branch). The one real surprise:
`CBRANCH`'s `line` argument is the line of whatever token follows the
condition's `)`, NOT the `if` statement's own line — confirmed via
`01_ifelse.1.golden`, whose first `CBRANCH` carries line 10 (where the
true-branch's `r = 1;` sits) even though `if (a > 0)` itself is on line
9. Tracing through `v7/cc/c02.c`'s actual `IF` case explains why: right
after parsing the condition, it does `o1 = symbol()` — reading ONE more
token — specifically to check whether the body is a bare
`goto`/`return`/`break`/`continue` (the "simpif" shortcut, see below).
That extra lookahead read advances the global `line` variable before
`cbranch()` is ever called. Since `mutos_c0`'s own parser structure
(`p->cur`/`p->la`) already sits on that next token once `)` is consumed
— no lookahead trick needed — this fell out for free: capturing
`p->cur.line` right after `expect(T_RPAREN, ...)` reproduces it exactly.
`while`/`do`-`while` were checked too (see below) to confirm this is
specifically an `if`-only quirk, not a general "line lags by one token"
rule.

**`while`/`do`-`while` (`02_while`/`03_dowhile`).** `while` is the
textbook 2-label shape: `LABEL(top)` (doubling as `continue`'s target)
before the test, `CBRANCH(end, cond=0)` after it, body, `BRANCH(top)`,
`LABEL(end)` (`break`'s target) — confirmed byte-for-byte on the first
attempt. `do`-`while` allocates THREE labels up front, in a fixed order
that doesn't match where they're placed: `contlab = isn++` first,
`brklab = isn++` second, then the body's own top-of-loop label third
(`label(o3 = isn++)`) — but only the top label is placed immediately
(right after `do`); `contlab` isn't placed until AFTER the body, right
before the trailing condition test — confirmed via `03_dowhile.1.
golden`'s label sequence (`LABEL(6)` = top, at the very start; `LABEL(4)`
= contlab, right after the body; `LABEL(5)` = brklab, at the very end).
The trailing `CBRANCH` branches back to the top label on cond=1 (branch
if TRUE), the opposite polarity from `if`/`while`'s cond=0. Neither
construct's own `CBRANCH` line shifts the way `if`'s does — no extra
lookahead happens for either in v7/cc, so `line` is simply wherever the
condition's own closing `)` sits (captured here BEFORE calling
`expect(T_RPAREN, ...)`, the mirror-image of `if`'s "after" capture).

**`for` (`04_for`) — the deferred-increment-emission trick.** This one
needed real design work, not just transcription. `v7/cc/c02.c`'s
`forstmt()` does something no other construct here does: when an
increment clause is present, it is PARSED immediately (`st = tree();`)
to keep the token stream in order, but its tree is stashed rather than
emitted, `contlab` is REASSIGNED to a fresh label (`l = contlab; contlab
= isn++;` — so `continue` now targets a point right before the
increment, not the original test label), the body is parsed next, and
only THEN — after the body — does `rcexpr(st)` finally emit the
increment's code, with `line` explicitly saved and restored around it
(`sline = line; ...; line = sline; ...; line = sline1;`) so the
increment's own `EXPR` opcode keeps the SOURCE line it was written on
(the `for`-header's own line), not wherever body-parsing left `line`.
Confirmed unambiguously via `04_for.1.golden`: the increment `i = i + 1`
(written on line 9, the `for`-header's own line) carries `EXPR(9)` even
though it is emitted, in the byte stream, AFTER the body (whose own
statement is on line 10) — and confirmed again, independently, for BOTH
loops of `05_breakcont.c`'s nested-for case (each increment keeps its
own header's line despite sitting after a body that itself contains
another entire nested loop). Real `cc` can do this trivially because it
already has a full expression tree to re-walk later (`rcexpr(st)`);
`mutos_c0` streams wire bytes as it parses and has no tree to defer. The
fix: parse the increment into an `open_memstream()` buffer instead of
the real output, capture its own line first, then — after the body is
parsed normally — flush the buffered bytes verbatim. `_POSIX_C_SOURCE
200809L` needed defining first (same as `mutos_as`/`mutos_ld` already do
elsewhere) for `open_memstream()`'s declaration to be visible under
`-std=c11`.

**`break`/`continue`, confirmed nested (`05_breakcont`).** `p->brklab`/
`p->contlab` are plain ints on the `Parser` struct, each loop/switch
construct saving the enclosing value locally and restoring it after its
own body — nesting requires no explicit stack at all, since C's own
call stack already provides one via the recursive `parse_statement()`
calls. Confirmed via the nested-for case's exact label numbering: the
outer `for` allocates labels 4 (test), 5 (break), 6 (its own reassigned
continue-target, since it has an increment); parsing recurses into the
outer's body, where the inner `for` continues the SAME counter from 7,
8, 9 — proving the outer's `p->contlab`/`p->brklab` were never
clobbered by the inner loop's own save/restore around itself.

**The "simpif" shortcut — confirmed via TWO different files.**
`v7/cc/c02.c`'s `IF` case has an inner `switch` on the token immediately
following the condition: if it's `goto`/`break`/`continue` (or a bare
`return;`, not exercised by any golden here), the whole `if` compiles to
a SINGLE `CBRANCH(target, cond=1)` — no extra label, no separate
branch-around-the-body shape at all. `target` is the `goto`'s own label
(or the enclosing `brklab`/`contlab`). This surfaced from two directions
at once: `07_goto.c`'s `"if (i >= 10) goto done;"` and `05_breakcont.c`'s
`"if (j == 3) break;"` / `"if (i == j) continue;"` — all three take this
shortcut, confirmed by each compiling to exactly one `CBRANCH`, never the
general two-label shape. `05_breakcont.1.golden` also pinned down the
CBRANCH's `line` precisely: for `"if (j == 3)\n    break;"` (condition
and body on DIFFERENT source lines, unlike every other confirmed `if` in
this corpus), the `CBRANCH`'s line is 15 — the `break;`'s own line, not
14 where `if (j == 3)` sits. This confirms (rather than merely being
consistent with) the "line of the token immediately following the
condition" rule established for `01_ifelse`, since here the two
candidate lines actually differ.

**`goto`/labels (`07_goto`).** A small function-scoped table (name →
intermediate-code label number) resolves both directions of reference:
`"loop:"` is DEFINED before any `goto` reaches it (allocated at
definition time), while `"done:"` is REFERENCED (via a forward `goto
done;`) before its own definition appears later in the source
(allocated at first reference instead). Both paths go through the same
`label_for_name()` helper — find-by-name, or allocate a fresh `p->isn++`
or first mention — so which direction each reference happens to be
never needs special-casing. `07_goto.1.golden` confirms both: `"loop:"`
becomes label 4 (right after the fixed `sloc`/`sloc+1`/`retlab` trio),
`"done:"` becomes label 5 (allocated when the forward-referencing `goto`
is parsed, before `"done:"` itself is ever seen).

**`switch`/`case`/`default` (`06_switch`) — a real jump table.** The
deepest single addition in this category. `v7/cc/c02.c`'s `pswitch()`
translates directly: the controlling expression is wrapped in `RFORCE`
(the SAME "force into the return-value convention" wrapper `do_return_
stmt()` already uses for `return`'s own expression) and emitted as an
ordinary expression-statement — confirmed via `06_switch.1.golden`'s
`NAME(x) / RFORCE(TY_INT) / EXPR(14)` sequence, matching `return`'s own
shape exactly except for what follows. A `BRANCH` then jumps PAST the
entire switch body to a fresh dispatch label (`swlab`); the body is
parsed inline as an ordinary `statement()` call, with `case`/`default`
simply placing a fresh label and recording either a `(label, value)`
pair (case) or the label alone (`default`, into `p->deflab`) before
falling through to whatever statement follows — confirmed via the
source's deliberate `case 2: case 3:` stack (two labels back-to-back
with no code between) and `case 3:`'s fallthrough into `case 4:`'s body
(no `break`, matching the source comment `/* fall through */`) both
compiling with zero extra machinery, exactly the "labels are cheap,
fallthrough is just not branching" semantics real C has. Once the body
is fully parsed, `pswitch()` emits one more unconditional `branch(brklab)`
(a safety net for a body that falls off the end without its own break),
places `swlab`, defaults `deflab` to `brklab` if no `default:` was seen,
then emits `OP_SWIT(deflab, line)` followed by every collected
`(label, value)` pair and a single LONE zero word as a terminator — NOT
a `(0, 0)` pair, confirmed by `outcode("0")` in the real source (a
one-character format string that always emits exactly one zero-valued
word, consuming no argument) and by the golden's own trailing byte
sequence (`... N 9 N 4 N 0`, i.e. a normal pair followed by one lone
zero, not two). One new field was needed to get `OP_SWIT`'s own `line`
argument right: it's confirmed to be the switch body's own CLOSING `}`
(line 28 in the source, `06_switch.1.golden`'s `OP_SWIT` carries exactly
that), but by the time `pswitch()`-equivalent code regains control after
its own `parse_statement()` call, `p->cur` has already moved past that
closing brace to whatever follows. Added `p->prev_line` — the line of
the token most recently consumed, updated inside `advance()` itself — to
recover it generically, without needing every construct that might need
this to hand-thread it through explicitly.

`c1`'s dispatch codegen is where the real surprise lives — this is a
genuine, worked-out jump table, not a compare-chain:
```
sub ax,#/1          ; normalize to 0-based (subtract the min case value)
cmp ax,*3.           ; range check: max - min
bhi L10              ; unsigned-above (out of range) -> default's label
shl ax,#1            ; scale index to a word offset
xchg bx,ax
seg cs
jmp @L10001(bx)      ; indirect jump through the table
L10001:L6            ; the table: one case-body label per word,
L7                   ; ascending by case value
L8
L9
```
AX already holds the switch value at this point (loaded by `RFORCE`
earlier). Two things needed real investigation before this could be
transcribed: first, `sub ax,#/1` — that `#/1` is not a typo or a stray
character; `man/mutos_as.1` documents a leading `/` as introducing a
HEX literal (`/FF`, `/2A0`), so this is simply decimal 1 written in hex
— the ONE confirmed immediate in this entire codebase rendered that way,
everywhere else uses the established decimal `*N.`/`#N.` convention.
Second, `@disp(reg)` (documented in the same manpage section) is
`mutos_as`'s memory-indirect jump syntax — "the word stored at
`disp+reg` is the real destination" — exactly the classic jump-table
dispatch idiom. `bhi` (branch if higher, unsigned-above) is a new
mnemonic, used only for the out-of-range check; `xchg`/`seg` are also
new but used completely literally, with no parameterization needed. One
quirk is confirmed but NOT explained by this single example: the table's
own internal label is `L10001`, not `L10000`, even though this switch is
the ONLY internal-label consumer anywhere in the file (nothing else uses
`GenState.next_lab`) — meaning one label is silently burned before the
real one is allocated, for a reason this corpus doesn't reveal. Only the
DENSE, CONTIGUOUS case-value shape (`1,2,3,4`, no gaps) is implemented;
a sparse switch would need a real compare-chain fallback that no golden
confirms, so that stays explicit "not yet supported" per this project's
standing rule against guessing.

**`02_long/01_addsub.c` fell out of implementing `if`, but needed three
more genuinely new pieces to actually complete** — each investigated
and confirmed independently before being implemented:

1. **An implicit `int`→`long` widening opcode (`ITOL`), a genuine gap,
   not a guess.** `"b = 23456;"` (`b` declared `long`, but `23456` has no
   `L` suffix and fits a plain `int`) takes the ordinary `CON` path, not
   `LCON`'s — confirmed via `01_addsub.1.golden`'s `CON(TY_INT, 23456)`
   immediately followed by a previously-unused opcode, `ITOL` (already
   named in `mutos_cc.h`, unused until now), before `ASSIGN`. `c1`'s
   codegen for it is the exact same `CWD` sign-extension idiom already
   established for `OP_CTOL` and `materialize_long()`'s in-range branch,
   just starting from a plain immediate/memory operand via
   `render_operand()` instead of a `movb`.
2. **`long` `+`/`-`, with TWO distinct confirmed shapes depending on the
   right operand.** `OP_PLUS`/`OP_MINUS` now carry `TY_LONG` when either
   operand is `long` (`parse_add()` gained the same type-propagation
   `parse_mul()` already had from the `02_muldiv` session). `"c = a + b;"`
   (both plain `long` locals) loads the left operand into `DI:SI` via two
   direct memory-to-register `mov`s, then `ADD`/`ADC` read the right
   operand straight out of ITS memory location — no registers needed for
   it at all. `"c = c + 1L;"` (a `long` CONSTANT right operand) is
   genuinely different and more roundabout: the constant is sign-extended
   into `DX:AX` first, then PUSHED to the stack (low word, then high) —
   seemingly just to free `DX:AX` before the left operand gets loaded
   into `SI:DI` — then popped back into `BX`(high)`:CX`(low), and only
   then does the same `ADD`/`ADC` pair run, this time against `CX`/`BX`
   instead of memory. This was confirmed down to a literal real-hardware
   inconsistency: the golden's second `pop` renders as `"pop cx"` — a
   plain space, not the tab every other instruction here uses — verified
   with `cat -A` against the raw golden bytes to rule out a display
   artifact before reproducing it verbatim.
3. **`long`-vs-constant relational comparison — a real 3-way branch, and
   a redesign of `OP_LCON` to make it possible.** `"if (c > 0L)"`
   compiles to nothing like the ordinary 16-bit `cmp`+branch pair:
   ```
   cmp <c-high>,*0
   blt <false-label>       ; high < 0 -> definitely c <= 0
   bgt <fresh-true-label>  ; high > 0 -> definitely c > 0
   cmp <c-low>,*0.         ; high == 0 -> only the low word decides,
   blos <false-label>      ; compared UNSIGNED (blos = "lower or same")
   <fresh-true-label>:
   ```
   This is the textbook technique for a 32-bit signed comparison on a
   16-bit ALU: compare high words SIGNED first (which alone decides the
   answer whenever they differ), only falling through to an UNSIGNED
   low-word compare when the high words are equal (since the sign is
   already settled at that point). Only this ONE shape — `OP_GREAT`
   against a literal `0L`, consumed at a "branch if false" `CBRANCH`
   site — is confirmed; every other operator, a non-zero or non-constant
   right operand, and a "branch if true" site are all explicit
   `gen_fatal()`s rather than guesses, since a different operator would
   need a genuinely different (and currently unconfirmed) three-way
   shape. Getting here required noticing that a comparison's own wire
   `type` argument is confirmed to ALWAYS be plain `TY_INT`, regardless
   of operand type (`01_addsub.1.golden`'s `GREAT` node carries type 0,
   not 6) — a comparison's RESULT is always `int` in C, operand type
   plays no part in this field at all. So `long`-ness has to be detected
   from the OPERANDS themselves instead, which meant `OP_LCON` — until
   now always eagerly emitting its `mov`/`cwd` sequence the instant it's
   read — had to become LAZY: it now pushes a raw, unmaterialized
   `VK_LCON(hi, lo)` marker with NO code emitted at all, since a `long`
   constant used as a comparison operand never loads into registers in
   the real output — it folds directly into a bare `cmp` immediate
   instead (`materialize_long()`, a new helper, only actually emits the
   `mov`/`cwd` sequence when something — so far, only `OP_ASSIGN`'s
   long-target case — genuinely needs a real `DI:SI` value). This
   redesign was checked for backward compatibility before being trusted:
   in every already-confirmed golden using `LCON` (`02_muldiv`,
   `08_castsize`), `ASSIGN` was already always the very next opcode with
   nothing in between, so moving the materialization from "the instant
   `LCON` is read" to "the instant `ASSIGN` consumes it" produces
   byte-identical output either way — re-verified by re-running the full
   suite, not just assumed from the logic.

**A real, pre-existing bug, unrelated to `long` or control flow, was also
found and fixed along the way.** `CMP`'s immediate right-hand side was
believed (from the `03_rellogic` session) to universally omit the
trailing `"."` decimal-terminator, the same way `SAL`/`SAR`'s shift-count
operand does. Re-checking `03_rellogic.s.golden` directly (`grep`ing
every `cmp` line) shows every single confirmed immediate CMP there is
value `0` specifically (`"cmp *-6.(bp),*0"`) — `03_ctrlflow`'s new,
non-zero cases (`"cmp *-6.(bp),*10."`, `"*5."`, `"*3."`) confirm the
period IS present for every other value, matching `render_operand()`'s
ordinary convention exactly. `render_bare_imm()` (still correct, and
still the right function, for `SAL`/`SAR`'s own confirmed no-period
shift-count) was left alone; a new, correctly-scoped `render_cmp_imm()`
(zero → no period, matching the one genuinely confirmed case; anything
else → period, matching every newly-confirmed non-zero case) replaced it
at `CMP`'s own call site.

**Confirmed byte-exact, full pipeline, zero mismatches, for all 8 files**
(all 7 of `03_ctrlflow` plus `02_long/01_addsub`) — every one matched its
golden on the FIRST complete implementation attempt except `06_switch`
(whose jump-table shape needed one real investigation pass, documented
above, before the codegen was written) and the `CMP`-immediate fix above
(found via the full-suite regression run after `03_ctrlflow`'s other six
files already passed, not derived up front). Full-corpus regression
(`tests/mutos_cc/run_goldens.sh`) confirms zero regressions elsewhere:
20 of 62 byte-exact end-to-end (up from 12 at the start of this session), 0
genuine mismatches anywhere. `02_long/03_retval.c` and `04_params.c`
were re-checked and confirmed to still fail with their same pre-existing
diagnostics — both need function calls/parameters (`04_funcs` scope),
not more control-flow or `long`-arithmetic work.

### `04_funcs` (6 of 7 files) — function parameters, calls, local `static` variables, and function pointers

**Scope this session:** `04_funcs/01_call.c`, `02_manyargs.c`,
`03_recfact.c`, `04_mutrec.c`, `05_staticvar.c` and `07_funcptr.c`
(`06_regclass.c` deliberately deferred — see its own note at the end of
this section). `02_long/04_params.c` fell out for free once parameters
were done, the same way `01_addsub` fell out of the `03_ctrlflow` session.
Method: `dump_temp.py` was temporarily extended (in a scratch copy) with
speculative `COMMA`/`CALL`/`MCALL` shapes, decoded against every
`04_funcs/*.1.golden`, and the results checked for a byte count that
exactly consumes the file and ends in a clean `EOFC` — the same
confirmation method every prior session in this milestone used.

**Parameter offsets and `ANAME` ordering (`01_call`, `02_manyargs`).**
`v7/cc/c02.c`'s `funchead()` was read for the real algorithm shape: for
each parameter, `pl` starts at `STARG` (v7's own value is also 4, so no
MUTOS-specific delta here — unlike `STAUTO`), and each parameter's own
offset is taken *before* `pl += rlength(param)`, the mirror image of an
AUTO local's "subtract first, then take the offset" order. `funchead()`
also structurally runs *before* `cfunc()`'s own `branch(sloc);
label(sloc+1);` pair. Both were confirmed byte-for-byte:
`01_call.1.golden` (`add(a, b)`, both `int`) decodes as `SYMDEF, PROG,
EVEN, RLABEL, SAVE, SETREG(4), ANAME(_a,4), ANAME(_b,6), BRANCH(1),
LABEL(2), ...` — `ANAME` for both parameters sits between `SETREG` and
`BRANCH`, confirming the funchead-before-branch/label structural order;
`02_manyargs.1.golden` (`sum6`, six `int` parameters) confirms the
offset sequence continues linearly: `4, 6, 8, 10, 12, 14`. Implemented in
`c0_sym.c`'s new `symtab_declare_param()` (a `paramlen` counter, mirror
image of the existing `autolen`) and `c0_parser.c`'s new
`parse_param_decls()`, called from `cfunc()` between `SETREG` and
`branch_op(sloc)`.

**Call callee and argument-list wire shape.** `v7/cc/c04.c`'s `treeout()`
`CALL` case: `treeout(tr1, 1); treeout(tr2, 0); outcode("BN", CALL,
tp->type);` — callee first, then the argument tree, then the `CALL` tag
itself. The callee (`tr1`) is a `NAME` leaf: `01_call.1.golden` byte 132
decodes as `NAME hclass=SC_EXTERN(12) type=FUNC.TY_INT(16)
name="_add"` (as `dump_temp.py` renders it today - it printed the
misleading `TY_INT ptr×2(16)` until its type decoder learned the full
derived-type chain in the `05_arrptr/02_array2d` session) — `type=16` is `TY_INT | FUNC` (`v7/cc/c0.h`: `FUNC=020`
octal = 16 decimal, `PTR=010` octal = 8 — the two `XTYPE` derived-type
tags this project already used, `PTR` for `05_incdec`'s pointers, are
now both directly confirmed distinct). The argument tree (`tr2`) for
`01_call`'s `add(3, 4)` decodes as `CON(3), CON(4), COMMA(TY_INT)` —
matching `v7/cc/c00.c`'s expression parser (`case COMMA: if (*op !=
CALL) os = SEQNC;` — inside a call's argument list specifically, a bare
comma token becomes a real `COMMA` tree node, `tnode(COMMA, INT, tr1,
tr2)`, left-associatively chaining each additional argument onto the
previous ones: `02_manyargs.1.golden`'s six-argument `sum6(1,2,3,4,5,6)`
decodes as `CON(1), CON(2), COMMA, CON(3), COMMA, CON(4), COMMA, CON(5),
COMMA, CON(6), COMMA` — a left-leaning `((((((1,2),3),4),5),6)` tree,
exactly matching a postorder walk of that shape. A single argument
(`03_recfact.1.golden`'s `fact(6)`) uses no `COMMA` at all — just
`CON(6)` directly as `tr2`. Zero arguments (`05_staticvar.1.golden`'s
`counter()`) uses a single `NULLOP`(218) leaf — `v7/cc/c04.c`'s
`treeout()` top: `if ((tp = atp) == 0) { outcode("B", NULLOP); return;
}` — the null-subtree shape, applied here since `tr2` is a null pointer
for a no-argument call. Implemented in `c0_parser.c`'s new
`parse_call()` (called from `parse_primary()`'s `T_IDENT` branch when
`peek2_kind() == T_LPAREN`) and a shared `parse_call_args_and_emit()`
tail (reused by the indirect-call path — see below). On the `c1` side:
a new `VK_FUNC` kind (`OP_NAME`'s `SC_EXTERN` branch, reading a symbol
via `c1_read_sym()` instead of a numeric offset) and a new `VK_ARGLIST`
kind (a fixed-capacity, heap-owned array of resolved argument `Val`s,
built by `OP_COMMA`'s handler — left `VK_ARGLIST` or plain, right always
plain, matching the left-associative wire shape — and by `OP_NULLOP`,
which pushes an empty one) feed a new `gen_call()`.

**Push order, argument rendering, and caller cleanup.**
`docs/MUTOS_C_ABI.md` sect. 1.1 (right-to-left push, caller cleanup via
`add sp,N`) was already fully researched from real `libc.a`/`crt0.o`
evidence before this session — this session's job was confirming
`mutos_c1`'s actual rendering choices, not the ABI itself.
`01_call.s.golden`'s `add(3, 4)` call renders as `mov di,*4./push
di/mov di,*3./push di/call _add/add sp,*4.` — pushed in REVERSE
declaration order (`4` then `3`), each via `mov di,<val>` then `push
di`, confirming the ABI's right-to-left rule directly in generated
code. `02_manyargs.s.golden`'s `sum6(1,2,3,4,5,6)` confirms this holds
for all six (pushed `6,5,4,3,2,1`) and that cleanup is `add sp,*12.`
(2×6 bytes). `07_funcptr.s.golden`'s `apply(fp, 5)` — args `fp` (a
plain local) and `5` — pushes `5` via the usual `mov di,*5./push di`
but `fp` via a DIRECT `push *-6.(bp)`, no `mov` at all: this is a
genuinely different, more general rule than every earlier call site
happened to exercise (which were all either immediates, needing `DI`
first since 8086 `PUSH` has no immediate form, or an already-`DI`
value from a preceding `MINUS`, where the pre-existing `load_into_di()`
no-op check already produced identical bytes either way — so this
generalization is a strict superset, not a behavior change, for every
prior confirmed site). `gen_call()` now pushes an argument AS-IS
(`render_operand()` directly) whenever it isn't `VK_IMM`, and routes
only a genuine immediate through `DI` first. A `long` CONSTANT argument
(`02_long/04_params.s.golden`'s `myseek(3, 90000L, 1)`) needs its own
shape: `90000L` (`hi=1, lo=24464`) renders as `mov di,#24464./push
di/mov di,*1./push di` — two ordinary immediate-load-then-push pairs,
LOW word first then HIGH (`docs/MUTOS_C_ABI.md` sect. 1.6's "push the
low word first" rule for a `long` argument, now directly confirmed in
generated code rather than just derived from disassembly). This also
exposed that `add sp,N` must track total WORDS pushed, not argument
COUNT — `04_params`'s three arguments (`fd`, `offset`, `whence`) occupy
four words, and the golden's `add sp,*8.` confirms it.

**The call result register, and a new `RFORCE`/`TIMES` finding.**
`docs/MUTOS_C_ABI.md` sect. 1.5 already established a call's result is
always in `AX`. `01_call.s.golden`'s `return add(3, 4);` renders as
`call _add/add sp,*4./jmp L6` — no `mov` of any kind between the call
and the epilogue jump, even though the EXISTING (pre-this-session)
`OP_RFORCE` handler unconditionally emitted `mov di,<v>` then `mov
ax,di`. This is a real, previously-unconfirmed optimization:
`v7/cc/c10.c`'s actual `RFORCE` case is `if ((r = rcexpr(tree, regtab,
reg)) != 0) movreg(r, 0, tree);` — `rcexpr()` returns 0 (meaning
"already in the target register") precisely when nothing needs
moving, and `movreg()` is skipped entirely. No prior `04_funcs`-
adjacent golden ever exercised a value already sitting in `AX` (every
earlier confirmed construct routes through `DI`), so this optimization
had never surfaced before. `03_recfact.s.golden`'s `return n *
fact(n - 1);` confirms the SAME optimization applies to `OP_TIMES`'s
own quotient-register convention, not just a call result: `...call
_fact/add sp,*2./mov ax,ax/imul *4.(bp)/jmp L3` — the recursive call's
result stays in `AX` (a literal, confirmed `mov ax,ax` self-move,
consistent with this project's no-peephole-optimization ethos —
`TIMES`'s own "load left operand into AX" codegen doesn't know or care
that its operand happened to already be there), `n` becomes the `IMUL`
operand, and `RFORCE` again emits nothing (the product is already in
`AX` from `IMUL`). Since the call's own result was the SOURCE-RIGHT
operand of `n * fact(n-1)` (source-left `n` is emitted first into the
wire stream) but ends up as the one KEPT in `AX`, `TIMES`'s codegen
was changed from "always load source-left into AX" to "keep whichever
operand is already `AX`, if either is, else load the (still,
source-)left one" — a strict generalization matching this one new case
without touching any previously-confirmed byte sequence (since neither
operand of any prior `TIMES` golden was ever already `AX`).
`OP_RFORCE` itself was changed to skip its two-instruction move
whenever the popped value is `VK_REG` with `reg=="ax"`.

**A symmetric `MINUS`-by-1 finding.** `03_recfact.c`'s `fact(n - 1)`
and `04_mutrec.c`'s `iseven(n - 1)`/`isodd(n - 1)` both compile the
argument as a plain `dec di`, not `sub di,*1.` — confirmed via
`03_recfact.s.golden`'s `mov di,*4.(bp)/dec di/push di`. A prior
session's `07_ternary` entry had left this explicitly unconfirmed
("`OP_MINUS` is deliberately left unchanged ... since no golden yet
shows whether `x - 1` gets the symmetric `DEC` treatment" — see that
section above); this session resolves it: yes, symmetrically with the
already-confirmed `+1`→`INC` case.

**`LTOI` on a `long` parameter must stay lazy.** `02_long/04_params.c`'s
`myseek(fd, offset, whence) { return fd + (int) offset + whence; }`
(`offset` a `long` parameter) surfaced a genuine context-dependent
codegen difference. The EXISTING (pre-this-session) `OP_LTOI` handler,
confirmed correct for `08_castsize`'s `"i = (int) l;"` (`mov
di,*-8.(bp)` then, via `OP_ASSIGN`, `mov *-6.(bp),di`), always eagerly
loaded the long's low word into `DI`. Naively reusing that for
`04_params` produced `mov di,*8.(bp) / mov di,*4.(bp) / add di,di` —
visibly wrong (both operands collapsed onto the same register) because
`OP_PLUS`'s own codegen ALSO uses `DI` as its accumulator, and an
eagerly-`DI`-resident `LTOI` result collides with it. The golden instead
shows `mov di,*4.(bp) / add di,*8.(bp)` — `fd` (source-left) loaded
into `DI` as usual, and the `LTOI`'d `offset` (source-right) used
DIRECTLY as `PLUS`'s memory operand, no separate move at all. Fix: a
new `VK_MEM_CVT` kind (rendered identically to `VK_MEM` everywhere
except `OP_ASSIGN`) — `LTOI` now pushes a lazy `VK_MEM_CVT` at
`(base_offset + MCC_SZINT)` instead of eagerly loading `DI`; `OP_ASSIGN`'s
plain-type case explicitly materializes a `VK_MEM_CVT` rhs via `DI`
(reproducing `08_castsize`'s confirmed two-instruction shape exactly)
while STILL rejecting a bare `VK_MEM` rhs (genuine, still-unconfirmed
direct `"x = y;"` memory-to-memory assignment) — the two cases are
distinguishable only because `LTOI`'s result is a different `Val` kind
from a plain `NAME`'s, even though both render as an identical
`"*N.(bp)"` string.

**Local `static` variables (`05_staticvar`).** `v7/cc/c03.c`'s
`declist()`, STATIC case: `dsym->hoffset = isn; ...
outcode("BBNBN", BSS, LABEL, isn++, SSPACE, rlength(dsym)); outcode("B",
PROG);` then (shared with every storage class) `prste(dsym)`, whose
STATIC branch (`v7/cc/c02.c`) is `outcode("BSN", SNAME, cs->name,
cs->hoffset)`. Confirmed byte-for-byte against
`05_staticvar.1.golden`'s `static int n;`: `BSS, LABEL(4), SSPACE(2),
PROG, SNAME("_n", 4)` — note `hoffset` here is the SAME label number
`isn` allocated for the `BSS` block, not a stack offset at all; every
later reference to `n` reuses `NAME(SC_STATIC, TY_INT, 4)` (same golden,
byte 59). `05_staticvar.s.golden` confirms the rendering: `L2:.bss\nL4:.
blkb\t2.\n.text\n| _n=L4\n` for the declaration, then `mov di,L4` / `mov
L4,di` wherever `n` is read/written — a bare `L<n>` operand, distinct
source-text shape from `VK_MEM`'s `"*N.(bp)"` but otherwise usable
identically (no `OP_ASSIGN` special-casing needed, unlike `VK_MEM_CVT`
above — an ordinary `mov L4,di` between a label and a register is
perfectly legal). Also confirmed in the same golden: a zero-argument
call's `NULLOP` shape (see above) via `counter()`'s two call sites, and
that `SETSTK`'s byte count only counts genuine AUTO locals — `main()`'s
`a, b, c` (all AUTO) give `SETSTK 10` as expected, with `counter()`'s
OWN static `n` contributing nothing to ITS `SETSTK 4` (no real AUTO
locals at all). Implemented via a new `symtab_declare_static()`
(`c0_sym.c`), `parse_static_decl()` (`c0_parser.c`, hooked into
`parse_compound_stmt()`'s decl loop alongside the existing
`int`/`char`/`long` cases), and a new `VK_STATIC` kind plus
`OP_BSS`/`OP_SSPACE`/`OP_SNAME` handlers (`c1_gen.c`).

**Function pointers (`07_funcptr`).** Three new pieces, all confirmed
against `07_funcptr.1.golden`/`.s.golden` in full:

1. *The derived type itself.* `v7/cc/c04.c`'s `incref(t) = ((t &
   ~TYPE) << TYLEN) | (t & TYPE) | PTR` (`TYPE=07` octal=7,
   `TYLEN=2`, `PTR=010` octal=8 — `v7/cc/c0.h`). Applied to
   `TY_FUNC_INT` (16, "function returning int" — already confirmed as
   a call's callee type this session): `incref(16) = ((16 & ~7) << 2)
   | (16 & 7) | 8 = 64 | 0 | 8 = 72`. Every `ANAME`/`NAME`/`AMPER`/
   `ASSIGN` touching `f` (a parameter) or `fp` (a local) in
   `07_funcptr.1.golden` uses type `72` — confirmed. Declared as `'('
   '*' IDENT ')' '(' ')'` — both a local-variable declarator
   (`parse_decl()`'s new `T_LPAREN` branch) and a parameter declarator
   (`parse_param_decls()`'s matching branch).
2. *A bare function name as a value.* `fp = square;` (not a call —
   no `(` follows) needs `c0` to recognize `square` as a function
   despite it never being a local variable. Real K&R architecture uses
   ONE persistent, whole-file symbol table (`v7`'s `hshtab`) where a
   function's own definition creates a lasting `EXTERN`-class entry;
   `c0_parser.c`'s per-function `Parser::syms` is reset every `cfunc()`
   and can't model this, so a new, separate, whole-file
   `Parser::funcnames[]` registry was added (`register_func()`,
   called from `parse_extdef()` for a definition and
   `parse_top_prototype()` for a forward declaration;
   `is_known_func()`, consulted by `parse_primary()`'s `T_IDENT`
   fallback only when the name is NOT a local). `07_funcptr.c`'s
   `main()` (the LAST function in the file) can see `square`/`cube`
   because they were defined earlier — confirmed by the golden's
   `NAME(SC_EXTERN, TY_FUNC_INT, "_square")` then `AMPER(72)` for `fp
   = square;`. Codegen-wise, this `AMPER` is NOT the already-confirmed
   array-decay one (`05_incdec`'s `lea di,*-16.(bp)`, a genuine
   runtime bp-relative address): a function's address is a link-time
   constant, so `07_funcptr.s.golden` shows `mov *-6.(bp),#_square` —
   a single memory-immediate `MOV`, no `lea`, no register at all. A
   new `VK_FUNCADDR` kind (holding the callee's own name, taken over
   from the `VK_FUNC` `AMPER` consumed) renders as `"#<name>"`
   directly.
3. *Indirect calls.* `apply(f, x) { return (*f)(x); }` — `f`
   declared `int (*f)();`. `07_funcptr.1.golden` decodes the body as
   `NAME(f, hclass=AUTO, type=72, offset=4), STAR(type=16),
   NAME(x, offset=6), CALL(type=0)` — `STAR`'s type (16) is
   `decref(72)` (the inverse of `incref` above: `v7/cc/c04.c`'s
   `decref(t) = (t >> TYLEN) & ~TYPE | t & TYPE`; `decref(72) =
   (72>>2) & ~7 | 72&7 = 18 & ~7 | 0 = 16` — confirmed). Critically,
   `07_funcptr.s.golden`'s rendering is `push *6.(bp) / call @*4.(bp)`
   — NO code at all corresponds to the `STAR` node itself: `f`'s own
   memory reference (`*4.(bp)`) is used directly as the indirect
   call's target, with a `@` prefix (mutos_as's indirect-call marker).
   This is the SAME "pure type-level operation, no code emitted"
   pattern as the `AMPER`-of-function case above, just for
   dereference instead of address-of. `OP_STAR`'s `TY_FUNC_INT` case
   now leaves its `VK_MEM` operand on the stack completely unchanged
   (asserting it stays `VK_MEM`, since only a plain function-pointer
   variable is supported as this grammar's dereference operand); a new
   `parse_indirect_call()` (`c0_parser.c`, entered from
   `parse_primary()`'s `T_LPAREN` branch when `peek2_kind() ==
   T_STAR`) emits this `NAME`/`STAR` pair then reuses the same
   `parse_call_args_and_emit()` tail `parse_call()` uses; `gen_call()`
   (`c1_gen.c`) now branches on the callee `Val`'s kind — `VK_FUNC` (a
   direct call) emits `call <name>` as before, `VK_MEM` (an indirect
   call) emits `call @<mem>` instead.

**`06_regclass.c` deliberately not attempted.** Its own comment states
the point plainly: "A K&R compiler is free to ignore [`register`] ...
but `mutos_c1` must at least parse and accept it." Its golden, however,
shows the REAL compiler does NOT ignore it byte-for-byte: `SETREG`'s
value drops from 4 to 3 (one register-variable slot consumed) and a new
`RNAME`(216) opcode (the same "BSN" family as `ANAME`/`SNAME`, confirmed
via `v7/cc/c02.c`'s `prste()`: `case REG: nkind = RNAME;`) declares `i`
by a REGISTER NUMBER rather than a stack offset or a BSS label —
meaning every later reference to `i` would need to compile to a direct
register operand (no `*N.(bp)` load/store at all) for the rest of that
function. This is genuine, novel register-allocation codegen — not
covered by extending any existing `Val` kind the way `VK_STATIC`/
`VK_MEM_CVT`/`VK_FUNCADDR` above were — and was explicitly left
unattempted rather than guessed at, consistent with this project's
"no silently-wrong output" rule: `parse_decl()`/`parse_param_decls()`
still reject `register` (via their existing "only 'int'/'char'/'long'"
error) exactly as before.

**Full-corpus regression** (`tests/mutos_cc/run_goldens.sh`, run via
`make test`): 27 of 62 byte-exact end-to-end (up from 20 at the start of
this session — 6 new `04_funcs` files plus `02_long/04_params` as a
side effect), 0 genuine mismatches anywhere, confirmed via a full
`make clean && make all && make test` from a clean checkout with zero
compiler warnings under `-Wall -Wextra -Wpedantic`.

### `02_long/03_retval` and `04_funcs/06_regclass` — `long` return values and real register-variable allocation: byte-level derivation (`mutos_c0`/`mutos_c1` extended and verified this session)

The prior session's two deliberately-deferred items — see "`02_long/
03_retval.c` and `04_params.c` were re-checked..." above and "`06_regclass.c`
deliberately not attempted" just above this entry — are both implemented
this session, closing out `02_long` (4/4) and `04_funcs` (7/7) completely.
Both were derived the same way this whole milestone always has: `od -A d -t
x1z` (extended with `dump_temp.py`, which needed a new opcode entry for
`06_regclass`'s `RNAME` before it could decode past it) on the `.1.golden`,
cross-checked against the matching `.s.golden` text and, where the real
compiler's behavior turned out to genuinely differ from `v7/cc`'s own
source, against `v7/cc/c0*.c` as an algorithmic reference only (Workflow
Guideline 3 — never assumed byte-identical without a golden to confirm it).

**`02_long/03_retval.c`: `long addlong(a, b) long a, b; { return a + b; }`,
called from `main` (`r = addlong(100000L, 5L);`) and cast back to `int`.**

- **A structural gap, not a `long`-specific one.** `parse_extdef()` could
  only parse a TYPED top-level declaration (`'int'|'char'|'long' IDENT
  '(' ...`) as `parse_top_prototype()`'s own narrow shape — an EMPTY
  parameter list terminated by `';'`, no body at all (exactly `04_mutrec.
  c`'s `"int iseven();"`). `long addlong(a, b) ...` has real parameters
  and a body, so it simply fell through to `parse_top_prototype()`'s own
  "must be a function prototype... a global variable declaration is not
  yet supported" error. The fix unifies `parse_extdef()`/
  `parse_top_prototype()` into one function: both forms share identical
  parsing through the closing `')'` of the K&R parameter-NAME list (an
  optional type keyword, IDENT, `'('`, optional bare identifiers,
  `')'`), and only THEN does the next token decide - `';'` (only when an
  explicit type was given and the parameter list was empty, preserving
  `04_mutrec.c`'s exact behavior byte-for-byte) means a prototype,
  anything else means `cfunc()` is entered with a real `ret_type`.
- **Return-type propagation.** Every function name is now registered with
  its own return type (`Parser.functypes[]`, parallel to the existing
  `funcnames[]`, via `register_func()`/a new `lookup_func_type()` - TY_INT
  for an as-yet-undeclared forward call, K&R's own implicit-int rule).
  `cfunc()` stashes it in a new `p->cur_ret_type` for the duration of that
  function, consulted by `do_return_stmt()` (`OP_RFORCE`'s type argument -
  previously always hardcoded `TY_INT`) and by `cfunc()`'s own trailing
  `OP_RETRN`. `parse_call()` looks the callee's type up and emits
  `OP_CALL` with it (previously always `TY_INT` too), returning
  `ev_dynamic_typed(ret_type)` so `r = addlong(...);`'s existing
  int-to-`long`-widening check (`rhs.type != TY_LONG` → insert `OP_ITOL`)
  correctly does NOT fire — the call's own result is already `long`-typed.
- **A second wire-format delta found only by a `.1` byte diff, not
  reasoned out in advance**: the callee's own `NAME` leaf's type argument
  is `ret_type | 020` (`020` being the same FUNC-degree bit `TY_FUNC_INT`
  already uses for `TY_INT`), not a hardcoded `TY_FUNC_INT` - confirmed via
  `03_retval.1.golden`'s `_addlong` callee using type `22` (`TY_LONG(6) |
  020(16)`), where the first implementation (matching every other
  confirmed call site) still emitted `16`. `c1_gen.c`'s `OP_NAME`
  `SC_EXTERN` handler was widened symmetrically (`type == TY_FUNC_INT ||
  type == (TY_LONG | 020)`).
- **Codegen (`c1_gen.c`)**: `OP_CALL`/`OP_RFORCE` gained `TY_LONG`
  branches. A `long`-returning call's result comes back in `DX:AX` (sect.
  1.5's ordinary convention) and is moved into the `DI(high):SI(low)`
  convention every other `long`-value producer here uses - confirmed via
  `"call _addlong / add sp,*8. / mov di,dx / mov si,ax"` (`gen_call()`
  gained an `is_long_ret` parameter for this). `OP_RFORCE`'s `TY_LONG`
  case does the reverse: `materialize_long()` then `"mov ax,si / mov
  dx,di"` - confirmed via `"return a + b;"`'s exact rendering. `OP_RETRN`
  itself needed NO change - its `"|RTYP n"` comment was already a generic
  passthrough of whatever type value it's given, never hardcoded.
- **A THIRD gap, again only found by a byte diff after the above was
  already working**: `gen_call()`'s existing `VK_LCON` (a `long` constant
  call argument) handling - confirmed in an earlier session against
  `02_long/04_params.c`'s `"myseek(3, 90000L, 1)"` - always direct-split
  the constant into two words. But `addlong`'s own `5L` argument (an
  int-range value merely carrying an `L` suffix, not a genuinely 32-bit
  one) instead renders as `"mov ax,*5. / cwd / push ax / push dx"` - the
  SAME CWD sign-extension idiom `materialize_long()` already uses for an
  assignment target, applied here for the first time to a call argument.
  The prior session's implementation only had the direct-split shape
  because no earlier golden's `long` constant argument happened to be
  int-range - `90000L` (hi=1, lo=24464) IS genuinely 32-bit, so the two
  shapes were indistinguishable until now. Fixed by giving `gen_call()`
  the same `hi == (lo < 0 ? -1 : 0)` branch `materialize_long()` already
  has. Both shapes still push low-word-then-high-word overall (sect. 1.6).

**`04_funcs/06_regclass.c`: `register int i;` as a `for`-loop induction
variable.** Contrary to the source file's own comment, the real compiler
does not ignore `register` - `i` lives in `di` for the function's entire
body, no stack slot at all.

- **Allocation (`c0_parser.c`)**: a new `p->regvar` (v7/cc's own global of
  the same name), reset to `MCC_INIT_REGVAR`(4) at the start of each
  `cfunc()`. A new `try_claim_register()` mirrors `v7/cc/c03.c`'s
  `goodreg()` exactly - `if (regvar < 3) return -1; return --regvar;` -
  the SAME threshold check, just with MUTOS's smaller `MCC_INIT_REGVAR=4`
  (2 claimable slots: `di`/`si`) standing in for v7's own larger PDP-11
  register set. `register` on anything but a plain (non-pointer,
  non-array) `int` declarator, or once `regvar<3`, silently falls back to
  an ordinary `AUTO` local - exactly v7's own `goodreg()`-fails-so-
  `skw=AUTO` fallback (not a guess - a direct algorithmic port), and
  exactly what the source file's own comment describes as acceptable.
- **Wire-format surprise**: `SETREG(newregvar)` is emitted IMMEDIATELY
  BEFORE its own claimed variable's `RNAME(name, newregvar)` - confirmed
  via `06_regclass.1.golden`'s exact byte order (`SETREG 3` then `RNAME
  "_i" 3`, both preceding `ANAME "_sum" -6`). This is NOT what a literal
  reading of `v7/cc/c02.c`'s `blockhead()` suggests (`declist(0)` - which
  calls `prste()`, emitting each variable's own `ANAME`/`RNAME`/`SNAME`,
  per declared name - runs to completion BEFORE `blockhead()`'s own
  trailing `"if (r!=regvar) outcode(SETREG,regvar);"` check), so this is a
  genuine, confirmed MUTOS implementation-order delta from what v7's
  source structure would imply, not just a transcription of it.
- **The end-of-function restore**: `cfunc()` now emits `SETREG
  (MCC_INIT_REGVAR)` right before the final `LABEL`/`RETRN` if `p->regvar`
  changed during the body - mirrors `v7/cc`'s `statement()` LBRACE-
  block-exit restore (`"if (sreg!=regvar) outcode(SETREG,sreg);
  regvar=sreg;"` - the function's own top-level compound statement is
  itself exactly such a block) - confirmed via the golden's trailing
  `"SETREG 4"`.
- **`mutos_c1`'s `|NREG n` comment, previously always silent** (no
  grammar coverage had ever produced a changing `regvar` before this
  session): a function's FIRST `SETREG` renders nothing; every SUBSEQUENT
  one renders `"|NREG %d\n"` with `n = regvar - 1` - confirmed against
  BOTH the golden's `"|NREG 2"` (the claim, regvar=3) and its `"|NREG 3"`
  (the restore, regvar=4) - the restore's raw value is numerically
  IDENTICAL to the silent initial `SETREG`'s, so only POSITION (first-in-
  function or not), never the value itself, can be what distinguishes the
  two - confirmed by the restore case alone, which would have been
  wrongly silent under a naive "skip when value == MCC_INIT_REGVAR" rule.
- **Codegen (`c1_gen.c`)**: `OP_NAME`'s new `SC_REG` case pushes
  `val_reg(<physical register>)` (a new `regvar_name()`: slot 3→`"di"`
  confirmed, 2→`"si"` extrapolated from the same algorithm but not itself
  golden-confirmed, anything else `gen_fatal`s). Because `di`/`si` are
  already this codebase's generic "working registers" for any
  intermediate value, most existing codegen worked completely unchanged
  once fed a `VK_REG("di")` - comparisons, `CBRANCH`, `ASSIGN`'s rhs
  materialization, all for free. Four genuinely new shapes did surface:
  1. `i + 1` → plain `"inc di"` - the pre-existing `"+1"→INC` optimization
     applies for free (`load_into_di()` is already a no-op for a value
     already in `di`).
  2. `sum = sum + i` → `"mov si,*-6.(bp) / add si,di"`, NOT the usual
     `"mov di,<lhs> / add di,<rhs>"` - when the RIGHT operand is already
     `di` (a live register variable), loading the LEFT operand into `di`
     as usual would clobber it before it's read, so the left goes into
     `si` instead.
  3. `i = i + 1` → the `ASSIGN` emits NO instruction at all - `"inc di"`
     has no trailing `"mov di,di"` - because the `+1` already wrote the
     new value directly into `i`'s own storage; `OP_ASSIGN` now elides its
     `mov` whenever lhs and rhs resolve to the identical register.
  4. `return sum;` (an ordinary `AUTO` local, entirely unrelated to `i`)
     → `"mov si,*-6.(bp) / mov ax,si"`, NOT the DI-then-AX shape every
     other `RFORCE` in this corpus uses - the most far-reaching finding:
     once a register variable occupies `di`, `di` becomes unavailable as
     the GENERIC scratch register for the REST of the function, not just
     at the register variable's own use sites. A new `GenState.
     di_reserved` flag (set by `OP_RNAME` when it claims `di`, cleared at
     each `OP_SAVE`) drives a new `load_into_si()` fallback, applied ONLY
     to `OP_RFORCE`'s default path - the one site this golden confirms
     needs it. Every other "go through DI" site (`OP_TIMES`'s `IMUL`,
     etc.) is left unchanged, since none is exercised with a live
     register variable by any golden yet - a live register variable
     interacting with one of THOSE sites remains unconfirmed and would
     currently render silently-plausible-looking but unverified output,
     which is exactly the risk this project's "gen_fatal rather than
     guess" rule exists to avoid; it is flagged here rather than patched
     speculatively.
- All of the above is deliberately scoped to the single confirmed shape
  (one `int` register variable, physical register `di`) - a second live
  register variable, a `register` value of any other eligible type, or
  any codegen combining two simultaneously-live register variables at
  once is left as an explicit `gen_fatal` rather than extrapolated.

**Full-corpus regression** (`tests/mutos_cc/run_goldens.sh`, run via
`make test`): 29 of 62 byte-exact end-to-end (up from 27), 0 genuine
mismatches anywhere, confirmed via a full `make clean && make all &&
make test` from a clean checkout with zero compiler warnings under
`-Wall -Wextra -Wpedantic`.

### `05_arrptr` (4 of 7 files) — pointer-degree chaining, array subscripting, and two real `c1` register-allocation surprises: byte-level derivation (`mutos_c0`/`mutos_c1` extended and verified this session)

The first category needing real type-system work beyond the flat "2 bytes,
maybe one hardcoded `TY_PTR_INT`" model every construct up to this point got
away with. Four of the category's 7 files are confirmed byte-exact this
session — `01_arrbasic.c` (single-dimension array subscripting),
`03_ptrbasic.c` (explicit `&`/`*` as general unary operators), `04_ptrarreq.c`
(the classic `a[i]`/`*(a + i)` K&R equivalence, plus array-parameter decay),
and `06_ptrptr.c` (multi-level pointers, `int **`). `02_array2d.c` (2-D
arrays) and `05_arrofptr.c`/`07_strlibc.c` (string literals) remain open —
see their own subsections below for exactly how far each got.

**Pointer-degree chaining is a real `incref()`/`decref()` walk, not "+8 per
level".** `c0_parser.c` already had `TY_PTR_INT = TY_INT | 010` and a
one-off `TY_PTR_FUNC_INT = 72`, the latter's own comment already citing the
underlying v7/cc formula (`incref(t) = ((t & ~TYPE) << TYLEN) | (t & TYPE) |
tag`, `TYPE=7`, `TYLEN=2`) without generalizing it, since nothing before this
session needed more than these two fixed values. `06_ptrptr.c`'s `int **pp;`
forced the generalization: `ty_incref_tag()`/`ty_ptr_of()`/`ty_decref()` now
implement the formula directly. Confirmed against `06_ptrptr.1.golden`:
`pp`'s own `NAME`/`AMPER`/`ASSIGN` all carry type **40**, not the naive
"`TY_PTR_INT + 8` = 16" a flat-degree model would predict —
`ty_ptr_of(ty_ptr_of(TY_INT))` = `ty_incref_tag(8, PTR)` =
`((8 & ~7) << 2) | (8 & 7) | 8` = `32 | 0 | 8` = `40`, matching exactly.
`**pp = 6;` confirms the inverse: two chained `STAR`s, `ty_decref(40) = 8`
then `ty_decref(8) = 0` — `06_ptrptr.1.golden`'s two `STAR` nodes are typed
8 then 0, in that order.

**`a[i]` is `*(&a + i*sizeof(elem))`, reusing `05_incdec.c`'s own
established `AMPER`/`ITOP`/`PLUS`/`STAR` shapes — the new part is entirely
in `c1`'s codegen for a *non-constant* scale.** `OP_ITOP` previously only
ever folded two compile-time constants (the literal "1" in `++`/`--` — see
`emit_incdec()`'s comment); `01_arrbasic.1.golden`'s `a[i]` needs the SAME
opcode to scale a genuinely runtime value (`i`) by a constant (2). Confirmed
shape (`01_arrbasic.s.golden`): `"lea di,*-14.(bp)" / "mov si,*-16.(bp)" /
"sal si,*1" / "add di,si"` — the base address (from `OP_AMPER`) claims DI
first, so the index is scaled into SI instead (repeated-shift strength
reduction, same style as `06_compasgn`'s confirmed `*=2`), then combined via
a plain `add`. `04_ptrarreq.c`'s `*(a + i)` (`a` a plain pointer parameter,
never needing an `AMPER`/`lea` at all) shows the *mirror* shape when DI is
still free at `OP_ITOP` time: `"mov di,*-6.(bp)" / "sal di,*1" / "add di,
*4.(bp)"` — the scaled index claims DI, and the pointer parameter is added
in straight from memory. The rule implemented in `c1_gen.c`'s `OP_ITOP`: use
SI instead of DI whenever the value stack's current top (right after
popping `OP_ITOP`'s own two operands) is already `VK_REG("di")` (a
preceding `OP_AMPER`) or a still-deferred `VK_MEM_DIRECT` (see below) — DI
free otherwise. `OP_PLUS`'s own new pointer-arithmetic case then picks
whichever operand is already resident in a register as the destination
(DI preferred) and adds the other in via its own rendered text directly —
legal on the 8086 for a memory or immediate right-hand `ADD` operand,
confirmed by `04_ptrarreq.s.golden`'s `"add di,*4.(bp)"` needing no separate
load of `a` at all.

**A dereferenced value must be force-materialized before feeding further
arithmetic — leaving it as a lazy `"(di)"` operand only works when it's
about to become an `ASSIGN`'s own lhs.** Confirmed by both
`01_arrbasic.s.golden`'s `"sum = sum + a[i];"` and `04_ptrarreq.s.golden`'s
`"s = s + *(a + i);"`, each showing `"mov di,(di)"` (in place, same
register) immediately after the address computation, before the outer `add`
— the 8086's `ADD` cannot take two memory operands, and `(di)` IS one. New
logic in the plain-`TY_INT` `OP_PLUS` case (gated to `OP_PLUS` only —
commutative, so operand order doesn't matter; deliberately NOT generalized
to `OP_MINUS`, unconfirmed by any golden and order-sensitive) intercepts a
`VK_IND` operand before the pre-existing register-class-variable special
case (`06_regclass`'s own "right operand already in DI" branch), which a
naively-materialized `VK_IND` would otherwise wrongly trigger — the two are
visually similar (both end up `VK_REG("di")`) but need different codegen,
so this had to be careful to fire first and always `break`.

**The genuinely unexpected discovery this session: an indirect-assignment
target whose right-hand side needs its own working registers gets its
address PUSHED to the real hardware stack, not left resident in a
register — confirmed two independent ways.** `01_arrbasic.s.golden`'s
`"a[i] = i * i;"`:
```
lea	di,*-14.(bp)
mov	si,*-16.(bp)
sal	si,*1
add	di,si
push	di                  <- address pushed BEFORE the rhs's own code
mov	ax,*-16.(bp)
imul	*-16.(bp)
pop	bx                    <- retrieved via BX (not DI/SI - both live)
mov	(bx),ax
```
`03_ptrbasic.s.golden`'s `"*p = *p + 1;"` (the exact construct flagged as an
open question at the end of the PRIOR session's investigation into this
file — see below for how it got resolved):
```
push	*-10.(bp)             <- p's own memory operand, pushed directly -
                                 NOT first loaded into any register
mov	di,*-10.(bp)              <- this is the RHS's own "*p", loading p
mov	di,(di)                     <- materializing that dereference
inc	di                            <- "+1" (the existing INC special case)
pop	bx
mov	(bx),di
```
Contrast the SAME file's `"*p = 20;"` (a bare-constant rhs, confirmed
already-passing, unaffected): `"mov di,*-10.(bp)" / "mov (di),*20."` — no
push at all. The discriminator, worked out by close comparison of these two
shapes within the same file: whether the wire IMMEDIATELY following the
completed address (this `OP_STAR`'s own position in the stream) is exactly
`CON` then `ASSIGN` and nothing else. Getting there took real dead ends
worth recording, since the next person hitting a similar case will want
them: the first hypothesis (spill only when DI is *already* occupied by
something else) was falsified by `01_arrbasic`'s FIRST-star equivalent (`a[i]`'s
own address computation, which has nothing else pending yet, matching
`"*p = 20;"`'s starting condition exactly, yet still needs the push) and by
value-stack-depth checks (`g.valsp == 0` holds identically at both the
push and no-push sites — confirmed by re-deriving `OP_EXPR`'s own stack-
reset behavior, which already unconditionally zeroes `g.valsp` between
statements). What actually distinguishes them is RHS complexity, which `c1`
(deliberately opcode-by-opcode, no lookahead — see its own file-header
comment) cannot know without cheating: the fix adds exactly that cheat,
narrowly. `c1_read_op()`/`c1_read_num()` are plain `getc()` calls against an
ordinary seekable `FILE*` (confirmed via `c1_stream.c`), so a save/restore
of `temp1`'s file position via `ftell`/`fseek` gives one-to-few opcodes of
real lookahead with zero effect on any other opcode's own reads afterward -
this is the first place `c1_gen.c` has ever needed or used lookahead. The
new `VK_IND_PENDING` value kind carries no register (the address lives on
the hardware stack, not in one); `OP_ASSIGN` retrieves it via `"pop bx"`
specifically (never DI/SI, both of which may still hold live pieces of the
just-evaluated rhs) and renders the store through `(bx)`. Scoped narrowly:
only attempted for a FINAL (`TY_INT`) dereference, never the first of a
chained `**pp` (`TY_PTR_INT` — always immediately re-dereferenced, never
itself a target), and the pushed operand is `OP_STAR`'s ORIGINAL,
pre-`load_into_di()` operand (a raw memory operand for a plain pointer
variable, or whatever register an `OP_PLUS` already computed it into for a
subscript) — never a fresh, unconditional `"push di"`, which would have
been wrong for the `*p` case (confirmed by first getting this wrong: an
earlier attempt that unconditionally loaded into DI before pushing produced
`"mov di,*-10.(bp)" / "push di"` for `*p = *p + 1;`, two instructions where
the golden has one).

**A parallel, independently-discovered deferral for `OP_AMPER`, covering
two more distinct needs.** An array's address-of (`AMPER`) is now deferred
by default — a new `VK_MEM_DIRECT` value, carrying just the original
`VK_MEM`'s own offset — rather than eagerly emitting a `"lea"` the moment
`AMPER`'s own opcode is processed, UNLESS the immediately-following opcode
is `OP_NAME` (one opcode of lookahead, same `ftell`/`fseek` technique):
that specific case (`a[i]`'s own runtime-index subscript, `emit_subscript()`'s
`NAME`/`AMPER`/`NAME`/`CON`/`ITOP`/`PLUS`/`STAR` shape) needs the `"lea"`
emitted eagerly to match `01_arrbasic.s.golden`'s own instruction order
(`"lea"` BEFORE the index is loaded and scaled) — deferring it instead would
land it AFTER, which was in fact the first, wrong attempt at this (confirmed
by trying the fully-deferred version first and diffing against the golden).
Two confirmed consumers for the deferred (non-`NAME`-followed) case: (1) a
compile-time-CONSTANT-index subscript (`04_ptrarreq.c`'s `"v[0] = 1;"`,
`v[1] = 2;"`, etc.) — `OP_PLUS`'s pointer-arithmetic case, on seeing a
`VK_MEM_DIRECT` base paired with a `VK_IMM` scaled index (always the case
when `OP_ITOP`'s own two operands were both constants, i.e. the index was
itself a bare `CON`, never a `NAME`), folds the WHOLE address into a single
new `VK_MEM_DIRECT(base_offset + scaled_index)` with **no instruction at
all** — confirmed against `04_ptrarreq.s.golden`'s `"v[0] = 1;"` compiling
to a lone `"mov *-12.(bp),*1."`, no `lea`/`add` of any kind; `OP_STAR`
recognizes a `VK_MEM_DIRECT` operand and passes it through completely
unchanged (dereferencing a compile-time-known address is just that memory
location itself — no load, no indirection). (2) A bare array-decayed CALL
ARGUMENT (`"sumarr(v, 4);"`) — confirmed the hard way, by first trying
eager-except-for-constant-index-peek (checking only for the `CON`/`CON`/
`ITOP` subscript-fold shape specifically) and finding it broke this case:
`gen_call()` pushes arguments RIGHT-TO-LEFT (`docs/MUTOS_C_ABI.md` sect.
1.1), so an eagerly-computed `"lea di,&v"` for the FIRST-declared argument
would sit in DI while the SECOND argument (`4`, a plain `CON`) gets pushed
FIRST — clobbering DI with `"mov di,*4."` before `v`'s own address is ever
pushed. `04_ptrarreq.s.golden` confirms the fix's own ordering:
`"mov di,*4." / "push di" / "lea di,*-12.(bp)" / "push di"` — `v`'s `"lea"`
materializes only inside `gen_call()`'s own reverse-order loop, at the exact
point it is about to be pushed, via a new `VK_MEM_DIRECT` case there.
`OP_ASSIGN` materializes a still-deferred `VK_MEM_DIRECT` rhs the same way,
for a bare `"p = &x;"`/`"p = a;"`/`"pp = &p;"` — confirmed UNCHANGED against
`05_incdec.s.golden` and `06_ptrptr.s.golden` (both still passing; a bare
assignment has nothing between `AMPER` and `ASSIGN` for deferral to affect).

**Array-parameter decay (`"int a[];"`), confirmed against
`04_ptrarreq.1.golden`/`.s.golden`.** `parse_param_decls()` accepts a
trailing `'[' ... ']'` on an `int` parameter now (any size token between the
brackets is consumed and discarded — real K&R semantics: a parameter's
array size is never meaningful), marking it exactly like a plain
`"int *a"` parameter (same `pptr`/`TY_PTR_INT` path) — confirmed by
`sumarr(a, n)`'s own `*(a + i)` codegen needing no `AMPER`/decay step at
all, unlike a real array local.

**Not yet attempted: `05_arrptr/02_array2d.c` (2-dimensional arrays).**
*(Done in a later session - see "`05_arrptr/02_array2d` - 2-D arrays,
`distrib()`, and the real constant-shift threshold" below, which also
derives the type-104 value this paragraph calls unexplained.)*
The WIRE shape is understood from the golden, including one genuinely
unexplained wrinkle: `m[i][j]`'s OUTER-dimension `OP_ITOP` node carries type
**104**, not the plain `TY_PTR_INT(8)` every other `PLUS`/`STAR`/`AMPER` node
in the same expression uses. `104 = ty_incref_tag(ty_incref_tag(TY_INT, ARRAY),
PTR)` — i.e. "pointer to array of int", a real, principled value under the
same `incref()` formula this session generalized (ARRAY=030 the same way
PTR=010 is) — but WHY this one specific node carries that richer type while
the `AMPER`/`PLUS`/`STAR` nodes immediately around it all stay flat
`TY_PTR_INT(8)` was not fully reconstructed; real v7/cc's own `build()`
(`v7/cc/c01.c`) suggests a candidate derivation (`disarray()`'s `decref()`+
`AMPER` chaining) but doesn't cleanly reproduce the OBSERVED byte, so this
is recorded as an empirically-pinned constant for this ONE confirmed shape,
not a general N-dimensional formula. Worse: even granting that type value,
`c1`'s ACTUAL codegen for `m[i][j]` (from `02_array2d.s.golden`) is not the
two-independent-scaled-adds shape a naive per-dimension implementation would
produce — it factors the two dimensions together into a single combined
computation using SI as scratch: `"lea di,&m" / "mov si,i" / "sal si,*1" /
"sal si,*1" / "add si,j" / "sal si,*1" / "add di,si"` (`i*4 + j`, then the
WHOLE sum scaled by 2, not `i*8` and `j*2` added separately) — a genuinely
different, smarter address-computation than anything currently
implemented, needing dedicated pattern-recognition codegen this session did
not attempt to write (high risk of a subtly-wrong "looks plausible" result
without more data points than this one file provides — deliberately left
as an explicit gap rather than guessed).

**Not yet attempted: `05_arrptr/05_arrofptr.c` and `07_strlibc.c` (string
literals).** Need an entirely new data-segment/string-constant emission
subsystem — the real v7/cc equivalent is `c00.c`'s `putstr()` (`"fake a
static char array"`), and `mutos_cc.h` already transcribes `OP_BDATA`/
`OP_WDATA`/`OP_DATA` as presumably-relevant pseudo-ops from `v7/cc/c0.h` —
but none of their argument shapes have been reverse-engineered from any
golden yet; `dump_temp.py`'s `OPCODES` table has no entries for them either
(they would stop the dump cleanly with a "no confirmed argument shape"
message on first encounter). Genuinely unstarted, not just unfinished.

**Full-corpus regression** (`tests/mutos_cc/run_goldens.sh`, run via
`make test`): 33 of 62 byte-exact end-to-end (up from 29), 0 genuine
mismatches anywhere, confirmed via a full `make clean && make all &&
make test` from a clean checkout with zero compiler warnings under
`-Wall -Wextra -Wpedantic`.

### `c1_gen.c` review: emission layer, register-occupancy guard, and the `09_abiprobe` `SETSTK` threshold (`mutos_c1` changed and verified this session)

A code review of `src/mutos_cc/c1_gen.c` (Milestone 4 grammar work paused for
it) produced one refactor and two classes of bug fix, all confined to that one
file: no `mutos_c0` change, no wire-format change, `dump_temp.py` unaffected.

**1. Emission layer (pure refactor).** Assembly text used to be written by 146
scattered `fprintf(out, "mnem\t%s,%s\n", ...)` calls, most preceded by a
`char buf[32]; render_operand(buf, sizeof buf, v);` dance, so `mutos_as`'s
line syntax (tab after the mnemonic, comma between operands, the deliberate
no-newline `L<n>:` label glue) was restated at every site. Now:

- `Opnd` is a small fixed-size value type holding one rendered operand,
  built inline by typed constructors: `o_reg()`, `o_sym()`, `o_ind()`
  (`"(di)"`), `o_lab()`, `o_val()` (any `Val`, via `render_operand()`),
  `o_imm()`, `o_mem()`, `o_cmpimm()`, `o_shift1()`, `o_indirect()` (`"@"`),
  and the checked `o_fmt()` escape hatch (never truncates silently).
- `Insn` (mnemonic + 0-2 `Opnd`s) is rendered only by `put_insn()`;
  `ins0()`/`ins1()`/`ins2()` wrap it, `put_label()` emits the no-newline
  label, `put_line()` (printf-format-checked) emits `|`-comments. Nothing
  else in the file writes to the output stream.
- Tables where they genuinely fit: `RELOPS` (relational opcode → branch
  mnemonic + inverse), `ALUOPS` (opcode → diagnostic name, 16-bit mnemonic,
  `long` high-word partner `adc`/`sbb`), and the confirmed fixed idioms as
  `const Insn` sequences (`SEQ_PROLOGUE`, `SEQ_DXAX_TO_DISI`,
  `SEQ_DISI_TO_AXDX`, `SEQ_PUSH_AXDX`) - in the spirit of `v7/cc/table.s`'s
  code templates, without its tree matcher (see "Decision" below).
- The postfix-`++`/`--` deferred queue stores structured `Insn`s instead of
  pre-formatted text; `GenState` carries the output stream, so every helper
  takes `GenState *` instead of `FILE *`.
- The confirmed quirks stay explicit and visible: `"pop cx"` (literal space)
  is a zero-operand "mnemonic", the switch table's hex `#/<n>` and `shl ax,#1`
  are literal `o_fmt()` operands.

A typed builder was chosen over a template mini-language with custom
`%`-directives (`"lea\tdi,%o\n"`) because the latter loses the compiler's
format/argument type checking.

**2. Register-occupancy guard (bug fix).** The value stack records WHERE each
pending value lives (`VK_REG "di"`, `VK_IND "(di)"`, `VK_LONG` = DI:SI, ...),
but no code checked whether an instruction was about to overwrite a register
still holding one. Found by constructing probe programs from the review, then
by differential fuzzing (below); every one of these compiled silently to wrong
code before this change:

| Source | Old `mutos_c1` output (excerpt) | What went wrong |
|---|---|---|
| `b = f(a) + a * b;` | `call _f` … `mov ax,a / imul b / mov di,ax / add di,ax` | `imul` overwrote `f()`'s result in AX |
| `v[a + 1] = 5;` | `lea di,v / mov di,a / inc di / …` | array base in DI overwritten by the index |
| `if (a + b < c)` | `mov di,a / add di,b / mov di,c / cmp di,di` | loading CMP's rhs into DI destroyed its lhs |
| `b = f(a) + g(a, b);` | `call _f` … `call _g` … `mov di,ax / add di,ax` | the second call overwrote the first result |
| `b = g(a + b, 5);` | `mov di,a / add di,b / mov di,*5. / push di / push di` | pushing arg 2 destroyed arg 1 |
| `b = a / (b % c);` | `… idiv c` (remainder in DX) `mov ax,a / cwd / idiv dx` | `cwd` overwrote the divisor in DX |
| `b = a - *p;` | `mov di,p / mov di,a / sub di,(di)` | pointer in DI overwritten |
| `b = (a < b) + (c < a);` | both 0/1 results materialized into DI | first result lost |
| `a = i + 1;` (`register int i`) | `inc di / mov a,di` | the register variable itself modified |

Root cause: `mutos_c1` has no register allocator (by design so far - each
operator loads into its golden-confirmed working register) and no collision
check. Fix, in two parts:

- **Automatic, in the emission layer.** `INSN_FX` lists every emitted
  mnemonic's register effects (explicit destination operand and/or implicit
  registers: `cwd` → DX, `imul`/`idiv` → AX|DX, `call` → AX|BX|CX|DX - DI/SI
  are callee-saved per `docs/MUTOS_C_ABI.md` sect. 1.2). A mnemonic missing
  from the table is itself a `gen_fatal()`, so the table cannot silently rot.
  `put_insn()` passes the written-register mask to `note_writes()`, which
  marks every value still on the value stack that lives in one of those
  registers as `clobbered`. `pop_val()` refuses to hand a clobbered value to
  a consumer; `discard_val()` (comma-operator left operand, a statement's
  leftover value at `EXPR`) and `pop_lvalue()` (a register lvalue is a
  location, not a value) do not. Binary operators now materialize both
  operands in place on the stack (`pop_operands()` - same emission order as
  before) so the first operand stays visible while the second's comparison
  code runs.
- **Explicit, where a handler holds popped operands.** `require_free()` at each
  site that writes a register before reading an operand it already popped:
  comparisons, `+`/`-`/`&`/`|`/`^`/shifts, `*`, `/`/`%`, pointer `+`, call
  arguments still waiting to be pushed, `?:`, `&&`/`||`, and `ASSIGN`.

For a `register` local (`GenState.reserved`), overwriting its register is
allowed only while computing that same variable's new value - the statement's
assignment target at the bottom of the value stack is that register (as in
`06_regclass.s.golden`'s `i = i + 1;` → `inc di`) - and the variable may not be
read again until that assignment completes (`regvar_dirty`). `ASSIGN`'s own
store into it is exempt.

Every collision is now an explicit "not yet supported" diagnostic, never a
spill or reordering: no golden yet shows what the real compiler emits for any
of these shapes, and inventing one would violate this project's verification
rule. The one golden with a `register` local shows the real compiler moving
its scratch work to SI (`mov si,sum / add si,di`), which is also why refusing
DI-as-scratch while DI is reserved costs no matchable golden. Generalizing
that SI-scratch rule, and real spill/reorder shapes, are future work that each
need a golden first.

**3. `SETSTK` threshold (bug fix).** The `09_abiprobe` goldens (frames of
80/128/176/224/256/300 bytes beyond the 4-byte register-save area) show:

- 80 bytes: `L1:sub\tsp,*80.`
- 128, 176, 224, 256, 300 bytes: `L1:mov\tax,#N.` / `call\tchkstk`

So the threshold was above 80 and at most 128 bytes (narrowed again by the
round-7 goldens - see "The round-7 fltprobe goldens"), and the `chkstk` form's immediate follows
the ordinary `render_operand()` rule - `#`, since every such N is above 127.
The old code refused 77..256 bytes and, above 256, emitted `mov ax,*N.` -
the wrong size marker, silently. It now uses `o_imm()` for both shapes
(`MCC_SUBSP_MAX` = 80, `MCC_CHKSTK_MIN` = 128) and refuses only 81..127. The
`09_abiprobe` files themselves still stop earlier (`char` arrays are not yet
supported), so this was verified with hand-assembled `temp1` streams carrying
each golden's own `SETSTK` value: all six now reproduce their golden's frame
epilogue byte-for-byte (before: five refused, one wrong).

**Verification methodology (reusable for any behavior-preserving `c1`
change).** The 33 end-to-end goldens exercise `c1` only through `c0`, so
four additional safety nets were used:

1. *`c1` on the golden intermediate files directly.* Feeding every
   `*.1.golden`/`*.2.golden` pair to `mutos_c1` (bypassing `mutos_c0`) and
   comparing `.s`, stderr and exit status against the pre-change binary
   covers the partial runs of the 29 files `c0` cannot parse yet too.
   37 of the 62 match their `.s.golden` through `c1` alone
   (`06_struct/01_stbasic`, `08_enum`, `09_typedef` and `07_scope/02_shadow`
   are blocked only by `c0`) - worth wiring into `run_goldens.sh` as its own
   stage.
2. *Coverage.* `gcov` showed the golden corpus executing 148 of the 151
   output-writing lines; random programs reached one more, and the other two
   (pointer-`PLUS` fallbacks no C source reaches) got hand-assembled
   `temp1` streams.
3. *Differential fuzzing.* A generator of random programs within
   `mutos_c0`'s grammar; each one accepted by `c0` is run through the old and
   new `c1`. For the refactor alone: 0 differences over 2060 accepted
   programs (551 compiled completely, ~425k lines of `.s`). For the guard:
   426 identical, 1634 refused, 0 unexpected - a difference is accepted only
   if the new binary stops with a guard diagnostic AND its partial `.s` is an
   exact byte-prefix of the old output (nothing before the detection point
   changed); a manual sample of refusals were all genuine clobbers. The
   `SETSTK` fix was isolated the same way (only frame lines differ).
4. *Sanitizers.* An ASan/UBSan build produced no reports on any of the
   above.

**Decision: no `table.s`-style tree matcher (yet).** The review considered
porting `v7/cc`'s table-driven matcher. Not now: `c1`'s decisions are
dominated by golden-confirmed special cases (operand-kind-dependent shapes,
one-opcode lookahead peeks, the AX-resident `imul` swap, DI-vs-SI choice
with a `register` local) that a pattern table expresses poorly. But several
confirmed quirks - `pop cx` with a space, the self-move `mov ax,ax`, the burned
switch label, the hex `#/` literal - look like artifacts of a real
template-driven MUTOS `c1`, so revisit this once `06_struct`/`08_float`
coverage multiplies the special cases.

**Other review findings, not changed here:** type constants duplicated
between `c0_parser.c` and `c1_gen.c` (`TY_PTR_INT`, `TY_PTR_PTR_INT`,
`TY_PTR_FUNC_INT`, `MCC_SZINT`, ...) belong in `mutos_cc.h`; `Val` overloads
fields by kind (`reg` holds an owned symbol for `VK_FUNC`/`VK_FUNCADDR`,
`offset` a label or a `long`'s high word); the ~1,800-line dispatch `switch`
would read better as per-opcode handler functions; the `AMPER`/`STAR`
lookahead needs a seekable `temp1` (never a pipe) and does not check
`ftell()`; `c1_read_sym()` does not check `malloc()`/`realloc()`.

### `05_arrptr/02_array2d` — 2-D arrays, `distrib()`, and the real constant-shift threshold (`mutos_c0`/`mutos_c1` extended and verified this session)

**Starting point.** The previous session understood `m[i][j]`'s wire shape
but not two things: why the outer `ITOP` carries type 104 while everything
around it is flat, and how `c1` arrives at `"sal si,*1" x2 / "add si,j" /
"sal si,*1"` instead of two independently scaled adds. Both turn out to be
straight consequences of the real V7 compiler's source.

**Type 104, derived.** `v7/cc/c01.c`'s `build()` handles `m[i]` as
`*(m + i)`. For `int m[3][4]` (type `ARRAY.ARRAY.INT` = 0330 = 216),
`disarray(m)` first retypes the NAME to `ARRAY.INT` (24) and wraps it in
`AMPER` of type `incref(24)` = `PTR.ARRAY.INT` = 104; the `PLUS` then
converts `i` via `convert(p2, t, ITP, plength(p1))` - an `ITOP` node of
the pointer's type 104, with the row size (8) as its constant - and the
`STAR` on top has type `decref(104)` = 24. So at this point AMPER, ITOP and
PLUS are all 104. The second `[` then `disarray()`s that `STAR` (type 24,
an array), which calls `setype(p, decref(24) = INT, p)`: `setype()` walks
`p->tr1` only - STAR gets 0, then `t = incref(t)` = 8 for PLUS, PLUS gets 8,
AMPER gets 8 and flips `t` back to 0, NAME gets 0, stop. The `ITOP` hangs
off `PLUS->tr2` and is never visited, so it alone keeps 104 - exactly the
golden's `NAME(0) AMPER(8) ... ITOP(104) PLUS(8) STAR(0) AMPER(8) ...`.
The same derivation predicts that an N-D array's k-th `ITOP` carries
"pointer to the remaining sub-array" (e.g. 872 for the outer step of an
`int[2][3][4]`), but with no golden for three dimensions `c0` refuses them.
`mutos_c0` now emits the 2-D shape via `emit_subscript_2d()`, byte-exact
against `02_array2d` and also against `10_integ/05_matmul`'s `.1`/`.2`
(whose `int a[2][2]` subscripts are all constant or all runtime).

**The address arithmetic is `distrib()`.** After `optim()`'s very first
rule (`if (tree->op==AMPER && tree->tr1->op==STAR) return tree->tr1->tr1`)
removes the row's `&*`, the tree is `(&m + i*8) + j*2`. `acommute()`
flattens the sum to `[&m, i*8, j*2]` and, for `PLUS`, calls `distrib()`:
find a term `c1c2*x` that divides no other constant but is divided by at
least one (`i*8`, divided by `j*2`'s 2), and rewrite the pair as
`c1*(y + c2*x)` - here `(i*4 + j)*2`. The multiplications by powers of two
become shifts, and the codegen is the ordinary one for `&m + <expr>`: `lea
di` for the base, the index expression computed in SI, `add di,si` -
exactly `02_array2d.s.golden`'s seven lines, and `05_matmul.s.golden`'s
`i*4 + k*2 → (i*2 + k)*2` too. `distrib()`'s other branch (two EQUAL
constants: `(*p2)->tr2 = (*p1)->tr1; (*p2)->op = PLUS`) would also swap the
operand order (`(j + i)*2`), and a non-power-of-two quotient needs a real
multiply, so `c1` refuses both (row size = element size, or `int m[N][3]`).

**How a streaming `c1` does it.** `c1` never builds a tree, so the fusion is
done with two symbolic value kinds instead: the row step's `OP_ITOP`
(recognizable by its type 104) pushes `VK_SCALED{i, 8}` without emitting
anything, the following pointer `OP_PLUS` turns base-in-DI plus that into
`VK_ROWADDR{di, i, 8}`, `OP_STAR` peeks one opcode ahead and - if it is
`OP_AMPER` - consumes it and leaves the stack alone (the `&*` cancel), the
column step's `OP_ITOP` sees the `VK_ROWADDR` below it and pushes
`VK_SCALED{j, 2}`, and the column `OP_PLUS` emits everything at once
(`gen_subscript_2d()`). `pop_val()` refuses both kinds, so no other handler
can accidentally render one; they are consumed only through `pop_any()` in
the two places written for them. A constant row index takes the existing
1-D fold path unchanged (`VK_MEM_DIRECT`, now flagged `rowbase` after the
`&*` cancel), which is how `05_matmul`'s `a[0][1] = 2;` becomes a lone `mov
*-10.(bp),*2.`. A constant index next to a runtime one is refused: V7's
`acommute()` would fold the constant into the base (`&m + 8 + j*2` → `lea
di,<m+8>`), plausible but not shown by any golden.

**Two more shapes from the same golden.** `m[i][j] = i * 10 + j;` confirmed
that a constant multiplier which is not a power of two is loaded into CX
(`mov ax,i / mov cx,*10. / imul cx`) - the same "IMUL/IDIV take no
immediate" workaround `06_compasgn`'s `/=` already showed for IDIV - and
that the product is then added to in place (`add ax,j`), not moved to DI
first as `c1`'s generic `PLUS` path did. `05_matmul.s.golden`'s `sum +
a[i][k] * b[k][j]` → `imul cx / add ax,*-36.(bp)` shows the same with the
AX value on the right, so the rule is keyed on where the value lives (AX),
not on operand position - which is also how the real table-driven code
generator sees it. `PLUS` only; `MINUS` with an AX operand is unconfirmed.

**The constant-shift threshold, from real compiler output already in the
repo.** `distrib()`'s shifts raised the question of how `c1` shifts by 3 or
more (`int m[3][4]` needs `i*4` - two single-bit shifts - but `int
m[3][8]` already needs `i*8`, a shift by 3). `tests/mutos_as/kernel_nonopt/*.s` is real, non-optimized
MUTOS `c1` output, and it answers it: no run of three or more single-bit
shifts exists anywhere in it, while `mov cx,*N.` followed by a shift `by cl`
appears for N = 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 (e.g. `amx.s`: `mov
di,*4.(bp) / and di,#255. / mov cx,*3. / sar di,cl`), and a run of exactly
two single-bit shifts does occur (`amx.s`, matching `04_shift.s.golden`'s
`r >> 2`). So a register is shifted by repetition for a count of 1 or 2 and
through CL from 3 up. `c1`'s `OP_LSHIFT`/`OP_RSHIFT` repeated the single-bit
form for any count - silently wrong from 3 up, a bug the corpus never
exercised. It now uses `emit_const_shift()` (threshold
`MCC_SHIFT_REPEAT_MAX` = 2). The kernel sources only show REGISTER
destinations, so `<<=`/`>>=`/`*=` on a memory operand with a count of 3 or
more are now refused rather than extrapolated. Lesson recorded below.

**Sanitizer sweep.** ASan/UBSan builds over the corpus plus 6000 random
programs found two pre-existing `mutos_c0` defects, both fixed: every
`T_STRING` token's malloc'd text leaked (no production consumes one yet;
`advance()` now frees `p->cur.sval`, and a future string-literal consumer
takes ownership by nulling it), and `parse_shift()` folded `(0 - 7) << 3`
with a left shift of a negative value (undefined behavior - now an unsigned
intermediate, with a count outside 0..15 an explicit error).

**Verification.** `make test` 34 of 62 byte-exact, 0 genuine mismatches; `c1`
alone on all 62 golden pairs changed only the 4 expected files (38 of 62
now match via `c1` alone); 3000 random scalar programs, old vs. new: `c0`
wire output identical, every `c1` output change one of the two intended
ones (AX-add, shift-by-CL), every new refusal a memory-operand shift;
3000 random 2-D programs: all 259 accepted outputs assemble. `05_matmul` is
now blocked only by its right-operand-first spill (`push di` ... `pop cx /
imul cx`), the evaluation-order problem the register-occupancy guard
already names.

### String literals (`05_arrptr/05_arrofptr`, `07_strlibc`), `09_abiprobe/01_argvmain`, and six older `mutos_c1` bugs found on the way (`mutos_c0`/`mutos_c1` extended and verified this session)

**The temp2 stream, finally used.** Every golden before `05_arrofptr` had a
2-byte `.2` file (just `EOFC`); `05_arrofptr.2.golden` is 82 bytes. Decoded
with `od -t x1z` and matched against `v7/cc/c00.c`'s `putstr()`, which sets
`strflg` so that `outcode()` writes to the string file instead of temp1:

```
70 fe 04 00          LABEL 4
c8 fe                BDATA
01 00 6f 00          (1, 'o')      one pair per byte
01 00 6e 00          (1, 'n')
01 00 65 00          (1, 'e')
01 00 00 00          (1, 0)        the terminating NUL, one more pair
00 00                0             ends the BDATA run
...                  LABEL 5 ... LABEL 6 ...
00 fe                EOFC
```

In temp1 the literal is `v7/cc`'s "fake a static char array": `NAME(SC_STATIC,
TY_CHAR, <label>)` - the same NAME shape a local static uses, the "offset"
being the label - followed by the `AMPER(9)` that decays it. `mutos_c0` now
reproduces both streams byte-for-byte for `05_arrofptr`, `07_strlibc` and
(temp2 only - its temp1 needs `char` access) `10_integ/04_strrev`. Two details
come from v7's source, not these goldens (no literal in them reaches 15
bytes): `putstr()` closes the run and opens a new one (`0 BDATA`) before the
15th, 30th, ... byte, and appends the NUL without that check; bytes beyond
10000 are dropped.

**Rendering, and 208 real literals that confirm it.** `mutos_c1` prints
`.data` after the code (every golden has it, strings or not - `v7/cc/c10.c`
prints `.globl` / `.data` at the same point) and then temp2: `L4:` (no
newline, like every label) and `.byte<TAB>/6f,/6e,/65,/0` - lower-case hex,
mutos_as's `/` prefix, no leading zeros - with a new `.byte` line after every
9th value (`07_strlibc.s.golden`'s 13-value literal: 9, then 4). v7's `c11.c`
prints each run on one line, in octal. The real, non-optimized MUTOS `c1`
output already in the repo settles how the two rules combine: all 208 string
blocks in `tests/mutos_as/kernel_nonopt/*.s` and `kernel_opt/*.s` have
exactly the line lengths "runs of 14, 15, 15, ... values, each broken 9 +
rest" predicts (a script over every `L<n>:.byte` block; e.g. `amx.s`'s
29-character `"AMX      Based %x level %d %s\n"` is 9/5/9/6/2). A literal
built from that same text by `mutos_c0`/`mutos_c1` reproduces `amx.s`'s `L22`
block byte-for-byte. A byte of 0x80 or above prints as its 8-bit value -
derived, not confirmed: the `/ff81`-style values in `amx.s` belong to a
char-array INITIALIZER (`.byte /ff81`, a space, one value per line - a
different code path).

**Escapes.** `c0_lex.c` now follows `v7`'s `mapch()` exactly: `\t \n \b \r`,
`\f` = 014, `\v` = 013, one to three octal digits (`\0`, `\012`, `\101`),
backslash-newline as a continuation, and any other escaped character standing
for itself (which is how `\\`, `\'` and `\"` work - `mapch()` has no case for
them). The previous lexer took only a single `\0` digit. A literal keeps an
explicit length, since `\0` can embed a NUL.

**Label numbering.** v7 allocates a literal's label when the token is LEXED
(`cval = isn++`); `mutos_c0` does it when the literal is PARSED. With v7's
one-token peeks these are the same moment in every realistic program (the
differences need a statement that starts with a string literal right after an
`if (...)` condition, after an if-statement without `else`, or as a
for-increment's first token). Checking this against `v7/cc/c02.c` turned up two
places where `mutos_c0` allocated labels in a different order than v7 -
invisible until an expression could allocate one: `for` allocates its two
labels before parsing the init expression, and `switch` its break label
before the controlling expression. Both fixed; no existing golden changes.

**Types.** `char`/`long` locals and parameters now take `*` and `[N]`
declarators (`char buf[20];`, `char *s;`, `char *names[3];` - an array of
element type 9 - and the parameter `char *argv[];`, type 41 per
`01_argvmain.1.golden`). Frame slots go through `v7`'s `rlength()` (size
rounded up to a word): `07_strlibc.1.golden`'s `src` at -24 and `dst` at -44.
A bare array name decays to `NAME(<element type>) AMPER(<pointer to
element>)` - previously hard-coded to `int`, and typed as an `int` result
(so `a + i` on an array was not pointer arithmetic). Prototypes take a
pointer result and comma lists (`char *strcpy();`, `int strlen(), strcmp();`);
a callee's NAME type is one FUNC degree via `incref()` - `strcpy`'s is 49,
which the old `ret_type | 020` would have made 25. A statement that is just a
call (`strcpy(src, "hello, mutos");`) is `NAME ... CALL` then `EXPR`.

**`char` element access is refused, and why.** Accepting `char buf[80]` made
`mutos_c0` reach the six `09_abiprobe/0N_frameNNN` files and emit a `.1`
that differs from their goldens: the real front end wraps a `char` element in
conversions `mutos_c0` does not insert. Opcode 109 (`OP_ITOC`), until now
only seen with type `TY_CHAR` (08_castsize's explicit `(char) i`), also
appears with type `TY_INT` - a char-to-int conversion:

```
buf[0] = 1;              ... STAR(1) CON(1) ITOC(1) ASSIGN(1)
buf[0] + buf[80 - 1]     ... STAR(1) ITOC(0) ... STAR(1) ITOC(0) PLUS(0)
```

and the matching `.s` is `movb *-84.(bp),*1.` / `movb ax,*-84.(bp)` / `cbw` /
`mov di,ax` / `movb ax,*-5.(bp)` / `cbw` / `add di,ax` - the first converted
value moved out of AX when the second one needs it. So opcode 109 is "convert
to/from char", its type argument the RESULT type. Until those conversions and
the byte loads/stores exist, reading or writing a `char` (or `long`) through a
subscript or pointer is refused in `mutos_c0`, which puts those six files back
where they were ("not yet supported") and records the shape for the next
session.

**`mutos_c1`.** A string literal's address is a new value kind,
`VK_STATICADDR`, rendered `#L4`: stored with one memory-immediate `mov`
(`05_arrofptr.s.golden`'s `mov *-10.(bp),#L4`) and passed as an immediate,
through DI (`07_strlibc.s.golden`'s `mov di,#L4` / `push di`); anywhere else
(arithmetic, a comparison, a dereference) it is refused. Pointer types are
accepted by type class instead of by a list of values (`ty_is_ptr()`,
`ty_is_word()`): a `char *` loads, stores, pushes and returns like an `int`.
A dereferenced call argument is loaded in place before the push
(`05_arrofptr.s.golden`'s `strlen(names[i])` -> `mov di,(di)` / `push di`,
not `push (di)`).

**The AMPER decision needed a real lookahead.** `OP_AMPER` emitted its `lea`
eagerly iff the next opcode was a NAME - meant for `a[i]`, whose index NAME
follows. A string argument broke it: in `strcpy(src, "...")` the array `src`
is followed by the literal's NAME, the eager `lea` left `&src` in DI, and
pushing the literal first (right to left) needed DI. It is now decided by
what CONSUMES the address: `scan_consumer()` walks the following opcodes
with a count of the values each pushes and pops (an arity table covering
every expression opcode) until one pops this value, then seeks back. Eager
iff the consumer is a pointer `PLUS` taking it as its left operand, the
right operand ends in `ITOP` and contains a variable - exactly `a[i]` and a
2-D row step; everything else defers. Output of every golden unchanged.

**A constant index on a pointer VALUE is a displacement.** `01_argvmain.s.
golden`'s `strlen(argv[1])` (`argv` a parameter) is `mov di,*6.(bp)` / `mov
di,*2.(di)`, not `add di,*2.` / `mov di,(di)` - `VK_IND` gained a
displacement, and pointer `PLUS` with a constant index, when the next opcode
is its `STAR`, produces an unloaded `VK_REGOFF` that `STAR` turns into
`*2.(di)`. `10_integ/03_linklist.s.golden` confirms the zero-offset case as
an assignment target (`cur->val = i;` -> `push *-8.(bp)` / `mov di,
*-10.(bp)` / `pop bx` / `mov (bx),di`); with a non-zero offset that golden
shows a different, right-operand-first shape (below), so that is refused.
With `char *argv[]` parameters now parsed, `01_argvmain` passes end-to-end.

**Six older bugs, found by testing around these changes** (all present in the
previous binary; none is in a golden):

| Source | Previous `mutos_c1` | Now |
|---|---|---|
| `if (*p)`, `return *p;` | `cmp <unpopped-ind-pending>,*0` (exit 0) | `mov di,p` / `cmp (di),*0`; the assignment-target test is `scan_consumer()` finding an `ASSIGN`, not "value stack empty" |
| `a = v[1];`, `f(v[2])` | stored / pushed `&v[1]`, `&v[2]` (`lea`) | refused / `push *-10.(bp)`: `STAR` of a folded constant address now yields a plain memory operand, not the address kind |
| `if (3 < a)`, `if (c < *p)`, `if (a < v[3])` | `cmp *3.,...`, `cmp *-6.(bp),(di)`, `cmp *-6.(bp),*-14.(bp)` - rejected only by `mutos_as` | a memory right operand of any kind is loaded into DI (the confirmed repair); the other two shapes are refused |
| `*a = *b;` | `mov (bx),(di)` | `02_bubsort.s.golden`'s `mov di,(di)` / `pop bx` / `mov (bx),di` |
| `*b = t;` | refused (memory-to-memory) | `02_bubsort.s.golden`'s `mov di,t` / `pop bx` / `mov (bx),di` |
| `f(square)` | `push #_square` (no such 8086 instruction) | through DI like any immediate |

and `a[i] += 2;` (a compound assignment through a subscript), which pushed a
garbage pending-address value, is refused. The `v[1]` bug is the instructive
one: every check used so far (goldens, prefix property, assembling fuzz
output) passes on it - the code is legal, just wrong. See "Recurring process
lessons".

**Verification.**
- `make test`: 37 of 62 byte-exact (up from 34 of 62; `05_arrofptr`,
  `07_strlibc`, `09_abiprobe/01_argvmain`), 0 genuine mismatches; zero
  warnings under `-Wall -Wextra -Wpedantic`.
- `mutos_c1` alone on all 62 golden pairs: 41 match their `.s.golden` (up
  from 38). Of those that stop, every partial `.s` is a byte-exact prefix of
  the golden except `02_bubsort` and `05_matmul` (both already failed that
  test before; both are the evaluation-order question below).
- 1500 random programs in the previous grammar: `mutos_c0`'s wire output
  identical to the previous binary for all; every `.s` difference or new
  refusal is a case where the previous output was garbage, did not assemble,
  or (7 programs) stored `&v[k]` for `v[k]`; the 28 newly accepted programs
  are the `*p = x;` swap shape above. 1500 random programs using literals,
  `char` arrays and pointers, pointer arrays and call statements: every
  accepted `.s` assembles with `mutos_as`.
- ASan/UBSan builds over the corpus and 1200 random programs: clean.
- `dump_temp.py` decodes BDATA runs; all three `.2.golden` files with
  literals now dump completely.

### Evaluation order - the plan for `10_integ/05_matmul`

`05_matmul` is the one golden whose `.1` `mutos_c0` already matches but whose
`.s` `mutos_c1` cannot produce, and two other goldens show the same
underlying behaviour:

```
sum = sum + a[i][k] * b[k][j];      (05_matmul)
    lea di,&b / mov si,k / sal si,*1 / add si,j / sal si,*1 / add di,si
    mov di,(di) / push di           <- RIGHT operand first, spilled
    lea di,&a / ... / add di,si
    mov di,(di) / mov ax,di / pop cx / imul cx / add ax,sum / mov sum,ax

for (i = 0; i < n - 1; ...)         (02_bubsort)
    mov di,*6.(bp) / dec di / cmp di,*-6.(bp) / ble L8
                                    <- operands swapped, relation reversed

cur->next = head;                   (03_linklist)
    mov di,*-6.(bp) / mov si,*-8.(bp) / mov *2.(si),di
                                    <- right-hand side first
```

All three are `v7/cc`'s code generator choosing an evaluation order by
subtree difficulty. `c12.c`'s `optim()` swaps a relational's operands (and
maps the operator through `maprel[]`) when `degree(tr1) < degree(tr2)`;
`degree()` (`c11.c`) is -3 for a constant, -2 for `&x`, 0/1 for a leaf, and
otherwise the node's Sethi-Ullman-style `degree` (`d1 == d2 ? d1 + islong :
max(d1, d2)`); `dcalc()` turns that into 20/24 ("fits in the registers left"
or not), which the code tables use to evaluate the harder operand first and,
when both are hard, to compute the right one onto the stack.

`mutos_c1` cannot do any of this today because it emits each opcode's code
as it reads it: by the time `TIMES` is read, the left operand's code is
already out. The plan keeps every existing handler and changes only the
ORDER in which the dispatch loop visits parts of temp1 - "subtree replay":

1. **One arity table.** `scan_op_args()` (added for `scan_consumer()` this
   session) already knows every expression opcode's arguments and arity;
   make it the single source for both uses.
2. **Per-statement pre-scan.** At the first opcode of an expression (after
   the previous statement's `EXPR`/`CBRANCH`/`RFORCE`), scan to its end and
   build an index: for each operator, the file offsets of its left and
   right operand subtrees (postfix, so each is one contiguous range), and
   each subtree's v7 `degree`.
3. **Replay.** When the loop reaches a left-operand range whose parent is
   marked "right first", it seeks to the right range, generates it with the
   ordinary handlers, then the left range, then skips the right range when
   it comes up again in the stream and handles the parent. A spilled right
   operand becomes a new value kind (`VK_STACKED`, consumed by `pop cx`); a
   swapped relational is only a reordering plus `maprel`.
4. **Only the golden-confirmed decisions.** `TIMES` with two hard operands
   (`05_matmul`), the relational swap (`02_bubsort`), and an assignment
   whose target is an offset-indirect (`03_linklist`) - each gated on its
   own golden; every other reordering stays refused.
5. **Verification.** The three goldens; `mutos_c1` alone on all 62 golden
   pairs unchanged elsewhere; the prefix property for the two files that
   fail it today; the random-program differential; and ideally an
   8086-emulator run of fuzzed programs, since reordering is exactly where
   legal-but-wrong code (the `v[1]` bug above) would hide.

`02_bubsort` and `03_linklist` need more than this (struct types for the
latter), so `05_matmul` is the one file the work would finish on its own.

### Evaluation order - implemented for `10_integ/05_matmul` (`mutos_c1` extended and verified this session)

**The v7 mechanism, confirmed from source.** `v7/cc/table.s`'s `*` entry
(`cr42`) tries, in order, `%n,aw` (right operand addressable), `%n,ew*` /
`%n,e` (right operand fits in the registers left over), and `%n,n`, whose
template is `SS` / `F` / `mul (sp)+,R`. In `c10.c`'s `cexpr()` a template
letter `S` is the right subtree and a second `S` routes it through the
stack table, so `%n,n` computes the RIGHT operand onto the stack first,
then the left into the working register - exactly `05_matmul.s.golden`:

```
lea di,&b / mov si,k / sal si,*1 / add si,j / sal si,*1 / add di,si
mov di,(di) / push di                                   SS
lea di,&a / mov si,i / sal si,*1 / add si,k / sal si,*1 / add di,si
mov di,(di)                                             F
mov ax,di / pop cx / imul cx                            mul (sp)+,R
```

Which operand is "right" comes from `c12.c`'s `acommute()`: `insert()`
keeps commutative operands sorted by descending `degree()`, moving a term
back only past a strictly LOWER degree, so two equal-degree operands stay
in source order - `b[k][j]` is the spilled one because it is the source's
right operand.

**Implementation ("subtree replay", as planned).** `c1_gen.c`'s new
"Evaluation order" section:

- `plan_expression()` runs at the first opcode of every expression (a
  leaf, with the value stack empty). `prescan_expr()` walks temp1 to the
  expression's terminator (`EXPR`/`CBRANCH`) with `scan_op_args()` - the
  arity table `scan_consumer()` already used, now its second client -
  building an `ENode` index: each node's own opcode offset, its subtree's
  start offset (postfix: every subtree is one contiguous byte range), and
  its operands. It then restores temp1's position.
- `order_right_first()` is the only decision taken: int `TIMES` whose two
  operands are both `is_2d_elem_read()` - the exact wire shape
  `emit_subscript_2d()` writes for `m[i][j]` with two plain-variable
  subscripts. If no node qualifies, no plan is made and the expression
  streams exactly as before (hence: every other golden and every fuzzed
  program without such a product produces byte-identical output).
- `plan_build()` turns the index into a `Plan` of `SEG_RANGE` steps
  (merged when adjacent) plus the two value-stack steps a reordered
  operator needs: `[right] SPILL [left] SWAP [operator ...]`.
  `plan_step()` executes it at the top of the dispatch loop - seeking to
  each range, letting the ORDINARY handlers read their own opcodes there,
  and stopping with an internal error if a handler ever reads past a
  range's end. Handler lookaheads (`OP_AMPER`'s `scan_consumer()`,
  `OP_STAR`'s `&*` cancel, `OP_PLUS`'s displacement peek) are unaffected,
  since a subtree is replayed whole and still sees temp1's real structure
  around it.
- `plan_spill()` loads a dereferenced value in place and pushes it (`mov
  di,(di)` / `push di`, the same shape a dereferenced call argument gets)
  and leaves the new `VK_STACKED` in its place; `plan_swap()` restores
  left/right order on the value stack. `OP_TIMES` pops a `VK_STACKED`
  right operand as `mov ax,<left reg>` / `pop cx` / `imul cx`.
  `pop_val()` and `discard_val()` refuse `VK_STACKED`, so no other
  consumer can silently unbalance the machine stack.

**Why the gate is narrower than v7's rule.** By `c12.c`'s `degree()`
(`islong()` is 1 for `int`; `TIMES` by a power of two keeps its operand's
degree), a 1-D element `a[i]` has degree 1 - the same as a 2-D element -
and both are a `STAR` over a computed address, so `dcalc()` is the same
too. The real compiler therefore very probably spills `a[i] * b[j]` the
same way. But no golden shows it, so it stays refused (by the
register-occupancy guard, as before). The MUTOS register budget itself
(`nrleft` at the match) cannot be recovered from one golden: all it shows
is that a degree-1 non-addressable right operand did not fit.

**Also noticed, not changed.** `tests/mutos_as/kernel_nonopt/*.s` has no
`mov ax,(reg)` anywhere - a value behind a computed address reaches AX
through the working register (`mov di,(di)` / `mov ax,di`, as in this
golden), and only an addressable operand (`mov ax,*10.(di)`, amx.s) is
loaded straight into AX. `c1` currently emits `mov ax,(di)` for e.g.
`x = a[i] * y;`, which is therefore probably not the real shape. No
golden covers it.

**A pre-existing `mutos_c0` bug, found by the semantic check below.** A
constant LEFT operand of a binary operator is emitted on the RIGHT: `7 -
x`, `7 / x`, `7 % x`, `7 << x` and `7 < x` produce the same temp1 bytes as
`x - 7`, `x / 7`, ...; `2 - 9 - x` becomes `x - (-7)`. Cause:
`c0_parser.c` keeps a constant unmaterialized (`ExprVal.is_const`) so it
can fold it with a constant sibling, and `emit_materialize()` writes it
only when the operator is reached - after the right operand's own bytes
have been streamed. v7 does not do this (`c01.c`'s `fold()` folds only
when BOTH operands are constants and never reorders), so the real front
end writes `CON 7, NAME x, MINUS`. No corpus file has a constant left
operand, which is why every `.1.golden` still matches. Silent wrong code
for any non-commutative operator; harmless (but not v7's wire order) for
`+`/`*`/`&`/`|`/`^`. Not fixed in this change - it is a `c0` change with
its own verification; fixed the same day (next section). The fix is to buffer
the right operand's bytes while the left one is still a pending constant
(the `open_memstream()` technique `for`'s deferred increment already
uses) and emit `CON` first unless the right operand folds too.

**Verification.**

- `make test`: 38 of 62 byte-exact end-to-end (up from 37 of 62:
  `10_integ/05_matmul`), 0 genuine mismatches; `mutos_as` 67/67 and
  `mutos_cpp` 5/5 unchanged; zero warnings under `-Wall -Wextra
  -Wpedantic` from a clean build.
- `mutos_c1` alone on all 62 golden `.1`/`.2` pairs, against the previous
  binary: `.s`, stderr and exit status identical for 61; only `05_matmul`
  changed, and now matches (42 of 62 match via `c1` alone, up from 41).
  Every file that still stops is a byte-exact prefix of its golden except
  `02_bubsort` (the relational swap, unchanged).
- A semantic check, as the lesson below asked for: a small interpreter for
  the instruction subset `c1` emits (validated first on real goldens:
  `05_matmul` returns 134, `02_array2d` 138, `01_arrbasic` 30, `04_for`
  45, `01_intarith` 2 - all correct by hand) runs every accepted output of
  a random-program generator built around 2-D arrays, and compares the
  final value of every scalar AND every array element with the value the
  generator computed (16-bit wraparound). Over 3500 such programs: every
  output that differs from the previous binary's is a program containing
  a product of two 2-D elements that the previous binary refused; every
  accepted output assembles with `mutos_as` and ends in exactly the
  expected state (690 programs executed, 593 of them through the new
  spill, 116 with the spill nested inside a pending indirect store - `push`
  address, `push` operand, `pop cx`, `pop bx`). 1500 scalar-only programs:
  output identical to the previous binary for all.
- Hand-written edge cases: a plan whose first step is not at the current
  position (`return a[i][k] * b[k][j];`), a loop condition, a `for`
  increment (whose bytes `c0` defers past the body), a call argument - all
  correct; two such products in one statement, `a[i] * b[j]`, and a
  `register` subscript are refused with explicit diagnostics.
- ASan/UBSan build of `mutos_c1` over the 62 golden pairs and 800 fuzzed
  programs: clean.

### Constant left operands, and semantic fuzzing (`mutos_c0`/`mutos_c1` fixed, `tests/mutos_cc/fuzz/` added, verified this session)

**The `mutos_c0` bug, and its three other faces.** `ExprVal` keeps a
constant unwritten so it can fold with a constant sibling;
`emit_materialize()` wrote it only when the operator was reached - after
the right operand had streamed its own bytes. `git log -S` puts the
pattern in `mutos_c0`'s first commit (`0688fbe`, 2026-09-14). Probed end to
end (x=5, y=3, c=1) before the fix: `7 - x` gave -2, `2 - 9 - x` 12, `7 -
x * y` 8, `7 < x` 1, `c ? 7 : y` 3 (the branches swapped, since `COLON`
received them in the wrong order), `y + (x = 1, 5)` 7 (a constant last
comma item was written after its `SEQNC`, which then paired with `y`'s
slot); and `z = 0 ? x : 3;` failed in `c1` with "internal: 2 unconsumed
expression values", because a constant `?:` condition returned one
branch while the other's bytes stayed in the stream.

v7 settles what the stream must be. `c01.c`'s `fold()` handles `+ - * /
% & | ^ << >>`, the unary `-`/`~` and the six comparisons, only when both
operands are constants, and never reorders them; `build()` calls
`fold(QUEST, ...)` only when the condition AND both `COLON` branches are
constants, and otherwise builds the tree; `SEQNC`, `&&` and `||` are
never folded. (`mutos_c0` does fold `&&`/`||` of two constants - a
wire-level divergence with no semantic effect, left as is.)

The fix (`c0_parser.c`): while a constant left operand is pending, the
right operand is parsed into an `open_memstream()` buffer
(`rhs_begin()`); `rhs_end()` then leaves a constant pair to the caller's
fold, or writes the left `CON` first and the buffered bytes after it -
the technique `parse_for_stmt()`'s deferred increment already used. It
is applied at all 13 binary-operator sites. `parse_expr()`'s `?:`
buffers its true branch while the condition is a pending constant and
its false branch while the condition or the true branch is, folds only
when all three are constants, and otherwise writes condition, true
branch, false branch, `COLON`, `QUEST` in that order; the comma list
materializes its last item before `SEQNC`. `rhs_end()` treats a constant
right operand that wrote bytes as an internal error (a constant never
writes anything).

**The `mutos_c1` counterpart.** The first fuzz run against the previous
build showed regressions: `7 | e` had compiled (correctly, `|` being
commutative) and was now refused. v7's `c1` moves constants right before
choosing code - `c12.c`'s `acommute()` sorts a commutative operator's
operands by descending `degree()` (a constant's is -3, below everything),
and `optim()` exchanges a relational's operands when `degree(left) <
degree(right)`, mapping the operator through `c10.c`'s `maprel[]`. The
old `c0` bug had been doing that for `c1`, by accident and for every
operator. `mutos_c1` now does it itself: `constant_to_right()` for `+ * &
| ^` (int and long), and a `mirror` column in `RELOPS` for the
relationals. A constant emits no code (`VK_IMM`/`VK_LCON`), so exchanging
the two values in the handler is exactly the same as receiving them in
the other order: the output for every commutative or equality operator
with a constant left operand is byte-identical to before the `c0` fix.
The general relational swap (two non-constant operands - `02_bubsort`)
still needs the evaluation-order plan.

**`tests/mutos_cc/fuzz/`.** `x86sim.py` interprets the instruction subset
`c1` emits for a call-free `main()`; every golden it can execute (30 of
62) returns its C source's value, struct and bit-field programs
included. It refuses rather than guesses - notably a conditional branch
on flags that a non-`cmp` instruction has since changed. `fuzz_c.py`
builds each program as a syntax tree, evaluates it under C semantics on a
16-bit `int` (short-circuit included), and compares every scalar and
array element at every statement boundary: a marker `m = 101;` before
each statement makes `x86sim.py` snapshot the frame. The markers were
added after the first large differential run showed "regressions" that
were really masked wrong code - a wrong value a later statement
overwrote. Generator constraints keep the expected values well-defined
(subscripts are constants 0/1 fixed after initialization; the side-effect
variables `u` and `n` are never read otherwise, at most one side effect
per statement) and raise the share of accepted programs (no constants in
truth contexts, no constant divisors, right operands mostly simple,
frames of at most 80 bytes): about 30% of programs compile with arrays,
60% without. `--baseline` classifies every difference against an
earlier build with no knowledge of the change.

**Two more `mutos_c1` wrong-code bugs, pre-existing, not fixed here** (then
`STATUS.md`'s open items 6 and 7; fixed later the same day - see the next
section). In a value context, `?:` branches and
a `&&`/`||` right operand are computed before the branch code, so their
side effects happen even when C skips the operand (`z = x ? y++ : 4;`
increments `y` for `x` = 0). And a postfix `++`/`--` in an `if`/`while`
condition waits for the next `EXPR` opcode - inside the branch taken
when the condition holds - so `while (n-- > 3)` ends with `n` one too
high. Both were confirmed identical in the previous build. The fuzzer's
default avoided both; `--side-effects-in-conditionals` and
`--postfix-in-conditions` generated them (both flags were removed with the
fix).

**Verification.**

- `make test` (clean build): 38 of 62, 0 genuine mismatches, zero warnings.
- `mutos_c1` alone on the 62 golden `.1`/`.2` pairs and `mutos_c0` on the 62
  `.i` goldens, each against the previous build: identical (`c0` differs
  only in the partial output of one compile that fails with the same
  diagnostics - error recovery turns an undeclared name into constant 0,
  which is now written on the left).
- 6000 fuzzed programs (four seeds with arrays, two scalar-only) against
  the previous build: 2356 compile, all correct at every statement
  boundary, 0 BAD. 198 go from WRONG to correct, 9 from WRONG to an
  explicit refusal, 202 from refused to correct, 7 from `c1`'s internal
  error (the orphaned `?:` bytes) to an explicit refusal.
- 51 programs changed but were correct in both builds. Each was re-run
  with up to 60 different initial values: the new build was correct on
  every variant, and a variant proves the old output wrong for 31 of
  them; for the other 20 the reversed order is unobservable (`if (12 -
  i)` tests the same as `if (i - 12)`; `(... ) == 2` of a 0/1 value) or
  sits in a branch no variant took.
- 4 programs the old build compiled "correctly" are now refused. Three
  computed a wrong value that happened to be invisible (`0 - a[j][k]`
  with that element 0; a truth-tested `7 - b[k][i]`; a truth-tested `(3 -
  a[k][i]) - 8`), in a `const - <compound>` shape `c1` now declines (no
  golden shows v7's register choice for it). The fourth is `y = (~9 ? (t
  % s) : ~2);`: `c0` now emits v7's real `QUEST` tree for the constant
  condition, and `c1` has no confirmed shape for one.
- ASan/UBSan builds of both passes over 1500 fuzzed programs (including
  300 with both known-bug flags on) and the golden suite: clean.

### Conditional evaluation: `&&`, `||`, `?:`, `,` and postfix `++`/`--` in conditions (`mutos_c1` fixed and verified this session)

Closes the two wrong-code bugs the fuzzer found in the previous session
(`STATUS.md`'s former open items 6 and 7). Both have one root cause:
`mutos_c1` streams temp1 in postfix order, so every operand's code was
out before its operator's branches, and a postfix fixup waited for the
statement's `EXPR`.

**What v7 does (`v7/cc/c10.c`, `c11.c`).**

- `rcexpr()` calls `delay()` - "`x + y++` is better treated as `x + y;
  y++`" - only when `table != cctab && table != cregtab`. Under `cctab`
  (a condition) a postfix operator is compiled in place: the `regtab`
  fallback loads the old value, does the increment (`efftab`), and the
  fixup code prints `tst r` for the condition codes. A comparison with a
  constant 0 is reduced to its left operand first (`rcexpr()`'s first
  test: `RELAT && tr2 == CON 0 && table == cctab`), so `n-- > 0` is the
  same `tst r` plus a branch.
- `cbranch()` turns `&&`/`||`/`!` into jumping code recursively (`LOGAND`
  with cond 1 and `LOGOR` with cond 0 allocate one label of their own),
  runs a `SEQNC`'s left operand with `efftab` (where `delay()` applies)
  before branching on its right one, and compiles anything else with
  `rcexpr(tree, cctab)` plus one branch.
- `cexpr()` computes a value-context relational/`&&`/`||`/`!` as
  `cbranch(tree, c=isn++, 1)` then `czero` / `jbr` / `c:` / `cone` /
  `label(isn++)`, and `?:` as `cbranch(tr1, c=isn++, 0)`, the true arm,
  `jbr r=isn++`, `c:`, the false arm, `r:`. Each arm is `rcexpr(arm,
  regtab)`, whose own `delay()` finishes the arm's postfix fixups inside
  it.
- `CALL` pushes each argument through `sptab` - with `delay()` - so an
  argument's postfix fixup follows its own push.

**Real-hardware evidence.** The kernel's non-optimized compiler output
has the postfix-in-condition shape five times (line numbers of the
`mov`):

```
lp_AC.s:251  L23:mov dx,si / dec si / or dx,dx / beq L22          truth test
lp_AC.s:317  L32:mov dx,*-10.(bp) / dec *-10.(bp) / or dx,dx / beq L33
lp_AC.s:585  L74:mov dx,*-10.(bp) / dec *-10.(bp) / or dx,dx / ble L75   > 0
sys1.s:340   L37:mov dx,_execnt / dec _execnt / cmp dx,*2. / blt L38      >= 2
sys1.s:646   L59:mov dx,*-54.(bp) / dec *-54.(bp) / or dx,dx / beq L60   while (n--)
```

`dx` there because those functions keep register variables in DI and
SI; `kernel_opt/delay.s` (`while (d--)`, no register variables) has `mov
di,*4.(bp) / dec *4.(bp) / test di,di / je L3` - `c2` rewrites `or` to
`test` and `beq` to `je` (compare `lp_AC.s` in both directories), so the
non-optimized form is `or di,di / beq`. MUTOS renders v7's two `tst`
spellings differently: the `cctab` template `tst A1` (an addressable
operand) is `cmp A1,*0` (`03_rellogic.s.golden`, and `cmp di,*0` for a
register variable, `sys1.s:208`), while the fixup's `tst r` is `or r,r`.
The same `or ax,ax` follows calls whose result is tested (`sys1.s:121`,
`sys1.s:1204`, `lp_AC.s:728`, `amx.s:3146`) - `mutos_c1` still emits
`cmp ax,*0` there (not changed: a shape difference, not a bug; see
`STATUS.md`'s "Next up"). `sys1.s` also has `clearseg(a++)` as `push
*-56.(bp) / inc *-56.(bp) / call _clearse`. And `10_integ/01_wordcount.
s.golden` shows jumping code for an `||` in an `if` (v7's `LOGOR` with
cond 0: `cmpb ...,*32. / beq L10000 / <second operand> / cmpb ...,*10.
/ bne L9 / L10000:`) - `mutos_c1` materialized that as 0/1 and tested
it again.

**Implementation (`c1_gen.c`).** Any expression containing `LOGAND`,
`LOGOR`, `QUEST` or `SEQNC` is now generated through the
evaluation-order plan; the streaming handlers for those opcodes (and
`COLON`) are gone, together with `gen_logand()`/`gen_logor()`/
`gen_quest()` and `VK_PAIR`. `plan_value()` (value context) and
`plan_cbranch()` (a condition - for a `CBRANCH`-terminated expression
the plan covers the `CBRANCH` too) transcribe `cexpr()` and `cbranch()`:
operand ranges interleaved with new steps - `SEG_ALLOC`/`SEG_LABEL`
(c1 labels, allocated when the step runs, so nested constructs number
their labels in v7's order), `SEG_BRANCH` (one condition:
`gen_cond_branch()`), `SEG_LOGVAL` (`gen_logval()`: the 0/1 tail, also
used by `materialize_cond()` now), `SEG_QTRUE`/`SEG_QFALSE` (an arm's
value into DI, its fixups, the jump and labels), `SEG_DISCARD` (a comma's
left operand) and `SEG_GOTO` (past the opcodes the plan stands for). The
plan's type checks reproduce the old handlers' refusals. A `!` of a plain
value and a bare relational stay lazy `VK_COND`s, whose output was
already v7's.

Postfix fixups are scoped by REGION: `SEG_MARK` opens one for each
condition, `?:` arm and comma left operand (`region_open()` records the
queue index and raises `defer_floor`), and its closing step emits the
fixups queued since (`region_close()`). A fixup queued before the region
stays queued, so it still happens on every path. `gen_cond_branch()`
emits a condition's fixups after its operand's load and before the
compare (the branch reads the flags), and uses `or reg,reg` when the
tested value is a postfix operand's old value (new `postfix` flag on
`Val`/`SimpleVal`, set only by `gen_incdec()`'s AFT case) tested for
truth or compared with 0. `gen_call()` emits the current region's fixups
before `call`. A simple condition (no `&&`/`||`/`?:`/`,`) still streams;
its `CBRANCH` handler now calls `gen_cond_branch()` too. `gen_fatal()` is
`_Noreturn` now (the new switch statements needed it to compile without
fall-through warnings).

**Found on the way (pre-existing).** A `long` comparison consumed as a
VALUE (`z = l > 0L;`) went through `materialize_cond()` into
`emit_cmp_and_branch()`, which renders a 16-bit compare and printed the
unmaterialized constant's placeholder as an operand (`cmp
*-8.(bp),<unmaterialized-long-const>`, exit status 0) - although
`cond_is_long`'s comment said every consumer but `CBRANCH` refused one.
`emit_cmp_and_branch()` now does; a condition reaches `gen_long_cmp()`
first, as before.

**Deliberately not changed.** The `or` shape only for a postfix
operand (call results and other computed values: the next step, above).
A postfix operand is still loaded into DI eagerly, so `f(a++)` is `mov
di,a / push di / inc a / call` where the kernel pushes the memory operand
directly; making it lazy needs care (another argument's call would flush
its fixup before the lazy push). A prefix `++`/`--` in a condition is
unchanged (`dec n / mov di,n / cmp di,*0`); the only kernel sample is
optimized (`dec (di) / jg`, `tty.s`'s `if (--q->c_cc <= 0)`), so the
non-optimized shape is unknown.

**Verification.**

- `make test` (clean build): 38 of 62, 0 genuine mismatches, zero warnings.
  `mutos_c1` alone on the 62 golden pairs: `.s`, stderr and exit status
  identical to the previous build for all 62 - no corpus `&&`/`||`/`?:`
  operand has code of its own, and the label order is unchanged for
  them.
- `fuzz_c.py` generates side effects in conditionally evaluated operands
  and postfix operators in `if` conditions by default now. 9000
  programs, six seeds (three scalar-only), against the previous build: 0
  WRONG, 0 BAD, 0 regressions; 88 wrong -> correct, 339 refused ->
  correct, 1 BAD -> correct (`t = ((11 | t) ? x : (19 / i));` with `i` =
  0: the old build divided by zero in the untaken arm). Every one of the
  499 programs whose output changed contains `&&`, `||`, `?:`, a comma
  operator or a postfix operator.
- 25 hand-written programs the generator cannot produce (loop conditions,
  nested and `&&`-conditioned `?:`, `!(x && y)`, a comma in a condition,
  `z = (n++, n + 1)`, `x ? y / x : 1`), executed with `x86sim.py` (taught
  that `or r,r` sets the flags of `cmp r,0`; it still executes the same
  30 of the 62 goldens, with the same values) and compared variable by variable with the host
  C compiler on `short` locals: 24 correct, 1 correct refusal (register
  guard). The previous build: 10 wrong, `do ... while (n-- > 0)` never
  terminated, `x ? y / x : 1` trapped, 3 refused.
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 62 golden pairs and
  1200 fuzzed programs: clean.
- After the `long` guard: `make test`, the 62 golden pairs and the
  hand-written cases unchanged; 3000 more programs (seeds 4 and 14) 0
  WRONG, 0 BAD, 0 regressions.

### `char` element access, call arguments right to left, and `02_bubsort`'s comparison shapes (`mutos_c0`/`mutos_c1` extended and verified this session)

Eight more corpus files byte-exact end-to-end (46 of 62, up from 38 of
62): the six `09_abiprobe/0N_frameNNN` files, `10_integ/04_strrev` and
`10_integ/02_bubsort`. Three pieces of work, each derived from the
goldens before any code was written, plus two pre-existing silent
wrong-code bugs found on the way.

**1. `char` conversions on the wire (`mutos_c0`).** Vanilla v7 has no
char conversions to speak of: `c01.c`'s `lintyp()` puts `CHAR` and `INT`
in the same row of `cvtab[]`, and the PDP-11's sign-extending `movb`
widens a char for free. The MUTOS front end inserts explicit ones, with
opcode 109 (`OP_ITOC`) carrying the RESULT type:

```
buf[0] = 1;            NAME(1) AMPER(9) CON CON ITOP PLUS STAR(1) CON(1) ITOC(1) ASSIGN(1)
buf[0] + buf[79]       ... STAR(1) ITOC(0) ... STAR(1) ITOC(0) PLUS(0)      (09_abiprobe)
n.b[0] + n.b[1]        ... STAR(1) ITOC(0) ... STAR(1) ITOC(0) PLUS(0)      (06_union)
return buf[0];         ... STAR(1) ITOC(0) RFORCE(0)                         (04_strrev)
t = *a;  *a = *b;      ... STAR(1) ASSIGN(1)    - no conversion, char = char (04_strrev)
c = (char) i;          NAME(i) ITOC(1)                                        (08_castsize)
l = (long) c;          NAME(c) CTOL(6)                                        (08_castsize)
```

The consistent reading is v7's `build()` with char given its own
`cvtab[]` row: every operand of a binary arithmetic, shift, bitwise,
relational or equality operator that is a char is widened first - BOTH
operands of `c + c`, which one `cvtab[]` lookup could not do, so MUTOS
promotes each operand before the lookup - and an assignment converts its
right-hand side to the target's type (int -> char `ITOC(1)`, char -> int
`ITOC(0)`, char -> long `CTOL`, char -> char nothing). v7's `build()`
runs the same conversion code for a cast and an assignment (`if
(dope&ASSGOP || op==CAST)`), so `(char) i` and `(long) c` confirm the
assignment conversions too, and `doret()` returns through a `build(ASSIGN)`
to the function's own type, which is where `return buf[0];`'s `ITOC(0)`
comes from. `c0_parser.c`: `promote_char()` (after each binary operand -
for the LEFT one before the right operand's bytes are parsed, so the
`ITOC` lands in postfix position), `convert_assign()` (every `=`: the
assign statement, `*p = ...`, an embedded `x = ...` in a comma list; it
also absorbs the old `ITOL`-for-a-long-target special case), a `char ->
int` cast (`ITOC(0)`, the same path), and the `return` in an int
function. Where v7 converts nothing - a condition, an operand of
`&&`/`||`/`!`/`~`/`?:`, a call argument, a comma operator's last operand
- no golden shows what MUTOS does, and `mutos_c1` would need a byte test
or push; `char_value_refused()` stops there.

`'&' IDENT '[' expr ']'` (the address of one element) is new too:
`build(AMPER)` of the subscript's `STAR`, so the element reference plus
`AMPER(pointer to element)` - `04_strrev.1.golden`'s `swapch(&s[lo],
&s[hi])`. And the `if (...) return;` "simpif" shortcut v7's `c02.c` has
alongside the `goto`/`break`/`continue` ones (`if (nextchar()==';') {
o2 = retlab; goto simpif; }`): one `CBRANCH(retlab, cond=1)`, no label
of its own - `04_strrev.1.golden`'s `if (lo >= hi) return;`. With these,
`mutos_c0` reproduces all eight files' `.1`/`.2` goldens (and
`02_bubsort`'s, which only needed the `&`).

**Found on the way (pre-existing, `mutos_c0` and `mutos_c1`): a plain
`char` variable used as an int was read as a word.** `char c; ... return
c;` compiled to `mov di,*-6.(bp)` / `mov ax,di`, and `a + b` of two char
locals to `mov di,*-6.(bp)` / `add di,*-8.(bp)` - `mutos_c0` emitted the
char `NAME` with no conversion, and `mutos_c1` treated every `NAME` as a
word, so the value picked up the slot's unused high byte (a char local
has a whole word of frame, `08_castsize`). Exit status 0, no diagnostic.
The conversions above fix the wire; `mutos_c1` now also refuses any char
operand a consumer was not written for (see below), so a missing
conversion can no longer reach a word instruction silently.

**2. Byte operands (`mutos_c1`).** Two ideas carry it:

- `Val.bytev` marks a memory operand that holds a char (a char `NAME`, a
  char `STAR`). Only a byte instruction may touch it, so `pop_val()`
  refuses it and the char-aware consumers - a char `ASSIGN` (either
  side), `ITOC`, `CTOL`, `AMPER` - pop with `POP_BYTE`.
- `VK_CHARX` is `ITOC(TY_INT)` of such an operand, left unloaded:
  loading a char as an int is always `movb ax,<mem>` / `cbw` (CBW is
  fixed to AL/AX), and in `buf[0] + buf[79]` both operands need AX, so
  the golden loads the left one, moves it aside, then loads the right
  one: `movb ax,*-84.(bp)` / `cbw` / `mov di,ax` / `movb ax,*-5.(bp)` /
  `cbw` / `add di,ax`. Loading at the `ITOC` would have had to
  overwrite the first value. v7's own `optim()` does the same in effect
  (`c12.c`'s `ITOC` case turns "ITOC of a NAME" into a char-typed NAME
  that the consuming template loads). Confirmed consumers only, via
  `POP_CHARX`: int `PLUS` of two chars, `RFORCE` (`04_strrev`'s `return
  buf[0];` -> `movb ax,*-24.(bp)` / `cbw`, already in the return
  register), and an int `ASSIGN`'s right-hand side (`movb ax,...` /
  `cbw` / `mov <lhs>,ax` - no corpus golden, but four instances in
  `tests/mutos_as/kernel_nonopt/amx.s`, the real non-optimized
  compiler's output). Everything else refuses a `VK_CHARX`: a char with
  ONE int operand computes in AX in the kernel (`movb ax,*23.(bx)` /
  `cbw` / `and ax,*-2.`), a shape `mutos_c1`'s `PLUS` would not produce;
  `MINUS`'s operand order, comparisons (`cmpb`, see below) and call
  arguments are unconfirmed.

Where a char element's address comes from decides its operand
(`OP_STAR`'s `TY_CHAR` case): a compile-time-known address is the stack
slot itself (`movb *-84.(bp),*1.`); a pointer VARIABLE is loaded into
DX and copied to BX, the byte addressed as `(bx)` - `04_strrev`'s
`swapch()`: `mov dx,*4.(bp)` / `mov bx,dx` / `movb dx,(bx)` (the int case
loads into DI instead), and the same `mov dx,*-6.(bp)` / `mov bx,dx` /
`movb ax,(bx)` / `cbw` in `kernel_nonopt/lp_AC.s`; an address already in
a base register is `(di)` (`amx.s`: `movb ax,(di)`). A char `ASSIGN`
routes a char source through DX, the byte working register `08_castsize`
already showed for `(char) i` - `movb dx,(bx)` / `movb *-6.(bp),dx`, and
before the `pop bx` of a pushed target: `*a = *b;` -> `push *4.(bp)` /
`mov dx,*6.(bp)` / `mov bx,dx` / `movb dx,(bx)` / `pop bx` / `movb
(bx),dx`; `*b = t;` -> `push *6.(bp)` / `movb dx,*-6.(bp)` / `pop bx` /
`movb (bx),dx` (the int `swap()` in `02_bubsort` pushes the same way). An
`ITOC(1)` of a constant folds to the sign-extended low byte (v7's `p->
value << 8 >> 8`), so `buf[0] = 1;` is one `movb`. A constant stored
through a char pointer and a constant index on a char pointer (`p[2]`)
have no golden and are refused.

The frame goldens also settled a rendering rule: a displacement outside
-128..127 takes the `#` marker exactly like an immediate - `movb
#-132.(bp),*1.` / `movb ax,#-132.(bp)` in `03_frame128` ... `07_frame300`,
while `*-5.(bp)` in the same files keeps `*`. `render_operand()` had
`*` unconditionally (no earlier golden had a larger displacement).
`kernel_opt` mostly agrees (`#610.(di)`), with some `*610.(bx)` forms
`c2` rewrote; the non-optimized kernel has no such displacement.

`&*` (an element's address, `STAR` followed by `AMPER`) cancels for a
1-D element too now, leaving the address computation's result; and an
`ITOP` scaling by 1 (a char index) leaves a plain variable index in
memory - v7's `optim()` drops a multiplication by 1 - so `&s[hi]` is
`mov di,*4.(bp)` / `add di,*8.(bp)`, the pointer loaded and the index
added, as the golden has it.

**3. Call arguments right to left.** v7's `comarg()` (`c10.c`) compiles
a call's arguments from the last to the first, each one straight onto
the stack before the next is started. `mutos_c1` streamed the arguments
left to right and pushed them afterwards (`gen_call()`), which is the
same bytes as long as at most the LAST argument has code of its own -
true for every earlier golden - but `reverse(s, lo + 1, hi - 1)`
computed `lo + 1` into DI and then `hi - 1` over it (a register-guard
refusal). A call with two or more arguments, any but the last of which
has code, is now generated through the evaluation-order plan
(`plan_call()`): the callee's `NAME` (no code), each argument from the
last to the first followed by `SEG_PUSHARG` (`push_call_arg()`), then
`SEG_CALL` (`finish_call()`) - `04_strrev.s.golden`: `mov di,*8.(bp)` /
`dec di` / `push di` / `mov di,*6.(bp)` / `inc di` / `push di` / `push
*4.(bp)` / `call _reverse` / `add sp,*6.`. A call planned for another
reason (a `&&`/`?:` inside an argument) takes the same order.
`gen_call()` is split into `push_call_arg()`/`finish_call()`, shared by
both paths.

**Found on the way (pre-existing, `mutos_c1`):** a comparison used as a
call argument (`f(a < b)`) reached the push unmaterialized and was
written as `push <unmaterialized-cond>`, exit status 0. `push_call_arg()`
materializes it (0/1 in DI, `push di`).

**More pre-existing silent wrong code, found while writing the
hand-written checks - all around pointers, which the fuzzer does not
generate.** Each now stops with an explicit "not yet supported" (or, the
last one, is fixed):

- `q - 1` on an `int *` subtracted one BYTE and `q - p` gave the byte
  difference (`mutos_c0` built a plain int `MINUS`; v7 scales the integer
  with `ITOP` and divides a pointer difference with `PTI`) - `mutos_c0`
  refuses pointer subtraction.
- `i + p` (the pointer on the right) added the unscaled integer - v7
  scales the LEFT operand here, whose bytes are already written when the
  pointer is seen - `mutos_c0` refuses it (`p + i` is unaffected).
- `p < q` was a signed compare; v7 turns an ordered comparison with a
  pointer operand into its unsigned `LESSP`... variants, which neither
  pass has, and addresses above 0x7FFF compare wrongly signed -
  `mutos_c0` refuses it (`==`/`!=` are unaffected).
- `p == &x` compared `p` with `x`'s CONTENTS: an address operand
  (`VK_MEM_DIRECT`, the deferred `lea`) was rendered as a memory operand -
  `mutos_c1` refuses it in a comparison and in int `+`/`-`.
- `return &x;` returned `x` - `load_into_di()`/`load_into_si()` now load
  an address with `lea`, as `OP_ASSIGN` and `gen_call()` already did.

**4. `02_bubsort`: four comparison and subscript shapes.**

```
L7:mov di,*6.(bp) / dec di / cmp di,*-6.(bp) / ble L8     i < n - 1
mov di,*-8.(bp) / sal di,*1 / add di,*4.(bp) / mov di,(di) a[j] ...
mov si,*-8.(bp) / sal si,*1 / add si,*4.(bp)                ... > a[j + 1]
cmp di,*2.(si) / ble L13
mov di,*-8.(bp) / sal di,*1 / add di,*4.(bp) / add di,*2. / push di    &a[j + 1]
```

- **The relational operand swap by degree.** `c12.c`'s `optim()`
  exchanges a relational's operands (mirroring the operator through
  `maprel[]`) when `degree(left) < degree(right)`, or when the degrees
  are equal and the left one is a NAME and the right one is not.
  `degree()` is -3 for a constant, -2 for `&x`, 0 for a leaf (1 for a
  char or float leaf), and for `n - 1` `max(0, max(-3, 0))` = 0 - so a
  variable compared with anything computed from one register always
  ends up on the right: `i < n - 1` is `n - 1 > i`, branching on `ble`
  when false. `mutos_c1` did this only for a constant left operand; now
  also for a NAME (`VK_MEM`/`VK_STATIC`, or a `register` local - new
  `Val.regvar` flag) against a computed value (`VK_REG`, `VK_IND`). The
  NAME has no code, so only the comparison's direction changes, never
  the order of any code. Two NAMEs (`lo >= hi` - `04_strrev`) stay.
- **A register left operand is compared directly.** `emit_cmp_and_
  branch()` used to load any memory right operand into DI (`mov di,hi` /
  `cmp *6.(bp),di` - still right for a memory left operand); with the
  left one already in a register it is now `cmp di,*-6.(bp)` - v7's
  template computes the left operand into a register and compares it
  with an addressable right one. A dereference on the left is loaded in
  place first (`mov di,(di)`), except against a constant, where the real
  compiler compares the memory operand directly (`cmp *16.(di),*0` in
  `kernel_nonopt/lp_AC.s`).
- **The left operand is loaded before the right one's code.** `a[j]` as
  the left operand of a comparison whose right operand has code of its
  own is loaded at the `STAR` (`mov di,(di)`), and the right operand's
  index takes SI because DI is busy - `di_busy()`, which generalizes
  `ITOP`'s old "DI on top of the value stack" test to any pending value
  in DI (not a `register` local's own NAME).
- **`a[j + 1]`: the constant goes into the displacement.** v7's
  `distrib()` rewrites `(j + 1) * 2` as `j * 2 + 2`, so the index `j + 1`
  is never computed: `OP_PLUS` of a variable and a constant whose
  consumer is an `ITOP` stays uncomputed (`VK_IDXOFF`), the `ITOP` scales
  the variable into a register and carries `N * size` as a pending
  offset (`VK_REGOFF`, the kind `argv[1]`'s displacement already used),
  the pointer `PLUS` adds the pointer variable, and the `STAR` makes the
  offset its displacement (`*2.(si)`); a cancelled `&*` - or any other
  consumer - adds it last instead (`add di,*2.`). On an ARRAY base v7
  would fold the constant into the `lea` (`lea di,<a + 2>`) instead, a
  shape no golden shows, so that is refused.
- The last line of `main()`, `v[0] + v[5] * 10`, already compiled right
  (`mov ax,*-6.(bp)` / `mov cx,*10.` / `imul cx` / `add ax,*-16.(bp)`),
  and `swap(&a[j], &a[j + 1])` is the new right-to-left call order.

**`x86sim.py`** now also executes `movb`/`cbw` (a register operand
meaning its low byte, as `mutos_as` spells it), calls of functions in
the same file (with `cret`: `lea sp,-4(bp)` / `pop si` / `pop di` / `pop
bp` / `ret`) and `chkstk` (`sp -= ax`, `docs/MUTOS_C_ABI.md` sect. 1.9),
and a `#`-marked displacement. That makes 47 of the 62 goldens
executable (up from 30), every one returning its C source's value -
`09_abiprobe`'s six frames 3 each (through `chkstk` from 128 bytes up),
`04_funcs/03_recfact` 720, `02_bubsort` 91, `06_union` 3, `02_long/
04_params` 24468 (90000 truncated to int, plus 4).

**Verification.**

- `make test` (clean build): 46 of 62 byte-exact, 0 genuine mismatches,
  zero warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build: `mutos_c0` on the 62 golden `.i` files
  changed exactly the eight files, all from a refusal to the golden
  `.1`/`.2`; `mutos_c1` alone on the 62 golden pairs changed eleven:
  nine from a refusal to their `.s.golden` - the eight plus
  `06_struct/06_union` (whose `c0` side still needs structs) - and two
  struct files (`03_starray`, `05_nestst`) that still refuse, at a later
  point, with identical output before it.
- `fuzz_c.py` against the previous build, 18000 programs (six seeds, two
  scalar-only): 0 WRONG, 0 BAD, 0 regressions; 1426 refused -> correct,
  292 changed and still correct. Every changed output differs only in
  the two comparison changes above (`cmp *N.(bp),di` / `bXX` becoming
  `cmp di,*N.(bp)` / the mirrored `bXX`; `mov di,<mem>` / `cmp <reg>,di`
  becoming `cmp <reg>,<mem>`); the fuzzer generates no chars and no
  calls.
- 36 hand-written programs (chars: element and variable loads and stores,
  sign extension, char through pointers, `swapch()`/`reverse()` without
  libc, `(int) c`, `l = c`; calls with computed, nested and comparison
  arguments; `bsort()` on seven values including duplicates and
  negatives; `p[k + 2]`; `a[i] < a[i + 1]`; pointer arithmetic and
  comparisons), executed with `x86sim.py` and compared with the host C
  compiler: all 26 that compile and return a value are correct, a 27th
  (`return &x;`) returns its frame address; 9 are explicit refusals (a
  char comparison, a char plus an int, `char *` `++`, `&s[i + 5]` on an
  array, `p++;` as a statement, the four pointer shapes above).
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 62 golden pairs,
  the hand-written programs and 2400 fuzzed programs: clean.
- All of the above re-run after the pointer refusals and the `lea` fix:
  the same results (the fuzzed outputs identical to the first run).

**Evidence for the next char steps, recorded here.** A char compared
with a small constant is a byte compare: `10_integ/01_wordcount.s.golden`
has `mov dx,*-6.(bp)` / `mov bx,dx` / `cmpb _text(bx),*10.`, and `c12.c`'s
`optim()` retypes a `CON` in 0..127 compared with a char operand as a
char (`tree->tr2->type = CHAR`) - with `ITOC` of a NAME already folded to
a char NAME, that is `cmpb`. `kernel_nonopt/lp_AC.s` has `cmpb *-8.(bp),
*122.` / `bgt` and `addb *-8.(bp),*-32.` (a char compound assignment),
`amx.s` `cmpb *1.(si),*0` and a char argument as `movb ax,*-10.(bp)` /
`cbw` / `push ax`.

### `07_scope` - file-scope variables and block scope (`mutos_c0`/`mutos_c1` extended and verified this session)

All three `07_scope` files byte-exact end-to-end (49 of 62, up from 46 of
62). The shapes were read off the goldens first (`dump_temp.py` stopped at
the first new opcode, `CSPACE`, so `01_globstat.1.golden` was decoded by
hand with `od`), then matched against `v7/cc/c02.c`'s `extdef()` and
statement() LBRACE case.

**1. The wire format of a file-scope variable.** `01_globstat.1.golden`
begins, before `bump()`'s `SYMDEF`:

```
205 254  '_counter' 0  2 0         CSPACE "_counter" 2       int counter;
204 254                            BSS
113 254  '_hidden' 0               NLABEL "_hidden"          static int hidden;
206 254  2 0                       SSPACE 2
207 254  '_bump' 0 ...             SYMDEF "_bump"            bump() ...
```

and `03_externdef.1.golden` has nothing for `extern int total;` and
`CSPACE "_total" 2` between `addto()`'s `SETSTK`/`BRANCH` and `main()`'s
`SYMDEF` - where `int total;` is in the source. This is `extdef()` for a
declarator followed by `,` or `;`: `getkeywords()` gives a top-level
declaration without a storage class the class `DEFXTRN` ("blklev==0?
DEFXTRN: AUTO"), which `extdef()` turns into `EXTERN` plus `scflag`, and
then

```c
o = (length(ds)+ALIGN) & ~ALIGN;
if (sclass==STATIC) {
        setinit(ds);
        outcode("BSBBSBN", SYMDEF, "", BSS, NLABEL, ds->name, SSPACE, o);
} else if (scflag)
        outcode("BSN", CSPACE, ds->name, o);
```

- so a plain `extern` writes nothing at all. One byte-level difference:
v7 writes `SYMDEF("")` in front of the static's block - an empty 'S' is
the lone NUL byte `outcode()` writes after the tag - and the golden has
nothing between `CSPACE`'s size word (`2 0`) and `BSS`'s tag (`204 254`).
The MUTOS front end drops it (`mutos_cc.h` delta 5); it would have had
no effect anyway (v7's `c1` writes no `.globl` for an empty name).

References: `NAME(12, 0, "_counter")` - `SC_EXTERN` and the symbol, the
`BNNS` shape a callee's `NAME` already has - for the static `hidden` too.
v7 declares every file-scope name through `decl1(EXTERN, ...)`, whatever
its storage class, and `treeout()`'s NAME case writes the symbol for
class `EXTERN`; the storage class only decides what `extdef()` writes.
`mutos_c0` therefore keeps a second, file-wide symbol table
(`Parser.globals`, `symtab_declare_global()`), consulted after the
function's own (`lookup_var()` - a local shadows a global), and
`emit_name()` writes either `NAME` shape; the twelve places that wrote a
symbol's `NAME` with `sym->offset` now go through it. `parse_extdef()` accepts a
leading `static`/`extern` and hands a declarator without `(` to
`parse_global_var()`. A redeclaration with the same type and linkage is
accepted (`03_externdef`'s `extern` then `int`), writing what its own
class calls for. Refused, with no golden for their shapes: a file-scope
pointer, array or initializer (`10_integ/01_wordcount`'s `char text[] =
"..."` - its golden shows `SYMDEF`, `DATA`, `NLABEL` and `BDATA` runs in
temp1), and a `static` function (v7's `SYMDEF("")` again - whether MUTOS
drops it there too no golden shows).

**2. Block scope.** `02_shadow.1.golden`:

```
LABEL 2
ANAME "_x" -6   NAME(11,0,-6) CON 1 ASSIGN EXPR 12      int x; x = 1;
ANAME "_x" -8   NAME(11,0,-8) CON 2 ASSIGN EXPR 16      { int x; x = 2; }
NAME(11,0,-6) RFORCE EXPR 18                            return x;
... SETSTK 8
```

The inner `x` takes the next slot below the outer one, its `ANAME` sits
where the block's declarations are (after the preceding statement's
`EXPR`), and after the `}` the name means the outer `x` again. v7's
statement() LBRACE case saves `autolen` and `regvar` on entry
(`blockhead()` raises `blklev`, so `decl1()` pushes an outer declaration
of the same name down instead of reporting a redeclaration) and on exit
restores both (`SETREG` if `regvar` changed) and calls `blkend()`, which
removes the block's names and brings the pushed-down ones back.
`mutos_c0` had one flat scope per function - "'x' redeclared".
`c0_sym` is now block-structured: `SymTab.scope` marks the first entry
of the current block (the redeclaration check stops there),
`symtab_block_enter()`/`symtab_block_exit()` save and restore the head,
the scope mark and `autolen`, and `parse_nested_block()` wraps every
compound statement except the function body, which - as in v7, where
parameters and body are both declared at `blklev` 1 - shares the
parameters' scope. Restoring `autolen` means sibling blocks reuse the
same slots while `maxauto` (`SETSTK`) covers the deepest one. That part
is v7's algorithm only: no corpus file has two sibling blocks with
declarations, the non-optimized kernel output has no mid-body
declaration at all (no `| _name=N.` comment after a function's first
statement), and neither kernel corpus gives one frame offset to two
names. The old flat scope gave each
sibling block fresh slots - `{ int b, c; ... } { int d; ... } { int e,
f, g; ... }` after `int a;` compiled to `sub sp,*14.`, v7's layout needs
8.

**3. `mutos_c1`: a memory operand named by its symbol.** The `.s` goldens
use a global exactly where a local static's `L<n>` would stand:

```
.comm   _counter,2
.bss
_hidden:.blkb   2.
...
L2:mov  di,_counter          counter = counter + 1;
inc     di
mov     _counter,di
...
mov     di,_counter          return counter;
mov     ax,di
```

(`04_funcs/05_staticvar.s.golden`: `mov di,L4` / `inc di` / `mov L4,di`
for `n = n + 1;`). So a `NAME(SC_EXTERN)` of a non-function type is a
`VK_STATIC` value with the new `Val.sym` set (and copied through
`SimpleVal`, so it survives a deferred comparison, a `VK_CHARX`, a plan
step); `render_operand()` writes the symbol instead of `L<n>`, and `&x`
(`VK_STATICADDR`) the immediate `#_counter` - `mov di,#_proc` and `mov
dx,#_swapmap` in `tests/mutos_as/kernel_nonopt/`. `Val` is copied freely,
so it does not own the text: `intern_name()` pools each distinct name in
`GenState` until `c1_generate()` returns. Everything a local static can
do, a global now can, and nothing more: `gen_incdec()` and the compound-
assignment handlers take `VK_MEM` only, and every `long` handler a
bp-relative operand only, so those stay refusals for both. `CSPACE`
renders `.comm\t_counter,2` - no trailing `.`, unlike `.blkb`'s, and
decimal: v7's `c1` prints the size in octal, but the kernel output has
`.comm\t_msgbuf,1024` and `.comm\t_dk_time,128` (no octal 8). `NLABEL`
renders `_hidden:` with no newline, glued onto the `.blkb` line like an
`L<n>:` label (`put_name_label()`) - and `01_wordcount.s.golden`'s
`_text:.byte ...` shows the same for a data label.

**Found on the way (pre-existing, `mutos_c1`): a static right-hand side
of a memory-to-memory assignment.** `OP_ASSIGN` refused a bare `VK_MEM`
right-hand side ("x = y;" - 8086 `MOV` cannot take two memory operands,
and no golden shows the register the real compiler routes it through)
but not a `VK_STATIC` one: `static int a, b; ... a = b;` compiled to `mov
L4,L5`, exit status 0, and `mutos_as` rejects the line. With globals the
same shape would have been `mov _x,_y`. A static or global right-hand
side is now refused unless the target is a register (`mov di,_y` into a
`register` local is an ordinary instruction). The kernel shows the shape
a later session will need: `_amxpeek` in `kernel_nonopt/amx.s` - a
function whose register variables hold DI and SI - stores a local into a
global through DX, `mov dx,*-6.(bp)` / `mov _cfreeli,dx`; which register
a function without register variables uses needs a golden. The same
file has in-place operations on global memory (`inc _amxslee`, `orb
_amxscd(bx),*4.`), the evidence for `++`/`--` and compound assignment on
a global.

**Tooling.** `dump_temp.py` decodes `CSPACE` (`S`, `N`) and `NLABEL`
(`S`); over all 124 golden `.1`/`.2` files its output changed only for
`01_globstat` and `03_externdef`, which used to stop at `CSPACE`.
`x86sim.py` allocates a zero-initialized word block for every `.comm`
and `.blkb` and executes `L4`, `_counter` and `#_counter` operands, so
50 of the 62 goldens now run (up from 47: `04_funcs/05_staticvar` 3,
`07_scope/01_globstat` 3, `03_externdef` 12), each returning its C
source's value; its `Result` exposes the addresses (`data`,
`word_at()`). `fuzz_c.py --scope` makes each of `x`, `y`, `s`, `t` a
file-scope variable with probability 1/2 (at least one) - `int y;`,
`static int y;`, or `extern int y;` before `main()` and `int y;` after
it - and adds block statements `{ int y; y = c; ... }` that shadow a
global for one or two inner statements; globals are checked at their
fixed addresses at every statement marker, the block's own local not at
all (it is out of scope there).

**Verification.**

- `make test` (clean build): 49 of 62 byte-exact, 0 genuine mismatches,
  zero warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build: `mutos_c0` on the 62 golden `.i` files
  produced the `.1`/`.2` goldens for the three `07_scope` files (before:
  refusals); among the files that still fail, eleven (the nine
  `06_struct` files, `01_wordcount`, `03_linklist`) have different
  partial output or messages - the error recovery reaches file-scope
  declarations differently - and the same exit status; the 46 passing
  files are byte-identical. `mutos_c1` alone on the 62 golden pairs:
  `01_globstat` and `03_externdef` changed from a refusal to their
  `.s.golden` (`02_shadow` already matched - it only needed `c0`),
  nothing else changed; 53 of 62 now match through `c1` alone.
- `fuzz_c.py` against the previous build: 18000 programs (seeds 11-16,
  two scalar-only), all identical - it generates no global and no block.
  With `--scope`: 18000 programs (seeds 21-26, three scalar-only), 0
  WRONG, 0 BAD, 9774 compiled and correct at every statement (the
  previous build refused every one of them), the rest refused, most by
  the register-occupancy guard as without `--scope`; 505 are the
  memory-to-memory refusal - a comma whose value is a bare variable,
  `y = (u = 19, y);`, which the generator writes with locals just as
  often (121 of 3000 scalar-only programs of seed 15 without
  `--scope`). A mutation check on the checker itself: deleting
  the last store into a global from 408 compiled `--scope` programs was
  flagged in 355; in the other 53 the deleted store did not change the
  global's value (a store in an untaken branch, or of the value already
  there).
- 22 hand-written programs, run through `mutos_cpp`, both passes,
  `mutos_as` and `x86sim.py` and compared with the host C compiler's exit
  status: globals shared by several functions, `static` and `extern`
  forms, a definition after `main()`, shadowing at two levels with a
  `return` inside a block, a parameter shadowing a global, three sibling
  blocks, a global loop counter with a block local in the body, `&global`
  passed to a function and stored through a local pointer, a `register`
  local stored into and read back from a global, char globals copied
  through a char local (`movb dx,_b` / `movb *-6.(bp),dx`, the local
  char shape), a `static char` read as an int, recursion counting its
  calls in a global - all 22 correct. 11 refusal cases (`x = y;` between
  globals, local statics, or a global loop counter's `count = lo`
  initialization, a `long`/array/initialized global, a `static`
  function, `n++` on a global, a conflicting redeclaration, a
  redeclaration in one block, a block local used after its `}`) stop
  with their diagnostic.
- ASan/UBSan builds of `mutos_c0`/`mutos_c1` over the 62 golden inputs,
  the hand-written programs and 3000 fuzzed programs (half `--scope`):
  clean.

### `10_integ/01_wordcount` - a file-scope char array, character constants, a char compared with a constant (`mutos_c0`/`mutos_c1` extended and verified this session)

`10_integ/01_wordcount` byte-exact end-to-end (50 of 62, up from 49 of 62) -
the last `10_integ` file that needs no structs. `dump_temp.py` stopped at
`DATA`, so `01_wordcount.1.golden` was decoded with a scratch copy that
knew `DATA` has no arguments (then added for real, see Tooling); the
`.s.golden` and the kernel corpus settled the codegen. Three front-end
pieces, one back-end piece, and a pre-existing bug.

**1. The wire format of `char text[] = "...";`.** The golden starts

```
207 254 '_text' 0             SYMDEF "_text"
203 254                       DATA
113 254 '_text' 0             NLABEL "_text"
200 254  1 0 116 0 ... 0 0    BDATA  14 bytes "the quick brow"
200 254  ...           0 0    BDATA  15 bytes "n fox\njumps ove"
200 254  ... 1 0 0 0   0 0    BDATA  16 bytes "r the lazy dog\n" + NUL
210 254                       EVEN
207 254 '_main' 0 ...         SYMDEF "_main" - main() follows
```

which is v7 `c02.c` `extdef()` for a declarator followed by `=`:
`setinit(ds); if (sclass==EXTERN) outcode("BS", SYMDEF, ds->name);
outcode("BBS", DATA, NLABEL, ds->name); if (cinit(ds, 1, sclass) & ALIGN)
outcode("B", EVEN);` - and `cinit()`'s string case, `putstr(0, flex ?
10000 : nel)`: the label-less `putstr()` writes to the current output,
temp1 (only a labelled string sets `strflg`). `cinit()` returns the
array's size, the string's 44 characters plus the NUL - odd, hence
`EVEN`. The runs are v7's `if (nchstr%15 == 0) outcode("0B", BDATA);`:
before the 15th and the 30th byte a new run starts, and the NUL is
appended without that check - 14 + 15 + (15 + NUL). Until now this split
was taken from v7's source and confirmed only indirectly (the `.byte`
line layout of the kernel corpus's 208 literals); this is the first
golden with a string of 15 or more characters. No MUTOS delta here, unlike
the static `BSS` case (`mutos_cc.h` delta 5): for `static` v7 writes no
`SYMDEF` at all, not an empty one. `mutos_c0`'s `putstr()` now takes the
output stream and returns the byte count, and `parse_global_chararray()`
writes the above. Only the flexible `[]` form with a string is accepted:
a sized array would need `SSPACE` padding, a brace list `INIT` per
element (the kernel's `_partab:.byte /1` / `.byte /ff81` - one value per
line, sign-extended - is that path, a different format), and neither has
a golden.

**The first attempt was off by one label.** With the data in place,
`main()`'s labels came out as `BRANCH 1` / `LABEL 2`, the golden's are
`BRANCH 2` / `LABEL 3`. Nothing in the golden's temp1 uses label 1: v7's
lexer takes a label number for every string token as it reads it (`c00.c`
`symbol()`: `cval = isn++` in the string case), whether the string then
becomes a literal (`putstr(cval, 0)`) or an initializer (`putstr(0,
...)`, which never writes it). `parse_primary()` already noted that v7
numbers a literal at lex time; the initializer now takes its number too.

**2. Character constants** were not parsed at all - `'\n'` reached
`parse_primary()` as a `T_CCON` token and stopped with "expected an
expression". v7's `getcc()` returns `CON` with the character's value -
an int constant like any other, sign-extended from a byte for a
one-character constant (`realc = cval; cval = realc;`, so `'\377'` is -1;
no golden has one). The lexer reads one character or escape; v7 packs up
to two into a word, which stays "Malformed character constant".

**3. A char compared with a constant.** The loop condition and both
`if`s:

```
text[i] != '\0'    NAME(12,1,_text) AMPER(9) NAME(-6) CON(1) ITOP(9) PLUS(9) STAR(1)  CON(1, 0)   NEQUAL(0)
text[i] == '\n'    ... STAR(1)  CON(1, 10)  EQUAL(0)
text[i] == ' '     ... STAR(1)  CON(1, 32)  EQUAL(0)
```

No `ITOC` on the char, and the constant typed char (1) - against `buf[0]
+ buf[79]`'s `ITOC(0)` on both operands. This is v7 `c12.c` `optim()`'s

```c
if (tree->tr1->type==CHAR && tree->tr2->op==CON
 && (dcalc(tree->tr1, 0) <= 12 || tree->tr1->op==STAR)
 && tree->tr2->value <= 127 && tree->tr2->value >= 0)
        tree->tr2->type = CHAR;
```

for any relational, which the MUTOS front end - converting chars in c0,
unlike v7 - does itself. `mutos_c0`'s `char_compare_rhs()` (shared by
`parse_relational()` and `parse_equality()`): when the left operand is a
char object as written (`ExprVal.char_obj` - a char variable's NAME, a
char element's or dereference's STAR, nothing on top), the right operand
is parsed into a buffer (`capture_begin()`); a constant in 0..127 has
written nothing, so `CON(TY_CHAR, v)` and the node follow directly;
anything else gets the left operand's `ITOC` first and then the buffered
bytes - the previous output, byte for byte. Only a STAR operand has a
golden; a variable is taken the same way (`optim()`'s rule does not
distinguish them in c0's terms, and its code is in the kernel - see
below). A constant on the left, a negative or larger constant, and a
non-constant keep the promotion (and `mutos_c1` keeps refusing them: v7's
own order is swap first, then retype, which no golden shows for MUTOS).

**4. `mutos_c1`: data, the element, the byte compare.** The `.s.golden`:

```
.globl _text / .data / _text:.byte /74,/68,/65,/20,/71,/75,/69,/63,/6b
.byte /20,/62,/72,/6f,/77 / .byte /6e,... / ... / .even
...
L5:mov dx,*-6.(bp) / mov bx,dx / movb dx,#_text(bx) / orb dx,dx / beq L6    text[i] != '\0'
mov dx,*-6.(bp) / mov bx,dx / cmpb _text(bx),*10. / bne L8                 text[i] == '\n'
mov dx,*-6.(bp) / mov bx,dx / cmpb _text(bx),*32. / beq L10000             text[i] == ' ' ||
mov dx,*-6.(bp) / mov bx,dx / cmpb _text(bx),*10. / bne L9                 text[i] == '\n'
L10000:mov *-14.(bp),*0. / jmp L10 / L9:cmp *-14.(bp),*0 / bne L11 / ...   ... } else if (inword == 0)
```

- `DATA` is `.data`; a temp1 `BDATA` run renders exactly like a string
  literal's (`put_bdata_run()`, factored out of `gen_strings()`); `NLABEL`
  glues `_text:` onto the first `.byte` line as it did onto `.blkb`.
- The element. `&text` is a link-time constant (`VK_STATICADDR` with the
  symbol - what `&global` already was), and the `ITOP` by 1 leaves the
  index variable itself (v7 drops the multiplication), so the pointer
  `PLUS` has nothing to compute: it becomes `VK_SYMIDX` (symbol + index
  operand) when a `STAR` follows, and the `STAR` does v7's `F*` for
  `*(&text + i)` - the index into DX, the byte working register, then into
  BX, since DX cannot address - `mov dx,*-6.(bp)` / `mov bx,dx` - and the
  element is `VK_IND` BX with the symbol as displacement. The kernel does
  the same for every char array element (`mov dx,*-10.(bp)` / `mov bx,dx`
  / `movb dx,#_amxcmd(bx)` in `kernel_nonopt/amx.s`).
- The two compare shapes are v7 `table.s`'s `cctab`: `%a,z` ([move1],
  `tstB1 A1`) and `%nb*,ab` ([move6], `F*` / `cmpB1 #1(R),A2` on the
  PDP-11) compare in memory - MUTOS `cmpb A1,A2`: `cmpb _text(bx),*10.`
  here, `cmpb *-8.(bp),*97.` / `blt` for a char local in
  `kernel_nonopt/lp_AC.s`, `cmpb *1.(si),*0`, `cmpb (di),*0` and `cmpb
  111.+_u,*0` in `amx.s`/`sys1.s` for addressable operands against 0.
  `%n*,z` ([move2], `F*` / `tstB1 #1(R)`) - an operand whose address had
  to be computed, against 0 - is, with no memory TST on the 8086, the
  byte loaded and tested: `movb dx,#_text(bx)` / `orb dx,dx`.
  `emit_byte_cmp_and_branch()` implements these for a char in memory
  (`VK_MEM`, `VK_STATIC`) and an element addressed through BX (the F*
  cases); an element through DI/SI (a local char array's runtime
  subscript, `lea di` / `add di`) has no golden in either shape and is
  refused. `ORB` clears OF and sets SF/ZF from the byte, so every signed
  branch reads it as a compare with 0.
- The `#`. `_text(bx)` takes no marker as an instruction operand
  (`cmpb`), and `#` when loaded into a register: v7's `#1` template
  operand (`c10.c`'s `'#'` case prints the address's constant part - a
  number, or the name of an `AMPER`) is what the real compiler marks, while
  `A1` (`pname()`) prints a symbol displacement bare. The kernel is
  consistent with that: `movb dx,#_amxcmd(bx)`, `mov dx,#_amxladd(bx)`,
  `mov ax,#4.+_amxobuf(bx)` (loads), but `orb _amxscd(bx),*4.`, `mov
  _amxaliv(bx),dx` (operands) and `movb ax,_amxscd(bx)` / `cbw` (MUTOS's
  own char-to-int template, not `#1`); with no symbol and no offset there
  is nothing to print (`movb dx,(bx)`, `04_strrev`; the kernel has no
  `#(`). `o_load()` renders the `#1` form; it is used for the zero test
  and for a char element loaded into DX by a char assignment (`movb
  dx,#_amxi_bu(bx)` / `movb *-10.(bp),dx` in `amx.s`); `load_charx()`
  keeps the bare form.
- The char-typed constant is accepted by `OP_CON` only as a relational's
  right operand (`scan_consumer()`), flagged `Val.charcon`; the relational
  handler, seeing one on top, pops a byte left operand and builds a
  `VK_COND` with `cond_is_byte` (the `SimpleVal` inside does not keep
  `bytev`). As a condition, in `||`/`&&` (the plan's `SEG_BRANCH`) and as a
  value (`materialize_cond()`) it goes through `emit_cmp_and_branch()`,
  which hands it to the byte shapes. The rest of the function was already
  supported - `else if`, `cmp *-14.(bp),*0`, the `||` jumping code
  (`L10000`) that the conditional-evaluation work had reproduced with an
  int version of this very condition.

**Found on the way (pre-existing, `mutos_c1`): `!` of a `long`
comparison.** `OP_EXCLA` built its inverted `VK_COND` field by field and
left out `cond_is_long`, so `if (!(l > 0L))` became an ordinary 16-bit
compare of the high word with an operand placeholder - `cmp *-8.(bp),
<unmaterialized-long-const>` - exit status 0 (checked with the previous
build). It now carries both width flags; `gen_long_cmp()` then refuses the
inverted operator (`<=`), as it refuses every long comparison but the
confirmed `l > 0L`.

**Tooling.** `dump_temp.py` decodes `DATA` (no arguments); over all 124
golden `.1`/`.2` files only `01_wordcount.1.golden`'s dump changed (it
used to stop at `DATA`); four still stop, at `STRASG`, `FSEL`, `FCON` and
`ITOF` (structs, floats). `x86sim.py` lays down `.byte` lines at fixed
addresses (consecutive lines contiguous, `.even` aligning), executes
`_text(bx)`/`#_text(bx)` operands, `cmpb` (both bytes sign-extended, which
keeps signed and unsigned order) and `orb` (`orb dx,dx` sets the flags of
`cmpb dl,0`): 51 of the 62 goldens run (up from 50), `01_wordcount`
returning 55 = 44 characters + 9 words + 2 lines. Mutations of its output
(a branch after a `cmpb` flipped, the `orb` test broken, the base
register changed) are all caught.

**Verification.**

- `make test` (clean build): 50 of 62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build: `mutos_c0` on the 62 golden `.i` files
  changed only `01_wordcount` (a refusal -> its goldens; 50 of 62 match
  through `c0` alone); `mutos_c1` alone on the 62 golden pairs changed
  only `01_wordcount` (a refusal at opcode 203 -> its `.s.golden`; 54 of
  62 match through `c1` alone).
- `fuzz_c.py` against the previous build: 36000 programs (seeds 11-16 and,
  with `--scope`, 21-26; five scalar-only): all byte-identical, 0 WRONG,
  0 BAD. The generator makes no chars, so this checks that nothing else
  moved.
- 10 hand-written programs through `mutos_cpp`, both passes, `mutos_as`
  and `x86sim.py`, against the host C compiler on `short`/`signed char`:
  all six relational operators on a char local and a global against
  constants and 0 (`cmpb *-12.(bp),*120.`, `cmpb _g,*113.`), vowel
  counting with `||` and `&&` over a global array, `c = s[i]` (`movb
  dx,#_s(bx)` / `movb *-6.(bp),dx`), `n = s[i]` (`movb ax,_s(bx)` / `cbw`),
  `return s[i]`, a `static` array walked with `else if`, byte comparisons
  as values and under `!`, three initializers of odd and even size plus
  a string literal walked through a `char *` against 0 (`movb dx,(bx)` /
  `orb dx,dx`), `*p == 'h'` (`cmpb (bx),*104.`), character constants
  (`'\377'` = -1), and `01_wordcount` itself - all correct, all assemble.
  17 refusal cases stop with their diagnostic (see `STATUS.md`). Two
  first drafts of these programs hit older, unrelated refusals (`k = n;`
  between memory operands, `(c == 'x') + (c == 'y')` - the same register
  guard refusal as `(a == 1) + (a == 2)` with ints in the previous build)
  and were rewritten around them.
- ASan/UBSan builds of both passes over the 62 golden inputs, the
  hand-written programs and 3000 fuzzed programs (half `--scope`): clean.

### `06_struct` and `10_integ/03_linklist` - structs, unions, bit-fields, enums, typedefs; more `char` (`mutos_c0`/`mutos_c1` extended and verified this session)

60/62 byte-exact end-to-end, up from 50 of 62: all nine `06_struct` files
and `10_integ/03_linklist`. Two sessions' work (2026-09-26/27), in the
roadmap's order: the remaining `char` forms first, then the struct front
end, then the struct back end.

#### 1. `char` with an int operand, as an argument, as a condition, stored into a global array

No golden has any of these; every shape comes from the real compiler's
own output in `tests/mutos_as/` (non-optimized kernel sources unless
noted).

- **A char with one int leaf operand is computed in AX.** `mutos_c0`
  already wrote `ITOC(0)` on it (`VK_CHARX`); CBW exists for AL/AX only,
  so the value arrives in AX and the operator works there in place:
  `movb ax,*23.(bx)` / `cbw` / `and ax,*-2.` / `or ax,*16.` / `pop bx` /
  `movb *23.(bx),ax` (3x in `amx.s`), `movb ax,*52.(di)` / `cbw` / `mov
  ax,ax` / `mov cx,*20.` / `imul cx` (5x - OP_TIMES's own `mov ax,<left>`,
  as in `03_recfact`), `cbw` / `sal ax,*1` and `sal ax,cl`
  (`kernel_opt`). `c - '0'` is `add ax,*-48.` (`kernel_opt/genio.s`) -
  v7 `optim()`'s `x - c` -> `x + -c`; `+ 1`/`- 1` are `inc`/`dec`, `+ 0`
  nothing. A commutative operator puts the char left (a char leaf's
  `degree()` is 1, an int leaf's 0); `x - c` and a char shift count have
  no example and are refused, as is a computed int operand (where the
  real compiler evaluates it relative to the char load is not shown).
- **AND with 0..127 skips the CBW**, loading the byte into DX (the mask
  clears what `movb` leaves in the high byte): `movb dx,#_amxscd(bx)` /
  `and dx,*12.` / `beq L144`, `movb dx,(bx)` / `and dx,*127.` / `pop bx`
  / `movb 4.+_amxtout(bx),dx`. The branch reads the AND's own flags - no
  `cmp` - also after `call _inb` / `add sp,*2.` / `and ax,*9.` / `bne`
  (`lp_AC.s`): `Val.flagsv`/`flags_at` mark a register value whose flags
  are still the AND's (`GenState.ninsn` counts instructions), and
  `gen_cond_branch()` branches directly on them. An int AND/OR/XOR whose
  left operand is already in AX or DX is done in place the same way
  (`and ax,*9.`), where it used to be moved to DI first.
- **A char compared with an int leaf** goes left (the relational is
  mirrored) and is compared as an int: `movb ax,*22.(di)` / `cbw` / `cmp
  ax,*-6.(bp)` (`kernel_opt/ifss.s`), `movb ax,_kennung(bx)` / `cbw` / `cmp
  ax,*-8.(bp)` (`cons_NOBIOS.s`). Two chars compared: refused.
- **Char objects where v7 converts nothing** - a call argument, a
  condition, an operand of `&&`/`||`/`!`, a `?:` condition - are written
  unconverted by `mutos_c0` (as v7 does) when they are a char variable,
  element or dereference (`char_nonobj_refused()`; a `(char)` cast's or
  a char assignment's value stays refused - it would be a byte in a
  register). `mutos_c1` pushes an argument as `movb ax,*-8.(bp)` / `cbw` /
  `push ax`, and tests a condition as a byte against 0 - `cmpb
  *1.(si),*0`, `cmpb (di),*0` (`amx.s`), or `movb dx,#_t(bx)` / `orb
  dx,dx` for an element addressed through BX (`as_cond()` sets
  `cond_is_byte`).
- **A store into a file-scope char array element** pushes the INDEX (the
  symbol is the store's displacement), computes the right-hand side into
  DX and pops the index into BX: `push *-34.(bp)` / `mov dx,*-30.(bp)` /
  `pop bx` / `movb _amxscd(bx),dx` (`amx.s`). A constant right-hand side
  has no example and is refused. `ITOC(1)` of a value already in AX or
  DX leaves it there (no `mov dx,ax`).

#### 2. The struct front end (`mutos_c0`)

Everything here is v7's algorithm, checked against the ten files' `.1`
goldens, all of which now match through `mutos_c0` alone.

- **Types.** One wire base type for every struct and union, `TY_STRUCT`
  (4) - v7's `c0.h`: UNION is "adjusted later to struct" - with the usual
  degrees on top (`struct point *` 12, `struct node **` 44); which struct
  a code means travels beside it, as a `StructDef` (`c0_sym.h`), like
  v7's `strp`. Declarations write nothing to temp1 (06_struct's goldens
  start with `main()`'s `SYMDEF`).
- **Layout** - `c03.c`'s `declist()`/`align()`: a non-char member on a
  word boundary; a bit-field packed after the previous one in the same
  word, the next word when it does not fit, and a non-field member
  moving past the bytes the fields used; union members at offset 0; the
  size rounded up to a word. Only int/unsigned fields (v7 also packs char
  fields into bytes; no golden has one).
- **Member chains** - `c01.c`'s `build()`: `a.b` is `(&a)->b`, `p->b` is
  `*(p + CON offset)`, and ARROW first retypes the left operand's spine
  with `setype()` - every AMPER/STAR/PLUS node down `tr1` (AMPER passing
  `decref(t)` on, STAR `incref(t)`) and the NAME below. So `p.x` is
  `NAME(p, INT) AMPER(8) CON 0 PLUS(8) STAR(0)` (`01_stbasic.1.golden`),
  and `pts[i].x` retypes the subscript's spine too while its `ITOP` - a
  PLUS's right operand, never visited - keeps `PTR.STRUCT` (12):
  `NAME(pts, 0) AMPER(8) NAME(i) CON 4 ITOP(12) PLUS(8) STAR(0) AMPER(8)
  CON 0 PLUS(8) STAR(0)` (`03_starray.1.golden`). Since the types are
  known only once the chain's last member is, `mutos_c0` collects a chain
  (its spine nodes, each PLUS's right operand as its bytes) and writes
  it at the end. `disarray()` (an array member decaying) retypes the
  same way: `n.b[0]` on `char b[2]` is `NAME(n, CHAR) AMPER(9) CON 0
  PLUS(9) STAR(1) AMPER(9) CON 0 CON 1 ITOP(9) PLUS(9) STAR(1)`
  (`06_union.1.golden`). A member's member through a pointer keeps the
  intermediate STAR/AMPER pair: `rp->botright.x` is `NAME(rp) CON 4
  PLUS(8) STAR(0) AMPER(8) CON 0 PLUS(8) STAR(0)` (`05_nestst.1.golden`).
- **Two new opcodes.** A bit-field member's STAR is followed by
  `FSEL(UNSIGN, bitoffs, flen)` - opcode 10, `treeout()`'s "BNNN" - and
  the member's own type is unsigned (07_bitfield: `STAR(7) FSEL(7, 2,
  2)`). A whole-struct assignment is `NAME(p2, 4) NAME(p1, 4) ASSIGN(4)
  STRASG(4, 4)` - opcode 115, "BNN", the struct's size
  (`04_stassign.1.golden`).
- **The rest.** Enum constants are int `CON`s counting from 0 or from an
  `=` value (`c = GREEN;` is `CON 1`, `08_enum`); `(int) x` on an int is
  just the NAME; a typedef name stands for its type (a typedef name
  followed by an identifier or `*` starts a declaration); `sizeof` takes
  a struct/union/enum/typedef/`unsigned` type name or a struct variable;
  `(struct node *) malloc(sizeof(struct node))` writes the CALL typed
  `PTR.STRUCT` (v7's `build(CAST)` of a pointer to a pointer retypes the
  top node, `03_linklist.1.golden`); `return` of an unsigned value in an
  int function is `RFORCE(7)` (`doret()` - `07_bitfield`). An arithmetic
  operator with an unsigned operand is typed `UNSIGN`; `/`, `%`, `>>` and
  ordered comparisons on unsigned (v7's `UDIV`/`ULSH`/`LESSP`...) are
  refused.

#### 3. The struct back end (`mutos_c1`)

`04_stassign`'s `STRASG` and a struct NAME were the easy part: a 4-byte
struct is assigned as a long (v7's `strasg()` retypes a struct of at most
4 bytes as a long - `mov si,*-6.(bp)` / `mov di,*-8.(bp)` / `mov
*-10.(bp),si` / `mov *-12.(bp),di`), a 2-byte one as an int; a larger one
(a block copy) has no golden. `01_stbasic`, `06_union`, `08_enum` and
`09_typedef` then already matched. The other five needed six decisions,
each read off the goldens:

**3a. A store through "pointer + constant" computes its right-hand side
first.** `03_linklist`:

```
cur->val = i;     push *-8.(bp) / mov di,*-10.(bp) / pop bx / mov (bx),di     offset 0
cur->next = head; mov di,*-6.(bp) / mov si,*-8.(bp) / mov *2.(si),di          offset 2
```

and `02_stptr`'s `pp->y = pp->y + dy;` -> `mov di,*4.(bp)` / `mov
di,*2.(di)` / `add di,*8.(bp)` / `mov si,*4.(bp)` / `mov *2.(si),di`, and
`03_starray`'s `pts[i].x = i;` (push/pop) against `pts[i].y = i * 2;`
(`mov di,*-18.(bp)` / `sal di,*1` / `lea si,*-16.(bp)` / `mov
dx,*-18.(bp)` / `sal dx,*1` / `sal dx,*1` / `add si,dx` / `mov
*2.(si),di`). The deciding difference is the offset, not the right-hand
side: on the PDP-11, `*p` is an addressable operand (`@-8(r5)`), `*(p +
2)` is not - so v7's table takes a different template, and the MUTOS
port's x86 version of the first pushes the pointer, of the second
computes the value first. `is_disp_store()` recognizes an ASSIGN whose
target is a STAR of a pointer that folds (`ptr_fold()`: through `+
constant`s and `&*` pairs) to a base plus a non-zero offset, with a
right-hand side that is not a constant; the plan (`ORD_DISPSTORE`)
streams the right-hand side, puts it into a register (`SEG_RHSREG`),
streams the target - which, with a value below it, is no longer taken
for a pushed target - swaps the two (`SEG_SWAP2`) and replays the
ASSIGN.

**3b. The next free register.** In those stores DI already holds the
value, so the pointer goes to SI and a subscript index to DX: the real
compiler allocates registers in order, R then R+1. `pick_addr_reg()`
takes DI, or SI while a pending value holds DI (an eager `lea`, a
pointer loaded for a STAR); an `ITOP`'s index takes DI, SI, then DX
(it is only ever added to the base register). Both DI and SI taken is
refused - DX cannot address memory, and no golden shows the spill.

**3c. Offsets become one displacement.** `05_nestst`'s `rp->botright.x`
(`CON 4 PLUS STAR AMPER CON 0 PLUS STAR`) is `mov di,*4.(bp)` / `mov
di,*4.(di)`, `.y` `*6.(di)` - v7's `optim()` cancels the `&*` pair and
`acommute()` merges the constants (tossing a `+0`). `OP_STAR`'s `&*`
cancel now keeps a pending "pointer + offset" (`VK_REGOFF`) when another
constant is about to be added (`next_is_con_plus()`), and the pointer
`PLUS` folds that constant into it; for any other consumer the address is
computed (`mov di,<p>` / `add di,*N.`). The same applies to an element
address `p[i + 1]` followed by a member offset (`*6.(si)`, not `add
si,*4.` / `*2.(si)`).

**3d. A left dereference is loaded before the right operand's code.**
`05_nestst`'s `h = rp->botright.y - rp->topleft.y;`: `mov di,*4.(bp)` /
`mov di,*6.(di)` / `mov si,*4.(bp)` / `sub di,*2.(si)`, and
`03_starray`'s sum: `... add di,si` / `mov di,(di)` / `lea si,*-16.(bp)`
/ ... / `add di,*2.(si)`. v7's templates compute the left operand into R
(`F`), then the right into R+1 (`S1`). `load_now()` extends the
existing relational rule (`02_bubsort`) to an int `+`/`-`: a
dereference whose consumer takes it as the LEFT operand, with more than
one opcode on the right, is loaded at once; the `+` then adds a
dereference to a value already in DI/SI straight from memory.

**3e. `x - *p`: the pointer pushed first.** `w = rp->botright.x -
rp->topleft.x;` -> `push *4.(bp)` / `mov di,*4.(bp)` / `mov di,*4.(di)` /
`pop bx` / `sub di,(bx)` - the same PDP-11 addressability of `*p` as in
3a, for an operand. `is_deferred_ptr()`: an int MINUS whose right operand
reads through a pointer variable with total offset 0 and whose left one
has code; the plan (`ORD_DEFPTR`) streams only the pointer's NAME,
pushes it (`SEG_DEFPUSH`), streams the left operand, pops the pointer
into BX (`SEG_DEFPOP`) and replays the MINUS - the right operand's other
opcodes are never streamed. A leaf left operand and `+` have no golden.

**3f. `acommute()` reorders an int `+` chain.** `sum = sum + pts[i].x +
pts[i].y;` adds `sum` LAST: `(pts[i].x + pts[i].y) + sum`. v7's
`insert()` places each term before the first one of strictly lower
`degree()` (carrying the displaced one on the same way) and the chain is
rebuilt left-deep in that order. But v7's PDP-11 `optim()` gives every
int expression degree 0 (`d1==d2 ? d1+islong : max(d1,d2)`), which would
keep the source order - the MUTOS compiler evidently counts: something
computed ranks above a variable. `enode_degree()` models that with a
Sethi-Ullman count (constant -3, a named object's address -2, a leaf 0 -
a char 1 -, anything computed at least 1, two equal operands one more);
only the relative order matters. `acommute_order()` applies it only
where the order changes, to a chain of at least three terms with two of
them computed, no constant term (v7 folds those) and no side effect,
and not to two constant multiples (`distrib()`'s factoring). The plan
(`ORD_ACOMMUTE`) streams the terms in the new order, loads a dereference
before a computed next term (`SEG_LOADIND`), and replays the chain's
own PLUS opcodes between them, the outermost last - so the one handler
whose lookahead matters sees what really follows.

**3g. Bit-fields.** `07_bitfield`:

```
f.ready = 1;    or  *-6.(bp),*1.                          1 bit at 0
f.error = 0;    and *-6.(bp),*-3.                         1 bit at 1
f.mode = 2;     and *-6.(bp),*/fff3 / or *-6.(bp),*8.     2 bits at 2
f.count = 9;    and *-6.(bp),#/ff0f / or *-6.(bp),#144.   4 bits at 4
f.ready + f.mode + f.count:
    mov di,*-6.(bp) / and di,*1.
    mov si,*-6.(bp) / sar si,*1 / sar si,*1 / and si,*3. / add di,si
    mov si,*-6.(bp) / mov cx,*4. / sar si,cl / and si,*15. / add di,si
```

The value is `unoptim()`'s FSEL -> `(word >> bitoffs) & ((1 << flen) -
1)`, the shift by the constant-shift rule; SAR drags the sign bit in, the
mask removes it. The stores are `c12.c`'s `lvfield()`/FSELA: 0 is
`ASAND` with `~mask` (a CON, printed in decimal, -3), a value equal to the
mask is `ASOR` with the value - v7 compares the unshifted value with the
SHIFTED mask, so for a field above bit 0 a value that fits never matches
(and one that matches does not fit: refused, as is any value outside the
field) - and anything else is the FSELA template, which prints the
complemented mask in hex (`/fff3`, `/ff0f`: `/` and lower-case digits,
with the operand's `*`/`#` marker) and ORs the shifted value in decimal
(a signed 16-bit constant, like every decimal immediate). A value read
ends in its AND, whose flags are the value's (`flagsv`), so `while (f.a)`
is `mov di,*-6.(bp)` / `and di,*7.` / `beq`. A computed value stored into
a field has no golden and is refused.

**Found on the way.**

- `mutos_c0`, recovering from a refused function returning a struct
  pointer, skipped to the first `;` - inside the K&R parameter
  declarations - and read the body as further external definitions (a
  cascade of bogus errors after the real one). It now skips a function
  definition's parameter declarations and body whole.
- The 2-D subscript code's "internal: row step without its base in di"
  was reachable once an eager `lea` could land in SI: now an explicit
  refusal.

**Tooling.** `dump_temp.py` decodes `STRASG` and `FSEL`: over all 124
golden `.1`/`.2` files only `04_stassign.1.golden`'s and
`07_bitfield.1.golden`'s dumps changed (both decoded to the end now);
`08_float`'s two still stop, at `FCON`/`ITOF`. `x86sim.py` takes the
flags of `and`/`or`/`xor` (as `cmp <result>,0`, which is what they are:
CF/OF cleared, SF/ZF from the result) for a mask tested directly; it
already ran all nine `06_struct` goldens.

**Verification.**

- `make test` (clean build): 60/62 byte-exact, 0 genuine mismatches, zero
  warnings; `mutos_as` 67/67, `mutos_cpp` 5/5.
- Against the previous build (`84d6548`): `mutos_c0` on the 62 golden
  `.i` files changed exactly the ten struct files (60 of 62 match through
  `c0` alone); `mutos_c1` alone on the 62 golden pairs changed exactly
  `02_stptr`, `03_starray`, `04_stassign`, `05_nestst`, `07_bitfield` and
  `03_linklist` (60 of 62, up from 54).
- `fuzz_c.py` against the previous build: 42000 programs (seed 11;
  seed 21 with `--scope`; seed 31 scalar-only): 0 WRONG, 0 BAD, 324
  changed and still correct, 1261 now correct (refused before). Every
  change is one of two: an AND/OR/XOR of a value already in AX or DX done
  in place, and a left dereference loaded before its right sibling's
  code. The refusals' largest group is now "a 2-D subscript while DI
  holds another value" (formerly counted under the generic
  register-overwrite refusal).
- 18 hand-written programs through `mutos_cpp`, both passes, `mutos_as`
  and `x86sim.py`, against the host C compiler (`short`/`signed char`):
  ten `char` programs and eight struct programs (see `STATUS.md`) - all
  correct, all assemble. Five refusal cases stop with their diagnostic:
  `p = &a;` while a `register` int holds DI (the `lea` goes through DI),
  a char plus a computed int, a function returning a struct pointer, file-scope
  struct and `unsigned` variables, and two chars compared. Drafts hit
  older limits (a struct of 6 bytes subscripted - a non-power-of-two
  scale; `a[i + 1]` on a local array, where v7 would fold the constant
  into the `lea`; `k * 100 + f()`, a product in AX across a call) and
  were rewritten around them.
- ASan/UBSan builds of both passes over the 62 golden inputs, the
  hand-written programs and 6000 fuzzed programs (a quarter `--scope`):
  clean.

### Test corpus: `tests/mutos_cc/11_kernel` added — real kernel driver golden
corpus, initial `mutos_c0`/`mutos_c1` coverage assessed (2026-09-27)

Nine real, unmodified MUTOS 1700 kernel driver source files were added to
`tests/mutos_cc/` as `11_kernel/` (commit `dd5dd73`): `01_delay.c`,
`02_prim.c`, `03_mem.c`, `04_pipe.c`, `05_nami.c`, `06_fio.c`, `07_v24.c`,
`08_tty.c` and `09_amx.c` (the AMX serial-board driver, the largest at
~40 KB), plus the local `h/`-tree headers they `#include` and their own
`Makefile.mutos`. This plays the same role for `mutos_cc`/`mutos_c0`/
`mutos_c1` that `tests/mutos_cpp/c/` already plays for `mutos_cpp`: a
second golden corpus made of real kernel source instead of constructed
test cases. Full real-hardware-verified goldens (`.s`/`.i`/`.1`/`.2` plus
base64 companions) arrived already captured — the golden-capture pipeline
had already been run on real MUTOS 1700 hardware for all nine files before
the directory was added here.

**Coverage assessment (this session, `tests/mutos_cc/run_goldens.sh`
against the `06_struct`-era build, commit `dd5dd73`).** `mutos_cpp`
matches all nine `.i.golden`s byte-for-byte (0 mismatches) - `mutos_c0`
refuses all nine, each with a diagnosed error, never silent wrong output,
so the construct-coverage corpus's 60/62 pass fraction is unaffected;
`11_kernel` is tracked separately from it, not folded into that count.
The nine break down as:

- **One genuinely new gap, confirmed with a minimal reproduction**:
  `01_delay.c`'s `while(i < 192) i++;` - a bare expression-statement
  (here a lone postfix `++`) used as a whole statement, not as part of an
  assignment's right-hand side. `src/mutos_cc/README.md`'s "Current scope"
  grammar has no `expr-stmt` production at all - `stmt := assign-stmt |
  star-assign-stmt | call-stmt | return-stmt` - because every `++`/`--`
  use in the existing corpus (`01_expr/05_incdec.c`) is inside a `j =
  i++;`-shaped assignment. `mutos_c0` diagnoses it correctly (`error:
  expected '=' or a compound-assignment operator, found token`) rather
  than misparsing it.
- **Two hits on already-documented gaps**, real-kernel confirmation of
  limits already named in `src/mutos_cc/README.md`: `05_nami.c`'s
  `uchar()` declares `register c;` - a storage-class specifier with no
  type keyword (K&R implicit `int`), which the parser correctly refuses
  ("only 'int'/'char'/'long', struct, union, enum and typedef'd local
  declarations are supported so far"), cascading into a further
  `'c' undeclared` on the following statements; `06_fio.c` hits the
  already-documented file-scope struct/union-or-`unsigned` variable
  refusal (see `src/mutos_cc/README.md`'s "Current scope" and
  `STATUS.md`'s "Next up" item "Struct shapes still refused").
- **Six not yet root-caused**: `02_prim.c`, `03_mem.c`, `04_pipe.c`,
  `07_v24.c`, `08_tty.c` and `09_amx.c` all refuse with the generic
  "external definition syntax (expected a function name...)" diagnostic -
  something at file scope beyond a plain function definition, prototype,
  or plain variable, per `c0_parser.c`'s `extdef` handling. Real driver
  code plausibly hits this via initialized arrays of structs,
  function-pointer tables, or similar file-scope initializer shapes not
  yet in the grammar, but which specific construct in each file triggers
  it has not been isolated - that is deliberately left as real,
  dependency-ordered future work (see `STATUS.md`'s "Next up" and
  `src/mutos_cc/README.md`'s "Next steps"), not guessed at here.

**Verification.** `make check-docs`: clean. `make test` (clean build,
unchanged by this addition): 60/62 byte-exact, 0 genuine mismatches,
`mutos_as` 67/67, `mutos_cpp` 5/5 - `run_goldens.sh`'s wildcard-per-category
loop already discovers `11_kernel/*.c` without any script change; its nine
files land entirely in the "grammar/opcode not yet supported" bucket, not
in the genuine-mismatch buckets that would fail the run.

### `08_float` — floating point through libc's software floating-point runtime (`mutos_c0`/`mutos_c1` extended and verified this session)

The last two files of the 62-file corpus: `01_floatbas.c` (`float a, b,
c;` - `a = 3.5; b = 2.0; c = a + b; c = a * b; return (int) c;`) and
`02_dblconv.c` (`double d; int i; long l;` - `i = 7; d = i; d = d / 2.0;
l = (long) d; i = (int) d; return i;`). **62/62 byte-exact end-to-end.**
Same method as every earlier category - the goldens decoded byte by byte
first (`dump_temp.py` stopped at `FCON`, so the first two `.1.golden`s were
read by hand with `od`), `v7/cc` as the algorithmic reference - plus a new
source of evidence: the real `libc.a`, whose objects are real MUTOS 1700
compiler and assembler output and whose floating-point part is the runtime
the goldens call.

**The front end's stream** (`01_floatbas.1.golden`, from byte 55):

```
NAME(AUTO, FLOAT, -8)  FCON  17 fe 03 00 33 2e 35 00   ASSIGN(DOUBLE)
                             op   DOUBLE  "3.5" NUL
NAME(c) NAME(a) NAME(b) PLUS(DOUBLE) ASSIGN(DOUBLE)
NAME(c, FLOAT) FTOI(INT) RFORCE(INT)
```

and `02_dblconv.1.golden`: `NAME(d, DOUBLE) NAME(i, INT) ITOF(DOUBLE)
ASSIGN(DOUBLE)`, `... NAME(d) FCON(DOUBLE, "2.0") DIVIDE(DOUBLE)`, `NAME(l,
LONG) NAME(d) FTOL(LONG) ASSIGN(LONG)`, `NAME(i) NAME(d) FTOI(INT)
ASSIGN(INT)`. All of it is v7's `build()` unchanged:

- `FCON` is `treeout()`'s `outcode("BNF", FCON, type, cstr)` - `F` is
  `outcode()`'s string format without `S`'s leading `_` (up to 1000
  characters). The text is the literal as written: v7's `getnum()` copies
  every character into `numbuf` (`c00.c`), and `tree()` makes every
  floating constant a `DOUBLE` `fblock()`. Nothing folds it - `fold()`
  handles integer `CON`s only - so `FCON` is written the moment it is
  parsed, like a `NAME`.
- The frame: `float` 4 bytes, `double` 8 (`SZFLOAT`/`SZDOUB`), the usual
  subtract-then-take offsets from `STAUTO` - `a`/`b`/`c` at `-8`/`-12`/`-16`,
  `d` at `-12`, then `i` `-14`, `l` `-18`.
- Every operator and assignment over floating values is `DOUBLE`, even
  `float + float` and an assignment INTO a float: `build()` ends with "`if
  (t==FLOAT) t = DOUBLE`". Only the `NAME` keeps `FLOAT` - the one place
  `c1` learns the variable's size.
- `cvtab[]` (`c05.c`) has one "double" row and column for both floating
  types (`lintyp()`), so float <-> double needs no node; int -> floating is
  `ITF` (`ITOF`), long -> floating `LTF` (`LTOF`), floating -> int `FTI`
  (`FTOI`), floating -> long `FTL` (`FTOL`). The new node's type is
  `convert()`'s `t`: for an assignment or cast `t1`, the target's type - so
  `d = i;` is `ITOF(DOUBLE)` (the golden) but `a = i;` into a float is
  `ITOF(FLOAT)` (no golden; straight from the code: `t = t1` happens before
  `convert()`, the FLOAT->DOUBLE retyping after it) - and for `+ - * /`
  the floating operand's type (`t = leftc ? t2 : t1`).
- An int LEFT operand of `+ - * /` is converted too (`cvtab[int][double]`
  carries `FTI<<4` - "leftc"): its `ITOF` must follow the left operand's
  bytes and precede the right operand's, which `mutos_c0` has already
  streamed out by the time it knows the right operand's type. So `+ - *
  /` now buffer the right operand behind any non-floating left operand -
  the `open_memstream()` capture `rhs_begin()` already used behind a
  pending constant - and `float_arith()` writes the conversion and then the
  buffer. For an all-integer expression the bytes are unchanged (the whole
  fuzz run below is byte-identical).
- `doret()` returns through an assignment to the function's type, so
  `return c;` in an int function is the golden's `FTOI(INT) RFORCE(INT)`
  exactly.

**The back end's code** (`01_floatbas.s.golden`, abridged):

```
.data
L10000:	.float 3.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-8.(bp)
call	fstsp
...
lea	ax,*-8.(bp)          | c = a + b;
call	flds
lea	ax,*-12.(bp)
call	fadds
lea	ax,*-16.(bp)
call	fstsp
...
lea	ax,*-16.(bp)         | return (int) c;
call	flds
call	ftoi
jmp	L3
...
.globl	fltused
.data
```

`02_dblconv.s.golden` adds `mov di,*-14.(bp)` / `mov ax,di` / `call itof`
(`d = i;` - the int through DI, the working register, into AX), `fldd`/
`fstdp` for the double, `lea ax,L10000` / `call fdivs` (`d / 2.0` - the
constant is SINGLE precision even against a double), and `call ftol` /
`mov di,dx` / `mov si,ax` (the long result moved into DI:SI like any long
value). No FPU instruction anywhere: the 8086 machines had none.

**The runtime, from `libc.a`** (symbol tables read with a small `a.out`
reader, code with `objdump -D -b binary -m i8086` over each text segment -
`tests/mutos1700_libc/`):

| object | defines |
|---|---|
| `stacks.o` | `flds`, `fldd`, `fstsp`, `fstdp`, `fsts`, `fstd`, `fdup`, `fltused`, `fpsp` (+ `incfpsp`/`decfpsp`) |
| `singles.o` | `fadds`, `fsubs`, `fsubrs`, `fmuls`, `fdivs`, `fdivrs`, `ftod` |
| `doubles.o` | `faddd`, `fsubd`, `fsubrd`, `fmuld`, `fdivd`, `fdivrd` |
| `stkmath.o` | `fadd`, `fsub`, `fmul`, `fdiv`, `fneg`, `fcmp`, `ftest` (stack with stack) |
| `convert.o` | `itof`, `itod`, `ftoi`, `dtoi` |
| `lconvert.o` | `ftol`, `ltof` |
| `dmath.o` | `dadd`, `dsub`, `dmul`, `ddiv`, `dnorm`, `dround`, `fac` |

A stack of 8-byte doubles (`fpstk`, 64 bytes, top at `fpsp`); every entry
point is an ordinary function (`push bp` ... `jmp cret`, so DI/SI survive,
AX/BX/CX/DX do not) that takes a memory operand's ADDRESS in AX. `flds`
pushes a float by copying its 4 bytes into the new entry's HIGH half and
zeroing the low half; `fstsp` stores the high 4 bytes of the top and pops
(truncating a double's mantissa, not rounding); `fadds` widens its operand
into a local double the same way and calls `dadd` with SI = the stack top,
DI = the operand, then copies the result (`fac`) over the top - "top op
memory". `fdivs` is the same with `ddiv`; `fdivrs` swaps SI and DI - the
"reversed" form, memory op top. (`fsubrs`, oddly, has `mov di,si` where
`fdivrs` has `mov si,di`, so it looks broken - irrelevant here, nothing
generates it yet.) `itof` takes the int in AX; `ftoi` leaves its result in
AX, `ftol` in DX:AX.

**The floating format**, from data bytes the real compiler and assembler
wrote. `atof.o`'s data segment begins `00 00 00 b9`: 2**56 = 0.1(binary) *
2**57, excess-128 exponent 57+128 = 185 = `b9` - and 2**56 is exactly the
`big` constant v7's own `atof()` uses (from memory of v7's `libc`; its
source is not in this repo, and MUTOS's `atof.o` defines more than v7's
did - `_fltrd`, `__fltu3`). The rest of it and `ecvt.o`'s agree: `00 00 20 84` = 0.101b * 2**4 = 10.0, `00 00 00 81` =
1.0, `00 00 20 83` = 5.0. So: the value little-endian as a whole, the
exponent in the highest byte, the sign in the top bit of the byte below,
then the mantissa after v7's implicit leading 1 - the layout Microsoft
later called MBF, NOT PDP-11 word order (`CLAUDE.md` rule 5 now says so).
A double has the same exponent byte and 55 mantissa bits; `convert.o`'s
`itof` builds exactly this (exponent `0x90` = 128+16 for a 16-bit integer,
normalized by shifting).

**Which constants are `.float`.** `ecvt.o`'s data holds `10.0` and `1.0` in
4 bytes each but `c3 f5 28 5c 8f c2 75 7b` - 0.03 - in 8: all three are
the constants of v7's `ecvt.c` (again from memory - not in this repo),
whose digit loop computes `(int)((fj+.03)*10)`, and the object's 80-byte
bss matches its `static char buf[NDIG]`, NDIG 80. So the real compiler keeps a
constant exactly representable as a float in 4 bytes and any other in 8 -
v7's PDP-11 `c1` has the same idea as `SFCON`, a constant whose low words
are zero. The 8-byte form's assembler text is not in any golden, so
`mutos_c1` refuses a constant that is not exactly a float, and also zero
(v7 special-cases a zero `SFCON`; the MUTOS `c1` may too). The text is
`%.17e`: 18 significant digits, exponent sign and two digits - `fltpr.o`'s
`_pscien` writes `e`, `+` or `-` (negating), then `decpt/10` and
`decpt%10`. For the accepted values - exactly a float, below 2**24, at most
18 significant digits - that is the exact decimal expansion, which v7's
`ecvt()` also produces digit by digit (below 2**24 its `fi/10` steps are
far inside the `+.03` margin); larger values could come out with different
digits, so they are refused too.

**Where the `.data` block goes.** v7's `c1` numbers an `FCON` from its own
label counter when it reads the tree (`c11.c`'s `getree()`: `fp->value =
isn++` - the counter behind `L10000`) and prints it in `cexpr()` when the
operator that uses it is matched, before any of that operator's code
(`c10.c`: "`.data\nL%d:%o;...\n.text`"). `mutos_c1` streams, so it writes
the block when it reads the `FCON` - the same place whenever the operator's
other operand has no code of its own yet, which covers both goldens. A
constant right operand of an already computed value (`(a + b) * 2.0`)
would put the block after the left operand's code, so it is refused; so
is anything that would need two stack entries (a computed right operand -
including `a + i`, whose right operand is converted; libc's `...r` entry
points suggest the real compiler computes the int first, then uses
`fsubrs`-style reversed calls, but no golden shows it).

**Everything else is refused** in one of three places. `mutos_c0`:
`promote_char()` - the operand hook of every integer operator (shifts,
bitwise, relational, `%`, a subscript) - refuses a floating operand, and
`char_value_refused()` a floating value in a condition, `&&`/`||`/`!`/`?:`,
a call argument or `switch`; floating parameters, globals, statics, arrays,
pointers and members are refused at their declarations. `mutos_c1`:
`pop_val_ex()` refuses the new floating value kinds (`VK_FMEM`, `VK_FCON`,
`VK_FACC`, `VK_FDONE`) to every consumer but the floating handlers, and
`plan_expression()` never plans an expression containing a floating value
(its order rules are integer rules) - and refuses one with `&&`/`||`/`?:`/
`,` in it, which only a plan can generate (`d = (i && j);` reached the
streaming handlers' "internal: ... outside an evaluation-order plan" until
`scan_op_args_v()` learnt the floating opcodes' shapes).

**`mutos_as`.** The goldens' `.s` does not assemble: `lea ax,L10000` is an
"unsupported instruction" (`lea` takes indirect operands only), and
`.float` was SILENTLY SKIPPED - `handle_directive()` ignores what it does
not know, so `L10000` named the next thing in `.data` and every later data
address was 4 bytes short. `.float`/`.double` are now explicit errors;
implementing them (the encoding above) and `lea` with a label is the first
"Next up" item. (Done - see the next section.)

**Tooling.** `dump_temp.py` decodes `FCON` (`F` field), `ITOF`, `FTOI`,
`FTOL` and `LTOF` - `08_float`'s `.1.golden`s now decode to the end.
`x86sim.py` runs floating code against a model of the runtime: host floats
on a stack for `flds`...`ftol`, `.float` data and stores in the MUTOS format
(`mbf_encode()`/`mbf_decode()` reproduce the `libc.a` bytes above), a float
store truncated like `fstsp`, AX/BX/CX/DX poisoned after each call.

**Verification.** `make test` (clean build): 62/62, 0 genuine mismatches,
zero warnings; `mutos_as` 67/67, `mutos_cpp` 5/5. Against the previous
build (`c2c7a68`), on all 71 golden inputs: `mutos_c0` changed the two
`08_float` files and the wording of one diagnostic in four `11_kernel`
files (it now lists `float`/`double`; identical temp1), `mutos_c1` the two
`08_float` files only. `fuzz_c.py`: 42000 programs against the previous
build byte-identical (0 WRONG, 0 BAD), 4500 through ASan/UBSan builds
clean. `x86sim.py`: 53 of the 62 goldens run (both `08_float` ones: 7 and
3). Eleven hand-written floating programs through the whole pipeline and
`x86sim.py`, compared with the host C compiler: ten equal, one refused
(an int right operand); about 60 refusal probes each stop with their
diagnostic. ASan/UBSan over the golden inputs, programs and probes: clean.

### `mutos_as`: `.float`, `lea <reg>,<label>` - `08_float` assembles, links and runs (`mutos_as` extended and verified this session)

The first "Next up" item after `08_float`: make `mutos_c1`'s floating output
assemble. Both goldens now assemble, link with `mutos_ld` against the real
`crt0.o` and `libc.a`, and - run under an 8086 emulator - return their C
sources' values. On the way, two older `mutos_as` bugs surfaced, one of them
fatal for exactly this code.

**`lea <reg>,<label>`: the encoding is in `libc.a`'s machine code**, not just
analogous to `mov`. Scanning every text segment in `tests/mutos1700_libc/` for
`8D` with ModRM mod=00 rm=110 and a relocation on the disp16 finds 31: `atof.o`
(`8d 06 00 00`, `8d 06 04 00`, ...) and `ecvt.o` with R_DATA - the compiled-C
objects' own floating constants, `lea ax,<constant>` / `call flds` exactly as in
the goldens - and `ldexp.o`, `modf.o`, `doubles.o`, `singles.o`, `stkmath.o`,
`dmath.o` with R_EXT `fac` or R_DATA, at even and odd offsets (`0x8004`,
`0x8038`: the relocation word's shift bit). So `encode_lea()` takes an
`ADDR_DIRECT` operand through `emit_modrm_direct()` - the same mod=00 rm=110
form and non-PC-relative relocation classification as `mov`'s direct form, no
accumulator short form (LEA has none: `atof.o`'s `lea ax` is `8d 06`). It also
now refuses a byte-register destination (`lea al,...`), which it used to
encode.

**`.float`: the format, and what is (not) known about the conversion.** The
format is the `08_float` section's (and `CLAUDE.md` rule 5's):
excess-128 exponent in the highest byte, sign in bit 7 of the byte below
(`stkmath.o`'s `fneg` XORs bit 7 of a double's byte 6, a float's byte 2 since
`flds` loads a float into a double's high half), 23 stored mantissa bits after
the leading 1, exponent 0 = zero. What the real assembler does with a decimal
text is only partly visible:

- Every nonzero 4-byte constant in `libc.a` is exact (2\*\*56, 10.0, 1.0, 5.0),
  and so is every constant `mutos_c1` writes (it refuses the rest). For those,
  any correct conversion gives the same bytes.
- `ecvt.o`'s one 8-byte constant, `.03` (`c3 f5 28 5c 8f c2 75 7b`), is the
  correctly ROUNDED 56-bit value; truncation would end in `c2`. One sample.
- **Zero is not a plain zero.** `atof.o` (data+4, +16) and `ecvt.o` (+0, +4,
  +8, +28) hold every zero constant as `bc a2 31 00`: exponent byte 0, mantissa
  bits `0.1011 0001 1010 0010 1011 1100` - the top of 5\*\*17 =
  762939453125 = `0xB1A2BC2EC5` (and of 10\*\*17 = 5\*\*17 \* 2\*\*17). A text with
  17 fraction digits and exponent 0 is `%.17e` of zero -
  `0.00000000000000000e+00` - and v7's `atof()` algorithm scales such a text by
  `flexp` = 5\*\*17, so the bytes look like a zero result that kept 5\*\*17's
  mantissa. But `libc.a`'s own `atof` (`atof.o`, read in full: v7's algorithm,
  `fl /= flexp` via `fdivd`) cannot produce them: `dmath.o`'s `ddiv` tests the
  dividend's exponent byte and jumps to `zero:`, which clears all 8 bytes of
  `fac`, and `ldexp()` returns a zero exponent unchanged. So the real
  assembler converts with something else (another `atof`/`ddiv` version, its
  own code), or zero constants never went through `.float` text. Either way the
  bytes `.float 0.00000000000000000e+00` must produce are not known.
- The manual (`MUTOS1700_Assembler_as.pdf` sect. 3.2) spells a float constant
  `0f` + "characters atof accepts" and a double `0d` with a `d` exponent; the
  real compiler writes `.float` operands bare (`3.50000000000000000e+00`).

So `.float` (`fltconst.c`) accepts an optional `0f`, then atof syntax (sign,
digits with optional `.`, optional `e`/`E` exponent; at least one digit;
nothing after), and encodes a value **only if it is exactly representable** -
decided exactly: the text becomes D \* 10\*\*E (D an integer held in a small
bignum, trailing zeros folded into E); for E < 0 the value is dyadic only if
5\*\*-E divides D; the remaining odd integer must fit 24 bits and the exponent
the byte's 1..255. No host floating point is involved. An inexact value
(`0.1`) and zero are explicit errors naming the reason; so, still, is every
`.double`. Operands are re-scanned raw from the source, like `.asciz` - the
tokenizer splits `3.5e+00` into `3.` `5` `e` `+` `00`, and punctuation tokens'
text does not point into the source - up to `,`, newline, `;` or a `|`
comment.

**Bug 1 - padding at a segment switch put a byte into the code.** The first
assembled `01_floatbas` disassembled with a `00` after `call fstsp`: `.data` at
an odd text location padded the text segment, the rule `assemble.c` had carried
since `malloc.o` ("an odd-length `.text` immediately followed by `.data`") and
its mirror from `mch.o`. `mutos_c1` writes `.data` / `L10001: .float ...` /
`.text` between two instructions, so the pad executed as `add [bx+si],al`. The
real objects settle it: in `atof.o` the `lea ax,<constant>` follows the previous
instruction directly at text 71 and 407 (both odd, both right after such a
block), in `ecvt.o` at 201 and 345. Each segment is rounded up to even once, at
the end of the file; `malloc.s`'s and `mch.s`'s switches were their files' last,
where both rules produce the same bytes. The change leaves all 136 other `.s`
inputs (67 kernel files, 69 compiler goldens) byte-identical to the previous
build's objects.

**Bug 2 - `mov` with a byte register.** A regression test for `lea` with real
bytes: `ldexp.o` (hand-written; `lea si,*4(bp)`, `lea di,fac`, `lea si,huge`,
`lea ax,fac`) reconstructed from its disassembly as
`tests/mutos_as/libc_recon/ldexp.s`, the real object as its golden. The first
attempt differed in three bytes - `A1`/`A3`/`8B` where the real object has
`A0`/`A2`/`8A`: `encode_mov()` chose the byte opcodes of its reg↔mem and
reg↔reg forms only for `movb`, so `mov al,fac+7` loaded AX. The reg,imm form
already inferred byte size from a byte register (`mch.s`'s real `mov al,*0`);
the other forms now do too. With that, `ldexp.s` reproduces `ldexp.o` byte for
byte - header, text, data, both relocation tables, symbol table (its order is
first mention, so the reconstruction keeps the original's: `ERANGE`, `DOFF`,
`_ldexp`, `fac`, `ldexp3`, `ldexp2`, `_errno`, `huge`, `cret`). No kernel file
writes a plain `mov` with a byte register and a memory operand, so no golden
moved.

**Why no compiled-C `libc.a` object is a golden yet.** `atof.o`/`ecvt.o` would
test `.float` and `lea` together, but both hold zero constants, `ecvt.o` a
`.double`, and none of `libc.a`'s 167 objects has an `L`-number symbol - the
real `as`'s default without `-L` - while `mutos_as` always writes them, because
the kernel goldens compiled from C have them (the deliberate `-L` deviation, see
Milestone 2's CLI reference). Maybe the kernel was assembled with `-L`; unknown.
Instead `tests/mutos_as/libc_recon/floatdat.s` holds four of those objects'
constants as `%.17e` text, and `check_floatdat.sh` compares each assembled
value with the real object's bytes at test time: 6/6 identical (2\*\*56, 10.0
twice, 1.0 twice, 5.0).

**End to end.** Both `08_float` goldens assembled by `mutos_as`, linked by
`mutos_ld` with the real `crt0.o` and `libc.a` (every floating entry point and
`fltused` resolved), and run from `_main` under the Unicorn engine's 8086 mode
(a scratch harness: the 0407 image loaded as one 64K segment, a HLT as the
return address, no syscalls) return **7** and **3** - `(int)(3.5 * 2.0)` and
`(int)(7 / 2.0)` - computed by the real runtime from the assembled constants;
with the constants edited to 2.5 and -3.0 the first returns -7. Ten more
programs (float and double locals, `+ - * /`, int/long conversions, loops,
float code in a called function, 2\*\*24-1) through `mutos_cpp`, `mutos_c0`,
`mutos_c1`, `mutos_as`, `mutos_ld` and the same harness return what the host C
compiler's build returns. Before this change the same executables would have
run into the pad byte.

**Verification.** `make test` (clean build, zero warnings): `mutos_as` 68/68
(62 `kernel_opt`, 5 `kernel_nonopt`, 1 `libc_recon`), `check_floatdat.sh` 6/6,
`assemble_cc_goldens.sh` 71/71 (was 69), `mutos_cpp` 5/5, `mutos_c0`/`mutos_c1`
62/62. Against the previous assembler, every `.s` it accepted (136 of 138)
assembles to an identical object. ASan/UBSan build of `mutos_as` over 143
inputs (those plus hand-written `.float`/`lea` probes and their error paths): no
reports, objects identical to the `-O2` build's. `fltconst.c` against an
independent exact-fraction encoder (Python `fractions`) and `x86sim.py`'s
`mbf_encode()`: about 59,000 random exact and inexact texts (random 24-bit
mantissas over the whole exponent range, plain, scientific, `0f`-prefixed and
trailing-zero spellings, plus inexact neighbours), 0 differences, sanitizer
clean.

### Real-hardware confirmation: `-L` is a manual/binary mismatch, and the two open `.float`/`.double` gaps are closed (2026-09-28)

Two open items from the previous section — the "-L" question left open in
"Why no compiled-C `libc.a` object is a golden yet", and the zero-`.float`/
`.double` gaps described under "`.float`: the format, and what is (not)
known about the conversion" — both got real-hardware evidence this
session, from two small test directories built for exactly this purpose
(`tests/mutos_as/README.md` and `tests/mutos_as/float_coverage/README.md`).

**`-L` is not implemented by the real `as` at all.** The plan was to
assemble every real kernel `.s` both with and without `-L` and diff each
result against the already-committed `.o.golden` files, to settle whether
the kernel build's goldens (all carrying `L`-number labels) came from an
`as -L` invocation that the lost `libc.a` build never used (none of its
167 objects has an `L`-number label — the documented default). That
comparison never had to run: the very first `-L` invocation tried,
`as -L -o v30ide.oL v30ide.s`, printed `Unknown option L ignored` and
assembled anyway. `docs/MUTOS1700_Assembler_as.pdf` sect. 3.1 documents
`-L` as a real switch; the binary on this hardware simply does not have
it. Since the flag is silently ignored rather than merely defaulted to
off, an `as`/`as -L` pair is necessarily byte-identical for any input on
this hardware — there is nothing left to diff, so no `.oL` goldens were
generated. This rules out "the kernel build used `-L`" as an explanation
for the `L`-label discrepancy (the flag has exactly one behavior,
unconditionally), but does not resolve the discrepancy itself: the
kernel-vs-libc difference is still open, and the next angle is
source-level — whether the *compiler* emits `L`-labels only under
conditions the lost `libc.a` C sources never met, not a policy the
assembler applies per invocation. `mutos_as`'s own always-emit-`L`-labels
behavior (the "Known, deliberate exception" in `CLAUDE.md`) is unaffected
either way, since it was already chosen for golden parity rather than for
matching any hypothesized real invocation.

**Zero `.float` and `.double` now have real, known-source ground truth.**
`tests/mutos_as/float_coverage/`'s four hand-written sources all assembled
successfully on real MUTOS 1700 hardware. Two are plain regression
confirmations — `fltaddr.o.golden`/`fltmulti.o.golden` (`lea <reg>,<label>`
+ `.float` in one whole object) are byte-identical to current `mutos_as`'s
own output. The other two close gaps the previous section left open:

- `fltzero.o.golden` (source: `.float 0.00000000000000000e+00`) has data
  segment `bc a2 31 00` — the exact bytes `atof.o`/`ecvt.o` were already
  known to hold for a zero constant, now confirmed from a known,
  hand-written source rather than lost compiled-C input whose conversion
  path could only be guessed at. Where the constant's bytes come from is
  still not derivable from first principles (the previous section's
  `ddiv`-clears-`fac` puzzle stands unexplained), but what they *are* is
  no longer in doubt, which is what `mutos_as`'s `FLT_ZERO` refusal was
  waiting on.
- `fltdbl.o.golden` (source: `.double 1.00000000023283064365386962890625`,
  i.e. `1 + 2**-32`, chosen to need more than a float's 24 mantissa bits)
  has data segment `00 00 80 00 00 00 00 81`: exponent byte `0x81`
  (excess-128 → `2**1`, correct for a value just above 1.0), sign bit 0,
  and a single mantissa bit set exactly 32 bits in — precisely where
  `2**-32` should land under the same `0.1mmm * 2**(e-128)` scheme
  `.float` already uses, just with 55 stored mantissa bits instead of 23.
  This is the first real evidence pinning down `.double`'s bit layout
  beyond `ecvt.o`'s single already-known `.03` sample.

Both gaps are implementation work now, not open questions — see
`CLAUDE.md`'s "Next up" and `STATUS.md`'s open item 7. `tests/mutos_as/
float_coverage/README.md`'s own "Results" section has the full byte
breakdown. (Implemented the same day - next section.)

### `mutos_as`: zero `.float` and `.double` implemented (2026-09-28)

From the two goldens above. `fltconst.c` has one encoder for both formats now,
`flt_encode(kind, ...)` with `kind` `FP_FLOAT` or `FP_DOUBLE`, driven by a small format table (size,
significant bits, prefix, limits); `assemble.c` handles `.double` in the same
`run_pass()` branch as `.float` (raw operand re-scan, no alignment, no
relocation, the location counter advanced by the full size even for a refused
operand) instead of refusing it in `handle_directive()`.

**`.double`.** 8 bytes: the float layout with 56 significant bits - the byte-7
excess-128 exponent, the sign in bit 7 of byte 6, 55 mantissa bits below it.
`fltdbl.o.golden` pins it (`1 + 2**-32` → `00 00 80 00 00 00 00 81`: the one
mantissa bit 32 places after the leading 1 is bit 23 of the value, byte 2's
`0x80`). As for `.float`, only exactly representable values are accepted and
encoded exactly - the golden's value is exact, and the one inexact 8-byte
sample (`ecvt.o`'s `.03`, correctly rounded) has a lost source spelling and
cannot show how the real conversion rounds in general (v7's `atof()` scheme -
up to 17 digits accumulated in a double, `flexp` built by repeated squaring,
one `fdivd`/`fmuld` - is not obviously correctly rounded, and `libc.a`'s own
`atof` is not what the assembler uses, see the zero puzzle above). The
bounds grow with the precision: an exact double has at most 183 binary
fraction digits (2\*\*-127 times a 56-bit mantissa) and at most 145
significant decimal digits, so `.double` accepts up to 160 significant digits
and decimal exponents down to -190; `.float` keeps its 120 and -160 so every
`.float` text gets the same answer as before. Syntax: the bare form `fltdbl.s`
uses (with atof's `e`/`E` exponent), or the manual's constant spelling (sect.
3.2): `0d`/`0D` prefix, then a `d`/`D` exponent - neither prefixed form (`0f`
nor `0d`) is confirmed by real bytes. A zero `.double` is refused: `libc.a`
holds no 8-byte zero anywhere (all six zero constants in its data segments
are 4-byte `bc a2 31 00`), and under the 5\*\*17-leftover reading below its
low bytes would carry more of 5\*\*17 (`00 00 c5 2e bc a2 31 00`?) - a guess.

**Zero `.float`: only the confirmed spelling.** `bc a2 31 00` is confirmed
for `.float 0.00000000000000000e+00` - the `%.17e` text `mutos_c1` (and the
real compiler) writes - and matches all six zero constants in `atof.o`/
`ecvt.o`. It is NOT confirmed for zero in general: the mantissa is 5\*\*17's,
which fits a v7-`atof()`-style conversion that scales the digits by `flexp` =
5\*\*(fraction digits) and keeps the divisor's mantissa in a zero result, so
`.float 0.0` (`flexp` = 5) would plausibly come out `00 00 20 00`, and a
minus sign might set bit 7 of byte 2. So a zero is accepted only in the
confirmed shape - `.float`, no `-`, exactly 17 digits after the `.`,
exponent 0 (any count of integer zeros, a `+`, `e-00` or no exponent at all,
and the `0f` prefix are the same text to such a conversion) - and every other
zero is refused with a diagnostic naming the accepted spelling. A real-hardware
`.float 0.0`/`-0.00000000000000000e+00` probe would settle whether this can be
widened.

**Tests.** `tests/mutos_as/float_coverage/` joins the top-level `make test`
(`run_goldens.sh`: 4/4 byte-identical - `fltzero.s` and `fltdbl.s` were
refused before). `libc_recon/floatdat.s` gains the zero, and
`check_floatdat.sh` compares it with all six of its occurrences in `atof.o`/
`ecvt.o`: 12/12 (was 6/6). The float_coverage directory's four raw real-hardware
`.o` files are tracked alongside their `.o.golden` copies (byte-identical);
`run_goldens.sh` writes its own `<name>.o` over them, with identical bytes
while `mutos_as` matches.

**Verification.** Clean `make clean && make all && make test`: zero warnings,
`mutos_as` 72/72 (62 `kernel_opt`, 5 `kernel_nonopt`, 1 `libc_recon`, 4
`float_coverage`), `check_floatdat.sh` 12/12, `assemble_cc_goldens.sh` 71/71,
`mutos_cpp` 5/5, `mutos_c0`/`mutos_c1` 62/62. Against the previous assembler,
over 151 `.s` inputs (the 72 golden sources, `floatdat.s`, the seven
`v30_speculative` sources and the 71 compiler `.s` goldens), the 148 it
accepted assemble to identical objects; the three it refused (`fltzero.s`,
`fltdbl.s`, the new `floatdat.s`) are the only differences. `fltconst.c` against an independent exact-fraction encoder
(Python `fractions`, the MBF layout computed separately): 136,818 random texts
over both formats (random mantissas up to 2 bits past each format's precision,
exponents over the whole range, positional, scientific, `%.17e`, prefixed and
trailing-zero spellings, inexact neighbours, both formats for each value, zero
spellings and syntax errors) - 0 differences; the 69,296 `.float` texts among
them give the old encoder's answer everywhere except the seven accepted zero
spellings. ASan/UBSan (`-O0 -g`, LeakSanitizer off - on a Pass 1 error the
assembler exits without freeing its buffers, as before this change): the
encoder driver over all 136,818 texts and `mutos_as` over the 151 inputs plus
`.double`/zero probes (success and every error path) - no reports, objects
identical to the `-O2` build's. **End to end**: five programs with `.double`
constants (`1 + 2**-32`, `1 + 12345 * 2**-50`, `-1 - 7 * 2**-52`, `3 + 0x5a5a *
2**-54`, and the manual spelling `0d1.5d3`/`0D1.0D0`), each `fldd` / `fsubd` /
`fmuld` / `ftoi` through the real runtime, assembled by `mutos_as`, linked by
`mutos_ld` with the real `crt0.o` and `libc.a` and run from `_main` under
Unicorn's 8086 mode (scratch harness, not part of `make test`), return 1,
12345, -7, 23130 (`0x5a5a`) and 1499 - so the low mantissa bytes land where
the real runtime reads them.

(Its two restrictions - one zero spelling, exact values only - were superseded
the same day by the next section.)

### `mutos_as`: the real conversion, re-enacted (2026-09-28)

**The probe.** `tests/mutos_as/float_open/fltopen.s` asked the real `as` for
the four spellings the previous section left refused; `fltopen.o.golden`
(real hardware) answered, data segment in declaration order:

| Text | Real bytes |
|---|---|
| `.float 0.0` | `00 00 20 00` |
| `.float -0.00000000000000000e+00` | `bc a2 b1 00` |
| `.double 0.00000000000000000e+00` | `00 00 c5 2e bc a2 31 00` |
| `.float 0.10000000000000000e+00` | `cc cc 4c 7d` |

The first and third are exactly the bytes the previous section predicted
from v7 `atof()` - "`.float 0.0` (`flexp` = 5) would plausibly come out
`00 00 20 00`", and for a zero `.double` "`00 00 c5 2e bc a2 31 00`?" -
before the real assembler ran: a zero dividend keeps the divisor
`flexp` = 5\*\*k's mantissa (k = 1 and 17 here) with exponent byte 0, and the
zero `.double`'s high half is `fltzero.o`'s `bc a2 31 00`. The second is that
zero with the sign bit set: `fl = -fl` after the division. The fourth is
0.1 **truncated** to 24 bits (`0xcccccc`; correct rounding gives `0xcccccd`):
a `.float` is the high four bytes of the double conversion, like `stacks.o`'s
`fstsp`, which also truncates.

**What truncation implies for exact values.** The previous encoder wrote the
exact value of any exactly representable decimal text. That is right only if
the real double conversion is exact too, or errs upward: an error of one
56-bit ulp downward, truncated, costs a whole float ulp. v7 `atof()` has
rounding steps an exact value can pass through: `fl = 10*fl + digit` once `fl`
nears 2\*\*56 (the 18th digit of a `%.17e` text often makes `10*fl` inexact),
`flexp` for k > 24 (5\*\*25 has 59 bits), and the final `fl /= flexp` or
`fl *= flexp`. Digits beyond `fl >= 2**56` are dropped entirely.
`2.93572534179687500e+03` - an exact float, as `mutos_c1` would write it -
comes out `9b 7b 37 8c` (exact) if `dmul` rounds `10*fl` up or to nearest,
but `9a 7b 37 8c` if it truncates.

**The implementation.** `fltconst.c` now re-enacts `atof()` step by step on a
56-bit mantissa (`uint64_t` plus a portable 64x64->128 multiply and a
restoring division - still no host floating point): the `fl < 2**56`
accumulation with v7's exponent bookkeeping, `flexp` by v7's repeated
squaring (including where it squares), the division or multiplication,
`ldexp`, negation, the zero rule, and a range check after every step (the
real arithmetic's overflow behaviour is unknown, so any step outside the
format's exponent range is `FLT_RANGE` - e.g. `1.00000000000000000e-38`, whose
`flexp` = 5\*\*55 overflows although the value fits). Each double operation's
rounding is a parameter - truncate, nearest-even, nearest-away, away from
zero - for `dmul` (which `10*fl` and the squaring also use), `dadd` and
`ddiv`. `fltdbl.o.golden` rules out a truncating `ddiv`: under `atof()` its
33-digit text keeps 18 digits and its one division lands 0.13 ulp below the
real result, so the division rounded up. No other golden constrains
anything, which leaves 48 of 64 combinations. `flt_encode()` runs all 48 and
accepts a constant only if they agree on the stored bytes; otherwise
`FLT_ROUNDING` (replacing `FLT_INEXACT`). Zeros are accepted where they are
observed: `atof()`'s division path (negative decimal exponent) with k <= 24,
so 5\*\*k is exact - either sign, either directive. A zero on the
multiplication path (`0`, `0e5`) or with k > 24 stays `FLT_ZERO`.

Consequences, measured with the model over compiler-style texts:
- newly accepted: inexact values whose stored bytes no remaining rounding
  reaches - `.float 0.1`, `.double 0.1` (`cd cc cc cc cc cc 4c 7d`: every
  non-truncating `ddiv` rounds its 0.8-ulp remainder up), and
  `.double 3.00000000000000000e-02`/`.03`, which gives `ecvt.o`'s real
  `c3 f5 28 5c 8f c2 75 7b` - a further, independent fit of the model
  (the source spelling of `ecvt.o`'s constant is unknown, but both plausible
  ones give these bytes);
- newly refused: about 6% of the exact `%.17e` floats `mutos_c1` can write
  (636 of 10,184 in a random sample of exact floats below 2\*\*24 with at most
  18 significant digits), and many long exact expansions. These were a latent
  mismatch: the previous encoder's exact bytes were a guess for them. No `.s`
  in the repository uses one - every one of the 153 inputs below that
  assembled before assembles to the same object.

**The independent model.** `tests/mutos_as/float_coverage/fltmodel.py` is the
same conversion written separately in Python (arbitrary-precision integers).
`survivors` reads every `<name>.s` in `float_coverage/` with a golden, takes
each `.float`/`.double` constant's real bytes from the golden's data segment
(9 constants in 5 goldens), and reports the combinations that reproduce all
of them - failing if that set is not the one `fltconst.c` hard-codes, if a
golden contradicts the model outright, or if a golden holds a constant the
model does not cover. `check` compares `fltconst.c`, via the new test driver
`src/mutos_as/fltconst_test`, with the model on 49 edge cases and 2,000 seeded
random texts. Both run in `make test`; a doctored golden (0.1 as the correctly
rounded `cd`) makes `survivors` fail with "MODEL CONTRADICTED".

**The next probe.** `tests/mutos_as/float_open/fltmode.s` (not yet run on real
hardware): four `.double` constants in the compiler's form, chosen from about
7,500 candidates by greedy refinement to split the 48 combinations into 32
classes - all that is left is `ddiv`'s tie rule, and a division by 5\*\*k
never ties - plus the refused `2.93572534179687500e+03` itself, `.double 0.1`
and a negative zero `.double` (accepted now on the model's prediction alone),
and three zeros outside the zero rule (decimal exponent 0 and +4 - `atof()`'s
multiplication path - and 25 fraction digits, where 5\*\*25 is rounded). Its
`README.md` lists every predicted outcome; a simulated golden built from one
assumed combination decodes back to exactly that combination with
`fltmodel.py survivors . ../float_open`.

**Verification.** Clean `make clean && make all && make test`: zero warnings,
`mutos_as` 73/73 (62 `kernel_opt`, 5 `kernel_nonopt`, 1 `libc_recon`, 5
`float_coverage`), `check_floatdat.sh` 13/13 (`ecvt.o`'s `.03` added, all 8
bytes), `fltmodel.py survivors` ok, `fltmodel.py check` 0 differences in
2,049 texts, `assemble_cc_goldens.sh` 71/71, `mutos_cpp` 5/5,
`mutos_c0`/`mutos_c1` 62/62. `fltconst_test` against the Python model on
170,778 texts (the previous section's 136,818 random ones plus 33,960
targeted: `%.17e` of exact floats and doubles, integers around 2\*\*56...2\*\*66,
decimal exponents to -60, zeros with 0 to 29 fraction digits and exponents
-30...+7, prefixed spellings) - 0 differences, and the same under ASan/UBSan.
Against the previous commit's assembler, over 153 `.s` inputs (the 73 golden
sources, `floatdat.s`, the seven `v30_speculative` sources, `fltmode.s` and
the 71 compiler goldens): the 150 it assembled give identical objects;
`fltopen.s` and the extended `floatdat.s`, which it refused, now assemble
(to their golden and real bytes); `fltmode.s` is refused by both, by design.
The ASan/UBSan build over the same 153: no reports, output identical to the
`-O2` build's. The five end-to-end `.double` programs from the
previous section: the three with short or exact-conversion texts still
assemble and still return 1, 12345 and 1499 through the real runtime; the
two with 53- and 54-digit texts are now refused (`atof()` keeps 17 digits,
and the rounding decides the last bit).

### `mutos_as`: rounding pinned, zeros on both paths, and `libc.a`'s own `atof` as a second oracle (2026-09-29)

**The golden.** `tests/mutos_as/float_open/fltmode.s` ran on real
hardware; `fltmode.o.golden`'s data segment, in declaration order:

| Label | Text | Real bytes |
|---|---|---|
| `M1` | `.double 1.47397409833160963e-08` | `ff ff ff ff 10 3a 7d 66` |
| `M2` | `.double 2.97967517326469533e-09` | `ff ff ff ff ff c2 4c 64` |
| `M3` | `.double 1.45615926012396812e-02` | `fe ff ff ff be 93 6e 7a` |
| `M4` | `.double 1007211910940938336` | `05 d5 52 58 5e a5 5f bc` |
| `CF` | `.float 2.93572534179687500e+03` | `9b 7b 37 8c` |
| `D1` | `.double 0.10000000000000000e+00` | `cd cc cc cc cc cc 4c 7d` |
| `NZ` | `.double -0.00000000000000000e+00` | `00 00 c5 2e bc a2 b1 00` |
| `Z0` | `.float 0.00000000000000000e+17` | `ff ff ff 00` |
| `Z4` | `.float 0.0e+05` | `00 40 1c 00` |
| `Z25` | `.double 0.0000000000000000000000000` | `85 14 40 61 51 59 04 00` |

`fltmodel.py survivors . ../float_open`: every modelled constant fits,
and 2 of the 64 rounding-mode combinations are left - `dmul` and `dadd`
to nearest, ties to even, `ddiv` to nearest with either tie rule. The
tie rule cannot matter: `atof()`'s one division has the divisor 5\*\*k,
and an exact quotient needs the divisor mantissa's odd part to divide
the dividend's, leaving at most 56 - log2(odd part) significant bits -
too few for a tie (57) whenever the odd part is 3 or more. The smallest
odd part among the in-range `flexp` mantissas (k = 1..54, 5\*\*55
overflows) is 5. `CF` is the exact value (a truncating `dmul` would have
given `9a`), `D1` and `NZ` are the predicted bytes, `Z4` is 5\*\*4's
mantissa with exponent byte 0 - the division path's zero rule, on the
multiplication path - and `Z25` the mantissa of 5\*\*25 as a
nearest-even `dmul` builds it (truncation would end in `84`). `Z0` fits
nothing: 1.0's mantissa would be `00 00 00`.

**`libc.a`'s own `atof`, run.** `atof.o` is v7's algorithm compiled by
the real compiler; `dmath.o`, `doubles.o`, `stacks.o`, `stkmath.o` and
`convert.o` are the software floating-point runtime it calls (`fmuld`
passes the operand it addresses as `dmul`'s `[si]` and the stack top as
`[di]`; `fdivd` the stack top, the dividend, as `[si]`). Linked by
`mutos_ld` from the base64 objects - `crt0.o`, a one-instruction
`_main`, those members named explicitly, then `libc.a` (whose
`__.SYMDEF` is stale, so `-u _atof` alone does not pull the runtime in)
- and `_atof(text)` called directly under Unicorn's 8086 mode (scratch
harness, not part of `make test`), with the result read from `fac`:
**every nonzero constant in `float_coverage/`'s goldens comes out byte
for byte** (11 of 11, `M1`...`M4` included), a `.float` again being the
double's high half. Every zero comes out as eight zero bytes. Reading
the runtime explains both. `dmath.o`'s `round` (text 0x161) compares a
guard byte with 0x80, rounds a tie to even by the low mantissa bit, and
every alignment or product step folds the bits it drops into the guard
byte as a sticky bit - nearest-even; checked against exact rounding on
4,000 random operand pairs each for `ddiv` and `dadd` (including
`atof`'s shape, an integer below 2\*\*60 plus a digit): 0 differences.
`dmul` (0x1ac) tests each operand's exponent byte, `ddiv` (0x1ed) the
dividend's, and both jump to `zero` (0x1a1), which clears all eight bytes
of `fac`.

**The zero rule.** Changing only `zero` - store 0 into `fac`'s exponent
byte, leave the other seven - makes the emulated `atof` reproduce all
seven real zeros with k >= 1 (`fltzero`'s, `fltopen`'s `Z1`/`Z2`/`Z3`,
`NZ`, `Z4`, `Z25`) and changes no nonzero result. The runtime computes
`fl /= flexp` as `fldd fl` / `fdivd flexp` and `fl *= flexp` as
`fldd flexp` / `fmuld fl`; neither load touches `fac`, and for k >= 1
the repeated squaring ends with `flexp *= exp5`, so `fac` still holds
`flexp` when the zero is found - on either path, which is why the
division-path rule of the previous section and `Z4` agree. For k = 0 no
multiplication builds `flexp`, and `fac` holds whatever the digit loop
last left there: `-2**56` from `fcmp`'s subtraction in `libc.a`'s
runtime (so `00 00 80 00`), `ff ff ff 00` in the real assembler's.
Removing `dadd`'s zero shortcuts too, or its "exponents more than 56
apart" shortcut, does not produce `ff ff ff`; the real runtime differs
somewhere else as well, which one probe cannot show. So `fltconst.c`
(and `fltmodel.py`) now accept a zero wherever k >= 1 - either path,
any k whose `flexp` stays in range - and for k = 0 exactly `Z0`'s
shape: a `.float`, 18 digits (integer and fraction digits run the same
floating operations), no minus sign. A `.double` with k = 0, a negative
one (`fneg` on `Z0`'s set bit 7: flip or set?), and the LOGHUGE path
(nd - k < -39: `fl = 0`, exponent 0, so again the digit loop's
leftover) stay `FLT_ZERO`.

**`libc.a`'s multiplication is not exact - an open question the goldens
could not raise.** Checking the model against the emulated runtime on
random texts turned up zeros with k = 50..54 that differed in the low
byte - `.double 0e54`: `6d 4e a6 40 3c 0c 27 00` from the model,
`45 4e ...` from the runtime, 40 units apart. `flexp` itself differed.
On 3,000 random pairs of full 56-bit mantissas `dmul` missed the
correctly rounded product 2,217 times, by up to 403 units. The cause is
one instruction in `pmuld` (text 0x2e4..0x425, sixteen 16-bit partial
products of the mantissas' words a0..a3, b0..b3, a3/b3 the top byte
with the leading 1): the a0\*b2 term loads its multiplier with
`mov dx,2[di]` (b1) where `4[di]` (b2) was meant. The product it rounds
is therefore a\*b + (a0\*b1 - a0\*b2)\*2\*\*32 (a = `[si]`), always
between 2\*\*110 and 2\*\*112; that formula, rounded to nearest-even,
matches the emulated `dmul` on 20,000 random pairs (including
all-ones mantissas and ones with long runs of zeros), 0 differences.
It equals the exact product whenever a0 = 0 or b1 = b2 - true of every
product `atof` needs for every real constant so far: 10\*fl (10.0's
b1 = b2 = 0), the squarings up to 5\*\*32 (the low word of 5\*\*16 is 0)
and `flexp` up to 5\*\*49 (whenever the multiplier `exp5` has b1 != b2,
`flexp`'s low word is still 0), and `fl *= flexp` for k <= 3 (5,
25, 125 have b1 = b2 = 0). It differs in `fl *= flexp` for k >= 4 with
a nonzero a0 - a positive decimal exponent of 4 or more, counting digits
dropped past 2\*\*56, i.e. `%.17e` text from `e+20` up - and inside
`flexp` for k = 50..54 (`e-33` down). The real assembler's runtime
rounds like `libc.a`'s but handles zeros differently, so it is a
different version; whether it has the same `pmuld` is not known.

So `dmul` keeps two candidates: nearest-even of the exact product, and
`RM_LIBC_MUL` - nearest-even of `libc.a`'s product (`fltmodel.py`: mode
`libc`, a fifth `dmul` candidate beside the four rounding modes, so
`survivors` now checks 80 combinations; 4 survive). `MODES_MUL_ADD` is
split into `MODES_MUL` (`RM_NEAR_EVEN`, `RM_LIBC_MUL`), `MODES_ADD`
(`RM_NEAR_EVEN`) and `MODES_DIV` (`RM_NEAR_EVEN`, `RM_NEAR_AWAY`), and a
constant is accepted only if all four runs agree, as before
(`FLT_ROUNDING` otherwise; its diagnostic now names the product).
Measured over the compiler's `%.17e` form: every text with an exponent
from `e-32` to `e+19` is accepted (60,000 random 18-digit texts plus
zeros: 0 refused apart from the k = 0 zeros) - the 6% of exact floats
the previous 48 combinations refused, `CF` among them, included. Over
the whole range the `dmul` question refuses 19% of random exact floats
and 29% of random exact doubles (binary exponents -126..127), e.g.
`.float 1.26765060022822940e+30` = 2\*\*100: `00 00 00 e5` from the exact
product, `ff ff 7f e4` from `libc.a`'s. The previous 48 combinations
refused far more of that sample (49% of the floats, 93% of the doubles);
of the 20,000 floats, 224 it accepted are refused now - latent
mismatches if the real `pmuld` is `libc.a`'s - and no double. Every text
both versions accept gets the same bytes.

**The next probe**, `tests/mutos_as/float_open/fltmul.s`: five constants
whose bytes the two products decide (`.float` 2\*\*70 and 2\*\*100 in the
compiler's form, a `.double` at `e+25`, one at `e-33`, and the zero
`0.00000000000000000e-37`, whose bytes are 5\*\*54 itself under the zero
rule), three zeros with k = 0 (one digit instead of 18, `Z0`'s text as
a `.double`, `Z0` negated), the LOGHUGE path (`.double 1e-41`) and the
first negative nonzero constant. Its `README.md` has both rows of
predicted bytes; a simulated golden built from either decodes back to
exactly that `dmul` candidate.

**Verification.** Clean `make clean && make all && make test`: zero
warnings, `mutos_as` 74/74 (62 `kernel_opt`, 5 `kernel_nonopt`, 1
`libc_recon`, 6 `float_coverage` - `fltmode.s` new), `check_floatdat.sh`
13/13, `fltmodel.py survivors` ok (19 constants, 4 of 80), `fltmodel.py
check` 0 differences in 2,074 texts, `assemble_cc_goldens.sh` 71/71,
`mutos_cpp` 5/5, `mutos_c0`/`mutos_c1` 62/62. One-off: `fltmodel.py
check` with 150,000 random texts and 40,000 targeted ones (`%.17e` of
exact floats and doubles over the whole exponent range, integers
2\*\*54...2\*\*67, zeros with 0 to 40 fraction digits and exponents
-60...+25, prefixed spellings) - 0 differences. Every constant
`fltconst_test` accepted among 26,319 texts (random, targeted
large-exponent and zero texts) against the emulated `libc.a` `atof` -
nonzero ones on the unmodified runtime, zeros with k >= 1 on the one
with the changed `zero` - 0 differences; and `fltconst.c` built with
`MODES_MUL` = `RM_LIBC_MUL` alone, over 9,512 nonzero texts (most of them
exact values in the compiler's form with large exponents, where the two
products disagree), against `fltmodel.py`'s `libc` mode and the emulated
`libc.a` `atof`: 0 differences. Against the previous commit's
assembler over 154 `.s` inputs (the 74 golden sources, `floatdat.s`, the
seven `v30_speculative` sources, `float_open/fltmul.s` and the 71
compiler goldens): 152 identical objects; `fltmode.s`, refused before,
now assembles to its golden; `fltmul.s` is refused by both, by design.
ASan/UBSan (`-O0 -g`, LeakSanitizer off) over the same inputs plus a
probe of every new acceptance and refusal path: no reports, objects
and diagnostics identical to the `-O2` build's; `fltconst_test` under
both sanitizers over 60,074 texts: output identical.

### `mutos_as`: the real product, zeros at exponent 0, and `libcatof.py` (2026-09-29)

**The golden.** `float_open/fltmul.s` ran on real hardware the same day;
`fltmul.o.golden`'s data segment, in declaration order:

| Label | Text | Real bytes | Exact product | `libc.a`'s product |
|---|---|---|---|---|
| `P1` | `.float 1.18059162071741130e+21` | `ff ff 7f c6` | `00 00 00 c7` | `ff ff 7f c6` |
| `P2` | `.float 1.26765060022822940e+30` | `ff ff 7f e4` | `00 00 00 e5` | `ff ff 7f e4` |
| `P3` | `.double 1.23456789012345678e+25` | `4d ea 27 82 c9 64 23 d4` | `a6 ea ...` | `4d ea ...` |
| `P4` | `.double 1.23456789012345678e-33` | `d5 c8 7d e1 b5 20 4d 13` | `cb c8 ...` | `d5 c8 ...` |
| `Z54` | `.double 0.00000000000000000e-37` | `45 4e a6 40 3c 0c 27 00` | `6d 4e ...` | `45 4e ...` |
| `K1` | `.float 0e0` | `ff ff ff 00` | | |
| `K2` | `.double 0.00000000000000000e+17` | `00 00 00 00 ff ff ff 00` | | |
| `K3` | `.float -0.00000000000000000e+17` | `ff ff 7f 00` | | |
| `LH` | `.double 1e-41` | `00 00 00 00 00 00 00 00` | | |
| `NG` | `.float -1.50000000000000000e+00` | `00 00 c0 81` | `00 00 c0 81` | `00 00 c0 81` |

`fltmodel.py survivors . ../float_open`: 2 of 80 combinations,
`M=libc A=ne D=ne|na`. **The real assembler's multiplication rounds the
product `libc.a`'s `dmath.o` forms**, with its misplaced partial product -
five times over, two of them exact floats in the compiler's own form,
which the real assembler stores one unit below their value.
`fltconst.c`'s `MODES_MUL` becomes `RM_LIBC_MUL` alone, `fltmodel.py`'s
`EXPECTED` likewise; with `ddiv`'s tie rule the only choice left, and
`atof` never dividing to a tie, every constant the model covers is
determined and `FLT_ROUNDING` cannot occur any more. Over 130,092 texts
(random, compiler-style over the whole range, zeros, LOGHUGE) through the
previous commit's `fltconst_test` and this one: 9,773 refused as
`FLT_ROUNDING` before are accepted now, and no text both accept has
different bytes.

**Zeros with decimal exponent 0.** `K1` has one digit, `Z0` (fltmode) 18,
and both are `ff ff ff 00`; `K2` shows the low half of the same double,
`00 00 00 00`; `K3` shows the sign: `fneg` flips bit 7 of byte 6 (`ff` →
`7f`), as `libc.a`'s `stkmath.o` does - the first observation where
flipping and setting differ. `Z0`, `K1`, `K2`, `K3` follow four
different constants in their files (`NZ`, `Z54`, `K1`, `K2`), so the
leftover does not come from the previous conversion either. So the
digit loop, run over any number of zero digits, leaves the same double in
`fac`: `00 00 00 00 ff ff ff`, exponent byte then cleared by the zero
rule. Why is still not known - `libc.a`'s runtime leaves `-2**56` there
(`fcmp`'s difference; `dadd` of two zeros returns one of them), so one of
the real runtime's operations on zero operands differs from `libc.a`'s in
a way these bytes do not identify. Its float-shaped low half (0) suggests
a value produced at float precision, but that is a guess. `fltconst.c`
accepts every such zero now, both sizes and signs, as `ZERO_LOOP_FAC`
with bit 7 of byte 6 flipped for a minus sign.

**LOGHUGE.** `.double 1e-41` (nd - k = -40) is eight 0 bytes. `atof`
gives up here - `fl = 0` (the dirty-zero constant `bc a2 31 00`),
exponent 0 - so the result is a k = 0 zero, and `fac` holds whatever the
digit loop left: the last operation was the `fadd` that made `fl` = 1.0
(0 + 1 through the zero rule's `10*fl`), whose stored mantissa bits are
all 0. The emulated runtime with the changed `zero` routine gives the
same, and for other LOGHUGE texts whose last digit was accumulated it
gives `fl`'s mantissa with exponent byte 0 (`12e-45` → `00 ... 40 00`,
`123456789e-50` → `00 00 00 a0 a2 79 6b 00`; `libcatof.py check` compares
thousands of such texts); a nonzero leftover is the same arithmetic the
model already tracks. So
LOGHUGE is accepted when the text's last digit was accumulated (`fl`'s
mantissa) or all digits are 0 (`ZERO_LOOP_FAC`); after a dropped digit
the last operation was `fcmp`, whose difference `fl - 2**56` no real
constant shows, and the text stays `FLT_RANGE` (its value is below the
format's range anyway). `FLT_ZERO` is gone from `fltconst.h` and
`fltconst_test`: nothing returns it.

**`NG`** (`-1.5` → `00 00 c0 81`) is the first negative nonzero constant
observed; the model's sign handling (now a flip of bit 7 of byte 6
everywhere) already gave it.

**Still refused**: a step outside the format's exponent range. The
practical case is `flexp` = 5\*\*k for k >= 55 - `%.17e` text from `e-38`
down (`e-39` when `atof` drops the 18th digit), e.g. `.float
1.00000000000000000e-38`, whose value would fit. In `libc.a`'s runtime
the overflow reaches `__ovfl` (`fperr.o`), which sets `errno` and sends
the process `SIGFPE` - the real assembler may simply die there, which a
probe would show.

**`libcatof.py`.** The emulator harness of the previous section is now
a tool, `tests/mutos_as/float_coverage/libcatof.py` (`make
check-libcatof`; needs the Python module `unicorn`, so not part of `make
test`). It decodes the real objects from `tests/mutos1700_libc/` and
`crt0.o`, assembles a one-instruction `_main` with `mutos_as`, links with
`mutos_ld` (the members named explicitly - `libc.a`'s `__.SYMDEF` is
stale), loads the 0407 image into one 64K segment and calls `_atof` (or
`dmul`/`ddiv`/`dadd` directly) with a HLT as return address; each call
starts from a fresh copy of the image. Before use it checks the linked
code it depends on: `dmath.o`'s `zero` routine (`lea di,fac` / `sub
ax,ax` / four `stosw` / `ret`), its entry points, and `pmuld`'s
`mov dx,2[di]`. By default `zero` is patched to `mov byte fac+7,0` /
`ret` (the real assembler's zero handling), `--libc` leaves it alone.
Subcommands: `atof` (bytes for given texts), `goldens` (every golden
constant; the all-zero exponent-0 texts are reported as "not emulated",
not as differences), `check` (`fltconst_test`'s accepted output on edge
cases, seeded random texts and targeted ones - `%.17e` over the whole
range, zeros, LOGHUGE), `ops` (the runtime's `dmul`/`ddiv`/`dadd` on
random operand pairs against `fltmodel.py`'s `libc` product and
nearest-even). A build of `fltconst.c` with the exact product fails its
`check` (251 of 2,558 compared constants differ).

**Verification.** Clean `make clean && make all && make test`: zero
warnings, `mutos_as` 75/75 (62 `kernel_opt`, 5 `kernel_nonopt`, 1
`libc_recon`, 7 `float_coverage` - `fltmul.s` new), `check_floatdat.sh`
13/13, `fltmodel.py survivors` ok (29 constants, 2 of 80), `fltmodel.py
check` 0 differences in 2,092 texts, `assemble_cc_goldens.sh` 71/71,
`mutos_cpp` 5/5, `mutos_c0`/`mutos_c1` 62/62. `make check-libcatof`: 25
of 29 golden constants identical to the emulated `atof`, 4 "not
emulated", 0 different; 7,528 accepted constants compared, 0 different;
6,000 operations, 0 different. One-off: `fltmodel.py check` with 150,000
random texts, 0 differences; `libcatof.py check` with 30,000 (82,592
texts, 75,150 accepted constants compared), 0 differences. Against the
previous commit's assembler over 154 `.s` inputs: 153 identical objects,
`fltmul.s` newly assembled to its golden. ASan/UBSan over the same inputs
plus a probe of every new acceptance and refusal path: no reports,
objects and diagnostics identical to the `-O2` build's; `fltconst_test`
under both sanitizers over 75,092 texts: output identical.

### The overflow probes: `float_open/fltovf.s` and `fltsig.s` (2026-09-29)

(Written before the real run; the results are in the next section.)

What `mutos_as` still refuses is a conversion that leaves the format's
exponent range at some step (`FLT_RANGE`). Reading `libc.a`'s runtime,
and running it under `libcatof.py` with a hook on `__ovfl`/`__div0`,
shows two different mechanisms:

- **The last step, `ldexp`, wraps.** `ldexp.o` (reconstructed in
  `libc_recon/ldexp.s`) does `mov al,fac+7` / `sub ah,ah` / `add ax,bx` /
  `jo ldexp2` / `mov fac+7,al`: a 16-bit sum of the exponent byte and the
  exponent, checked only for signed 16-bit overflow, stored as its low
  byte. An exponent byte past 255 or below 1 therefore wraps, silently:
  `2.00000000000000000e+38` (the product before `ldexp` has exponent byte
  235, plus 21) becomes exponent byte 0, `1.0e+39` 2, `1.0e+50` 39;
  `2.5e-39` 0, `1.0e-39` 255, `5.0e-40` 254 - each with the value's
  mantissa. `ldexp2` (`huge`, `errno` = `ERANGE`) is reachable only for a
  16-bit overflow, i.e. an exponent far beyond anything `flexp` survives.
  Inside `atof` nothing else can leave the range for these texts: the
  digit loop stays below 2\*\*60, and a quotient `fl / flexp` with k <= 54
  has an exponent between about -125 and +60.
- **An overflow inside `atof` raises `SIGFPE`.** `flexp` = 5\*\*k overflows
  from k = 55 on (`flexp *= exp5` for k = 55..63, the squaring
  `exp5 *= exp5` itself from k = 64). In `dmul` the exponent sum
  overflows (`jo ovchk`); `ovchk` sends a wrapped-positive sum (an
  underflow) straight to `zero` - silently - and anything else through
  `call __ovfl` first. `__ovfl` (`fperr.o`) sets `errno` = `ERANGE` and
  calls `kill(getpid(), 8)`. The hooked emulation reaches `__ovfl` for
  `1.00000000000000000e-38` (5\*\*23 × 5\*\*32), `2.93873587705571877e-39`
  (5\*\*24 × 5\*\*32) and `1.0e+65` (5\*\*32 × 5\*\*32), and for none of the
  `ldexp` cases above. If the process survives the signal, `dmul` still
  ends in `zero`, and the division by that "zero" `flexp` reaches `__div0`
  - `SIGFPE` again.

So the probe is two files: `fltovf.s` holds the `ldexp` cases (two in-range
controls at the edges, `E1` 2\*\*127 in the compiler's form and `E2`
`3.0e-39`, which `mutos_as` already writes, then four above and three
below the range) and is expected to assemble, with the wrapped bytes
`libcatof.py` gives as its prediction - if the real `as` uses `libc.a`'s
`ldexp`. `fltsig.s` holds the three `__ovfl` cases, the compiler-relevant
`1.00000000000000000e-38` first; if the real `as` does not catch
`SIGFPE`, it dies there and writes no object, and the message is the
finding. Kept in one file, that death would have taken `fltovf.s`'s data
with it. `Makefile.mutos` assembles `fltovf.s` first; the modern
`Makefile`'s `goldens` now skips an empty object. A simulated golden built
from `libcatof.py`'s predictions decodes with `fltmodel.py survivors` as
expected: the controls check against the model, the seven others are
listed as "NOT MODELLED" with their bytes.

### `mutos_as`: out of range - `ldexp` wraps, an overflow aborts (2026-09-29)

Both probes of the previous section were run on real MUTOS 1700 hardware.

**`fltovf.o.golden`** (pushed 2026-09-29). Decoded with `fltmodel.py
survivors . ../float_open` before any change: `E1` and `E2` check against
the model, and the seven constants the model refused are listed as "NOT
MODELLED" with exactly the bytes `libcatof.py` had predicted:

| Label | Text | Real bytes | Exponent byte |
|---|---|---|---|
| `E1` | `.double 1.70141183460469232e+38` | `c9 ff ff ff ff ff 7f ff` | 255 (in range) |
| `E2` | `.double 3.0e-39` | `14 11 ff 27 1e ab 02 01` | 1 (in range) |
| `O1` | `.float 2.00000000000000000e+38` | `99 76 16 00` | 256 → 0 |
| `O2` | `.float -2.00000000000000000e+38` | `99 76 96 00` | 256 → 0 |
| `O3` | `.double 1.0e+39` | `eb 50 e2 a4 3f 14 3c 02` | 258 → 2 |
| `O4` | `.double 1.0e+50` | `ce 24 f3 2b 76 d8 08 27` | 295 → 39 |
| `U0` | `.double 2.5e-39` | `21 c7 53 ed dc c7 59 00` | 0 |
| `U1` | `.double 1.0e-39` | `1b 6c a9 8a 7d 39 2e ff` | -1 → 255 |
| `U2` | `.float 5.0e-40` | `7d 39 2e fe` | -2 → 254 |

The real assembler's `ldexp` is `libc.a`'s: a 16-bit sum of `fac`'s
exponent byte and the exponent, its low byte stored, no diagnostic. The
mantissa is always the value's own; the sign sits where `fneg` puts it
(`O2`), and a wrapped exponent byte of 0 (`O1`, `O2`, `U0`) gives bytes
that read as a "zero" with a nonzero mantissa - the same shape as the
real zeros.

**`fltsig.s`** - `as -o fltsig.o fltsig.s`, verbatim:

```
as -o fltsig.o fltsig.s
***ERROR*** floating point over/under flow- assembly aborted
W
?h), line 41
***ERROR*** floating point over/under flow- assembly aborted
W
?h), line 41
*** Error code 4

Stop.
```

Exit status 4, no object. Line 41 is `S1`, `.float
1.00000000000000000e-38` (5\*\*23 × 5\*\*32 overflows in `dmul`), so the
assembly stops at the first constant and `S2`/`S3` are never converted.
There was no "Floating exception" and no core: the real `as` catches
`SIGFPE`. The emulation explains the two messages if its handler prints
and returns: with `__ovfl` and `__div0` hooked to return instead of
signalling, `atof` on `S1` reaches `__ovfl` (the overflowing `dmul`) and
then `__div0` (the division by the zero `dmul` left in `flexp`); `S2`
the same; `S3` (`1.0e+65`, on the multiplication path) only `__ovfl`.
Two signals at line 41, two messages, then the assembler gives up with
exit status 4. The `W` and `?h)` lines stand where a file name might be
expected; they are recorded as they appeared, not interpreted.

**Changes.**

- `fltconst.c`: the last step stores `(x + 128 + exp10) & 0xFF` as the
  exponent byte instead of refusing a result outside 1..255. `|exp10|` is
  at most 54 there (a larger k has already overflowed in `flexp`), so the
  16-bit sum of the real `ldexp` never overflows and its `huge` branch is
  unreachable. `convert()` returns `CV_OK`, `CV_RANGE` (an operation of the
  arithmetic overflows: `flexp`, or `fl` × `flexp` as in `.double 9.9e+54`)
  or `CV_UNKNOWN`.
- New status `FLT_UNKNOWN` (`fltconst.h`): LOGHUGE after a dropped digit
  (`fac` holds `fcmp`'s difference, never observed) and a text of more
  than 100,000 digits - both were `FLT_RANGE`. `FLT_RANGE` now means
  exactly what the real assembler aborts on, and its message quotes the
  real one. `mutos_as` keeps its exit status 1 for every error; the real
  `as` exits with 4 here - a difference noted, not changed, since nothing
  shows the real status of any other error.
- `fltconst_test` prints `UNKNOWN`; `fltmodel.py` implements the same wrap
  and the `UNKNOWN` status, and its edge cases gain the nine `fltovf.s`
  texts, `fltsig.s`'s `S2` and `S3` (`S1` was one already) and four more
  (`9.9e+38`, `1.0e+38` as a float, `9.9e+54`, an 18-digit text with
  exponent +54).
- `libcatof.py`: `__ovfl` and `__div0` are hooked; reaching either stops
  the emulation, and `atof` prints `SIGFPE (__ovfl)` instead of bytes.
  `check` now requires that every text `fltconst_test` refuses as `RANGE`
  raises `SIGFPE` and that no accepted one does. Mutation checks: a
  `fltconst.c` that refuses the wrapped results instead fails it with 57
  `RANGE` texts that raise no `SIGFPE` (of 8,357 texts at `LIBCATOF_N` =
  3000); one with the exact product fails with 749 of 7,588 compared
  texts different.
- `fltovf.s` and its golden moved to `float_coverage/` (header rewritten as
  resolved, code unchanged; its `Makefile`/`Makefile.mutos` now list eight
  files). `fltsig.s` stays in `float_open/` unchanged, so that "line 41"
  keeps meaning `S1`; that directory has no open probe, its
  `Makefile.mutos` has only the `fltsig` rule and is expected to stop with
  `*** Error code 4`, and its `README.md` records the output above.

**Verification.** Clean `make clean && make all && make test`: zero
warnings, `mutos_as` 76/76 (62 `kernel_opt`, 5 `kernel_nonopt`, 1
`libc_recon`, 8 `float_coverage` - `fltovf.s` new), `check_floatdat.sh`
13/13, `fltmodel.py survivors` ok (38 constants, 2 of 80), `fltmodel.py
check` 0 differences in 2,107 texts (150,107 in a one-off run),
`assemble_cc_goldens.sh` 71/71, `mutos_cpp` 5/5, `mutos_c0`/`mutos_c1`
62/62; `make check-docs` clean. `make check-libcatof`: 34 of 38 golden
constants identical to the emulated `atof`, 4 "not emulated" (the known
all-zero exponent-0 texts), 0 different; 8,357 texts, 0 differences, 210
`RANGE` all raising `SIGFPE`; `ops` 0 differences. A one-off `check` with
30,000: 82,607 texts, 75,589 compared, 1,717 `RANGE` all raising `SIGFPE`,
0 differences. The previous commit's assembler against this one over 156
`.s` inputs (every golden source, `floatdat.s`, `float_open/fltsig.s`, the
seven `v30_speculative` sources, the 71 compiler goldens): 155 identical
results, `fltovf.s` newly assembled to its golden; over 130,107 texts
through both `fltconst_test`s no text both accept has different bytes,
1,853 are newly accepted (wrapped), 2 went from `RANGE` to `UNKNOWN`.
ASan/UBSan over the same 156 inputs plus a probe of every new acceptance
and refusal path: no reports, objects, diagnostics and exit statuses
identical to the `-O2` build's; `fltconst_test` under both sanitizers over
the 130,107 texts: output identical.

### Floating shapes from libc.a's compiled C (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-09-29)

`STATUS.md`'s "Next up" item 1 listed the floating shapes `mutos_c1` still
refused, "each waiting for a golden". Most of them turned out to be in the
repository already, as real compiler output: four of `libc.a`'s objects
(`tests/mutos1700_libc/`) are C compiled by the real MUTOS 1700 compiler -
`atof.o` (v7's `atof.c`, plus a MUTOS `_fltrd` and an empty `__fltu3`),
`ecvt.o` (v7's `ecvt.c`: `ecvt`, `fcvt`, the static `cvt`), `gcvt.o` and
`fltpr.o` (MUTOS's `_pgen`/`_pfloat`/`_pscien`) - found by listing every
object whose symbol table names a floating runtime entry point, then read
with a small `a.out` reader that annotates `objdump -D -b binary -m i8086`
output with each relocation's symbol or segment (scratch script, not in
the repo; the sources are v7's, from memory - not in this repo either).

**The caveat: `libc.a` was optimized.** `atof.o`'s `if (negexp<0) fl /=
flexp; else fl *= flexp;` ends both arms in ONE `lea ax,fl` / `call
fstdp` (the `if` arm jumps to the `else` arm's tail) - `c2`'s
cross-jumping, which `c1` alone never does - and `ecvt.o`'s `if (fi != 0)`
is `jne` over a `jmp`. So only shapes the optimizer cannot have changed
are taken: the runtime calls and their operands, the order operands are
loaded in, which entry point is used. Branch structure, label numbers,
where a `.data` block sits relative to code, and whether an `Lnnn:` return
label precedes a function's final `jmp cret` are not readable from it.

**Frames.** `atof(p) register char *p;` with `register c; double fl, flexp,
exp5; double big = 72057594037927936.; int nd; register eexp, exp, neg,
negexp, bexp;`: `p` in DI, `c` in SI (the two register variables), `fl`
at -12, `flexp` -20, `exp5` -28, `big` -36, `nd` -38, then the five
surplus `register` ints as autos from -40 down; `ecvt.o`'s `cvt(arg, ndigits,
decpt, sign, eflag) double arg;` has `arg` at `4(bp)` and `ndigits` at
`12(bp)` - a double parameter takes 8 bytes.

**What the code shows** (`fl`, `big`, ... for their frame slots, `Ln`
for a `.data` constant - the objects hold no `L` symbols):

| Source (v7) | Real code |
|---|---|
| `double big = 72057594037927936.;` (an auto initializer) | `lea ax,L` / `call flds` / `lea ax,big` / `call fstdp` |
| `fl = 0;` (atof), constants `bc a2 31 00` | `lea ax,L` / `call flds` / `lea ax,fl` / `call fstdp` |
| `if (fl<big)` | `lea ax,fl` / `call fldd` / `lea ax,big` / `call fldd` / `call fcmp` / `sahf` / `jge` (false branch) |
| `fl = 10*fl + (c-'0');` | `lea ax,L(10.0)` / `call flds` / `lea ax,fl` / `call fmuld` / `mov ax,si` / `add ax,*-48.` / `call itof` / `call fadd` / `lea ax,fl` / `call fstdp` |
| `flexp *= exp5;`, `exp5 *= exp5;` | `lea ax,exp5` / `call fldd` / `lea ax,flexp` / `call fmuld` / `lea ax,flexp` / `call fstdp` |
| `fl /= flexp;` | `lea ax,fl` / `call fldd` / `lea ax,flexp` / `call fdivd` / (shared) `lea ax,fl` / `call fstdp` |
| `fl *= flexp;` | `lea ax,flexp` / `call fldd` / `lea ax,fl` / `call fmuld` / (shared) ... |
| `fl = ldexp(fl, negexp*bexp);` | `mov ax,negexp` / `imul bexp` / `push ax` / `lea ax,fl` / `call fldd` / `sub sp,*8.` / `mov ax,sp` / `call fstdp` / `call _ldexp` / `add sp,*10.` / `call fldd` / `lea ax,fl` / `call fstdp` |
| `if (neg<0) fl = -fl;` | ... `lea ax,fl` / `call fldd` / `call fneg` / `lea ax,fl` / `call fstdp` |
| `return(fl);` | `lea ax,fl` / `call fldd` / `lea ax,fac` / `call fstdp` / `lea ax,fac` / `jmp cret` |
| `if (arg<0)` (ecvt) | `lea ax,L(0)` / `call flds` / `lea ax,arg` / `call fldd` / `call fcmp` / `sahf` / `jle` - the operands exchanged |
| `if (fi != 0)`, `while (fi != 0)` | the zero loaded first again, `fcmp` / `sahf` / `jne` |
| `else if (arg > 0)` | the zero first, `fcmp` / `sahf` / `jl` (true branch) |
| `fj = modf(fi/10, &fi);` | `lea dx,fi` / `push dx` / `lea ax,fi` / `call fldd` / `lea ax,L(10.0)` / `call fdivs` / `sub sp,*8.` / `mov ax,sp` / `call fstdp` / `call _modf` / `add sp,*10.` / `call fldd` / `lea ax,fj` / `call fstdp` |
| `(int)((fj+.03)*10) + '0'` | `lea ax,fj` / `call fldd` / `lea ax,L(.03, 8 bytes)` / `call faddd` / `lea ax,L(10.0)` / `call fmuls` / `call ftoi` / `add ax,*48.` |
| `while ((fj = arg*10) < 1)` | `lea ax,L(10.0)` / `call flds` / `lea ax,arg` / `call fmuld` / `lea ax,fj` / `call fstd` / `lea ax,L(1.0)` / `call flds` / `call fcmp` / `sahf` / `jl` - and in `.data` the 1.0 comes BEFORE the 10.0 |
| `arg *= 10;` | `lea ax,L(10.0)` / `call flds` / `lea ax,arg` / `call fmuld` / `lea ax,arg` / `call fstdp` |
| `cvt(arg, ndigits, decpt, sign, 1)` (ecvt) | `mov di,*1.` / `push di` / three `push` / `lea ax,*4.(bp)` / `call fldd` / `sub sp,*8.` / `mov ax,sp` / `call fstdp` / `call _cvt` / `add sp,*16.` |
| a store through a pointer (`_fltrd`) | `push s` / `call _atof` / `add sp,*2.` / `call fldd` / `mov bx,pp` / `mov ax,(bx)` / `call fstdp` (or `fstsp`) |

**The runtime's side** (`stkmath.o`, disassembled): `fcmp` pops both
entries, computes second minus top with `dsub`, and returns the flags of
the difference in AH - ZF from its exponent byte (0: zero), SF from its
sign, OF and CF clear (`or ah,ah` / `je` / `or al,7fh` / `lahf`); after
`sahf` the ordinary signed branches read it. `fadd`/`fsub`/`fmul`/`fdiv`
pop the top and combine it into the entry below (second op top - left op
right for operands pushed left first); `fneg` flips the top's sign bit in
place; `ftest` pops the top and returns its flags the same way. `stacks.o`
has `fsts`/`fstd` - store without popping - and `dmath.o` defines `fac`.

**How v7's c1 explains it.** Everything above is v7's own algorithm:

- `optim()` exchanges a relational's operands when `degree(left) <
  degree(right)`, or when the degrees are equal and the left one is a
  NAME and the right one is not (`v7/cc/c12.c`). A DOUBLE NAME and every
  constant have degree 0, a FLOAT NAME 1 (`degree()` gives a FLOAT leaf
  1), anything computed at least 1 - so `arg < 0` becomes `0 > arg`
  (equal degrees, NAME on the left), `fl < big` stays, `(fj = arg*10) <
  1` stays.
- `unoptim()` folds ITOF of a CON into a floating constant (SFCON); the
  MUTOS compiler writes it like a written one, `%.17e` of its value. The
  zeros in `atof.o`/`ecvt.o` are all `bc a2 31 00` - exactly what the
  real assembler makes of `.float 0.00000000000000000e+00`
  (`tests/mutos_as/float_coverage/fltzero.o.golden`), so the text is
  that, and a zero is loaded like any other constant (`flds`), not
  special-cased.
- `cexpr()` prints a constant's `.data` block when the operator using it
  is matched, before any of that operator's code - so for `(fj =
  arg*10) < 1` the comparison's 1.0 is printed before the product's
  10.0, as `ecvt.o`'s data order shows.
- `acommute()` keeps equal-degree operands of `+`/`*` in source order:
  `10*fl` loads the 10 first (`flds` / `fmuld fl`).
- `cbranch()` treats a zero right operand specially (`op += 200`, "special
  for ptr tests" - the PDP-11 tests instead of comparing); the MUTOS
  counterpart is very likely `ftest`, but nothing in `libc.a` reaches it
  (every zero there is exchanged to the left).

**Refuting a guess.** The old "Next up" suggested that an int right
operand (`d + i`) is computed first and combined through `singles.o`'s
and `doubles.o`'s "reversed" entry points (`fsubrs`, `fdivrd`, ...).
`atof.o`'s `10*fl + (c-'0')` does not do that: the left operand is
computed, then the int converted (`itof`), then `stkmath.o`'s
stack-with-stack `fadd`. No object in `libc.a` but the two that define
the reversed entry points refers to one. Where the real compiler uses them is still unknown;
one consistent reading of v7's tables is `-=`/`/=` with a computed right
operand (the right operand on the stack, the target in memory) - v7's
`table.s` has `%ad,nf` templates for exactly that - but that is a
hypothesis, which `fltprobe/p2_arith.c` tests.

**`=*` is not v7's PDP-11 template.** v7's `table.s` compiles `a =* b`
(two addressable doubles) as "load a, multiply by b, store a" (`cr72`
reuses `[addq1a]`); `atof.o` loads the RIGHT operand and multiplies by the
target in memory, for variables and for `arg *= 10` alike, while `/=`
loads the target. So the MUTOS table was changed for `=*` (a commutative
shortcut); nothing in `libc.a` shows `=+` or `=-`, which are not assumed
to follow either.

**Implemented** (`mutos_c0` and `mutos_c1`; all in `c0_parser.c`'s and
`c1_gen.c`'s "Floating point" sections):

- **Comparisons** as conditions (`if`, `while`, `do`, `for`, `!` on
  one): `mutos_c0` converts an int operand to the floating one's type
  and types the node INT, as v7's `build()` does (the right operand of
  a non-floating left one is buffered, so an int left operand's `ITOF`
  can go between them - the same capture `+ - * /` already used);
  `mutos_c1`'s `gen_fp_compare()` exchanges by the degree rule, loads
  both, `call fcmp` / `sahf`, and the branch follows directly
  (`VK_COND` with `cond_is_float`; `gen_cond_branch()` refuses anything
  between).
- **Int constants converted** (`gen_itof()`): a `.float` of `%.17e` of
  the value, zero and negative values included; a written zero
  (`fcon_render()`) too.
- **Unary minus**: `mutos_c0` now writes `NEG`, typed like its operand
  (FLOAT for a float variable - v7's unary `build()`); `mutos_c1`: load,
  `call fneg`.
- **`*=` and `/=`** into a float or double variable (`gen_fp_asop()`),
  the right operand a variable or a constant - an int constant converted
  to the target's own type (ITOF(FLOAT) into a float, as for `=`); a
  computed one, an int variable's `itof` included, is refused.
- **A computed left operand with an int right operand** of `+ - * /`:
  `call itof`, then `call fadd`/`fsub`/`fmul`/`fdiv` - checked on the
  pre-scanned tree (`float_order_check()`) to be `atof.o`'s kind, an int
  variable or a variable plus or minus a constant, so that v7's
  `acommute()` would not reorder it.
- **An assignment whose value is used** (`gen_fp_assign()`, told by
  `scan_consumer()`): `fst<s|d>`, the value left on the stack.
- **Double and float parameters** (a float parameter is a double - v7's
  `funchead()` - 8 bytes of the frame), **double arguments**
  (`push_fp_arg()`), **functions returning a double** - `double f();`
  prototypes and definitions, `return` through `fac` (`gen_fp_rforce()`,
  `|RTYP 3`), and the caller's `call fldd` after the call.

**Still refused, each with its own diagnostic**: a zero as the right
operand after the exchange (a float variable or a computed value compared
with 0), a floating truth test, a comparison as a value, `&&`/`||`/`?:`
around floating code, a constant right operand of a computed value (in a
comparison too - its `.data` block would have to precede the computed
value's code), two computed comparison operands, any other computed right
operand of `+ - * /` (`c * (a+b)`, `(a+b) * (c+d)`) and an int right
operand of a variable (`d + i`), `+=`, `-=`, `*=`/`/=` by a computed
value or an int variable, a
constant that is not exactly a float or is 2\*\*24 or more, a written
constant after an int constant converted in the same expression (v7 numbers
a written constant while reading the tree and a converted one only in
`optim()`; how the MUTOS compiler numbers the converted one is unknown),
a floating constant as a call argument, a computed floating argument other
than the last, an unused double result, a doubly negated value, a function
returning a float, floating globals, statics, arrays, pointers and
members, and `char`/`long` to or from floating. `tests/mutos_cc/fltprobe/`
holds nine programs for real hardware: four that exercise only what is now
implemented (to confirm it byte for byte, including what `libc.a`'s
optimized code cannot show - label numbers, `.data` placement, where the
return sequence goes) and five that exercise the refused shapes above.

**First real-hardware result (the same day).** `cc -S p1_compare.c` stopped
with `p1_compare.c:37: Illegal type of operand` - `if (!e)`, `e` a double -
and nothing else in the file: the real front end rejects `!` on a floating
operand, although v7's `build()` accepts one (`!un` has no `LWORD` in v7's
`opdope[]`; `c01.c`'s EXCLA case just types the node INT). So the MUTOS `c0`
differs from v7 here, and `mutos_c0`'s refusal of `!e` is right - only its
wording ("not yet supported") understates it. The line was removed from
`p1_compare.c`. The second run got past `c0` and failed in `c1`: `37:
Floating point stack underflow` - the line of `while (a)`, `a` a float -
then `38: floating point stack underflow` (lower case: another message
site) for `a = 0;`, and from there one message per stack pop of every
floating statement to the end of the function (two per comparison, four
for the `&&`), so the real `c1` keeps a compile-time model of the runtime
stack's depth, and after the first underflow it stays below zero. Nothing
earlier in the file failed, `if (d)` on line 35 (its CBRANCH carries line
36 - `if` reads one token past its `)`, `while` does not) included, but
whether `if (d)` already left that model one entry short cannot be told.
Both truth tests were removed from `p1_compare.c`; a floating value tested
for truth stays refused, now with a real reason.

**Tooling.** `dump_temp.py` decodes `NEG` (`mutos_c0` writes it now).
`x86sim.py` runs the new calls - `fsts`/`fstd`, `fadd`...`fdiv`, `fneg`,
`fcmp` with `sahf` (only directly after it), `fac` - and no longer divides
eagerly: its table of the four results of a floating operation computed a
division for every `fmuls`/`fmuld` too, so a multiplication by zero
stopped it with Python's `ZeroDivisionError` (no golden multiplies by
zero, so it never showed).

**Verification.** `make test`: 62/62 byte-exact, `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 71/71 compiler goldens assemble, `mutos_cpp`
5/5, zero warnings. Against the previous build (`3c801a0`), on all 71
golden inputs: `mutos_c0` (`.i` -> `.1`/`.2`, diagnostics included) and
`mutos_c1` alone (golden `.1`/`.2` -> `.s`) identical, every file.
`fuzz_c.py` against the previous build: 42,000 programs (18,000 with
arrays, seed 11; 18,000 `--scope`, seed 21; 6,000 scalars only, seed 31):
all identical, 0 WRONG, 0 BAD; 4,500 more through ASan/UBSan builds: 0
WRONG, 0 BAD (the generator makes no floating code - what it checks is
that the integer paths `mutos_c0`'s relational change touched stayed the
same). Thirteen hand-written floating programs -
the four `fltprobe` confirmation files and nine more (every new shape,
`atof.o`'s and `ecvt.o`'s statements among them, loops with floating
conditions, three double functions calling each other) - through
`mutos_cpp`, both passes, `mutos_as` and `x86sim.py`, compared with the
host C compiler: all equal; 27 refusal probes and the five `fltprobe`
open-question files each stop with their diagnostic. ASan/UBSan builds of
`mutos_c0`/`mutos_c1` over all of these and the 71 golden inputs: no
reports, output and exit status identical to the `-O2` build's.
`x86sim.py` gives the same output as before on all 53 goldens it runs.

### The fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-09-29)

The nine `tests/mutos_cc/fltprobe/` programs came back from real hardware
the same day (commit `ac4ba38`). **`mutos_c0` matched every `.1`/`.2` golden
it produced at once** (the four confirmation files, `p1_compare`,
`p4_const`). **`mutos_c1`'s four confirmation files differed in a few lines
each**, and every difference traced to one v7 rule the libc.a-based model
had slightly wrong; the five open-question goldens then answered every
question they asked. All nine are now byte-exact end-to-end; none of the 62
corpus files changed.

**1. A floating constant has degree 1.** v7's `degree()` gives a constant
leaf -3 (`CON`) or, as a DOUBLE leaf, 0 (`FCON`/`SFCON`), and a FLOAT leaf 1 -
and the model gave floating constants 0. The goldens say 1: `03_fltcmp`'s `a
< 1.5` (`a` a float) loads 1.5 first and branches `ble` (exchanged:
degree(a) = 1 is not below degree(1.5) under the old rule, so v7 would not
exchange), `p1_compare`'s `a < 0` and `a == 0` the same, and `1.5 < i` /
`i < 1.5` both keep their order (the constant and the converted int
tie at 1, the left one is no NAME). `04_fltconst`'s `d = e + 10` loads
10.0 first and adds `e` from memory - acommute() puts the constant (1)
before the double (0). The reading that fits: the MUTOS compiler keeps a
constant that is exactly a float as a 4-byte `.float`, typed FLOAT, and
`degree()` does the rest unchanged. `libc.a` could not show it: `atof.o`
and `ecvt.o` never compare a float variable with a constant, and their
`fl < big`, `arg < 0`, `10*fl` fit either rule.

**2. The rest of v7's order, confirmed.** A '+'/'*' chain is acommute()'s -
terms by decreasing degree, equal ones as written, rebuilt left to right:
`p3_global`'s `(int)(gd + gi + ga[2] + arr[0] + gs.y)` starts with the
float `gs.y`, `p2_arith`'s `(d + e) * (i * j)` with `i * j` (an int product
has degree 2 - acommute()'s `d++` for TIMES), `b * (a + e)` with `b`. Every
operator loads its left operand first; a right operand in memory is combined
from there (`lea ax,<it>` / `call fadd<s|d>`), a computed one is computed
onto the runtime stack after it and combined with `stkmath.o`'s
stack-with-stack entry point: `d - i` is `fldd d / itof / fsub`, `d + i` is
`itof / faddd d` (acommute: the conversion first), `i - d` is `itof / fsubd
d`. **No "reversed" entry point** (`singles.o`'s `fsubrs`...) is used
anywhere. A `.data` block is printed where v7's `cexpr()` prints an `FCON`
operand - when its operator is matched, before the operator's code: `(d +
e) * 2.0` is `.data` / `L10004:<TAB>.float 2.0...` / `.text` / `fldd d` /
`faddd e` / `lea ax,L10004` / `fmuls`. `-(-e)` is `e`. `-1.5` is folded into
the constant (`.float -1.50000000000000000e+00`, no `fneg`), as v7's
`unoptim()` folds a negated `FCON`. `(d + e) - 0.5` stays a subtraction:
v7's `optim()` makes `x - c` into `x + -c` only for a `CON`/`SFCON`.

**3. A zero is compared, never tested.** `p1_compare`'s `(d + e) < 0` and
`(d - e) != 0` load a `.float 0` and `fcmp` it - no `ftest`, although v7's
`cbranch()` tests a zero right operand. So the MUTOS compiler makes no
`SFCON` of a zero (or does not test one); `stkmath.o`'s `ftest` is unused
by compiled code.

**4. Labels.** A written constant takes its label when `c1` reads the tree,
an int constant converted (unoptim()'s `ITOF(CON)` fold) one only in
`optim()`: `p4_const`'s `d = 2 * 1.5` numbers 1.5 `L10007` and 2.0
`L10008`, and prints 2.0's block first (it is the left operand). A file-scope
initializer's `FCON` takes a label too, though none is written:
`p3_global`'s first code constant is `L10002` after two initializers.

**5. `x - c` is `x + -c` for an int** (v7's `optim()`, a `CON` right operand):
`05_fltasop`'s `c - '0'` is `add ax,*-48.` - `mutos_c1` wrote `sub`, and
`gen_charx_binop()` already had the rule for a char. `n - 1` stays `dec di`
(`04_funcs`), now as "`+ -1`". The 62 corpus files never subtract another
constant; the fuzzer's programs do (see Verification).

**6. The register an int is converted in.** `itof` takes the int in AX. For
an int variable the real code goes through DI - `mov di,i` / `mov ax,di` -
when the conversion is evaluated first, after a variable or constant was
loaded, or after a computed `+`/`-` (`05_fltasop`'s `(t + fl) / c`); but
straight into AX after a computed floating `*`: `(fl * 2) - c` is `mov ax,c`
/ `call itof`, and `10*fl + (c-'0')` computes `c - '0'` in AX (`mov ax,c` /
`add ax,*-48.`). v7's `c1` hands registers out by number and its `oddreg()`
gives a product the odd register - evidently a floating one too in the
MUTOS compiler - and the next operand's register follows. A product itself
is in AX already (`mov ax,i` / `imul j` / `call itof`), a char loads into AX
(`movb ax,c` / `cbw`), and a register variable in DI goes through SI (`mov
si,di` / `mov ax,si`, `p5_call`). After any other computed operand the
register is not known (refused; `fltprobe/p7_itofreg.c` asks).

**7. Smaller shapes.** A double argument is `sub sp,*8` - no decimal point,
unlike every other constant (the caller's `add sp,*8.` has one), and
`lconvert.o`'s `ltof` is called with the long pushed low word first and
popped with `add sp,*4`. A call's unused double result is left in `fac` (no
`fldd`); a function returning a float returns through `fac` like a double
(`|RTYP 2`) and its caller loads it with `fldd`. A comparison used as a value
is v7's usual 0/1 (`blt L10007` / `mov di,*0.` / `jmp L10008` / `L10007:mov
di,*1.`), and `r + (d < e)` then adds `r` to it (`add di,*-28.(bp)`: the
comparison has the higher degree). `+=`, `-=`, `/=` load the target and
combine the right operand (a computed one: `fldd d` / `fldd e` / `faddd e` /
`fdiv`), `*=` loads the right operand and multiplies the target in from
memory (`itof` / `fmuld d`), a used value is `fstd` (no pop); `i *= e` into
an int is `call ftoi` / `mov ax,ax` / `imul i` / `mov i,ax` - `mutos_c0`
already converts `e` first (`FTOI`, then `ASTIMES(INT)`), v7's own semantics:
`i * (int)e`, not `(int)(i * e)`. In a function with a register variable in
DI, a constant argument goes through SI (`mov si,*3.` / `push si`) and a long
lives in SI(high):DX(low) - `l = 7` is `mov ax,*7.` / `cwd` / `mov si,dx` /
`mov dx,ax`, stored from DX and SI - the next register pair v7's allocation
takes. Floating data: `.comm _gd,8`, `.bss` / `_sd:.blkb 8.`, and for
`double gi = 2.5;` a `SYMDEF`, `.data` and `_gi:<TAB>.double<TAB>2.5...` (a tab
after the directive, unlike a code constant's space); an element or member is
addressed as one operand (`lea ax,16.+_ga`, `8.+_gs`, `*-12.(bp)`), a value
through a pointer as `mov di,p` / `lea ax,(di)` - loaded after the right-hand
side (`*p = 2.0` is `flds <2.0>` / `mov di,*-22.(bp)` / `lea ax,(di)` / `fstdp`),
and `gp = &gd` is `mov _gp,#_gd`.

**8. The text of a constant that is not exactly a float** (`p4_const`): an
8-byte `.double`, loaded with `fldd` - `0.1` is `.double
1.00000000000000000e-01` (glibc's `%.17e` would give `...06e-01`),
`3.14159265358979` is `3.14159265358979001e+00`, `1e30`
`1.00000000000000005e+30`, `16777217.` `1.67772170000000000e+07`; `2**56`
(`72057594037927936.`), exactly a float, is a `.float`
`7.20575940379279360e+16`. The real `c1` is a MUTOS program linked with
`libc.a`: it converts with `libc.a`'s `atof()` and prints with `printf`,
whose `%e` is `fltpr.o`'s `_pscien()` - and its disassembly shows that
calling `_ecvt` (`libc.a`'s `ecvt.o`, v7's `cvt()` compiled; 18 digits for
`%.17e`), then writing the first digit, `.`, the rest, `e`, the sign and two
digits of `decpt - 1`. Running exactly that - `libc.a`'s `atof` then
`libc.a`'s `ecvt` on `libc.a`'s runtime, under the 8086 emulator that
`tests/mutos_as/float_coverage/libcatof.py` already had - reproduces all
seven texts. `mutos_c1` now reproduces them without the emulator:
`src/mutos_cc/c1_fltdec.c` takes the value from `mutos_as`'s own
`fltconst.c` (libc.a's `atof()` exactly, for every nonzero value - compiled
into `mutos_c1` as it is) and runs `cvt()` on exact 56-bit arithmetic.
`cvt()` only ever multiplies by 10.0, divides by 10 and adds its own `.03`
(`c3 f5 28 5c 8f c2 75 7b`), and 10.0's mantissa words below the top one are
zero, so `dmul`'s wrong partial product never contributes: every operation
is the exact result rounded to nearest-even, `modf.o` splits a value
exactly (it shifts the integer bits out), `ftoi` truncates. Against the
emulated `libc.a` - the new `libcatof.py ecvt` subcommand, part of `make
check-libcatof` - 28,188 random C literals (fractions, exponents from e-37
to e+37, long integers, leading and trailing points): 0 differences in text
or size. The 4- or 8-byte choice is "the double's low four bytes are zero".
An 8-byte constant's degree (0, as a DOUBLE leaf?) has no golden yet: one
where it would decide an order - in a `+`/`*` chain or a comparison - is
refused (`fltprobe/p6_dblcon.c` asks); elsewhere (an assignment, an
argument, `d - 0.1`) it is compiled.

**How `mutos_c1` does it now.** Every expression with a floating node is
planned (see "Evaluation order": `plan_expression()` pre-scans the tree): a
new part of that planner, `plan_fvalue()`, rebuilds v7's order - `fdeg()`
(v7's `degree()` as above, with `optim()`'s rules for `-`, `/`, `*=`, `/=`,
relationals, conversions and calls), `plan_fchain()` (acommute()'s
`insert()` order and the rebuilt nodes' `.data` blocks, outermost first), a
relational's exchange, `SEG_FLOAD` (a leaf left operand loaded before a
computed right one), `SEG_FDATA` (a constant's block where `cexpr()` prints
it), `SEG_FSWAP` (the handler mirrors the relation), `SEG_FITOF` (point 6's
register), `SEG_FLEAF` (an element or member at a constant offset, `ffold()`
- `v7`'s `optim()` makes `*(&x + c)` one NAME, so its opcodes are never
streamed). `fplan_constants()` numbers the expression's constants when it is
planned, written ones first. The handlers then just generate each node with
its operands arriving in that order - `gen_fp_binop()` no longer guesses
anything. `&&`/`||`/`!` over floating comparisons go through the existing
`plan_cbranch()`. What is still refused is listed in `src/mutos_cc/README.md`'s
"Floating point" and `tests/mutos_cc/fltprobe/`'s round 2 (`p6`..`p9`) asks
about most of it.

**`mutos_c0`.** `+=`/`-=` into a float or double (like `*=`/`/=`: the right
operand converted to the target's type), `i *= e` into an int (`FTOI`, then
`ASTIMES(INT)`), an embedded compound assignment whose value is used (`e =
(d *= e)` - `parse_comma_item()`), a char to floating (`ITOC(INT)`, then
`ITOF`) and back (`FTOI`, then `ITOC(CHAR)`), functions returning a float,
floating struct members, local floating arrays and pointers, local `static
double`/`float`, file-scope floating variables, arrays, pointers and
initializers (`SYMDEF`, `DATA`, `NLABEL`, `FCON`, `INIT(type)`, `EXPR` - v7's
`cinit()`), file-scope struct variables of any kind (`mutos_c1` refuses an
int member's access as it refuses an int file-scope array's element), and
`(int) (<floating expression>)`. `dump_temp.py` decodes `INIT` and an empty
symbol name (v7's `SYMDEF("")` every 64 initializers - `11_kernel/08_tty`
now decodes up to a `GREATQP`). `11_kernel/03_mem`'s diagnostic moved from
line 617 to 610 (`'y' undeclared` - the K&R `register y,x;` with no type):
its file-scope struct is accepted now.

**Tooling.** `x86sim.py` runs `ltof` (the long from the machine stack),
`.double` constants and floating initializers, and `N.+_sym` addresses;
every round-1 golden now runs under it and returns its program's value
(`p2_arith` 16, v7's `i *= e`; the C value is 17). `libcatof.py` links
`ecvt.o` and `modf.o` too and gained the `ecvt` subcommand;
`src/mutos_cc/fltdec_test` feeds it.

**Verification.** `make test`: 62/62 corpus files and 9/9 `fltprobe` files
byte-exact (71 in `run_goldens.sh`'s list), 0 genuine mismatches, zero
warnings; `mutos_as`, `check_floatdat.sh`, `assemble_cc_goldens.sh` and
`mutos_cpp` unchanged. `fuzz_c.py` against the previous build (`3c801a0`):
30,000 programs (12,000 with arrays, seed 111; 12,000 `--scope`, seed 121;
6,000 scalars only, seed 131) - 0 WRONG, 0 BAD, 398 changed but still
correct (`x - c` is now `add ...,*-c.`, and `p[i - 1]` through a pointer
takes `p[i + 1]`'s path, the -2 a displacement: `mov di,*-2.(di)`), 1
formerly refused now correct; 3,000 more through
ASan/UBSan builds: 0 WRONG, 0 BAD, no reports. ASan/UBSan builds over all
71 golden inputs and the nine `11_kernel` refusals: clean. Seven more
hand-written floating programs (calls with three double arguments, a float
function, compound assignments, `&&`/`||`, loops with floating conditions,
globals, arrays, pointers, members, `long`/`char` conversions, inexact
constants) through `mutos_cpp`, both passes, `mutos_as` and `x86sim.py`,
compared with the host C compiler: all equal, but for `k *= a` where the
difference is v7's `i * (int)a`. `libcatof.py ecvt`: 28,188 literals, 0
differences; `libcatof.py goldens`, `check` and `ops` still pass with the two
extra objects linked in.

### The round-2 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)

The four round-2 programs (`p6_dblcon`, `p7_itofreg`, `p8_misc`,
`p9_init`) came back from real hardware in commit `4c668c2` - their `.s`,
`.1` and `.2`, **but not their `.i`** (`make goldens` packages whatever is
on disk; the `.i` files stayed on the MUTOS machine). `run_goldens.sh` keyed
on `.i.golden` and skipped all four without a word - `make test` would have
gone on reporting 71 files byte-exact while these four were never compared.
It now checks a file with a `.1.golden` but no `.i.golden` from `mutos_c0`
on (`mutos_cpp`'s own output feeding it, nothing to diff that against) and
lists a match apart, as category 7. `mutos_c0` matched `p6`'s and `p7`'s
`.1`/`.2` at once - so `mutos_cpp`'s `.i` and the real one tokenize alike -
and refused `p8` (`i += d`) and `p9` (its initializers); `mutos_c1` refused
`p6` and `p7`. All four are byte-exact now, the 71 others unchanged.

**1. An 8-byte constant has degree 0** (`p6_dblcon`). v7's `degree()` gives
a DOUBLE leaf 0, and a `.double` constant is one: `d + 0.1` and `0.1 + d`
both keep their order (equal degrees), `a * 0.1` and `0.1 * a` both load
the float `a` first, `d < 0.1` is exchanged (equal degrees, only `d` a
NAME: `fldd <0.1>` / `fldd d` / `fcmp` / `ble`), `0.1 < d` is not, `a >
0.1` is not and `0.1 > a` is (`a` has 1). So only a constant that is
exactly a float has degree 1 - it is kept as a FLOAT `.float` (round 1's
finding); the rule is simply `degree()` on the constant's real type.
`fdeg()` no longer refuses one.

**2. The register an int is converted in** (`p7_itofreg`). After the
operand computed before it (each statement `f + (<computed> op <int>)`):

| Computed first | Register | Golden |
|---|---|---|
| `d / e` | AX | `mov ax,*-30.(bp)` / `call itof` |
| `tw(d)` (a call, its result loaded with `fldd`) | AX | the same |
| `-d` | DI | `mov di,*-30.(bp)` / `mov ax,di` / `call itof` |
| `d * e`, int `i + 1` | AX | `mov ax,*-30.(bp)` / `inc ax` |
| `d * e`, int `j - 1` | AX | `mov ax,*-32.(bp)` / `dec ax` |
| `d * e`, int `i + j` | AX | `mov ax,*-30.(bp)` / `add ax,*-32.(bp)` |
| nothing (a chain's first term), int `i + j` | DI | `mov di,i` / `add di,j` / `mov ax,di` |
| nothing, int `i * 3` | AX | `mov ax,i` / `mov cx,*3.` / `imul cx` |

With round 1 (DI after a leaf and after a computed `+`/`-`, AX after a
computed `*`) the reading that fits: the next operand takes the register
the previous one's result was given, and the MUTOS compiler gives a
product, a quotient and a call's result AX - the register the 8086's
`imul`/`idiv` and a function's return use; an addition, subtraction or
negation stays in the register it was computed in. The operands of a
floating `*` itself are still in DI (`p2_arith`'s `d * i`: `mov di,i` /
`mov ax,di`), so this is the result's register, not v7's `oddreg()` applied
to the node. `fafter()` gives AX after `*`, `/` and a call and keeps the
context after `+`, `-` and a negation; in DI any int computed the ordinary
way is then moved, `mov ax,di` (`gen_itof()`); in AX, `x + 1` / `x - 1` are
`inc ax` / `dec ax` and a sum of two variables `mov ax,i` / `add ax,j` (the
`OP_PLUS` handler, for an `ITOF` consumer). After any other computed
operand - a conversion, an element, an assignment - the register is still
not known, and refused.

**3. `p8_misc`.** The `long` converted with DI free goes through DI, as
inferred (`mov di,<high>` / `push di` / `mov di,<low>` / `push di` / `call
ltof` / `add sp,*4`). `*p + 1.5` loads `*p` first: v7's `unoptim()` gives a
STAR `max(islong(type), degree(operand))` = 1, the constant's degree, so
the operands stay as written (`fdeg()` computes it now; ITOP as optim()'s
TIMES). `i += d` and `i -= d` into an int are, like round 1's `i *= e`,
the right operand converted first (`.1`: NAME i, NAME d, `FTOI(INT)`,
`ASPLUS(INT)`/`ASMINUS(INT)` - v7's `build()` converts an assignment
operator's right operand to the left's type, so the value is `i + (int)d`)
and then a single in-place instruction, `call ftoi` / `add *-44.(bp),ax`
(`sub` for `-=`). A double array subscripted by a variable is addressed as
an int array is, with the constant-shift rule for the element size 8:
`lea di,*-38.(bp)` / `mov si,*-44.(bp)` / `mov cx,*3.` / `sal si,cl` / `add
di,si`, then used through DI, `lea ax,(di)` / `call fldd`; as an
assignment's target (`arr[i] = d`) the address is computed after the
right-hand side is loaded (`fldd d` first), as a pointer variable is loaded
only after it (`*p = 2.5`). `return i + (int) d` is `call ftoi` / `add
ax,*-44.(bp)`.

**4. `p9_init`: v7's `doinit()`.** The real `c1` writes an initializer as
v7's `doinit()` does, after `optim()`: the value as a double for a
`double` variable - `double gi = 2;` is `.double<TAB>2.00000000000000000e+00`
(a code constant 2.0 would be a `.float`), `double gx = 0.1;` `.double
1.00000000000000000e-01` (the MUTOS `ecvt()`'s text, as for a code
constant), `double gn = -1.5;` `.double -1.50000000000000000e+00` (`.1`:
`FCON 1.5`, `NEG(DOUBLE)`, `INIT` - v7's `c0` folds a negated int constant,
not a floating one; `unoptim()` folds it) - and for a `float` variable
`sfval = fval; printf(...)` of that: `float gy = 0.1;` is `.float
9.99999940395355225e-02`, the double **truncated** to a float, not rounded
(0.1 rounded would be `1.00000001490116119e-01`). The MUTOS `c1` is a
MUTOS program, so `sfval = fval` is `fldd` / `fstsp` on `libc.a`'s runtime;
run under the emulator (`libcatof.py`'s new `fecvt` subcommand), `fstsp`
stores the double's high four bytes for every one of 5,649 literals (and
3,000 random doubles, 1,429 of which rounding would change), and the text
`fdec_render_single()` writes for each matches `ecvt()` of the stored float
exactly. `static double gs = 2.5;` is `DATA` / `NLABEL _gs` (no `SYMDEF`, no
`.globl`) - `mutos_c0` already wrote that. Every initializer takes a `c1`
label though none is written - the int one too (`unoptim()`'s `ITOF(CON)`
fold, as in code): the first code constant after five initializers is
`L10005`. `mutos_c0` now accepts an int constant (`CON`, `ITOF(type)`), a
negated one (`CON -n`) and a negated floating literal (`FCON`, `NEG`);
`mutos_c1`'s new `gen_finit()` reads the whole initializer, runs before any
expression planning, and replaces the old `FCON INIT` special cases in
`gen_fcon()` and `plan_expression()`.

**5. What `mutos_c1` now compiles by inference** (no golden yet - round 3
asks): an element subscripted by a variable as the second operand after a
leaf (`1.5 + arr[i]`: the leaf loaded, then the address, `lea ax,(di)` /
`call faddd`) or after another element (`arr[i] < arr[j]`: the first
loaded before the second's address is computed - `plan_fload()`); any int
computed in DI converted (`d + (i << 2)`: `mov di,i` / `sal di,*1` / `sal
di,*1` / `mov ax,di`), a quotient converted from AX; an element of any
power-of-two size subscripted by a variable (`mov cx,*N.` / `sal si,cl`
from 8 bytes up, by the constant-shift rule - a struct array's too); `i +=
f()` / `i -= f()` (`add i,ax`, the call's result where `ftoi`'s is);
`float` initializers from an int constant (`ITOF(FLOAT)`), negated
constants, `static float`; a shift's degree (optim()'s "def:").
`tests/mutos_cc/fltprobe/`'s round 3 (`p10_elem`, `p11_itof2`, `p12_init2`)
asks about each, `p13_open` about what is still refused (a conversion after
a conversion, an element or an assignment; a difference or a shift for AX;
`x + 0` for AX; `i /= d`; a floating zero added or subtracted).

**Tooling.** `run_goldens.sh`'s category 7 (above); `libcatof.py fecvt`
and `fltdec_test -f` (`make check-libcatof`); `fltprobe/Makefile.mutos`'s
`round3`. `x86sim.py` needed nothing new: all four round-2 goldens run
under it and return their programs' values (9, 62, 6, 6).

**Verification.** `make test`: 62/62 corpus and 9/9 round-1 `fltprobe`
files byte-exact (category 1, 71), the four round-2 files byte-exact from
`mutos_c0` on (category 7), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 84/84 compiler goldens assemble, `mutos_cpp`
5/5. With a scratch `.i.golden` from `mutos_cpp` for each, all 75 are
byte-exact in category 1. Against the previous build (`4c668c2`) over all
84 inputs (the 71, the four round-2 files, the nine `11_kernel` refusals):
identical output and diagnostics but for the four. `fuzz_c.py` against
`4c668c2`: 30,000 programs (12,000 with arrays, seed 411; 12,000
`--scope`, seed 421; 6,000 scalars only, seed 431), all identical, 0 WRONG,
0 BAD; 3,000 more (seed 441, `--scope`) through ASan/UBSan builds: 0 WRONG,
0 BAD. ASan/UBSan builds over the 84 inputs: no reports, output identical
to the `-O2` build's. Seven hand-written floating programs (elements in
every position, struct arrays of 8 and 16 bytes, ints computed in DI and
AX converted, `i += d`, `i += f()`, every initializer form - the round-3
probes among them) through `mutos_cpp`, both passes, `mutos_as` and
`x86sim.py`, compared with the host C compiler: equal, but where v7's
semantics differ (`i -= d` is `i - (int)d`) or a float initializer is
truncated (`p12_init2`: 6, the host's 7); ten refusal probes each stop with
their diagnostic. `make check-libcatof`: `goldens` 34/38 identical (4 not
emulated), `check` 8,357 texts 0 differences, `ops` 0, `ecvt` 5,630
literals compared 0 differences, `fecvt` 5,649 compared 0 differences.

### The round-3 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)

The four round-3 programs (`p10_elem`, `p11_itof2`, `p12_init2`,
`p13_open`) came back from real hardware in commit `33ec944`, all four
kinds of file this time; round 2's four `.i` files followed in commit
`34c9195`. Those four `.i` goldens are byte-identical with `mutos_cpp`'s
output, so `p6`..`p9` moved from category 7 into category 1 with no
change, and category 7 is empty again. Of round 3, `p11` and `p12` matched
at once - every inference they asked about holds - `p10` differed in two
places of its `.s`, and `mutos_c0` refused `p13` (`j /= e`). All four are
byte-exact now, the 75 others unchanged.

**1. An element of a local array has degree 2** (`p10_elem`'s `d = 1.5 +
arr[i]`). The golden computes the element's address, loads the element
and adds the constant from memory - `lea di,*-28.(bp)` / ... / `lea
ax,(di)` / `call fldd` / `lea ax,L10003` / `call fadds` - where `mutos_c1`
had loaded 1.5 first. By v7's own numbers the two would tie: `degree()`
gives an `AMPER` -2, `acommute()` puts `i*8` (degree 1, `TIMES` by a power
of two) ahead of `&arr` and gives the sum 1, the `STAR` `max(1, 1)` = 1,
the `.float` constant's degree - so `insert()` would keep the source order,
constant first. The element going first means its degree is at least 2.
The same golden's address code says why: `lea di,<arr>` comes BEFORE the
index is scaled, which `insert()` only does when the address term's degree
is not below the index's 1. One change fits both: a local's address is not
a link-time constant on the 8086 but a computed `lea`, and counts as
computed - degree 1; the two terms tie (the `lea` first, as written) and
their sum has degree 2. `fdeg()` now gives an `AMPER` of an `SC_AUTO` name
1; a static or file-scope object's address stays -2 (a constant: `_ga(si)`,
`16.+_ga`). The integer planner's `enode_degree()` already gave such an
element 2 (its "anything computed at least 1" clamp). The rest of `p10`'s
`main()` matched before and after - `d < arr[i]` exchanged (`ble`), `arr[i]
< arr[j]` and `arr[i] + arr[j]` the first element loaded before the
second's address, `fa[i] * arr[i]` as written.

**2. A dereferenced left operand of an int `*` is loaded first** (`p10`'s
`ps[i].c * qs[i].g`, arrays of 8- and 16-byte structs subscripted by a
variable): `lea di,*-20.(bp)` / `mov si,*4.(bp)` / `mov cx,*3.` / `sal
si,cl` / `add di,si` / `mov di,*4.(di)` / `lea si,*-52.(bp)` / `mov dx,*4.
(bp)` / `mov cx,*4.` / `sal dx,cl` / `add si,dx` / `mov ax,di` / `imul
*12.(si)`. v7's `F` then `S1`, as for `+`/`-` (`02_bubsort`, `05_nestst`):
`load_now()` now includes `*`, and the multiply's ordinary `mov ax,<left>`
moves the loaded value. `mutos_c1` had left the element as `*4.(di)` and
loaded it into AX at the end. This is also the first golden of a product
of two 1-D elements: it is NOT spilled - the right operand fits in the
registers left over - so the "Evaluation order" sections' guess that the
real compiler "very probably spills `a[i] * b[j]`" like `05_matmul`'s 2-D
elements is wrong for these. (`a[i] * b[j]` of int arrays itself was no
longer refused - it compiled with the late load; `p14_axint` asks.)

**3. The register an int is converted in, completed** (`p13_open`):

| Computed first | Register | Golden |
|---|---|---|
| `(double) i` - a conversion, through DI | DI | `mov di,*-40.(bp)` / `mov ax,di` / `call itof` / `call fadd` |
| `arr[i]` - an element, its address in DI | DI | the same |
| `d * e`, int `i - j` | AX | `mov ax,*-38.(bp)` / `sub ax,*-40.(bp)` / `call itof` |
| `d / e`, int `i << 1` | AX | `mov ax,*-38.(bp)` / `sal ax,*1` / `call itof` |
| `d * e`, int `j + 0` | AX | `mov ax,*-40.(bp)` / `call itof` - no `add` |

The reading from round 2 holds: the next operand takes the register the
previous one's result was given - an int converted through DI leaves DI,
and so does an element whose address was computed there. `fafter()` keeps
the context after an `ITOF`, and after an element when the context is DI
(after a `*`, `/` or call no golden shows an element, and its address needs
a base register anyway - still refused there). In AX the `OP_PLUS` handler
forms a difference as it forms a sum (`mov ax,i` / `sub ax,j`), and
`OP_LSHIFT`/`OP_RSHIFT` shift a variable there by a constant - `sal ax,*1`;
a count of 3 or more by CL and a right shift are inferred from DI's rule
(`emit_const_shift()`; `p15_fltinf` asks).

**4. An int `+ 0` is dropped.** `(j + 0)` converted is just `mov ax,j`:
v7's `acommute()` "toss[es] out +0" (`c12.c`, for `PLUS`/`OR` whose last
term is a constant 0 - `isconstant()`, an int `CON`) before any code is
chosen. `mutos_c1` refused it converted in AX and everywhere else emitted
`add di,*0.`, a shape no golden had; now the `OP_PLUS` handler pushes the
other operand unchanged for any int `x + 0` - and `x - 0`, which v7's
`optim()` makes `x + -0` first - so `s = j + 0` is `mov di,j` / `mov s,di`
(`p14_axint` asks).

**5. `j /= e` into an int** (`p13`): `.1` - NAME j, NAME e, `FTOI(INT)`,
`ASDIV(INT)`, as `i *= e` and `i += d` (v7's `build()` converts the right
operand to the target's type first, so the value is `j / (int)e` = 3 where
C gives 2 - the golden returns 42, the host 41); `.s` - `lea ax,*-20.(bp)` /
`call fldd` / `call ftoi` / `mov cx,ax` / `mov ax,*-40.(bp)` / `cwd` / `idiv
cx` / `mov *-40.(bp),ax`: the converted divisor moved to CX out of the way
of the dividend. `mutos_c0` accepts `/=` into an int; `mutos_c1`'s
`ASDIV`/`ASMOD` take a right operand in AX that way - a call's result
(`x /= f()`, `y %= f()`) by inference (`p14_axint` asks).

**6. A floating zero is an ordinary constant** (`p13`): `d + 0.0` is
`.data` / `L10004:<TAB>.float 0.00000000000000000e+00` / `.text` / `lea
ax,L10004` / `call flds` / `lea ax,*-12.(bp)` / `call faddd` - kept (the
toss only sees an int `CON`), and loaded first (degree 1, `d` 0); `d - 0.0`
is `fldd d` / `lea ax,L10005` / `call fsubs`. Both refusals are gone; a
negated zero is still refused (`p16_open2` asks).

**7. A value in AX plus a constant stays in AX** (`p13`'s `return r + (int)
d - 30`): `call ftoi` / `add ax,*-42.(bp)` / `add ax,*-30.` (`optim()`'s
`- 30` as `+ -30`), no `mov ax,di` - the sum never left AX. v7's templates
compute into the register their left operand's code returned, and the MUTOS
`c1` returns AX for `ftoi`, `imul`, `idiv` and a call;
`kernel_nonopt/amx.s` shows the same for a product (`imul cx` / `add ax,di`
/ `add ax,*22.`). The `OP_PLUS` handler's AX branch now takes a constant
too, `+ 1` / `- 1` as `inc ax` / `dec ax` (as in DI, and as `p7_itofreg`'s
int converted in AX). By inference: that value stored to memory (`mov
s,ax`) or to a register variable (`mov di,ax`, where `mutos_c1` had `mov
di,ax` / `add di,*3.`); `kernel_opt/clock.s`'s `idiv cx` / `add ax,bx` /
`mov si,ax` / `add si,*-20.` is optimized code from a source not in this
repository, so it settles neither (`p14_axint` asks).

**8. Confirmed as inferred** (`p11`, `p12`, the rest of `p10`): ints
computed in DI converted (a shift, `i - 6` as `add di,*-6.`, `0 - i` as
`mov di,*0.` / `sub di,i`), a quotient
converted from AX (`idiv *-24.(bp)` / `call itof`), a product by a constant
after a `/` (`mov ax,i` / `mov cx,*3.` / `imul cx`), `i += f()` / `i -=
f()` (`call _seven` / `add *-22.(bp),ax`); an int constant into a float
(`.float<TAB>4.0...`), a negated int into a double, `-0.1` into a float
truncated (`-9.99999940395355225e-02`), a `static float`, `0.3` truncated
(`2.99999982118606567e-01`) - `p12`'s golden returns 6, the host 7;
elements of 8- and 16-byte structs subscripted by a variable (`mov cx,*3.`
/ `sal si,cl`, `mov cx,*4.`), an element as a second operand and next to
another.

**Round 4** (`tests/mutos_cc/fltprobe/`, `make -f Makefile.mutos round4`)
asks about what this round's changes infer and what is still refused:
`p14_axint` (no floating point - a value in AX plus a constant stored,
`f() + 1`, `x * y - 1`, `j + 0` / `j - 0` in DI, `a[i] * b[j]` and `a[i] &
b[j]` of int arrays, `x /= f()`, `y %= f()`), `p15_fltinf` (`(d * d) +
arr[i] + i` - the element first by its degree, i through DI; `i << 3` and
`j >> 1` converted in AX; a call's result then an element) and `p16_open2`
(refused: an int converted after `*p` and after an assignment, a remainder
converted, `-0.0`, `(int) 2.5`, `i = 2.5`, `d += c` with a char, an
`unsigned` converted - none is known to stop the real compiler, so they
share one file; a floating truth test, which does, stays out).

**Tooling.** `x86sim.py` needed nothing new: all seventeen goldens run
under it and return their programs' values (`p10` 21, `p11` 109, `p12` 6,
`p13` 42). `fltprobe/Makefile.mutos`'s `round4`.

**Verification.** `make test`: 79 files byte-exact at all four stages
(62/62 corpus, 17/17 `fltprobe`), category 7 empty, 0 mismatches, zero
warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13, 88/88 compiler
goldens assemble, `mutos_cpp` 5/5. Against the previous build (`33ec944`)
over all 88 inputs (the 79, the nine `11_kernel` refusals): identical
output and diagnostics but for `p10` and `p13`. `fuzz_c.py` against
`33ec944`: 8,000 programs (3,000 seed 1; 3,000 seed 7 `--scope`; 2,000
seed 3 scalars only), 0 WRONG, 0 BAD, 37 changed and still correct (the
dropped `+ 0` and a value kept in AX), the rest identical; 500 more (seed
11) through ASan/UBSan builds: 0 WRONG, 0 BAD. ASan/UBSan builds over the
88 inputs: no reports, the 79 byte-exact. Round 4 through `mutos_cpp`, both
passes, `mutos_as` and `x86sim.py`: `p14_axint` 129 and `p15_fltinf` 45,
the host C compiler's values; `p16_open2` stops with four `mutos_c0`
diagnostics.

### The round-4 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)

The three round-4 programs (`p14_axint`, `p15_fltinf`, `p16_open2`) came
back from real hardware in commit `27d0cea`, all four kinds of file. Every
`.1`/`.2` of `p14` and `p15` matched at once; their `.s` differed in two
places and one, and `mutos_c0` refused `p16` (an `unsigned` local, a cast
of a constant, `d += c`). All three are byte-exact now, the 79 others
unchanged.

**1. A right element at offset 0 is computed first** (`p14_axint`). `s =
a[i] * b[j]` (int arrays, both subscripted by a variable): `lea di,*-16.
(bp)` / `mov si,*-20.(bp)` / `sal si,*1` / `add di,si` / `mov di,(di)` /
`push di` - `b[j]`'s value onto the machine stack - then `a[i]` the same
way into DI, `mov ax,di` / `pop cx` / `imul cx`: v7's `%n,n` (SS, F), as
`05_matmul`'s 2-D elements, NOT round 3's `ps[i].c * qs[i].g` (`p10`: the
left loaded, the right's address into SI, `imul *12.(si)` - `%n,ew*`). And
`s = a[i] & b[j]`: `b[j]`'s ADDRESS pushed (`push di`), `a[i]` computed and
loaded, `pop bx` / `and di,(bx)` - v7's `%n,nw*` (SS*, F, `I *(sp)+,R`),
which cr40 shares between `+`, `-`, `|` and `&` (`ANDN` on the PDP-11).

What separates the two kinds: every right operand computed after the left
in a golden has a constant offset (`p10`'s `qs[i].g` - 12, `02_bubsort`'s
`a[j + 1]` - 2, `03_starray`'s `pts[i].y` - 2, `05_nestst`'s `*(rp + 2)`),
every one computed first has none (`p14`'s `b[j]`, `05_matmul`'s
`b[k][j]`). By v7's `degree()` both kinds have the same degree - the
constant term never raises it, and `dcalc()` compares only the degree with
the registers left - so the mechanism is not understood. A context rule
also fits `*` (`p10`'s product is returned, `p14`'s assigned; `oddreg()`
makes `*` cost one register more than `+`), but not `&` beside
`03_starray`'s `+` (both assigned, both of degree 2, only the offset
differs). `mutos_c1` follows the offset: `is_elem_read()` (a word read
through an address whose index reads a variable, with `zero_off` one at
offset 0 - `a[j + 1]`'s `+ 1` counts as an offset, v7's `insert()` makes it
a constant term), `order_right_first()` now `*` of two such reads with the
right one at offset 0 (the 2-D special case it replaces is one of them),
and `is_pushaddr()` / `ORD_PUSHADDR` for `+`, `-`, `&`, `|`: the right
element, `SEG_ADDRPUSH` (`push <its register>`), the left, `SEG_DEFPOP`
(the `x - *p` step: load the left, `pop bx`, `(bx)`), the operator; a `+`
chain's first link the same way. `fltprobe/p17_elem2` asks `return a[i] *
b[j]` (offset rule: pushed; context rule: not) and `s = ps[i].c * qs[i].d`
(the reverse), and the inferred `+`, `-`, `|`.

**2. The context in a chain is the previous term's** (`p15_fltinf`'s `(d *
d) + arr[i] + i`): the element (degree 2) first, `d * d` onto the stack,
`fadd`, then `i` straight into AX (`mov ax,*-46.(bp)` / `call itof`) - as
after any computed `*`. `plan_fchain()` had given every term after the
second the chain link's context (`+`: unchanged, DI); it now passes each
term the context the term before it leaves. The rest of `p15` matched: the
element first by its degree (round 3's rule), `i << 3` in AX (`mov cx,*3.`
/ `sal ax,cl`), `j >> 1` (`sar ax,*1`), a call's result then an element.

**3. `p16_open2`, every refusal settled:**

| Source | Golden | Now in `mutos_c0`/`mutos_c1` |
|---|---|---|
| `d = *p + i` | `mov di,p` / `lea ax,(di)` / `call fldd` / `mov di,i` / `mov ax,di` / `itof` / `fadd` | `*p` loaded before the right operand's code (`plan_fload()`), DI after it (`fafter()`) |
| `d = (e = d * 2) + i` | `flds 2.0` / `fmuld d` / `lea ax,e` / `fstd` / `mov ax,i` / `itof` / `fadd` | after an assignment, its right-hand side's context (AX after the `*`) |
| `d = d * (i % j)` | `mov ax,i` / `cwd` / `idiv j` / `mov ax,dx` / `itof` / `fmuld d` | a remainder in DX converted with `mov ax,dx` |
| `e = -0.0` | `.float 0.00000000000000000e+00` | a negated zero written as the zero (the real `printf` writes no sign) |
| `(int) 2.5`, `i = 2.5` | `.float 2.5...` / `flds` / `ftoi` | not folded: `mutos_c0` writes FCON, FTOI(0) for the cast, `mutos_c1` converts at run time |
| `d += c` (char) | `movb ax,c` / `cbw` / `itof` / `lea ax,d` / `faddd` / `fstdp d` | `+=` with a computed right operand computes it first and adds the target from memory (v7's efftab `%a,n`) |
| `unsigned u; d = u` | `mov si,u` / `sub di,di` / `push si` / `push di` / `call ltof` / `add sp,*4` | ITOF of an unsigned is v7's LTOF(ITOL): high word cleared, the long pushed as a long variable is |

`.1`: `unsigned u;` is NAME typed `UNSIGN` (7), `u = 5` an `ASSIGN(7)` of
`CON 5`; `d += c` is NAME c, `ITOC(0)`, `ITOF`, `ASPLUS(3)`; `d = u` NAME u,
`ITOF(3)`. `mutos_c0` accepts a plain `unsigned` local (not `register`,
not a pointer or array), a floating constant cast to an integer, and a
char right operand of a floating compound assignment. `-=` with a computed
right operand is now refused: by v7's table `=-` computes it first too,
which leaves `target - value` to a reversed subtraction (singles.o has
`fsubrs`), and no golden shows which. `p18_fltop3` and `p19_open3` ask.

**4. Bugs found testing `unsigned` locals** (no golden involved - each was
silently wrong code, now a refusal or the conversion v7 writes):

- **A `long` combined with an int-class variable.** `mutos_c0` typed `l +
  i` `PLUS(LONG)` but wrote no `ITOL` for `i`, and `mutos_c1` read `i` as
  the first word of a long - `add si,*-4.(bp)` / `adc di,*-6.(bp)`, two
  words at `i`'s address. The same for `-`, `*`, `/`, `%`, the comparisons
  (`l > i`), and `&`/`|`/`^` (typed `INT` with a long operand). `x = l`
  stored `l`'s HIGH word, `x = 40000` an unrendered long constant. Now:
  an int or unsigned target of a long variable gets `LTOI` (as `(int) l`
  writes it - `08_castsize.1.golden` - and v7's `build()` converts); a
  long constant into a word, an int-class variable with a long in an
  arithmetic operator or a comparison, `&`/`|`/`^` with any long, a
  compound assignment of a long into a word, and an unsigned into a long
  (which `mutos_c1`'s `ITOL` would sign-extend) are refused.
  `fltprobe/p19_open3` asks for the real shapes.
- **Octal and hex constants.** `mutos_c0` made every constant above 32767
  long; v7's `getnum()` does so only for a decimal one - an octal or hex
  constant is long above 0177777 (`(lcval>>1)>MAXINT`), K&R sect. 2.4.1.
  `ip->i_mode = 0100000` and `ip->i_mode&0170000` in the `11_kernel`
  sources are ints: their goldens have `CON -32768` and `CON -4096`. The
  lexer now records the base (`Token.is_octhex`).

**Round 5** (`make -f Makefile.mutos round5`): `p17_elem2` (no floating
point: the offset-or-context question, `+`, `-`, `|`, `^` of two elements,
a `+` chain, a comparison of two elements), `p18_fltop3` (`d += e * 2`, `f
+= i` into a float, a remainder converted after a `*`, `(long) 3.75`, a
negated zero as an initializer) and `p19_open3` (refused: the `long`
mixing above, `u = 40000`, an unsigned compared, `a[i] * b[j - 2]`, `x -
b[j]`, `d -= c`, `d -= e * 2`, an unsigned converted after a `*`).

**Tooling.** `x86sim.py` needed nothing new: all twenty goldens run and
return their programs' values (`p14` 129, `p15` 45, `p16` 79).
`fltprobe/Makefile.mutos`'s `round5`.

**Verification.** `make test`: 82 files byte-exact at all four stages
(62/62 corpus, 20/20 `fltprobe`), 0 mismatches, zero warnings; `mutos_as`
76/76, `check_floatdat.sh` 13/13, 91/91 compiler goldens assemble,
`mutos_cpp` 5/5. Against the previous build (`27d0cea`) over all 94
inputs (the 82, the nine `11_kernel` refusals and round 5): identical
output but for the round-4 and round-5 files and, in four kernel files, the
new refusals (`long` `&` in `03_mem`) and the octal constants' `.1` (`04_pipe`,
`08_tty`), a diagnostic gone where an `unsigned` local is now accepted
(`07_v24`) and one replaced (`09_amx`'s `register unsigned`); every
kernel file still refuses. `fuzz_c.py` against `27d0cea`: 14,000 programs
(seeds 1, 7 `--scope`, 3 scalars only, 21), 0 WRONG, 0 BAD, 53 changed and
still correct, 353 now correct that the old build refused (the pushed
element and the spilled product free the registers the occupancy guard
had refused on); 1,000 more (seed 31) through ASan/UBSan builds: 0 WRONG,
0 BAD. ASan/UBSan builds over the 94 inputs: no reports. Round 5 through
the whole chain: `p17_elem2` 84 and `p18_fltop3` 27, the host C
compiler's values; `p19_open3` stops with its diagnostics. Hand-written
`unsigned` programs (assignment, `+`, `*`, `&`, `==`, a call argument, to
char, to double, from int) give the host's values; the rest refuse.

### The round-5 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-03)

The three round-5 programs (`p17_elem2`, `p18_fltop3`, `p19_open3`) came
back from real hardware in commit `434c320`, all four kinds of file.
`p18` matched at every stage at once; `p17`'s `.1`/`.2` matched and its
`.s` differed in two places; `mutos_c0` refused `p19`. Now `p17` and `p18`
are byte-exact, and `p19` at `.i`/`.1`/`.2` and in its `.s` up to line
223 - after which the golden is not valid assembly (item 5). The 82 others
are unchanged.

**1. The offset decides, not the context** (`p17_elem2`). Round 4 left
open whether a right element at offset 0 is computed first because of its
offset or because of the context (`p10`'s product was returned, `p14`'s
assigned). `return a[i] * b[j]` (`f1`) pushes `b[j]` (`mov di,j` / `sal
di,*1` / `add di,b` / `mov di,(di)` / `push di` / ... / `mov ax,di` / `pop
cx` / `imul cx`), and `s = ps[i].c * qs[i].d` (`f2`) does not (`mov
di,*4.(di)` / `lea si,qs` / `mov dx,i` / ... / `mov ax,di` / `imul *6.(si)`):
the offset. Why v7's `c1` treats the two differently is still not
understood - both have the same `degree()`; in cr40/cctab order a right
operand matching `%n,ew*` is taken first, and one at offset 0 evidently is
not `ew*` on this port. `+`, `-`, `|` and a `+` chain's first link pushed
`b[j]`'s address exactly as inferred.

**2. `^` and a comparison** (`p17_elem2`): `a[i] ^ b[j]` pushes the
address as `&` does (`pop bx` / `xor di,(bx)`) - the PDP-11 rule that
`^`'s right operand be in a register (`acommute()`'s "rt. op of ^ must be
in a register", which inserts a LOAD) has no effect here. `if (a[i] >
b[j])` pushes the LEFT element's address, computes and loads the right
one, and compares through BX in the order written:

    lea di,*-10.(bp) / mov si,i / sal si,*1 / add di,si / push di
    lea di,*-16.(bp) / mov si,j / sal si,*1 / add di,si / mov di,(di)
    pop bx / cmp (bx),di / ble L10

This is cctab's `%nw*,nw*` ([move11]: FS*, S*, compare through both) -
`02_bubsort`'s `a[j] > a[j + 1]` (right one with an offset) matches the
earlier `%n,ew*` instead (`cmp di,*2.(si)`). `mutos_c1`: `is_pushleft()`
(a relational of two element reads at offset 0), `ORD_PUSHLEFT` -
`SEG_KEEPIND` (the left element stays "(di)" although `load_now()` would
load a comparison's left operand), `SEG_ADDRPUSH`, the right operand,
`SEG_DEFPOPL` (it loaded, `pop bx`, the left "(bx)" marked `memleft`), and
`emit_cmp_and_branch()` comparing `(bx)` with the register as is. A
comparison with one element at an offset keeps the old shape (no
golden - `p23_elem3` asks).

**3. `p18_fltop3`, all as inferred**: `d += e * 2` (`flds 2.0` / `fmuld
e` / `lea ax,d` / `faddd` / `fstdp d` - the right operand first), `f += i`
into a float (`mov di,i` / `mov ax,di` / `itof` / `fadds f` / `fstsp f`),
`d = (d * e) + (i % j)` (the remainder first, by its degree: `mov ax,i` /
`cwd` / `idiv j` / `mov ax,dx` / `itof`, then `fldd d` / `fmuld e` /
`fadd`), `(long) 3.75` (`flds` / `ftol` / `mov di,dx` / `mov si,ax`),
`double gz = -0.0` (`_gz: .double 0.0...`).

**4. `p19_open3`** - every refusal settled but one:

| Source | Golden | Now in `mutos_c0`/`mutos_c1` |
|---|---|---|
| `l + i`, `i + l` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `add si,l+2` / `adc di,l` (both) | `.1`: NAME l, NAME i, ITOL, PLUS(6) - and NAME i, ITOL, NAME l, PLUS; `c1`: the widened int first (acommute(): ITOL of a variable has degree 2, a NAME 0) |
| `r + (int) (l - 100000)` | `mov di,l+2` / `add di,r` / `add di,#31072.` | `.1`: NAME l, LCON, MINUS(6), LTOI(0); `c1`: `VK_LOWADD` - v7's `unoptim()` distributes the LTOI (low word of l, low word of the LCON); the low word first, the variable, the constant last |
| `if (l > i)` | `mov ax,i` / `cwd` / `cmp dx,l` / `bgt L4` / `blt L10000` / `cmp ax,l+2` / `bhis L4` / `L10000:` | v7's optim() swaps (ITOL degree 2 > NAME 0): `i < l`, branch when false: `longrel()`'s `lrtab[0]` for GREATEQ |
| `if (l == i)` | ... `cmp dx,l` / `bne L5` / `cmp ax,l+2` / `bne L5` | NEQUAL: `bne` both |
| `l += i` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `add l+2,si` / `adc l,di` | `.1`: NAME l, NAME i, ITOL, ASPLUS(6) |
| `l = u` | `mov si,u` / `sub di,di` | ITOL of an unsigned: the high word cleared (`itolu[]` - which ITOLs take an unsigned, from the pre-scan) |
| `l = m & 255`, `m \| 6` | `mov ax,#255.` / `cwd` / `push ax` / `push dx` / `mov si,m+2` / `mov di,m` / `pop bx` / `pop cx` / `and si,cx` / `and di,bx` | `.1`: CON 255, ITOL, AND(6) - the constant widened at run time, `c + 1L`'s shape (`gen_long_constop()`, "pop cx" with its space) |
| `u = 40000` | `mov u,#-25536.` | `.1`: LCON(0, -25536), LTOI(7) |
| `r + (u > 39999)` | `mov si,u` / `sub di,di` / `push si` / `push di` / `mov si,#-25537.` / `mov di,*0.` / `pop cx` / `pop bx` / `cmp di,cx` / `blt` / `bgt` / `cmp si,bx` / `blo` / ... 0/1 | `.1`: NAME u, ITOL, LCON, GREAT; swapped (ITOL of an unsigned variable: degree -1 < 0) to `39999 < u`, the widened u pushed, the constant loaded, u popped into CX:BX (cctab's `%nl,nl`: SS, F); a value, branching when true |
| `a[i] * b[j - 2]` | `mov di,(di)` / `lea si,b` / `mov dx,j` / `sal dx,*1` / `add si,dx` / `mov ax,di` / `imul *-4.(si)` | the index through DX, added to the array's address, the constant term the displacement (not folded into the `lea`, as v7's acommute() would) |
| `x - b[j]` | `lea di,b` / ... / `push di` / `mov di,x` / `pop bx` / `sub di,(bx)` | `is_pushaddr()` for `-` with a variable on the left; `SEG_DEFPOP` loads it |
| `d -= c` | `lea ax,d` / `call fldd` / `movb ax,c` / `cbw` / `call itof` / `call fsub` / `lea ax,d` / `call fstdp` | the target loaded FIRST (as for `/=`), no reversed subtraction - `+=` alone computes its right operand first |
| `d -= e * 2` | `fldd d` / `flds 2.0` / `fmuld e` / `fsub` / `fstdp d` | the same |

`mutos_c0` writes the conversions where v7's `build()` does - right after
the int-class operand's own bytes (`mix_rhs_end()`/`emit_itol()`), a
constant too (`m & 255`'s CON, ITOL - v7's `c0` folds nothing for a long;
`c1`'s `lconst()` folds later); a char with a long is refused (no
golden). A parenthesized long cast to int (`(int) (l - 100000)`) is the
tree and LTOI. `mutos_c1`'s long comparison (`gen_long_relop()`/
`gen_long_cmp()`) is v7's `longrel()`/`xlongrel()` table for a comparison
not with an ITOL of 0 (`lrtab[0]`), which also reproduces `01_addsub`'s
`c > 0L`; the operand shapes are the three goldens' only.

**5. The real compiler's output is invalid for `d = (d * e) + u`.** After
`fldd d` / `fmuld e` the golden has

    mov  ax,*-36.(bp)
    mov  <118 bytes>,ax
    sub  ax,ax
    push <the same 118 bytes>
    push ax
    call ltof

where the 118 bytes are `08 08 08 08`, eighteen spaces, `08`, `10` x15,
`04` x10, `10` x7, `AAAAAA`, `01` x20, `10` x6, `BBBBBB`, `02` x20, `10`
x4, a space - libc's `_ctype_` character-class table (the entries from
`'\n'` up), read as a string where a register name should be. The intended
code is `p16_open2`'s `mov si,u` / `sub di,di` / `push si` / `push di` for
an unsigned made a long (v7's `unoptim()`: ITOF of an unsigned is
LTOF(ITOL)); in AX context (after a computed `*`, `/` or call) the code
table asks for the register pair starting at AX, and the register-name
table has no entry after AX. `mutos_as` stops at line 229 ("could not
classify operands"); whether the real `as` assembles it is not known
(asked in `STATUS.md`'s "Next up"; settled 2026-10-07: it does not, see
"The real assembler on `p19_open3.s`" below). There is nothing meaningful to
reproduce: `mutos_c1` refuses an unsigned converted after a computed
operand, with a diagnostic that says why, for good.

The golden itself stays as it came back (CLAUDE.md's Golden Master
Integrity). `tests/mutos_cc/invalid_goldens.txt` lists it with the
number of valid leading lines (223); `run_goldens.sh` checks a listed file
at `.i`/`.1`/`.2` as usual, then requires `mutos_c1` to refuse it and its
output up to the refusal to be exactly those lines - category 8 -, and
`tests/mutos_as/assemble_cc_goldens.sh` requires `mutos_as` to refuse it
(a listed file that assembles is a failure: the list is out of date).

**6. Silent miscompiles found while testing `long`** (no golden involved;
found by hand-written programs run through `x86sim.py` against the host
C compiler):

- two long variables compared (`l > m`, `l == m`) - `c0`'s
  `long_mix_refused()` only looked at mixed pairs, and `c1` compared the
  high words only (`mov di,m` / `cmp l,di`). `mutos_c1` now notes every
  comparison with a long operand in the pre-scan (`lrel[]` - a Val keeps
  no type) and sends it to `gen_long_relop()`, which refuses the shapes
  no golden shows;
- a long tested for truth (`if (l)`, `!l`, `l && i`, `l ? a : b`): the
  high word only (`cmp l,*0`); refused in the pre-scan (v7's `longrel()`
  would test both words with `tst`);
- `~l`, `l << n`, `l >> n` and `x ? l : m`: typed int by `mutos_c0`, one
  word each; refused in `mutos_c0`;
- `l = -65536;` stored 0: `mutos_c0` folded `-` of a long constant to a
  truncated int. v7's `fold()` folds CONs only, so its `c0` writes LCON,
  NEG(LONG); `mutos_c0` does the same now, and `mutos_c1` folds NEG and
  COMPL of an LCON (v7's `unoptim()`).

A safety net came with it: `put_insn_ex()` refuses any operand
`render_operand()` writes as a `<...>` placeholder (an unsupported value
reaching an instruction) - such a slip once reached the output unnoticed
(`cmp *-8.(bp),<unmaterialized-long-const>`, 2026-09-26).

**Round 6** (`make -f Makefile.mutos round6`): `p20_fltlv` (floating
lvalues beyond variables: struct members, pointers to double subscripted,
with an offset and incremented, compound assignments into elements,
`&d`, a function returning `double *`), `p21_fltexp` (chained assignment,
`++`/`--`, char operands, casts of computed ints, `-i`, a long sum
converted, `(unsigned) d`, `?:` and comma with doubles, an assignment as
an argument), `p22_long2` (the `long` shapes still refused or inferred)
and `p23_elem3` (one element with an offset compared, `x * b[j]`, a leaf
minus an element, `b[j + 1]` of a local array). All four refused today;
their results (128, 60, 83, 136) are the host's.

**Tooling.** `x86sim.py` executes `adc`/`sbb` (the carry of the `add`/
`sub` immediately before - anything else between is an error) and the
unsigned branches `blo`/`bhis`: 55 of the 62 corpus goldens run (`02_long/
01_addsub` 11009 and `03_retval` -31067 newly), 22 of the 23 `fltprobe`
goldens (`p17` 84, `p18` 27); `p19`'s first 223 lines completed with `r =
r + (int) d` return 104, the host's value for that program.

**Verification.** `make test`: 84 files byte-exact at all four stages
(62/62 corpus, 22 `fltprobe`), `p19_open3` in category 8, 0 mismatches,
zero warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13, 93/93
compiler goldens assemble and `p19_open3`'s is refused as listed,
`mutos_cpp` 5/5. Against the previous build (`434c320`) over all 98
inputs: identical output but for `p17`, `p19`, the round-6 files'
diagnostics and three kernel files (`03_mem`'s two long `&` converted now
- its partial `.1` changes -, the cast diagnostic reworded in `07_v24` and
`09_amx`); every kernel file still refuses. `fuzz_c.py` against `434c320`:
14,000 programs (seeds 1, 7 `--scope`, 3 scalars only, 21), 0 WRONG, 0
BAD, 69 changed and still correct, 632 now correct that the old build
refused; 1,000 more (seed 31) through ASan/UBSan builds: 0 WRONG, 0 BAD.
ASan/UBSan builds over the 98 inputs: no reports. 79 hand-written
programs (long mixing, element comparisons, negated long constants,
floating shapes): every one compiled gives the host's value.

### The round-6 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-04)

The four round-6 programs (`p20_fltlv`, `p21_fltexp`, `p22_long2`,
`p23_elem3`) came back in commit `2540271`, all four kinds of file - and,
for `p21`, with two messages from the real compiler. All four had been
refused (`mutos_c0` refused `p20`/`p21`/`p22`, `mutos_c1` `p23`). Now all
four are byte-exact at every stage, `p21` together with the real
compiler's own two messages and its exit status; and `11_kernel/
01_delay`, the first of the nine kernel files, is byte-exact too. The 84
files byte-exact before are unchanged.

**1. `p21`'s messages: the real `c1` counts the floating-point stack.**
`cc -S p21_fltexp.c` printed

    56: floating point stack underflow
    57: Floating point stack underflow
    *** Error code 1

and wrote the whole `.s` all the same. Line 56 is `f = half(d = 3.0);` -
an assignment whose value is a floating call argument - and the golden's
code for it is wrong:

    lea ax,L10013 / call flds          3.0 onto the stack
    lea ax,*-12.(bp) / call fstdp      stored into d - and POPPED
    sub sp,*8 / mov ax,sp / call fstdp the argument: popped again
    call _half / add sp,*8. / call fldd / lea ax,*-28.(bp) / call fstdp

A statement-level store pops (`fstdp`), a store whose value is used keeps
it (`fstd` - `d = e = 2.5` is `fstd e` / `fstdp d` in the same golden);
for an assignment under a call the real compiler picks the popping one,
and the argument push then pops a value that is no longer there. At run
time the stack underflows (`x86sim.py` stops there: "'fstdp' with an
empty floating-point stack"; with line 250 made `fstd`, the program
returns 60, the host's value). At compile time the real `c1` noticed:
it keeps a model of the runtime stack's depth and reports a pop below the
bottom. "*** Error code 1" is `make`'s: `c1` exits with status 1 when it
reported errors (v7's `c11.c` `error()` counts them and goes on; `c10.c`
ends with `exit(nerror!=0)`), and `cc -S` keeps the `.s` it wrote, since
with `-S` that file is the output itself, not a temporary that `cc`'s
`dexit()` would remove. So the `.s` is a complete, valid golden - of code
that does not work.

The two messages differ in case, so they come from two places in the
real `c1`. The model `mutos_c1` now runs (`fp_track()`, called for every
`call` it emits) is the simplest one that gives exactly these two lines
and nothing else on any other floating golden:

- a load (`flds`/`fldd`, a call's result `fldd`), `itof`, `ltof` and
  `fdup` push one value;
- a store with a pop (`fstsp`/`fstdp`) pops one and reports "floating
  point stack underflow" (lower case) below 0 - line 56's store into
  `f`;
- `ftoi`/`ftol` pop one and report "Floating point stack underflow" (upper
  case) - line 57's `ftoi` (`r = r + f * 2.0 + d`), a balanced statement
  reporting because the model was still one short after line 56;
- the argument push (`sub sp,*8` / `mov ax,sp` / `fstdp`) pops one
  without a check: line 56 has one message, not two;
- `fadd`/`fsub`/`fmul`/`fdiv` pop one, `fcmp` two, checked with the
  upper-case message (an inference: round 1's `p1_compare` notes "two
  per comparison" for `if (d)`, which the real `cc` reported; no golden
  shows the case);
- the model starts at 0 in every function (an inference - `p21`'s
  underflow is in its last function) and is not reset inside one.

`mutos_c1` writes the same code, prints the same two lines (`c1_error()`:
"<line>: <text>", the line from the statement's EXPR), finishes the `.s`
and exits with status 1. `tests/mutos_cc/c1_errors.txt` lists the
expected messages; `run_goldens.sh` requires, for a listed file, a
nonzero exit status, exactly those messages and the golden `.s` - its new
category 9. A correct program never goes below 0, so `mutos_c1` reports
nothing for one; at the end of each statement it checks that the model is
back at 0 (or below 0 only after a reported underflow) and stops with an
internal error otherwise, so the model is checked against every floating
golden by `make test`. The same wrong code with the call's result unused
(`half(d = 3.0);` as a statement: one below the bottom and nothing
reported yet) is refused with a diagnostic, not an internal error - what
the real `c1` reports later is what `p28_fltstk` asks.

**2. Floating lvalues beyond variables** (`p20_fltlv`):

| Source | Golden |
|---|---|
| `q->x += 0.5`, `a[i] += 1.5` | `mov di,q` / `lea ax,(di)` / `\|` / `push ax` / `call fldd` / `lea ax,L10004` / `call fadds` / `pop ax` / `call fstdp` - the target's address kept on the machine stack, an empty `\|` comment line before it (v7's code table for a compound assignment through a register) |
| `a[i] *= s.y` | `lea ax,s.y` / `call fldd` - the right operand first - then the element's address, `lea ax,(di)` / `\|` / `push ax` / `call fmuld` / `pop ax` / `call fstdp` |
| `*p = *p * 2.0` | `mov di,p` / `lea ax,(di)` / `call fldd` / ... / `call fmuls` / `mov ax,p` / `mov bx,ax` / `lea ax,(bx)` / `call fstdp` - after a `*` the pointer goes through AX (the register an int is converted in after a `*` - `fafter()`) |
| `q->y = q->x + 1.5` | ... `call fadds` / `mov di,q` / `lea ax,*8.(di)` / `call fstdp` - DI after a `+` |
| `p++` (`double *p`) | `add *-48.(bp),*8.` - a statement-level `++` in place |
| `return a + i` (`double *pick()`) | `mov di,i` / `mov cx,*3.` / `sal di,cl` / `add di,a` / `mov ax,di` |
| `*pick(a, 2)` | `call _pick` / `add sp,*4.` / `mov bx,ax` / `lea ax,(bx)` / `call fldd` |
| `twice(&d)` | `lea di,d` / `push di` / `call _twice` |

`mutos_c0` accepts a pointer to a double (as a parameter, a local, a
function's return type - one more degree stays refused), floating
compound assignments to members and elements (`ASPLUS(3)` after `NAME q,
CON 0, PLUS, STAR(3)`) and a double stored through a pointer
(`ASSIGN(3)` - a float target written DOUBLE, as for a variable).
`mutos_c1`: `SEG_FSTREG` (the register a store through a pointer is
addressed in), `gen_fp_asop()`'s `FM_IND` path, a statement-level
`++`/`--` in place.

**3. Floating expression forms** (`p21_fltexp`, all as inferred but the
messages): `d = e = 2.5` (`fstd e` / `fstdp d`); `d++` and `++d` as
statements (`fldd d` / `lea ax,L10002` / `fadds` / `fstdp d` - the 1.0 a
`.float`, `mutos_c0`'s `ITOF` of `CON 1`); `e = d--` (`fldd d` / `call
fdup` / `fsubs` / `fstdp d` / `fstdp e` - the old value kept under the new
one); `d + c` (`movb ax,c` / `cbw` / `itof` / `faddd d` - the char
first), `c * 2.5`; `(double) (i + j)` (`mov di,i` / `add di,j` / `mov ax,di`
/ `itof`); `d = -i` (`mov di,i` / `neg di` / `mov ax,di` / `itof` - `NEG`
typed INT); `e = l + 1` (the long sum in DI:SI, then `push si` / `push di`
/ `call ltof` / `add sp,*4`); `u = (unsigned) d` (`fldd d` / `ftol` /
`mov u,ax` - v7's `unoptim()`: FTOI to an unsigned is LTOI(FTOL)); `x ? d
: e` (each arm `fldd`, `jmp` between - `plan_quest()`'s third kind of
arm); `g = (i = 2, d - 40000.0)` (a comma typed DOUBLE: the left operand
for its effect, the constant's `.data` block where its `-` is matched);
`r + u - 39990` (an unsigned widened, then only the low words:
`mov di,r` / `add di,u` / `add di,#25546.` - Val's `lowonly`).

**4. `long` shapes** (`p22_long2`, every refusal and inference settled):

| Source | Golden |
|---|---|
| `l - i` | `mov ax,i` / `cwd` / `push ax` / `push dx` / `mov si,l+2` / `mov di,l` / `pop bx` / `pop cx` / `sub si,cx` / `sbb di,bx` - v7's `%nl,nl`, the widened int pushed (`c + 1L`'s shape, "pop cx" with its space) |
| `i - l` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `sub si,l+2` / `sbb di,l` |
| `l + 1`, `l ^ 7` | `mov ax,*1.` / `cwd` / `push ax` / `push dx` / ... / `add si,cx` / `adc di,bx` - a constant that fits an int is ITOL(CON) (v7's `getree()`) |
| `l - 2` | `add si,*-2.` / `adc di,*-1.` - "x - c" is "x + -c" (`optim()`), and a negative constant folds back into an LCON (`unoptim()`) |
| `l -= i` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `sub l+2,si` / `sbb l,di` |
| `l *= i` | `mov ax,i` / `cwd` / `push ax` / `push dx` / `lea di,l` / `push di` / `call almul` / `add sp,*6.` - libc's in-place multiply |
| `l / i`, `l % i` | `mov ax,i` / `cwd` / `push ax` / `push dx` / `mov di,l+2` / `push di` / `mov di,l` / `push di` / `call ldiv` (`lrem`) / `add sp,*8.` |
| `x = 40000` | `mov x,#-25536.` |
| `l < 0L`, `l >= 0L`, `l > 2`, `l > 0` | `cmp l,*0` / `bgt`/`blt` / `cmp l+2,*0.` (or `*2.`) / `bhis`/`blo`/`blos` - v7's `lrtab[0]` for all four: no `tst` for a widened 0 |
| `x = l > 0L` | the same branches, `mov di,*0.` / `jmp` / `mov di,*1.` |
| `i < l` | `mov ax,i` / `cwd` / `cmp dx,l` / `bgt` / `blt` / `cmp ax,l+2` / `bhis` |
| `if (l)`, `if (!l)` | `mov si,l+2` / `mov di,l` / `cmp di,*0` / `bne` / `cmp si,*0` / `beq` (`!l`: `bne` both) - v7's `cbranch()` on a long |
| `u < l` | `mov si,u` / `sub di,di` / `push si` / `push di` / `mov si,l+2` / `mov di,l` / `pop cx` / `pop bx` / `cmp di,cx` ... - swapped to `l > u` |
| `l > m`, `l == m` | `mov si,l+2` / `mov di,l` / `cmp di,m` / ... / `cmp si,m+2` |
| `l = -100000` | `mov si,#31072.` / `mov di,*-2.` (LCON, NEG folded) |
| `~l` | `not di` / `not si` |
| `l << 2`, `l >> 1` | `sal si,*1` / `rcl di,*1` (twice); `sar di,*1` / `rcr si,*1` |
| `x ? l : m` | each arm into DI:SI |
| `x = (int) (l - 5)` | `mov di,l+2` / `add di,*-5.` / `mov x,di` |
| `(long) u`, `l & m` | `mov si,u` / `sub di,di`; `and si,m+2` / `and di,m` after loading l |

`mutos_c0` types `~l`, `l << n`, `l >> n` and a `?:` with long arms LONG
now (they were refused as silent int miscompiles in round 5), widens a
shift count (ITOL) and writes `(long) e` as ITOL. The expectation in
`p22`'s header that `l > 0` would be a `tst` (v7's `longrel()` for an
ITOL of 0) was wrong: the real compiler compares a widened constant like
any other.

**5. Element shapes** (`p23_elem3`): a left element with an offset
compared with one at offset 0 keeps the offset as the displacement off BX
(`pop bx` / `cmp *4.(bx),di`); `a[i] > ps[i].c` is cctab's `%n,ew*` (`cmp
di,*4.(si)`); `x * b[j]` loads the element into AX and multiplies by the
variable (`mov ax,(di)` / `imul x`); `x - *p` pushes the pointer (`push p` /
`mov di,x` / `pop bx` / `sub di,(bx)`), and so do `5 - b[j]` and `g - b[j]`
the element's address; `b[j + 1]` is `mov di,*2.(di)` after the index,
`a[i] + b[j + 1]` takes the index through DX (`lea si,b` / `mov dx,j` /
`sal dx,*1` / `add si,dx` / `add di,*2.(si)`); `(a[i] < b[j]) + (a[i] >
b[j]) * 2` computes the right term first and pushes it (`sal di,*1` / `push
di` / ... / `pop bx` / `add di,bx` - `is_relsum()`, `ORD_SPILL`); and `b[j]
> 0` under `&&` loads the element and tests it, `mov di,(di)` / `or di,di`
/ `ble` (v7's `rcexpr()` makes `x > 0` a test, and a STAR is computed into
a register for it - Val's `cond_ortest`). Through a pointer variable or at
a constant offset the dereference stays an operand (`cmp (di),*0`, as in
the kernel) - whether an element subscripted by a variable tested for
truth (`if (b[i])`) is also loaded first is what `p26_elem4` asks.

**6. `11_kernel/01_delay` byte-exact.** Its `i++;` (a statement of its
own) is `inc *-6.(bp)` - v7's `efftab`: an increment whose value is
unused, in place; `while (d--)` is `mov di,d` / `dec d` / `or di,di` /
`beq`. `mutos_c0` did not accept a bare `i++;`/`++d;` statement before
(`p21`'s `d++;` needed it too), and `mutos_c1` would have computed the
unused value; with both settled, the first kernel file is byte-exact at
all four stages.

**7. Inferred, and round 7.** `mutos_c1` compiles, with no golden yet:
`e = ++d` (`fstd`, the new value kept), a `?:` with constant arms or a
comparison as its condition, `-=`/`/=` through a pointer to a struct, `l
| m`/`l ^ m`, `x = (int) (l + 5)`, `l += 1`, `l = c`, `l / 7`, `x * *ip`,
`if (b[i])` and other element tests, and the stack model's inferred
parts. Round 7 (`make -f Makefile.mutos round7`) asks: `p24_fltinf2`,
`p25_long3` and `p26_elem4` (those inferences, and the refusals left:
`(x ? d : e) * 2.0`, `q->x += d * e`, a double element by `i + 1`,
`l << 3`, `-l`, a long under `&&`/`||`/`?:`, `l++`, `m = l = 5`, `l * 3`,
int compound assignments into elements - `b[i] += b[j]` - and through
pointers), `p27_frame` (frames of 82 to 127 bytes: `sub sp` or `chkstk`
- the gap no golden has shown) and `p28_fltstk` (not a program to run: the
floating-stack model after `half(d = 3.0)` - reset per function, the
argument push and an unused result, `fmul`/`fcmp` below the bottom,
`return d`).

**Tooling.** `x86sim.py` models `libc.a`'s `long` helpers (`lmul`,
`ldiv`, `lrem`, `almul` - operands on the machine stack, the result in
DX:AX), `fdup`, `neg`, and a long shifted through the carry (`rcl`/`rcr`
right after a one-bit `sal`/`sar`): 56 of the 62 corpus goldens run
(`02_long/02_muldiv` 24 newly), and 25 of the 27 `fltprobe` goldens (`p20`
128, `p22` 83, `p23` 136; `p21` 60 with its doubly popped store made
`fstd`).

**Verification.** `make test`: 88 files byte-exact at all four stages
(62/62 corpus, 25 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp` in category 9 (two messages, exit status 1, the
`.s` byte-exact), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 97/97 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`2540271`) over all 98 golden inputs: identical output but for the
round-6 files, `11_kernel/01_delay` and, in six other kernel files, later
diagnostics and partial `.1` output (constructs accepted now further
down; each still refuses, at the same first error). `fuzz_c.py` against
`2540271`: 22,000 programs (seeds 1, 7 `--scope`, 3 scalars only, 21), 0
WRONG, 0 BAD, 172 changed and still correct, 104 now correct that the old
build refused; 1,500 more (seed 31) through ASan/UBSan builds: 0 WRONG, 0
BAD. ASan/UBSan builds over the 98 golden inputs, the five round-7 probes
and the hand-written programs: no reports, output identical. 158
hand-written programs (members and pointers to double, compound
assignments into elements, `++`/`--`, conversions, `?:`, comma, every
`long` shape above and its neighbours, element orders): the 133 compiled
give the host C compiler's value; the rest refuse with a diagnostic.

### The round-7 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-04)

The five round-7 programs (`p24_fltinf2`, `p25_long3`, `p26_elem4`,
`p27_frame`, `p28_fltstk`) came back in commit `bdb2728`, all four kinds of
file, with the real compiler's messages for `p28` (`round7.log`). All five
had been refused (`mutos_c0` refused `p24`/`p25`/`p26`, `p27` differed in one
line number and was refused for its frames, `mutos_c1` refused `p28`). Now
`p24`..`p27` are byte-exact at every stage, and `p28` together with all
twelve of the real compiler's messages; the 88 files byte-exact before are
unchanged.

**1. An `EXPR`'s line is the line of the token that ends the expression.**
`p27_frame.1.golden`'s `return f82() + ... +` / `f127();` (lines 86 and 87)
has `EXPR 87`; `mutos_c0` wrote 86, the line the statement starts on. v7's
`statement()` and `doret()` call `rcexpr(tree())` - and `rcexpr()` writes
`EXPR` with the lexer's current `line`, which is where `tree()` stopped:
at the `;` it peeked. `mutos_c0` now writes the `;`'s line for every
expression statement (`return`, an assignment, a store through a pointer,
a call or `++`/`--` statement), the `;` after a `for`'s initialization and
the `)` after its increment (`forstmt()`'s `sline = line` right after
`tree()` - the increment's `EXPR` keeps that line although it is written
after the body). A condition's `CBRANCH` already took the lexer's line (the
token after `)` for an `if` - `p28`'s line-65 messages are the `if` on line
64). No other golden had an expression spanning lines, so nothing else
changed.

**2. Frames from 82 to 127 bytes** (`p27_frame`): 82 and 90 bytes are `sub
sp,*82.` / `sub sp,*90.`; 100, 110, 120, 124 and 126 are `mov ax,*100.` /
`call chkstk` (the immediate as any other: `*` up to 127); `char buf[127]`
is rounded to 128, `mov ax,#128.`. The switch lies in `(90,100]`:
`MCC_SUBSP_MAX` is 90 and `MCC_CHKSTK_MIN` 100 now, and the frames 92, 94,
96 and 98 (a frame is even) are refused - round 8's `p29_frame2` asks.

**3. A sum of calls.** `p27`'s `main()` returns `f82() + f90() + ... +
f127()`:

    call _f127 / push ax / call _f126 / push ax / ... / call _f90 / push ax
    call _f82 / pop bx / add ax,bx / pop bx / add ax,bx / ... (seven times)

`acommute()` keeps calls (equal degree) in source order and rebuilds the
chain left-deep; each `+` then has a call on its right, which no register
can hold across the next call, so v7's cr40 `%n,n` computes it first and
pushes it (`SS`), computes the left operand (`F` - the call's value stays in
AX) and adds the popped one (`I (sp)+,R`: `pop bx` / `add ax,bx`).
`is_callsum()` makes such a `+` (its left operand a call or such a sum)
`ORD_SPILL`; `OP_PLUS`'s pushed-right-operand path takes a left operand in
AX as well as in DI. `p28`'s `main()` is the same chain of four int calls,
then `call itof` / `call _f5` / `call fldd` / `call fadd` / `call ftoi` - a
double function's result added on the floating-point stack.

**4. `p28`'s twelve messages: the stack model is one per file, and a store
under a call is not always wrong.** `cc -S p28_fltstk.c` printed

    49: floating point stack underflow
    50: Floating point stack underflow       (twice)
    59: floating point stack underflow
    60: floating point stack underflow
    61: floating point stack underflow
    62: floating point stack underflow
    65: Floating point stack underflow       (twice)
    73: floating point stack underflow
    79: Floating point stack underflow       (twice)

and the code says how the model goes on. In `f1`, `x = half(d = 3.0);`
stores `d` WITHOUT a pop (`fstd`) and the argument push pops the value -
right code, no message; in `f3`, `f4` and `f5` the same assignment under an
unused call (`half(d = 3.0);`) is stored with a pop and popped again, as
`p21`'s `f = half(d = 3.0)` was. Counting with the round-6 model, every
message falls out once three things change:

- the model is NOT reset between functions: `f3` ends two below the bottom
  (line 48 silent - the unused result pops nothing, the argument push is
  unchecked - line 49's store reports, line 50's `fmul` and `ftoi` report),
  and `f4`'s `d = 1.0;` at line 59 - a balanced statement - reports because
  it starts two short; `main()`'s line 79 (`fadd`, `ftoi`) is still short;
- a double function's returned value, stored into `fac` (`lea ax,fac` /
  `call fstdp`), pops without a check, like the argument push: `f5`'s
  `return d;` at -5 is silent (`half()` must pop it, or every count after
  it is one off);
- `fmul`, `fadd` and `fcmp` (twice) below the bottom report with the
  upper-case message, as round 6 inferred for them (lines 50, 65, 79).

`fp_track()` no longer resets at `SAVE`, `gen_fp_rforce()` sets
`fp_nocheck`, and the end-of-statement check compares the model with where
the statement started, one lower per double pop (`fstart`,
`stmt_argpops`) - a statement may now end below 0 silently, as line 48 does.
Which store the real compiler picks under a call is inferred from the
three statements there are: `fstdp` when the statement's own value is
floating (`p21`'s `f = ...`, typed DOUBLE; `half(...);`, a double call),
`fstd` when it is not (`f1`'s `x = ...`, an int `ASSIGN` of an `FTOI`) -
`stmt_ftop`. The other reading is a state the previous floating statement
leaves behind: `f1`'s line 29 follows `half()`'s `return`, a value
computed into the register table. Round 8's `p30_fltstk2` separates the
two (an int statement right after a floating one, a call compared, a
double function's `return half(d = 3.0)`, two such arguments).
`tests/mutos_cc/c1_errors.txt` lists all twelve messages;
`run_goldens.sh` checks them in its category 9.

**5. Int elements** (`p26_elem4` - all its inferences checked, these corrected
or settled):

| Source | Golden |
|---|---|
| `if (b[i])`, `if (ps[i].c)` | `lea di,b` / ... / `add di,si` / `mov di,(di)` / `or di,di` / `beq` (`mov di,*4.(di)` with the member's offset) - an element read through a computed address tested for truth is loaded and tested, as one compared with 0 (`b[j] != 0`, round 6); `mutos_c1` had inferred `cmp (di),*0`, the shape through a pointer variable |
| `x = b[i] * b[j] + b[j - 1]` | the product (`b[j]` pushed first) / `mov di,ax` / `lea si,b` / `mov dx,j` / `sal dx,*1` / `add si,dx` / `add di,*-2.(si)` - the left operand moved into the working register before the right element's address is computed (v7's `%n,ew*`: `F`, then `S*` into the next register) |
| `x = (b[i] + 1) * b[j]` | `b[j]` loaded and pushed / `mov di,(di)` / `inc di` / `mov ax,di` / `pop cx` / `imul cx` - the element at offset 0 first, as when the left operand is an element |
| `x = b[j] / b[i]`, `b[j] % b[i]` | `b[i]` loaded and pushed / `b[j]` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx` (the remainder `mov x,dx`) |
| `r = r + (b[i] < b[j]) + (b[i] > b[j]) * 2 + (b[i] == b[j]) * 4` | C (pushed in both arms) / B (pushed in both arms) / A / `sal di,*1` / `pop bx` / `add di,bx` / `sal di,*1` / `pop bx` / `add di,bx` / `add di,r` - see below |
| `b[i] += b[j]`, `b[j] -= x` | `b[i]`'s address / `push di` / `b[j]` loaded, or `mov di,x` / `pop bx` / `add (bx),di`, `sub (bx),di` |
| `b[0] *= b[i]` | `b[i]` / `mov di,(di)` / `mov ax,di` / `imul *-28.(bp)` / `mov *-28.(bp),ax` |
| `*ip += 2` | `mov di,ip` / `add (di),*2.` |
| `ps[i].c += x` | `mov di,x` / `lea si,ps` / `mov dx,i` / `mov cx,*3.` / `sal dx,cl` / `add si,dx` / `add *4.(si),di` - the right-hand side first, as a store through "pointer + constant" (`is_disp_store()`) |
| `r + b[0] + b[1] + b[2] + ps[i].c` | the member first (`mov di,*4.(di)`), then `add di,*-28.(bp)` / `*-26.` / `*-24.` / `add di,r` |

The sum of three comparisons is v7's `distrib()` at work. `acommute()`
collects the terms by degree - the comparisons ahead of `r`, as written:
C, B\*2, A\*4, r - and `distrib()` divides A\*4 by B\*2's constant ("c1\*y +
c1\*c2\*x -> c1\*(y + c2\*x)"): C, (A\*2 + B)\*2, r. That list, rebuilt
left-deep, would compute the factored term first (the right operand of the
first `+`); the golden computes C first. The second `optim()` explains it:
`rcexpr()`'s `reorder()` optimizes the tree again before any code, and that
`acommute()` sees (A\*2 + B)\*2 with a higher degree than C - its inner sum
of two equal-degree terms is one more (the MUTOS degree that already
explained `03_starray`'s `sum` added last), and a TIMES by a power of two
keeps its operand's - so the list becomes (A\*2 + B)\*2, C, r. Each `+`
then takes cr40: a right operand computed with branches goes first, onto
the stack (`%n,n`), a variable is added from memory. Pushing a comparison's
0/1 onto the stack puts the `push di` into each arm (`cexpr()` compiles a
relational value as `cbranch()` and then `czero`/`cone` through the
requested table - `sptab`): `cmp (bx),di` / `blt L10002` / `mov di,*0.` /
`push di` / `jmp L10003` / `L10002:mov di,*1.` / `push di` / `L10003:`.
`mutos_c1` reproduces it for an int `+` chain of comparisons, two or more of
them times a power of two, and int variables (`is_distrib()`,
`plan_distrib()`: insert(), distrib() and the second acommute() on a small
tree of the terms; `SEG_SHIFT` for the rebuilt multiples, `plan_spill()`
pushing a comparison in both arms). `p23`'s two-term `(a[i] < b[j]) + (a[i]
> b[j]) * 2` is consistent with it: equal degrees, as written, the scaled
term on the right pushed first.

Constant-index elements are leaves: `enode_degree()` gives `*(&b + 0)` -
`b[0]` - degree 0, as v7's `optim()` folds it into the NAME, so the last
row's elements are added in order after the member and before `r`.
`mutos_c0` accepts `*p op= expr` (`NAME ip, STAR(0), CON 2, ASPLUS(0)` -
the golden's tree); `mutos_c1` the in-place compound assignments into an
element or through a pointer (`is_aspush()`/`SEG_ASPOP`, `is_asdisp()`), and
`*=` into a constant-index element with an element on the right.

**6. `long` shapes** (`p25_long3`):

| Source | Golden |
|---|---|
| `l += 1`, `l++`, `++l` | `add *-6.(bp),*1.` / `adc *-8.(bp),*0` - in place; `l--`: `sub` / `sbb ...,*0` (the high word's constant written `*0`, as in `cmp di,*0`) - `mutos_c0` writes the 1 widened (`CON 1, ITOL, INCAFT(6)`) |
| `l = -l` | `mov si,l+2` / `mov di,l` / `neg di` / `neg si` / `sbb di,*0` |
| `l = l << 3`, `l = l >> 4` | `mov cx,*3.` / `sal si,*1` / `rcl di,*1` / `loop .-4` (`sar di,*1` / `rcr si,*1` for `>>`) - from 3 up, as an int is shifted by CL |
| `if (l && i)`, `if (l \|\| j)`, `r + (l ? 2 : 50)` | both words tested through `cbranch()`, the high one first - `cmp di,*0` / `bne L10000` / `cmp si,*0` / `beq L4` / `L10000:` (false-branch), `bne` / `bne` (true-branch); the `?:`'s value then `add di,r` |
| `r + (int) (l - 9990)` | `mov di,r` / `add di,l+2` / `add di,#-9990.` - the other term first, unlike `(int) (l - 69990)`'s `mov di,l+2` / `add di,r`: v7's `optim()` makes "x - c" "x + -c" only for a CON or an ITOL of one, not an LCON |
| `r + 10 - (int) (l - 4995)` | `mov di,r` / `add di,*10.` / `mov si,l+2` / `add si,#-4995.` / `sub di,si` |
| `l * 3` | `mov ax,*3.` / `cwd` / `push ax` / `push dx` / ... / `call lmul` (as `l / 7`) |
| `l + i * j` | `mov ax,i` / `imul j` / `cwd` / ... (no `mov ax,ax`) |
| `m = l = 5` | `mov ax,*5.` / `cwd` / `mov di,dx` / `mov si,ax` / both stores |

`mutos_c0` writes `-l` as `NEG(6)`. The inferences `l | m`, `l ^ m`, `x =
(int) (l + 5)`, `l = c` and `l / 7` matched as inferred. The `?:`'s DI
value plus a variable, stored, is now `add di,r` (`acommute()`: the
computed term first) - inside a larger expression it stays `mov si,r` / `add
si,di` (a later sibling may need DI; no golden). `mutos_as` assembled `loop
.-4` already (`E2 FA`).

**7. Floating shapes** (`p24_fltinf2`):

| Source | Golden |
|---|---|
| `e = ++d` | `fldd d` / `fadds 1.0` / `fstdp d` / `fldd d` / `fstdp e` - the prefix value stored WITH a pop and the variable loaded again (`mutos_c1` had inferred `fstd`) |
| `f++` (a float) | `flds f` / `fadds 1.0` / `fstsp f` - `mutos_c0`: `CON 1, ITOF(2), INCAFT(3)` |
| `r + (x ? d : e) * 2.0` | the `.data` of 2.0 first, the `?:` (each arm `fldd`), `lea ax,L` / `fmuls`, then `mov ax,r` / `itof` / `fadd` (the int in AX after a `*`) - a `?:` has degree 2 (`optim()`'s default) |
| `q->y -= e`, `q->x /= e` | `... push ax` / `fldd` / `fsubd e`; but `/=` loads the divisor: `fldd` / `lea ax,e` / `fldd` / `fdiv` |
| `q->x += d * e` | `mov di,q` / `lea ax,(di)` / `\|` / `push ax` / `fldd` / `fldd d` / `fmuld e` / `fadd` / `pop ax` / `fstdp` - the target pushed and loaded first (`SEG_FLOADTP`) |
| `a[i + 1] = 2.0` | `flds 2.0` / `lea di,a` / `mov si,i` / `mov cx,*3.` / `sal si,cl` / `add di,si` / `lea ax,*8.(di)` / `fstdp` - the index scaled by 8 through CL |
| `d = q->k` | `mov di,q` / `mov di,*16.(di)` / `mov ax,di` / `itof` |
| `*pick(a, 1) = 2.5` | `flds 2.5` / the call / `mov di,ax` / `lea ax,(di)` / `fstdp` - through DI for a store (BX for a read, `*pick(a, 2)` in `p20`) |

Its other inferences matched: `e = --d`, `x ? 1.5 : 2.5` (each constant's
`.data` block inside its arm), `(i > j) ? e : d`, `-i * e`, `x = q->y *
4.0`, `q->x > e`, `a[i] < q->y`, `a[i] + q->x * s.y`, `d + i * j`, `d * (i
+ 1)`. `mutos_c0` writes a store through a function's result (`NAME _pick,
..., CALL(11), STAR(3), FCON, ASSIGN(3)`) and `++`/`--` on a float.

**8. Round 8** (`make -f Makefile.mutos round8`): `p29_frame2` (frames of
92 to 98 bytes), `p30_fltstk2` (not a program to run: which store an
assignment under a call gets - see 4), `p31_long4`, `p32_elem5` and
`p33_fltinf3` (what `mutos_c1` now infers from round 7, and what it still
refuses: a long `++`'s value, `l += 70000`, `l << i`, `x = !l`, a constant
minus a long's low word; `*ip += x`, `b[3] ^= b[i]`, `x + f(1) + g(2)`, a
quotient plus a remainder of elements; a float's `++` as a value, the value
of a compound assignment through a pointer, `++d` as an operand).

**Tooling.** `x86sim.py` runs `loop .-4` (back over a one-bit long shift
pair) and takes CF from `neg` (for `sbb ...,*0`): 29 of the 32 `fltprobe`
goldens now run (`p24` 174, `p25` 153, `p26` 238, `p27` 24 - their C
sources' values; `p19`, `p21` and `p28` as before or not meant to run).

**Verification.** `make test`: 92 files byte-exact at all four stages
(62/62 corpus, 29 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp` and `p28_fltstk` in category 9 (2 and 12 messages,
exit status 1, the `.s` byte-exact), 0 mismatches, zero warnings;
`mutos_as` 76/76, `check_floatdat.sh` 13/13, 102/102 compiler goldens
assemble and `p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against
the previous build (`bdb2728`) over all 103 golden inputs: identical output
but for the five round-7 files. `fuzz_c.py` against `bdb2728`: 22,000
programs (seeds 1, 7 `--scope`, 3 scalars only, 21), 0 WRONG, 0 BAD, no
regression; 2,636 changed and still correct - an element tested for truth
loaded and `or`ed (most), `/`, `%` and `*` of elements with the right one at
offset 0 computed first, a computed value plus a variable stored as `add
di,x` - and 401 now correct that the old build refused (`/` and `%` of two
2-D elements). Two regressions found on the way were fixed first: a left
operand ending in AX (`(a * b) & 17`) no longer takes the right-first order
(`ends_in_axdx()`), and a computed value plus a variable keeps the old
register order inside a larger expression. 1,500 more (seed 31) through
ASan/UBSan builds: 0 WRONG, 0 BAD. ASan/UBSan builds over the 103 golden
inputs, the five round-8 probes and the hand-written programs: no reports,
output identical. 66 hand-written programs (sums of calls, frames from 81
to 127 bytes, elements tested, multiplied and divided, sums of scaled
comparisons, compound assignments into elements and through pointers,
every `long` shape above and its neighbours, the floating shapes): the 55
compiled give the host C compiler's value; the rest refuse with a
diagnostic.

### The round-8 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-07)

The five round-8 programs (`p29_frame2`, `p30_fltstk2`, `p31_long4`,
`p32_elem5`, `p33_fltinf3`) came back in commit `2eaa65f`, all four kinds of
file, with `round8.log`. The log has no compiler message at all: `cc -S
p30_fltstk2.c` - written to make the real `c1` report its floating-stack
underflow again - printed nothing and `make` reported no error code. All five
had been refused (`mutos_c0` refused `p33`, `mutos_c1` the other four); now
all five are byte-exact at every stage, and the 92 files byte-exact before are
unchanged.

**1. Frames of 92 to 98 bytes** (`p29_frame2`): `sub sp,*92.`, `*94.`,
`*96.`, `*98.` - every one. With `p27_frame`'s `mov ax,*100.` / `call chkstk`
the switch is exactly at 100 bytes, `(98,100]` - and since a frame is always
even (`p27`'s 127-byte array is rounded to 128), there is no size left
between the two. `MCC_SUBSP_MAX` is gone: `OP_SETSTK` emits `sub sp,N` below
`MCC_CHKSTK_MIN` (100) and `mov ax,N` / `call chkstk` from there; nothing is
refused any more.

**2. Which store an assignment passed as a floating argument gets**
(`p30_fltstk2`). Round 7 had three statements: `fstdp` (the value popped,
and popped again by the argument push - the real `c1`'s own wrong code and
messages) for `f = half(d = 3.0)` and `half(d = 3.0);`, `fstd` (right code)
for `x = half(d = 3.0)`; `mutos_c1` inferred "the statement's own type".
`p30` stores with `fstd` in all eight places:

| `p30` | statement | type of the statement | the call's value | store |
|---|---|---|---|---|
| `f1` | `x = half(d = 3.0);` after `d = 1.0;` | int | converted (`FTOI`) | `fstd` |
| `f2` | the same after `x = d;` | int | converted | `fstd` |
| `f3` | `e = half(d = 3.0) + 1.0;` | double | an operand of `+` | `fstd` |
| `f4` | `if (half(d = 3.0) > 1.0)` | - | compared | `fstd` |
| `f5` | `x = half(d = 3.0) > 1.0;` | int | compared | `fstd` |
| `f6` | `return half(d = 3.0);` in a double function | double | returned | `fstd` |
| `f7` | `x = two(d = 1.0, e = 2.0);` | int | converted | `fstd`, both |

`f3` and `f6` refute the inference - a floating statement - and so does the
"state the previous floating statement leaves" reading (`f1`/`f2` follow a
floating statement and keep). What the ten statements of `p21`, `p28` and
`p30` share is the CALL's own consumer: the store pops exactly when the
call's value goes nowhere (`half(d = 3.0);`) or straight into the
statement's floating store (`f = half(d = 3.0);`), and keeps the value under
anything else - a conversion, an operator, a comparison, a `return`. Why the
real `c1` takes the popping store in those two places is not known; the rule
is what the goldens show. `plan_expression()` now marks the floating
assignments that are arguments of such a call (`argpop_end`, found on the
pre-scanned tree, up through the argument list's `COMMA`s), and `fp_store()`
pops for those only; `stmt_ftop` is gone. Without a double pop nothing goes
below the bottom, so `p30` reports nothing, as the real `c1` did, and its
code is right: under `x86sim.py` it returns 9 (`f1` 1 + `f2` 1 + `f3` 2 +
`f4` 1 + `f5` 1 + `f7` 3) - the first of these probes that runs. A call
nested in an argument, an int function and other argument stores have no
golden (round 9's `p37_fltstk3` asks).

**3. `long` shapes** (`p31_long4`). Matched as inferred: `l -= 1`, `--l`,
`l += 300` (`add *-6.(bp),#300.` / `adc *-8.(bp),*0`), `m = -l`, `l && m`,
`l << 5`, `l >> 7`, `l * 7`, `l ? x : 7`. Corrected and settled:

| Source | Golden |
|---|---|
| `m = m * i` | `mov di,m+2` / `push di` / `mov di,m` / `push di` / `mov ax,i` / `cwd` / `push ax` / `push dx` / `call lmul` - the long variable pushed FIRST, the int widened after it: `acommute()` puts the `ITOL` (degree 2) ahead of the `NAME`, and cr82's `SS` pushes the right operand first (`mutos_c1` had inferred the int widened and pushed first). An int variable times a long keeps its `ITOL` unwidened (`Val`'s `itolw`) until `gen_long_binop_call()` |
| `x = l++` | `mov di,l+2` / `inc l+2` / `mov x,di` - the LOW WORD incremented as an int, the carry into the high word lost (the real compiler's own wrong code for a low word of 0xffff): the `LTOI` distributed into the `++` as v7's `unoptim()` distributes it into a `+`, `LTOI(l)` the low word and `LTOI(ITOL(1))` the 1. The increment comes right after the load - `cr32`'s `%a,1` - not after the store as an int `x = i++`'s does (v7's `delay()` finds no postfix operator right under the assignment). `x = ++l` and `x = --l` inferred the same way (round 9's `p34_long5` asks) |
| `l += 70000` | `mov si,#4464.` / `mov di,*1.` / `add *-6.(bp),si` / `adc *-8.(bp),di` - the constant into DI:SI as for an assignment, then added. A negative int constant widened (`l += -5`) is an `LCON` to v7's `unoptim()` - inferred the same (`p34`) |
| `l = l << i` | `mov si,l+2` / `mov di,l` / `mov cx,i` / `or cx,cx` / `jz .+8` / `sal si,*1` / `rcl di,*1` / `loop .-4` - v7's `rcexpr()` takes the `ITOL` off a long shift's count, the count goes into CX, and a count of 0 skips the loop (`.+8`: past the pair and the `loop`, two bytes each). `>>` inferred the same with `sar di` / `rcr si` |
| `x = !l` | `mov si,l+2` / `mov di,l` / `cmp di,*0` / `bne L10006` / `cmp si,*0` / `beq L10005` / `L10006:mov di,*0.` / `jmp L10007` / `L10005:mov di,*1.` / `L10007:` - v7's `cexpr()` compiles a logical value with `cbranch()`, both words tested as for `if (!l)` |
| `x = 10 - (int) (l - 35)` | `mov di,*10.` / `mov si,l+2` / `add si,*-35.` / `sub di,si` - the constant loaded first ("F"), then `p25`'s shape |

**4. Int shapes** (`p32_elem5`). Matched as inferred: `if (!b[i])`, `x =
!b[j]`, `b[j] ? 3 : 9`, `if (a[i][j])`, `(b[i] - x) * b[j]`, `~b[i] * b[j]`,
`(b[j] + 3) / b[i]`, `(b[3] & 7) % b[4]`, `b[i] += 7`, `ps[i].d ^= x`, `p->b +=
x`, `a[i][j] += x`, `x = (b[i] < b[j]) * 2 + (b[j] < b[i]) * 2 + x` (the two
equal multiples merged by `distrib()`), `x = f(1) + g(2)`, `x = f(1) + g(2) +
h(3)`. Corrected and settled:

| Source | Golden |
|---|---|
| `x = (x > 2) * 2 + (x < 9) * 4 + (x == 5) + x` | `distrib()`'s `((x < 9)*2 + (x > 2))*2 + (x == 5) + x`, each comparison of a variable with a constant computed AFTER the left operand, into SI: `cmp x,*9.` / `blt` / ... `mov di,*1.` / `sal di,*1` / `cmp x,*2.` / `bgt L10006` / `mov si,*0.` / `jmp L10007` / `L10006:mov si,*1.` / `L10007:add di,si` / `sal di,*1` / [`x == 5` into SI] / `add di,si` / `add di,x`. Its degree is 1, so it fits v7's `%n,e` (F, S1, `add R1,R`); `p26`'s comparisons of two elements (degree 3) are `%n,n` - pushed. `mutos_c1` had pushed every one (`plan_dnode()`'s `hard` now asks for the degree; `materialize_slot()` puts a comparison's 0/1 into SI when it is the right operand and the left one is in DI - `cond_reg()`) |
| `*ip += x` | `push ip` / `mov di,x` / `pop bx` / `add (bx),di` - the pointer variable pushed straight from memory (`%n*,n`: FS* - a pointer NAME through `sptab` is the variable pushed), the right-hand side into DI (`is_asptr()`, `ORD_ASPTR`). A constant still goes through DI (`p26`'s `*ip += 2` - `add (di),*2.`) |
| `b[3] ^= b[i]` | [`b[i]`] / `mov di,(di)` / `xor *-50.(bp),di` - the element loaded, the operation in place into the constant-index element (efftab `%aw,n`: S, `I R,A1`). Any right-hand side computed into DI or SI takes the same shape by inference (`x += y * 2`) |
| `x = x + f(1) + g(2)` | [`g(2)`] / `push ax` / [`f(1)`] / `pop bx` / `add ax,bx` / `add ax,x` / `mov x,ax` - `acommute()` orders by degree, the calls (10) ahead of the variable (0): the calls summed first (`is_callsum()`'s shape), the variable added to AX after them (`is_callacom()`, `ORD_CALLACOM`; one call or more, three terms or more) |
| `x = b[j] / b[i] + b[j] % b[i]` | [`b[j] % b[i]`: ... `idiv cx`] / `push dx` / [`b[j] / b[i]`: ... `idiv cx`] / `pop bx` / `add ax,bx` / `mov x,ax` - neither waits in a register across the other's `idiv`: the right operand computed first and pushed from its own register, DX (cr40's `%n,n` again - `is_callsum()` now takes a quotient or a remainder as the right operand, a quotient or a call as the left one; a remainder on the left, a product and a difference of calls have no golden and stay refused) |

**5. Floating shapes** (`p33_fltinf3`). Matched as inferred: `++f`, `--f`,
`f--` on a float as statements, `(x ? d : e) + 1.5`, `((i > j) ? d : e) * f`,
`q->y /= 2.0`, `q->x -= d * e`, `q->x /= d + e`, `a[i + 1] += 1.0`, `a[i + 2] =
a[j] * 2.0`, `d = q->k * 2.0`, `*pick(a, i) = d + e`. Settled:

| Source | Golden |
|---|---|
| `e = f++` (a float) | `flds f` / `call fdup` / `fadds 1.0` / `fstsp f` / `fstdp e` - as a double's `e = d--` |
| `d = ++f` | `flds f` / `fadds 1.0` / `fstsp f` / `flds f` / `fstdp d` - as a double's `e = ++d` |
| `e = (q->x += d)` | `fldd d` / `mov di,q` / `lea ax,(di)` / `\|` / `push ax` / `call faddd` / `pop ax` / `call fstd` / `fstdp e` - its value used: the right operand loaded FIRST, the target combined from memory and stored without a pop (a statement `q->x += 0.5` loads the target first). `mutos_c0` now writes an embedded compound assignment into a floating member or element (`parse_comma_item()`; `lvalue_asop_ahead()` scans the tokens ahead, the lexer put back where it was): NAME q, CON 0, PLUS, STAR(3), NAME d, ASPLUS(3), then the outer ASSIGN. `*=` inferred the same (its right operand loaded first by the plan, as for a statement); `-=` and `/=` would need the operands the other way round - refused |
| `e = ++d * 2.0` | `.data` / `L10029: .float 1.0` / `.text` / `fldd d` / `fadds L10029` / `fstdp d` / `.data` / `L10028: .float 2.0` / `.text` / `flds L10028` / `lea ax,d` / `fmuld` / `fstdp e` - v7's `rcexpr()` `reorder()`s the `*`'s subtree first: `sreorder()` compiles a prefix operator on a `NAME` for its effect (efftab) and puts the `NAME` in its place, so the increment comes before the `*`'s code, its 1.0's `.data` block with it, and the variable is an operand of degree 0, after the constant (degree 1). The constants' labels are as before: the written 2.0 numbered first, the converted 1.0 after it (`fplan_constants()`). `is_fpreinc()` / `plan_fhoist()` / `SEG_FDROP` do it for an operand of a floating `+`, `*`, `-` or `/` |

**6. Tooling.** `x86sim.py` runs `jz .+8` (past a long shift's pair and its
`loop` when CX is 0, after `or cx,cx`): 34 of the 37 `fltprobe` goldens now
run and return their C sources' values (`p29` 50, `p30` 9, `p31` 140, `p32`
284, `p33` 82; `p19`, `p21` and `p28` as before).

**7. Round 9** (`make -f Makefile.mutos round9`): `p34_long5`, `p35_elem6`
and `p36_fltinf4` - what `mutos_c1` now compiles by inference only (the
prefix `x = ++l`, `l += -5`, `l >> i`, `i * m`; `*ip -= y`, `x -= b[i]`, `x =
x + f(1) + y`, a call plus a quotient, `(y > 2) + (x < 3)`; `e = (q->x *=
2.0)`, `e = (a[i] += d)`, a float's `f--` and `--f` as values, `++d` under `+`,
`-` and `/`, `(d += 1.0) * 2.0`) and what it still refuses (`l <<= 3`, `l /=
m`, `(int) (l * 2)`, `l - m - 1`, `100000 - l`; `c % d + c / d`, `c * d +
f(1)`, `f(1) - g(2)`, `f(1) * g(2)`, `b[i] + f(1)`; `e = (q->y -= d)`, `d++ *
2.0`, `++d > 2.0`) - and `p37_fltstk3` (not a program to run: the
argument-store rule of 2. under a nested call, an int function, two
arguments of an unused call, a float target).

**Verification.** `make test`: 97 files byte-exact at all four stages
(62/62 corpus, 34 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp` and `p28_fltstk` in category 9 (2 and 12 messages,
exit status 1, the `.s` byte-exact), 0 mismatches, zero warnings; `mutos_as`
76/76, `check_floatdat.sh` 13/13, 107/107 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`2eaa65f`) over all 108 golden inputs: identical output but for the
five round-8 files. `fuzz_c.py` against `2eaa65f`: 28,000 programs (seeds
1, 7 `--scope`, 3 scalars only, 21, 41 `--scope`, 55 scalars only), 0 WRONG,
0 BAD, no regression, no program's output changed, 138 now correct that the
old build refused. One regression found on the way was fixed first: a
comparison materialized into SI whenever DI held a pending value made `((--n
|| (11 / y)) % ((x >= 6) + !y))` reach the stacked-`+` path with its left
operand in SI (an internal error) - `cond_reg()` now takes SI only for the
right operand next to a left one in DI. 1,500 more (seed 31) through
ASan/UBSan builds: 0 WRONG, 0 BAD; ASan/UBSan builds over the 108 golden
inputs, the four round-9 probes and the hand-written programs: no reports,
output identical. Hand-written programs (the round-9 shapes, `h1`..`h4`):
every one compiled gives the host C compiler's value; the round-9 probes
with their refused statements taken out give theirs (`p34` 178, `p35`
189, `p36` 124). `make check-libcatof` not run (`unicorn` is not installed
in this sandbox); `c1_fltdec.c` is unchanged.

### The real assembler on `p19_open3.s` (real-hardware finding, 2026-10-07)

Round 5 left one question open. The real compiler's `d = (d * e) + u`
writes 118 bytes of libc's `_ctype_` table where a register name belongs
(golden lines 229, `mov <garbage>,ax`, and 231, `push <garbage>`). Would
the real assembler accept that? If it did, `mutos_as` would have to
reproduce whatever it made of the bytes. If it did not, no MUTOS 1700
binary can ever have contained the code.

The bytes are libc's `_ctype_` class table from the `'\n'` entry on (see
"The round-5 fltprobe goldens"). Read as text (`od -c` of line 229), they
are mostly control characters (`\b`, `020`, `004`, `001`, `002`), plus
runs of spaces and the letters `AAAAAA` and `BBBBBB`, between `mov\t` and
`,ax`.

The run on MUTOS, in `fltprobe` (`tests/mutos_cc/fltprobe/p19_as.log`):

```
# as -o p19.o p19_open3.s
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 231
***ERROR*** syntax error, line 231
p19_open3.s: 3 errors.
# ls -l p19.o
-rw-r--r-- 1 root     other          0 Oct  7 16:47 p19.o
```

- **Refused.** The real `as` rejects both lines and writes no code; the
  output file it created stays at 0 bytes (in both runs). No other line
  draws a message. That covers the 223 lines `invalid_goldens.txt` counts
  as valid, and also the ordinary lines of the statement and the function
  around 229 and 231 (`mov ax,*-36.(bp)`, `sub ax,ax`, `push ax`, `call
  ltof`, ...).
- **Message form.** The real format is `***ERROR*** <text>, line <n>`,
  followed by a summary `<file>: <n> errors.`. This is the same `***ERROR***`
  prefix as the floating overflow abort (`***ERROR*** floating point
  over/under flow- assembly aborted`, `tests/mutos_as/float_open/`).
  A syntax error does not abort, though: the assembler goes on to line 231
  and prints the summary at the end. There are six messages but the count
  is 3. How the real `as` counts is not known; one message per token it
  trips over is only a guess. Nothing here depends on it.
- **`mutos_as`** refuses the same two lines: `error: could not classify
  operands at line 229` and `... 231`, then `2 error(s) in Pass 1,
  aborting before Pass 2`, exit status 1, and no object file at all. As
  with its other messages, it does not copy the real text. The 0-byte
  object the real `as` leaves behind is not reproduced either; no golden
  or test depends on a failed run's output file.
- **Exit status 2** (a second run, `as -o p19.o p19_open3.s` then `echo
  $?`, the same six messages, appended to `p19_as.log`). The floating
  overflow abort exits with 4. So the real `as` uses a different status
  for each case, and neither is the error count (3). `mutos_as` exits with
  1 for both. That difference is documented, and nothing in this project
  depends on the exact value: every script only checks for a nonzero
  status.
- **A process note for future real-hardware runs.** The first attempt was
  `as -o p19.o p19_open3.s > p19_as.log 2>&1 ; echo "exit=$?" >>
  p19_as.log` followed by `ls -l p19.o a.out >> p19_as.log 2>&1`. It left
  `p19_as.log` at 0 bytes: no message, not even the `echo`'s line, not
  even `ls`'s. `df` showed the disks 13 % and 22 % used, so it was not a
  full file system. The cause is unexplained. The round logs, written
  with `2>&1 | tee`, have always come back complete, so `| tee` is the
  form to ask for. When a log comes back empty, copying from the screen
  works, as it did here.

Consequence: none for the code. `p19_open3` stays in `invalid_goldens.txt`
(category 8), `mutos_c1` keeps refusing an unsigned converted after a
computed operand, and `assemble_cc_goldens.sh` keeps requiring `mutos_as`
to refuse the golden. That is now confirmed real-assembler behaviour, not
just this project's choice.

## Milestone 5 — Optimizer (`c2`) & NEC V30 (`-mv30`)

**Status:** not started (no `c2` work has begun). This section currently covers a
single, resolved design question, worked out ahead of any implementation because it
touches the calling convention `mutos_c1` must already target correctly from
Milestone 4: **should `-mv30`-compiled code use the 80186/V30 `ENTER`/`LEAVE`
instructions for function prologue/epilogue?**

### Hard constraint (settled)

`-mv30`-compiled object code **must remain link-compatible with the real, unmodified
`libc.a`/`crt0.o`** — this project does not maintain (and Milestone 5 does not plan
to introduce) a separate, parallel `-mv30`-only runtime. This rules out *any* change
to the ABI-visible frame layout established in `docs/MUTOS_C_ABI.md` §1.2–1.4,
regardless of what performance case could otherwise be made — `-mv30` may only change
*instruction selection inside* a function body, never the calling-convention contract
itself. This immediately disqualifies the naive form of `ENTER` (see below), and
means any `LEAVE`-based reimplementation of `cret` must be proven behaviorally
identical (same precondition, same postcondition) before it's even a candidate.

### Evidence: `docs/V20_V30_Users_Manual_Oct86.pdf`

NEC's own µPD70108(V20)/µPD70116(V30) User's Manual (Oct 1986), added to the project
this session specifically to answer this question with real per-chip timing data
(the existing `docs/210973-001_AP-186..._Mar83.pdf` — Intel's 80186 application
note — documents `ENTER`/`LEAVE`'s *algorithm* in Appendix H but contains **no**
timing/clock-count table at all; it's an architecture guide, not a datasheet).

**Navigational gotcha worth recording**: this manual does **not** use Intel's
mnemonics. `ENTER` is called **`PREPARE`** and `LEAVE` is called **`DISPOSE`**
throughout — searching the OCR'd text for "ENTER"/"LEAVE" finds nothing relevant
(matches on unrelated prose like "entering standby mode"). Search for `PREPARE`/
`DISPOSE` instead. Encodings are confirmed identical to Intel's (`PREPARE
imm16,imm8` = `C8 iw ib`, 4 bytes; `DISPOSE` = `C9`, 1 byte) — this is an
object-code-compatible superset, just independently documented and renamed by NEC.
The manual is a genuine text-layer PDF (Adobe "Paper Capture" OCR over a scan, not
the ZIP-of-JPEGs format the 80186 AP-186 doc uses) — `pdftotext -layout` works
directly, no rasterization needed.

Per this manual, MUTOS 1700 (Robotron A7100/A7150) targeting `-mv30` means the
**µPD70116 (V30)** specifically — the 16-bit, 8086-pin-compatible part (as opposed to
the µPD70108/V20, the 8/16-bit 8088-pin-compatible part). All timings below are
quoted for `pPD70116` (OCR misread of "µPD70116") in both its **odd-address** and
**even-address** variants (V-series timings depend on whether the effective operand
address is even or odd — the even case, cheaper, applies whenever code/data happens
to land on a word boundary), alongside the V20 figure for completeness.

### `PREPARE`/`DISPOSE` vs. the discrete instruction sequence — real numbers

The **only** layout-compatible use of `PREPARE`/`DISPOSE` (see "Hard constraint"
above) is: `PREPARE framesize+4,0` + `mov [bp-2],di` + `mov [bp-4],si` in place of
the prologue, and `mov si,[bp-4]` + `mov di,[bp-2]` + `DISPOSE` + `ret` in place of
`cret`. (Naive `PREPARE framesize,0` immediately followed by ordinary `push di`/
`push si` reserves the locals *before* saving `di`/`si`, landing them **below** the
locals instead of above — the inverse of the fixed `bp-2`/`bp-4`-reserved-for-di/si
layout every real compiled function and `cret` depend on. Off the table per the hard
constraint regardless of speed.)

Real per-instruction clocks from the manual (V20 / V30-odd / V30-even), each cited
against its own instruction-summary entry:

| Instruction | V20 | V30 odd | V30 even |
|---|---|---|---|
| `PUSH reg16` | 12 | 12 | 8 |
| `POP reg16` | 12 | 12 | 8 |
| `MOV reg,reg` (e.g. `MOV BP,SP`) | 2 | 2 | 2 |
| `SUB reg,imm` | 4 | 4 | 4 |
| `LEA reg,mem` (`LDEA` in NEC's notation) | 4 | 4 | 4 |
| `MOV mem,reg` (word store) | 13 | 13 | 9 |
| `MOV reg,mem` (word load) | 15 | 15 | 11 |
| `RET` (near, no operand) | 19 | 19 | 15 |
| `PREPARE imm16,0` | 16 | 16 | 12 |
| `DISPOSE` | 10 | 10 | 6 |
| `PUSHR` (=`PUSHA`, all 8 regs) | 67 | 67 | 35 |
| `POPR` (=`POPA`, all 8 regs) | 75 | 75 | 43 |
| `SHL reg,imm8` (multi-bit shift, any count) | 7+n | 7+n | 7+n |

**Prologue**, `push bp/mov bp,sp/push di/push si/sub sp,N` vs. the compatible
`PREPARE`-based replacement:

```
current:   12 + 2 + 12 + 12 + 4  = 42  (V20 / V30-odd)     8 + 2 + 8 + 8 + 4  = 30  (V30-even)
PREPARE:   16 + 13 + 13          = 42  (V20 / V30-odd)    12 + 9 + 9         = 30  (V30-even)
```

**Epilogue**, `cret` (`lea sp,[bp-4]/pop si/pop di/pop bp/ret`) vs. the compatible
`DISPOSE`-based replacement:

```
current:    4 + 12 + 12 + 12 + 19 = 59  (V20 / V30-odd)    4 + 8 + 8 + 8 + 15  = 43  (V30-even)
DISPOSE:   15 + 15 + 10 + 19       = 59  (V20 / V30-odd)   11 + 11 + 6 + 15    = 43  (V30-even)
```

**Result: an exact tie, cycle-for-cycle, on real V20/V30 hardware, in both cases and
at every address alignment.** This is a **materially different conclusion** than the
ad-hoc, appropriately-caveated-at-the-time estimate from earlier the same session,
which used 80286 timing data as a proxy (no 80186/V30-specific timing table was
available yet) and suggested a clear loss for `ENTER` specifically — real 80286
`ENTER 0,0` genuinely is slower than its component instructions (well documented
Intel-silicon history), but NEC's V-series implementation of the equivalent
`PREPARE`/`DISPOSE` is evidently better-optimized and lands at parity instead.
**This correction is recorded here explicitly so a future session trusts this
primary-sourced table over the earlier proxy estimate.**

Code size is a small, real regression either way: `PREPARE`-variant prologue is 10
bytes vs. 8 for the current small-frame case (`PREPARE` has no imm8 short form,
unlike `sub sp,imm8`); `DISPOSE`-based epilogue is 8 bytes vs. 7 — the latter is
irrelevant in aggregate since `cret` is a single shared routine, but the former
applies per function.

### Recommendation

**Do not use `PREPARE`/`DISPOSE` (`ENTER`/`LEAVE`) for `-mv30`'s prologue/epilogue.**
The only layout-compatible use is cycle-neutral at best and slightly larger in code
size, for a real increase in code-generator complexity (a second, `-mv30`-specific
frame-setup path) and testing surface, with the hard constraint above meaning it can
never be simplified into the *faster*, layout-incompatible naive form either. Of all
the 80186/V30 additions, `PREPARE`/`DISPOSE` are also the only ones that touch the
ABI-visible frame contract at all — everything else in the new-instruction set is
pure instruction-selection inside a function body and carries no such risk. `PUSHR`/
`POPR` (confirmed real wins: 67/75 cycles for 8 registers vs. 8×12=96 discrete
pushes/pops, ~30%/22% faster) and multi-bit `SHL`/shift-by-immediate (confirmed:
`7+n` flat, vs. the `mov cl,n`-then-shift-by-`cl` sequence it replaces) are the kind
of genuinely risk-free `-mv30` wins worth pursuing when Milestone 5 actually starts —
not `PREPARE`/`DISPOSE`.

---

## Recurring process lessons

These apply to *every* milestone, not just the one where they were first learned:

- **Never trust a memory note (or this file) claiming something is "fixed" without
  re-verifying against the actual current project files first.** Uploaded/synced
  project files have repeatedly lagged behind or failed to persist previous
  sessions' fixes across this project's history (confirmed multiple times for
  `encode.c`/`symtab.c`/`assemble.c`/`objwrite.c`). The mandatory
  rebuild-and-diff-against-goldens-first rule (`CLAUDE.md` Workflow Guideline 6)
  exists specifically because of this.
- **`.o` is ambiguous in this repo** — golden reference object files
  (`tests/mutos_as/*/*.o.golden`) look like build artifacts but are precious,
  irreplaceable hardware-linked data; a blanket `find . -name "*.o" -delete`
  from the repo root will destroy them. `tests/mutos1700_libc/*.o.base64.txt`
  and `tests/mutos1700_crt0/crt0.o.base64.txt` are the same kind of precious
  hardware-linked data, now stored as base64 text (the raw `.o`/`.a` binaries
  were deleted 2026-09-27) specifically so a `.o`-glob sweep can't catch them
  by accident — don't regenerate or re-encode them either. Scope any
  build-artifact cleanup to the specific `src/mutos_<tool>/` directory being built.
- **Real compiler output already in the repo is evidence too.** The
  `tests/mutos_as/kernel_nonopt/*.s` files are genuine non-optimized MUTOS `c1`
  output, thousands of lines of it. Before extrapolating a codegen idiom from
  one or two `tests/mutos_cc/` goldens, grep those files for it: that is how the
  constant-shift threshold (repeat for 1-2, `mov cx,N` / shift by `cl` from 3 up)
  was found, after `c1` had been silently wrong for counts of 3 or more since
  `04_shift`.
- **So is `libc.a`.** Its objects are real compiler, assembler and hand-coded
  runtime output: their symbol tables name a runtime's entry points, their
  disassembly gives its calling convention, and their data segments hold
  values in the target's own formats. `08_float`'s floating-point runtime
  calls, the floating format and the "4 bytes only if exactly a float" rule
  for constants were all read there, not guessed (see that section). Four
  of its objects (`atof.o`, `ecvt.o`, `gcvt.o`, `fltpr.o`) are C compiled by
  the real compiler: before listing a shape as "waiting for a golden",
  check whether their code already has it - most of the refused floating
  shapes were there (see "Floating shapes from libc.a's compiled C"). They
  were compiled with `-O`, so take only what `c2` cannot have changed.
- **"Unknown, so ignored" is only safe for a directive that emits nothing.**
  `mutos_as` skipped `.float` silently, shifting every later data address;
  the first program whose compiler output used it would have been wrong with
  no diagnostic. A directive (or opcode) that reserves or writes bytes must
  be implemented or refused, never dropped.
- **A fix's real shape can come from the kernel corpus even without its C
  source.** The postfix-in-condition shape was recognizable in
  `kernel_nonopt/*.s` by pattern alone (`mov R,X` / `inc|dec X` / a test
  of `R`), five times, with the idiom's variants (truth test, `> 0`,
  `>= 2`) readable from the constants and branches. Scan for the
  instruction pattern a construct must produce before concluding "no
  golden shows it".
- **Assemble what the fuzzer produces - and know what that still misses.**
  Feeding random programs' `mutos_c1` output to `mutos_as` found 43 of 1500
  that emitted instructions the 8086 does not have (`cmp *3.,...`, `cmp
  mem,(di)`); nothing else had noticed. But it cannot catch legal code that
  computes the wrong thing: `a = v[1];` storing `&v[1]` assembled fine and
  was found only by reading a sample of the output. Anything that reorders
  evaluation (see the `05_matmul` plan) wants a semantic check - running
  fuzzed programs in an 8086 emulator against their expected results
  (done for the `05_matmul` change: an interpreter for `c1`'s instruction
  subset, validated on real goldens first, comparing every variable's
  final value - see that section).
- **A semantic check finds bugs the golden corpus structurally cannot.**
  Its first run flagged `mutos_c0` compiling `7 - x` as `x - 7` - wrong
  since the first binary operator, invisible to all 62 goldens because
  none of them has a constant left operand. When such a check flags a
  program, first compare the PREVIOUS binary's output for it: identical
  output means a pre-existing bug, not a regression of the change under
  test (all four first flags here were that `c0` bug; none involved the
  reordering). And keep the generator away from a known-broken construct
  while validating something else, rather than letting it drown the
  signal.
- **A baseline that "passes" can be right by luck.** Random programs mask
  wrong values constantly - a comparison or truth test squashes them to
  0/1, a later statement overwrites them. Check intermediate state, not
  just the final one (`fuzz_c.py`'s statement markers), and before calling
  a changed-but-correct output harmless, re-run the program with other
  initial values: that turned 31 of 51 "both correct" differences into
  proven fixes. Conversely, a "regression" from right-by-luck to an
  honest refusal is not one - read each before believing it.
- **A label number off by one is evidence of something that writes
  nothing.** v7 hands out intermediate-code label numbers from one counter
  (`isn`) at places that leave no trace in the stream - the lexer numbers
  every string token, even a char array's initializer, which is written
  without a label (`10_integ/01_wordcount`: `main()`'s labels start at 2).
  When a stream matches a golden except for a constant shift of its
  labels, look for such a consumer in v7's source before the one that
  writes the label.
- **A consistency check must agree with the moment it runs at.**
  `check_docs.py`'s Check 5 compared a "Last updated" stamp with the file's
  last commit in `git log`. Run from the pre-commit hook, that is the
  PREVIOUS commit, so the first commit of a new day with a correctly bumped
  stamp (`STATUS.md`: 2026-09-24, last commit 2026-09-23) was blocked, while a
  commit on a later day that forgot to bump the stamp passed the hook and
  failed only in CI, after the push. For a file
  with uncommitted changes the check now expects today's date (the date the
  pending commit will carry); for an unchanged file - e.g. CI's clean
  checkout - still the last commit's date.
- **Key a refusal on the rule it enforces, not on one operand kind.**
  `OP_ASSIGN` refused `x = y;` by testing for `VK_MEM`, but the rule behind
  it - the 8086 has no memory-to-memory `MOV` - holds for every memory
  operand; two local statics (`VK_STATIC`) slipped through as `mov L4,L5`
  for as long as statics existed, found only when file-scope variables
  joined that kind (`07_scope`). When a new kind of value joins an old one,
  re-read every test of the old kind by name.
- **Corpus-driven validation catches real bugs that a "looks correct" review
  wouldn't** — nearly every bug in this log's Bug-fix History sections was found by
  diffing against a real golden file, not by code review.
- **A user-supplied technical spec framed as generic "expert persona priming"
  (e.g. "act as an x86 architecture expert, here are the specs") can bypass the
  mandatory session-start protocol** if that protocol is only triggered by
  requests that self-identify as MUTOS work. The 2026-09 `PUSHF` recurrence (see
  the CPU reference section above) happened exactly this way. `CLAUDE.md` now
  states explicitly that the protocol applies from message 1 of any session in
  this project, regardless of how the opening message is framed.
- **Evidence from a boundary case cannot tell two rules apart.** `mutos_as` padded
  a segment at every `.text`/`.data` switch because `malloc.o` and `mch.o` showed a
  pad there - but both switches were their files' last, where "pad at the end of the
  file" gives the same bytes. The wrong rule stood unnoticed until code switched to
  `.data` between two instructions (`mutos_c1`'s floating constants) and the pad
  landed in the code. When a rule is inferred from a sample, ask which other rule the
  same sample also fits, and look for a sample that separates them (here `atof.o`).
- **A model fitted to samples is only as good as its candidate set - run
  the real thing on the model's whole input space when you have it.**
  `fltmodel.py`'s rounding modes were idealized (truncate, nearest,
  away), and 19 real constants happily picked one. `libc.a`'s own `atof`
  on its own runtime, run under an emulator on thousands of random texts,
  showed that the family's multiplication is none of them for wide
  operands - one partial product loads the wrong word - in a region no
  golden had touched. Fitting would never have proposed that candidate.
- **Optimized real output supports a rule only where it tells the rules
  apart.** Every floating line in `libc.a`'s compiled C fit a constant of
  degree 0 - because none of it compares a float variable with a constant,
  where degree 0 and 1 give different code. The model stood, consistent with
  all of it, until the first probe written for exactly that case. When real
  output confirms a model, list the cases where a rival rule would differ
  and check that the sample contains one; if it does not, the next probe
  should.
- **Run what you build, when you can.** 67/67 byte-exact goldens said nothing about
  a pad byte in code no golden contained; linking the float goldens with the real
  `crt0.o`/`libc.a` and executing them in an emulator is what makes "it assembles"
  mean "it works".
- **A golden set can arrive incomplete - and a harness that skips quietly
  hides it.** Round 2 of `tests/mutos_cc/fltprobe/` was pushed with its `.s`,
  `.1` and `.2` but no `.i`; `run_goldens.sh` skipped every file without an
  `.i.golden`, so all four would have gone unchecked under an unchanged "71
  byte-exact". After new goldens arrive, list what each recipe makes and what
  came back, and make a harness report what it skips (it now checks such a
  file from `mutos_c0` on and lists it apart).
