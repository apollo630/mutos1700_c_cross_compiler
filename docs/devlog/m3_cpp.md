# Milestone 3 — `mutos_cpp` (C preprocessor)

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). The preprocessor: golden generation and the two bugs worth remembering.

---

**Since 2026-10-22 `mutos_cpp` is a port of the original binary** - see
`originals_and_ports.md`, "The port of `cpp`", and `src/mutos_cpp/README.md`. This
part records the re-implementation it replaced (deleted that day, the last version in
commit `03b72f7`; its `STATUS.md` records are in `docs/history/status_m3_cpp.md`): it
was complete for the real corpus — 5/5 golden files byte-for-byte identical, 0
ASan/UBSan errors. What follows was true of it when written; where it describes the
original `cpp`, the port's reading of the binary has the final word.

**Golden generation command** (critical, easy-to-lose detail): the reference `.i`
files were generated on real MUTOS 1700 hardware via exactly:

```
cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE <name>.c
```

These specific defines are required to reproduce byte-exact output — `mch.c`'s
conditional branches resolve differently without them.

## The two bugs worth remembering

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

## Core output-line-accounting rule

A directive line (`#define`/`#undef`/`#include`/`#ifdef`/`#ifndef`/`#if`/`#else`/
`#endif`/`#line`) contributes exactly one blank output line **iff the
conditional-active state was true immediately *before* that directive's own effect
is applied** (its "pre-state") — this single rule correctly collapses a false
`#ifdef ... #endif` block of *any* body size to exactly one blank output line, and
correctly handles `#else` transitions in both directions. See
`src/mutos_cpp/directive.c`'s file header comment for the full derivation.
