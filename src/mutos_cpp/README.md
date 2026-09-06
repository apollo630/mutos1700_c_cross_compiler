# mutos_cpp - MUTOS 1700 Cross C Preprocessor

`mutos_cpp` re-implements the observable behavior of the real MUTOS 1700 /
Unix V7 "fast cpp" (`v7/cpp/cpp.c`, written by John F. Reiser, July/August
1978 - see `v7/cpp/README` for the original documentation) from scratch in
modern C11. It is **not** a line-by-line port of the original K&R,
pointer-arithmetic-heavy source (which relies heavily on 1978-era buffer
tricks that are neither necessary nor desirable on a modern host); it is a
clean-room implementation validated **byte-for-byte** against real golden
reference output, matching this project's approach for `mutos_as`/`mutos_ld`.

Status: **5/5 golden tests byte-identical** (see `STATUS.md` for the
currently-verified details). Golden references were generated on real
MUTOS 1700 hardware via `cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE <name>.c`;
`cc`'s `-P` ("stop after preprocessing") is forwarded verbatim to `cpp`
(see `v7/cc/cc.c`'s option parsing), where `-P` means "suppress
`# N \"file\"` line markers" - which is why none of the golden `.i` files
contain any such markers, and why `mutos_cpp` never emits them either.

## Behavioral spec (reverse-engineered from `v7/cpp/cpp.c` + `cpy.y` + `yylex.c`)

This is a K&R-era preprocessor, not an ANSI/ISO one. Notable consequences:

- **No `#elif`.** Only `#if`/`#ifdef`/`#ifndef`, a single optional `#else`,
  and `#endif`.
- **No `#` (stringize) or `##` (token-paste) operators.**
- **`#if` identifiers are a defined-or-not boolean test, not macro-value
  substitution.** A bare identifier in a `#if` expression evaluates to `1`
  if it is currently `#define`d and `0` otherwise; it is *not* replaced by
  its macro value first. `defined(NAME)` / `defined NAME` evaluate to that
  same `0`/`1` - in this variant's grammar they are genuinely redundant
  with a bare identifier, not just similar. This is a real, deliberate
  divergence from ANSI C's `#if`, inherited directly from `yylex()`.
- **Directives are recognized only when `#` is the very first character of
  a physical line** (no leading whitespace tolerated) - a faithful
  reproduction of `cpp.c`'s `cotoken()`/`prevlf` logic, not a
  simplification. Unobserved either way by this project's current test
  corpus (no indented directives exist in it), but it is the historically
  accurate choice, so it's what this implementation does.
- **Comments are uniformly removed**, but a comment always terminates a
  token, so `foo`, comment, `bar` outputs the literally-adjacent text
  `foobar` while still treating `foo` and `bar` as independently
  macro-eligible tokens (matching `v7/cpp/README`'s own documented
  example). Comments contribute **zero** characters of their own text -
  not even a separating space.
- **A comment's embedded newlines are *always* individually emitted to
  output, unconditionally** - even inside a currently-false
  `#ifdef`/`#ifndef`/`#if` region, where every other kind of output is
  otherwise completely suppressed. This is a genuinely surprising, easy
  -to-miss quirk: the reference `cpp.c`'s comment-skipping loop calls
  `putc('\n', fout)` directly for each embedded newline, bypassing the
  normal `flslvl`-gated suppression path (`dump()`) entirely. Confirmed
  against `tests/mutos_cpp/c/mch.c`, which has multi-line comments inside
  a false `#ifdef M1834` block (M1834 undefined in this project's golden
  fixtures) whose internal newlines still show up as blank lines in the
  golden output.
- **A false `#ifdef`/`#ifndef`/`#if` body outputs nothing at all - not
  even blank placeholder lines** (per `v7/cpp/README`'s explicit
  "Stylistic choice" section), *except* for the comment-newline quirk
  above. This project's implementation reproduces the exact accounting
  rule (see `src/mutos_cpp/directive.c`'s file header comment): a
  directive line contributes exactly one blank output line
  if-and-only-if the conditional-active state was true *immediately
  before* that specific directive's own effect is applied (its
  "pre-state"), which correctly reproduces every case checked so far,
  including a false `#ifdef ... #endif` block (no `#else`) of any body
  size collapsing to exactly one blank line total, and an `#else` that
  flips a false branch to true contributing no blank line of its own
  while the newly-active body that follows is output normally.
- **Object-like macro bodies discard exactly one separating character**
  (typically a space or tab) immediately after the macro name before the
  body text starts; **function-like macro bodies keep everything**
  starting right after the closing `)` of the formal-parameter list, with
  no trimming. Confirmed via `param.h`'s `major(x)` macro, whose body's
  own leading tab is preserved verbatim at every call site in the golden
  output.
- **Backslash-newline line continuation** is spliced out of the input
  stream transparently, everywhere (this project's `source_getc()`
  handles it at the lowest possible layer, so every higher-level piece of
  code - directive parsing, macro bodies, `#if` expressions, ordinary
  text - gets it "for free" and can otherwise ignore it entirely). Not
  exercised by the current golden corpus (no backslash continuations
  exist in it), but implemented for correctness/completeness.
- **Self-referential macros are protected against infinite recursion**
  via a standard "blue paint" / hide-set: a macro is marked `hidden` for
  exactly the lifetime of its own expansion's pushback buffer, so
  `#define a a` terminates (`a` stays literal on rescan) instead of
  looping forever. This reproduces the *effect* (no infinite loop) of the
  original's `maclvl`/`macforw`/`symsiz`-based recursion counter without
  replicating its incidental, fixed-budget-specific mechanics - not
  exercised by the current golden corpus either way.

## Known, documented simplifications (not exercised by the current corpus)

- **Formal parameters embedded inside a string/char literal within a
  macro's own definition are not recognized/substituted.** The real
  reference `cpp` has a special quote-aware scan specifically for this
  (see `v7/cpp/README`'s `#define foo(a) '\a'` example); this
  implementation treats such literals as opaque text during body capture.
  No macro in this project's real MUTOS kernel corpus does this.
- **A function-like macro name not immediately (mod whitespace/comments/
  newlines) followed by `(` is left as a plain, unexpanded identifier.**
  The reference `cpp` instead expands it anyway (with all-empty actual
  arguments) accompanied by a warning - a real but obscure K&R-era
  quirk. This implementation uses the more conventional/modern behavior
  instead. No real corpus code relies on the original quirk.
- **A multi-line macro *call*'s embedded raw newlines are captured as
  part of whichever actual argument they fall within** (matching the
  reference's own argument-collection behavior, which suppresses output
  during actuals-scanning), rather than being separately emitted. Not
  exercised by the current corpus (no macro call spans multiple physical
  lines in it).
- No default system include directory (nothing resembling `/usr/include`)
  is baked in; only `-I` directories and the including file's own
  directory (for `"..."`-style includes) are searched. Every golden
  fixture uses relative `"../h/foo.h"`-style includes that resolve
  without needing one.

## Source layout

- `mutos_cpp.h` - shared types and declarations.
- `util.c` - small shared helpers (identifier/whitespace scanning, string
  utilities).
- `source.c` - the nested source stack: real files (top-level input and
  `#include`) plus in-memory "pushback" buffers (macro-expansion results
  re-scanned exactly like ordinary text). Pushback (`ungetc`) is scoped
  **per source frame**, not globally - see the file's header comment for
  why that distinction is load-bearing (a global pushback stack causes a
  peeked-but-not-yet-consumed character from the *parent* source to
  incorrectly "leak" ahead of a macro expansion's freshly-pushed buffer).
- `macro.c` - the macro table, `#define`/`#undef` processing (including
  formal-parameter recognition and body-piece constructions), and
  macro-call expansion (argument collection, substitution, recursion
  guard).
- `ifexpr.c` - the `#if` constant-expression evaluator (precedence
  climbing matching `v7/cpp/cpy.y` exactly).
- `directive.c` - dispatches every control line (`#define`, `#undef`,
  `#include`, `#ifdef`/`#ifndef`/`#if`/`#else`/`#endif`, `#line`) and
  implements the output-line accounting rule described above.
- `scan.c` - the single flat top-level scanning loop. It does not
  recurse for `#include` (the new file is simply pushed onto the source
  stack and the very next loop iteration starts pulling from it) or for
  macro expansion (the substituted text is pushed as a new source buffer
  and re-scanned by this same loop) - both "just work" via the source
  stack abstraction.
- `main.c` - CLI entry point (`-D`, `-U`, `-I`, `-P`, `-C`, `-R`, `-o`,
  positional `infile [outfile]`) and diagnostics.

## Testing

`tests/mutos_cpp/run_goldens.sh` preprocesses every `*.c` file in
`tests/mutos_cpp/c/` with the exact flags the golden references were
generated with, and diffs the result byte-for-byte against `<name>.i.golden`.
