# MUTOS 1700 Cross-Compiler Toolchain — Status

This document tracks the current functional status of the toolchain components. It is
meant to be re-verified, not trusted: per the project's core methodology, no fix or
feature is considered real until it has been rebuilt and diff-checked byte-for-byte
against a real hardware-linked golden reference file. Sections below are marked as
either **verified this session** (rebuilt and diffed against goldens as part of writing
this document) or **carried from prior session records** (not re-checked here — treat
with the same skepticism the project applies to any unverified claim).

Last updated: 2026-09-18.

---

## Top-level build (verified this session)

A repo-root `Makefile` now exists, fulfilling `CLAUDE.md`'s standing Build
Requirement ("one top level Makefile to build all 4 components") — an open
item since Milestone 1, not specific to any single milestone. `make`/`make
all` builds `mutos_ld` (compiled directly; it has no sub-Makefile of its
own yet), and delegates to each of `mutos_as`/`mutos_cpp`/`mutos_cc`'s own
`src/mutos_<tool>/Makefile`. `make test` runs every component's own
golden-diff suite from one place (`mutos_as`'s `kernel_nonopt`/`kernel_opt`,
`mutos_cpp`'s, and `mutos_cc`'s — see each milestone's section below for
current results). Verified this session via a full `make clean && make all
&& make test` from a clean checkout.

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

**Status: PROVISIONALLY COMPLETE** for the real corpus; re-verified fresh this session
(rebuild + full regression + sanitizers, independent of the prior session's own
verification — see below). "Provisional" because, per this project's core rule, no
milestone is ever declared permanently closed: it stays open to correction the moment
new real hardware evidence (e.g. a V30-targeted golden `.o`) surfaces. Functionally,
this milestone's work is done and Milestone 3 has begun on top of it.

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
byte-exact, end-to-end, for 8/62 of the full corpus: `tests/mutos_cc/
00_smoke`'s 3 files plus `tests/mutos_cc/01_expr/01_intarith.c`,
`02_bitwise.c`, `03_rellogic.c`, `04_shift.c` and `05_incdec.c`. ABI/
calling-convention research is done, the `c0`/`c1` process split is
confirmed as a deliberate design decision, the K&R test corpus now
has full-corpus goldens (all 62 files across all 11 categories) present in
this checkout, and its real-hardware golden-generation pipeline is confirmed
working end-to-end.**
`src/mutos_cc/` now exists — see `src/mutos_cc/README.md` for full detail.

### `mutos_c0`/`mutos_c1`: verified this session (byte-exact, `00_smoke` + `01_intarith` + `02_bitwise` + `03_rellogic` + `04_shift` + `05_incdec`)

Built `mutos_c0` (lexer, diagnostics, the `temp1`/`temp2` stream writer, a
symbol table, and a front-end driver) and `mutos_c1` (the stream reader and
a code generator with an operand-kind-aware value stack) in `src/mutos_cc/`
— a from-scratch, modern-C11 reimplementation per this project's
established approach (`/v7/cc/` used as an algorithmic reference only, per
CLAUDE.md Workflow Guideline 3, not copied wholesale).

**Verification: the real, unmodified pipeline — `<n>.c` → real
`mutos_cpp -P` → `mutos_c0` → `mutos_c1` → `<n>.s` — was run for all 3
`tests/mutos_cc/00_smoke/` files plus `01_expr/01_intarith.c`,
`02_bitwise.c`, `03_rellogic.c`, `04_shift.c` and `05_incdec.c`, and every
intermediate artifact (`.i`,
`.1`, `.2`, `.s`)
matches its real-hardware golden byte-for-byte.** Re-run via
`tests/mutos_cc/run_goldens.sh` (also wired into the top-level `Makefile`'s
`test` target), which covers the full 62-file corpus and reports every file
outside current grammar/opcode coverage as an explicit, expected "not yet
supported" diagnostic (nonzero exit, clear message) — distinct from a
genuine byte mismatch, so the script's pass count is an honest,
non-inflated measure of verified coverage. Current full-corpus result: 8
byte-exact end-to-end, 54 "not yet supported" (expected), **0 genuine
mismatches anywhere** (`.i`, `.1`, `.2`, or `.s`).

