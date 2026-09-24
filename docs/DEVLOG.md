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

**Status:** provisionally complete for the real corpus — 67/67 golden object files
byte-for-byte identical (full file: header + text + data + trel + drel + symtab), 0
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
`LEA`=`8D /r`; `MUL`/`NEG`/`DIV`/`IMUL`/`NOT`/`IDIV` = group3 `F6`/`F7` `/4`/`/3`/`/6`/
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
ended at an odd offset — this rule applies symmetrically at `.text`↔`.data`
transitions and at end-of-file.

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
switch-for-switch parity with the man page here.

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

Real hardware-linked evidence, primarily two sources: `tests/mutos1700_crt0/crt0.o`
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
and what `mutos_c1` now does with it.

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

So the threshold is in `(80,128]`, and the `chkstk` form's immediate follows
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
  (`tests/mutos1700_libc/*.o`, `tests/mutos_as/*/*.o.golden`) look like build
  artifacts but are precious, irreplaceable hardware-linked data. A blanket
  `find . -name "*.o" -delete` from the repo root will destroy them; scope any
  build-artifact cleanup to the specific `src/mutos_<tool>/` directory being built.
- **Real compiler output already in the repo is evidence too.** The
  `tests/mutos_as/kernel_nonopt/*.s` files are genuine non-optimized MUTOS `c1`
  output, thousands of lines of it. Before extrapolating a codegen idiom from
  one or two `tests/mutos_cc/` goldens, grep those files for it: that is how the
  constant-shift threshold (repeat for 1-2, `mov cx,N` / shift by `cl` from 3 up)
  was found, after `c1` had been silently wrong for counts of 3 or more since
  `04_shift`.
- **Assemble what the fuzzer produces - and know what that still misses.**
  Feeding random programs' `mutos_c1` output to `mutos_as` found 43 of 1500
  that emitted instructions the 8086 does not have (`cmp *3.,...`, `cmp
  mem,(di)`); nothing else had noticed. But it cannot catch legal code that
  computes the wrong thing: `a = v[1];` storing `&v[1]` assembled fine and
  was found only by reading a sample of the output. Anything that reorders
  evaluation (see the `05_matmul` plan) wants a semantic check - running
  fuzzed programs in an 8086 emulator against their expected results.
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
