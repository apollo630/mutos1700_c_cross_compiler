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
4. [Recurring process lessons](#recurring-process-lessons)

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
- Default base is **decimal**, not octal as `Assembler_as.pdf`'s V7/PDP-11 wording
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
  (confirmed by reading Assembler_as.pdf Anlage C directly: their encoding is the
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

A full systematic audit (not just corpus-reactive fixing) was done by unzipping
`Assembler_as.pdf` (it's a zip archive of scanned page images *plus* per-page OCR
`.txt` files — extract and read those directly for the full Anlage A/B/C tables) and
cross-checking every documented mnemonic against `encode.c`'s dispatch table one by
one. Found and fixed ~30 real gaps in one pass, including `not`/`notb`, `idiv`/
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
Soviet 1:1 clone of the *base* 8086 without V30/80186 opcodes — `Assembler_as.pdf`
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
- **Corpus-driven validation catches real bugs that a "looks correct" review
  wouldn't** — nearly every bug in this log's Bug-fix History sections was found by
  diffing against a real golden file, not by code review.