Reverse-engineering the real `temp1`/`temp2` wire format (byte-level, via
`od`/hexdump, against the `00_smoke`, `01_intarith`, `02_bitwise`,
`03_rellogic`, `04_shift` and `05_incdec`
goldens) surfaced **four confirmed MUTOS-1700-specific deltas from vanilla
V7 `cc`** (found against `00_smoke`), the full local-variable wire format
(found against `01_intarith`), the bitwise-operator/immediate-marker
findings below (found against `02_bitwise`), the deferred-comparison/
short-circuit codegen findings below (found against `03_rellogic`), the
shift-codegen findings below (found against `04_shift`), and the
increment/decrement/pointer/array findings below (found against
`05_incdec`), each
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
6. The `SETSTK`-vs-`.s` local-frame threshold is now implemented for real
   (not just bounded): `extra <= 76` bytes emits a plain `"sub sp,*N."`
   (confirmed via `01_intarith`'s `extra=6` case), `extra > 256` emits
   `"mov ax,*N. / call chkstk"` (per `docs/MUTOS_C_ABI.md` sect. 1.9, not
   yet confirmed against its own golden), and the unconfirmed `(76,256]`
   gap remains an explicit "not yet supported" rather than a guess.
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

**Current grammar/opcode scope (deliberately narrow, by design — see
`src/mutos_cc/README.md`):** function definitions with no parameters; a
body of `int`/`int *`/single-dimension `int[N]` local declarations (no
initializers) followed by `name = expr;` assignment, `*<ptr-expr> = expr;`
dereferenced-pointer assignment, and/or `return` statements; expressions
over `+ - * / % & | ^ ~ < <= > >= == != && || ! << >>` (unary/binary as
applicable), postfix/prefix `++`/`--` (pointer-scaled where applicable),
parens, integer
constants, and variable references (with array-to-pointer decay on an
array name used as an rvalue), constant-folded at parse time exactly
like real K&R `cc`'s own per-operation `build()`-time folding, with a real
`NAME`/operator tree emitted the moment a variable is involved. Everything
else (compound-assignment operators, non-`int` element types, array
subscripting, multi-level pointers/multi-dimensional arrays, explicit `&`
outside array decay, function parameters, control flow, memory-to-memory
assignment, an immediate `IMUL`/`IDIV` operand) is an explicit "not yet
supported" error, not silently-wrong output — confirmed via a direct test
against `01_expr/06_compasgn.c` (correctly rejected with a clear message,
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

### ABI research (carried from prior session records — 2026-09-06)

Before writing any `mutos_c1` code generation logic, the real MUTOS 1700 function
calling convention and C runtime startup/cleanup behavior were reverse-engineered
byte-for-byte from real hardware-linked objects: `tests/mutos1700_crt0/crt0.o` and
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
a `chkstk` stack-overflow guard for large local frames (threshold empirically
bounded to `(76, 256]` bytes, not pinned down further); and the real `crt0` →
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
per `tests/mutos_cc/`'s own increasing-difficulty ordering — `06_compasgn`
next (`+= -= *= /= %= &= |= ^= <<= >>=`), then `07_ternary` (`?:`) and
`08_castsize` (casts/`sizeof`) to finish `01_expr`. See
`src/mutos_cc/README.md`'s "Next
steps" for the full, dependency-ordered plan through `long`/full
arrays-and-pointers (subscripting, multi-level)/structs, `03_ctrlflow`,
function calls, the `09_abiprobe`
`chkstk`-threshold goldens, and the `mutos_cc` driver itself.

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
4. Begin Milestone 4 (`mutos_cc`/`mutos_c0`/`mutos_c1`) on top of the now-complete
   `mutos_cpp`. `v7/cc/` is the reference source tree.
5. If a real MUTOS source file ever surfaces that exercises one of `mutos_cpp`'s
   documented simplifications (a formal parameter embedded in a macro-body string
   literal, a function-like macro name not immediately followed by `(`, or a macro
   call whose argument list spans multiple physical lines), re-check that specific
   behavior against it — see `src/mutos_cpp/README.md`'s "Known, documented
   simplifications" section for exactly which three cases these are.
