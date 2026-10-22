# `STATUS.md` history — Milestone 3 (`mutos_cpp`), the re-implementation

Sections moved here from `STATUS.md` on 2026-10-22, unchanged: the records of the from-scratch `mutos_cpp` (`util.c`, `source.c`, `macro.c`, `ifexpr.c`, `directive.c`, `scan.c`, `main.c`, `mutos_cpp.h` - deleted 2026-10-22, the last version in commit `03b72f7`), which re-implemented the observable behaviour of the original and was replaced by a port of the original binary. Each was true when written ("this session" means that session); the current state is in [`STATUS.md`](../../STATUS.md), the narrative in [`docs/devlog/`](../devlog/). Where these records describe the original `cpp` (the "simplifications" below, the line accounting), the port's reading of the binary has the final word (`src/mutos_cpp/README.md`).

---

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
