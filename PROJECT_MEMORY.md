# MUTOS 1700 Cross-Compiler Toolchain — Projekt-Memory (Export)

> Backup/Snapshot des Claude-Project-Memory, Stand 2026-09-16. Dient als
> repo-gehostete, versionierte Sicherung unabhängig vom Zustand des
> Claude-internen Memory-Systems. Die technische Referenz bleibt weiterhin
> `CLAUDE.md` / `STATUS.md` / `docs/DEVLOG.md` — dieses File ist ein
> Backup, keine dritte Quelle der Wahrheit.

## index.md
MUTOS 1700 Cross-Compiler Toolchain — Clean-room C11 reimplementation of the
MUTOS 1700 vintage cross C compiler toolchain.

## overview.md

### Purpose
- Building the MUTOS 1700 Cross C Compiler — a clean-room, modern C11
  reimplementation of a vintage compiler toolchain
- Validated byte-for-byte against real-hardware-generated corpus files
- Hosted at github.com/apollo630/mutos1700_c_cross_compiler

### Session-start protocol (mandatory)
- At the start of any MUTOS project session, read these files before doing
  anything else — do not rely on memory for technical depth
- CLAUDE.md — architecture, conventions, repo URL
- STATUS.md — current verified status per milestone
- docs/DEVLOG.md — CPU/encoding reference, bug-fix history, debugging methodology

### Milestone structure (current numbering)
1. mutos_ld — linker
2. mutos_as — cross-assembler (provisionally complete)
3. mutos_cpp — C preprocessor (complete for real corpus)
4. mutos_cc / mutos_c0 / mutos_c1 — C compiler (next up)
5. Optimizer c2 and NEC V30 support (-mv30)

### Domain terminology
- temp1 / temp2 — intermediate binary streams
- mutos_as-syntax assembly: `*N.` for immediates, `|comment` annotations
- outcode() — tagged stream writer, format chars B / N / S / 1 / 0
- Opcodes: SYMDEF, PROG, EVEN, RLABEL, SAVE, SETREG, SETSTK, RETRN, RFORCE
- STAUTO — starting auto/local variable offset
- cfunc() / funchead() — V7 cc algorithmic references only
- sloc, retlab
- NCPS — 8 significant chars per symbol
- chkstk

### Tools and resources
- GitHub repo: github.com/apollo630/mutos1700_c_cross_compiler
- Key project docs: CLAUDE.md, STATUS.md, docs/DEVLOG.md, docs/MUTOS_C_ABI.md,
  src/mutos_cc/README.md, src/mutos_cpp/README.md
- Test infrastructure: tests/mutos_cc/run_goldens.sh, tests/mutos_cpp/run_goldens.sh
- Reverse engineering tools: `od -c`, `od -t x1z`

## working-practices.md

### Approach and patterns
- Validation standard: byte-exact match against real-hardware-generated
  corpus files at every intermediate stage
- Mandatory doc-sync: CLAUDE.md, STATUS.md, and docs/DEVLOG.md must be
  updated after each significant session
- Reference discipline: V7 cc is consulted for algorithmic understanding
  only; implementation is always clean-room
- Core constraint: v7/cc/ is a historical algorithmic reference only —
  never copied wholesale (CLAUDE.md Workflow Guideline 3)
- Milestone 3 (mutos_cpp) golden regeneration: use exactly
  `cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE <name>.c`; verify via
  tests/mutos_cpp/run_goldens.sh; full spec at src/mutos_cpp/README.md;
  man page at man/mutos_cpp.1

### Key learnings and principles
- PUSHF behavior does NOT distinguish 8086 from 80186 — both set flag
  bits 12–15 to 1 on the stack identically; shift-count masking (AND 1Fh)
  is the actual distinguishing behavior. (Confirmed a second time on
  2026-09-16 — see docs/DEVLOG.md's CPU reference section and "Recurring
  process lessons" for the recurrence and its root cause.)
- Byte-level reverse engineering (`od -c` / `od -t x1z`) of wire formats
  against hardware goldens is the proven methodology for discovering
  MUTOS-specific compiler deltas
- All technical facts, bug history, and CPU/encoding details belong in
  docs/DEVLOG.md, not in memory

## current-state.md

### Milestone 4 (mutos_cc) — current state
- Implementation complete for the smoke-test phase
- mutos_c0 and mutos_c1 built in src/mutos_cc/ with supporting Makefile,
  README.md, and golden test runner (tests/mutos_cc/run_goldens.sh)
- All three 00_smoke golden files pass byte-exact end-to-end verification
  (.i → .1 → .2 → .s)
- Full 62-file corpus run: 3 byte-exact matches, 59 explicit
  "not yet supported" diagnostics, zero genuine mismatches
- CLAUDE.md, STATUS.md, and docs/DEVLOG.md updated per mandatory doc-sync rules

### Confirmed MUTOS-1700-specific deltas from vanilla V7 cc
- Discovered via byte-level reverse engineering of the temp1/temp2 wire
  format against 00_smoke goldens
- STAUTO = -4 (not -6)
- Extra EVEN opcode between PROG and RLABEL in cfunc()
- RETRN carries an extra return-type argument
- Initial SETREG budget of 4 (not 5)

### Open questions
- chkstk invocation threshold not yet pinned

### On the horizon
- Expanding Milestone 4 corpus coverage: resolving the 59
  "not yet supported" cases
- Milestone 5: optimizer c2 and NEC V30 support (-mv30)
