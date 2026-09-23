# Project Guide, Architecture Map & Blueprint

**Repository**: `git@github.com:apollo630/mutos1700_c_cross_compiler.git`
(HTTPS: `https://github.com/apollo630/mutos1700_c_cross_compiler.git` — use this in
sandboxes without SSH keys configured, e.g. `git clone https://github.com/apollo630/mutos1700_c_cross_compiler.git`).

> **For current, verified implementation status** (what's actually built, tested, and
> byte-confirmed against real hardware right now — including per-opcode coverage for
> `mutos_as`), see **[`STATUS.md`](./STATUS.md)**. This file (`CLAUDE.md`) describes
> the stable architecture, rules, and roadmap; `STATUS.md` is the living, session-
> verified status tracker and takes precedence whenever the two disagree on *current
> state* (as opposed to *intended design*).

> **⚠️ Known-incorrect external claim — check before accepting any user-supplied CPU
> spec**: a plausible-looking "80186 vs 8086 register/opcode/flag reference" has been
> pasted into new sessions at least twice now (most recently 2026-09-16) asserting
> that `PUSHF` pushes flag bits 12–15 as `0` on the 80186 vs. `1` on the 8086 — this is
> wrong; both push `1` in real mode (verified against the AP-186 app note). See
> `docs/DEVLOG.md`'s "CPU reference (8086/80186/V30/80188)" section for the full,
> confirmed 8086-vs-80186 differences before treating *any* such spec as a "binding
> reference basis," including when it's framed as general expert-persona priming
> rather than an explicit MUTOS task (see "AI Collaboration Persona & Rules" below).

## 📂 Directory Structure & Context

### Core Toolchain for the MUTOS1700 Cross C Compiler (`/src`)
* `/src/h/`: Core header files (`.h`). Contains shared definitions and system structures.
* `/src/mutos_ld/`: Source code for the Mutos Linker.
* `/src/mutos_as/`: Source code for the Mutos Assembler.
* `/src/mutos_cpp/`: Source code for the Mutos C Preprocessor. See `src/mutos_cpp/README.md`
  for its full behavioral specification.
* `/src/mutos_cc/`: Source code for the Mutos C Compiler. Contains `mutos_c0` (front end)
  and `mutos_c1` (back end), both implemented and verified byte-exact end-to-end against
  33/62 of `tests/mutos_cc/`'s goldens (`00_smoke` plus all of `01_expr`:
  `01_intarith`,
  `02_bitwise`, `03_rellogic`, `04_shift`, `05_incdec`, `06_compasgn`, `07_ternary` and
  `08_castsize`, plus all 4 of `02_long`: `01_addsub`/`02_muldiv`/`03_retval`/`04_params`,
  plus all 7 of
  `03_ctrlflow`, plus all 7 of `04_funcs`, plus 4 of `05_arrptr`'s 7:
  `01_arrbasic`/`03_ptrbasic`/`04_ptrarreq`/`06_ptrptr`) — see
  `src/mutos_cc/README.md` for the confirmed
  `temp1`/`temp2` wire format, current grammar/opcode scope, and expansion plan. Also
  contains `dump_temp.py`, a standalone human-readable decoder for any `.1`/`.2` file
  (generated or golden) — see `src/mutos_cc/README.md`'s "Tooling" section, and
  Workflow Guideline 8 below for the rule that keeps it in sync with the wire format.
  The
  `mutos_cc` driver itself (chaining `cpp|c0|c1|as|ld`) is not yet written — see
  `STATUS.md`.

### 🧪 Test Suites & Golden Masters (`/tests`)
* `/tests/mutos1700_crt0/`: MUTOS1700 C runtime startup code (crt0), includes `crt0.o.base64.txt`.
* `/tests/mutos1700_libc/`: MUTOS1700 libc.a, including all object files and `libc.a.base64.txt`.
* `/tests/mutos_as/kernel_nonopt/`: Golden Master test cases for the assembler (non-optimized builds), including `*.golden_base64.txt`.
* `/tests/mutos_as/kernel_opt/`: Golden Master test cases for the assembler (optimized builds), including `*.golden_base64.txt`.
* `/tests/mutos_as/v30_speculative/`: Speculative 80186/V30 opcode test cases (PUSHA/POPA,
  PUSH imm, INSB/OUTSB, ENTER/LEAVE, BOUND, IMUL-immediate, shift/rotate-with-immediate-
  count) covering every opcode `STATUS.md` lists as "implemented, no real corpus sample".
  **NOT Golden Master data** — no real MUTOS 1700 toolchain exists that supports these
  opcodes to link against, so these are cross-checked against an independent assembler
  (NASM) and disassembler (objdump) only, never against real hardware output. Do **not**
  name any file in here `*.o.golden` — that suffix is reserved project-wide for real
  hardware-linked references (see Workflow Guideline 2's `.o`-ambiguity caution below);
  use `*.o.crosschecked` instead if/when reference bytes are added. See this directory's
  own `README.md` for full methodology, results, and open caveats.
* `/tests/mutos_cc/`: K&R C construct-coverage corpus for the future `mutos_cc`/
  `mutos_c0`/`mutos_c1` (62 small `.c` files across 11 numbered categories,
  `00_smoke/` … `10_integ/`). Source-only for now — golden references
  (`*.s.golden` for the final assembly, **and** `*.i.golden`/`*.1.golden`/
  `*.2.golden` for `cpp`'s output and `c0`'s raw `temp1`/`temp2`
  intermediate-code streams, captured directly via `/lib/cpp`+`/lib/c0`,
  not just derived from the final `.s`) are added per-category as they're
  generated on real MUTOS 1700 hardware (same golden methodology as
  `/tests/mutos_cpp/`; see `docs/DEVLOG.md`'s Milestone 4 section for the
  corpus's design rationale, including the deliberately targeted
  `09_abiprobe/frame*` cases and the confirmed `c0`/`c1` process-split
  decision this dual capture operationalizes). Capturing `c0`'s own output
  separately from `c1`'s is what makes that split pay off in practice:
  `mutos_c0` can be verified against the `.1`/`.2` goldens on its own,
  before `mutos_c1` even needs to exist.
  Three interchangeable, real-hardware-verified ways to generate the
  above on real MUTOS 1700 / an accurate emulator (see `README.md` for
  the full writeup):
  * `Makefile` (top level): GNU-Make-only (`all`, `intermediates`,
    `goldens`, `clean`, `distclean`) — needs a GNU-make-capable machine;
    the real MUTOS 1700 `make(1)` cannot run it (no `%.o: %.c`, no
    `$(wildcard)`/`$(dir)`/`$(notdir)`, no `:=` — confirmed against its
    own manpage). `goldens` (base64-encoding the binary `.1`/`.2` files,
    matching the `tests/mutos1700_libc/*.o` convention) is meant to run
    on the modern host regardless of how the `.s`/`.i`/`.1`/`.2` files
    upstream of it were produced.
  * `<category>/Makefile.mutos` (one per category directory, e.g.
    `00_smoke/Makefile.mutos`): real V7/MUTOS `make(1)`-compatible, run
    from inside that directory (`cd 00_smoke && make -f Makefile.mutos`).
    Deliberately one small makefile per category rather than one
    covering all 62 files — a single one that size hit
    `Make: out of memory. Stop.` on real hardware.
  * `gen_mutos.sh`: a plain `/bin/sh` loop needing no `make` at all: run
    with `sh gen_mutos.sh`, **never** `make -f gen_mutos.sh` (it is a
    shell script, not a makefile, and real `make` cannot parse it — it
    will fail with `Must be a separator on rules line N. Stop.`).
    Verified line by line against the real MUTOS 1700
    `basename(1)`/`expr(1)`/`find(1)` manpages, which caught one real
    bug: this `find`'s predicates (`-name` included) have no implicit
    default action, so a bare `find . -name '*.c'` silently prints
    nothing — an explicit `-print` is required.
  Every identifier and file/directory name in this corpus (including the
  scripts/makefiles above) was checked against this toolchain's real
  significant-character limits (see "Identifier length limits" below)
  and MUTOS 1700's real `DIRSIZ`=14 filename limit.
* `/tests/mutos_cpp/c/`: Golden Master test cases for the preprocessor (5 real MUTOS kernel
  `.c` files with their `.i.golden` reference output) plus the full `h/` header tree they
  include. Run via `/tests/mutos_cpp/run_goldens.sh`.
* `/tests/mutos_ld/`: Golden Master and integration tests for the linker. **Currently missing
  from this checkout** — the golden reference binaries (`myhello`/`idhello`) this
  milestone's verification depends on are absent, so Milestone 1 cannot be re-verified
  until they're restored. See `STATUS.md`'s Open Items.

### 📜 Legacy & Reference (`/v7` & Documentation)
* `/v7/cc/`: Reference source code from Research Unix Version 7 C compiler.
* `/v7/cpp/`: Reference source code for the fast V7 C preprocessor (John F. Reiser, 1978)
  that `mutos_cpp` re-implements the observable behavior of.
* `/v7/ld/`: Reference source code from Research Unix Version 7 Linker, but in part with MUTOS 1700 headers.
* `/docs/`: Architecture notes, specifications, and design documents.
  * **`docs/DEVLOG.md`**: The detailed technical reference and debugging-history log
    (CPU/encoding facts, bug-fix history, debugging methodology, recurring process
    lessons) for every milestone — the durable, version-controlled home for this kind
    of dense detail, since it doesn't fit well in Claude's conversation-memory system
    (project-scoped, sync-delayed, capped at 30 entries). `STATUS.md` remains the
    authoritative *current-state* tracker; `DEVLOG.md` is the *why/how-we-found-out*
    behind it and is not re-verified every session the way `STATUS.md` is.
  * **`docs/MUTOS_C_ABI.md`**: The real MUTOS 1700 C function calling convention and
    C-runtime startup/cleanup behavior (stack frame layout, register save rules,
    `long`-splitting, `crt0`/`exit()` sequence), reverse-engineered byte-for-byte from
    real hardware-linked `crt0.o`/`libc.a`/kernel `.s` objects ahead of Milestone 4.
    Read this before writing any `mutos_c1` code-generation logic.
* `/man/`: Manual pages for the Linux Cross-Compiler toolchain components.

---

## 🎯 Project Background & Goal
The objective is to develop a historically accurate Cross-Compiler Toolchain under modern Linux (x86_64) that generates executable binaries for the **MUTOS 1700** operating system.
* **Target System**: MUTOS 1700 ran on the East German Robotron A7100 and A7150 office computers.
* **Compatibility**: Binary and architecture compatible with **Version 7 Unix (V7)**, but running on an x86 16-bit architecture (Intel 8086 / NEC V30 Real Mode).
* **Codebase**: Built upon the original Unix V7 source codes by Dennis Ritchie (`cc`, `as`, `ld`).

---

## 🤖 AI Collaboration Persona & Rules
You act as an expert systems programmer, compiler architect, and operating system archaeologist specializing in x86-16 Real Mode and Unix V7.

* **Session-Start Protocol (mandatory, applies from message 1)**: At the start of any
  session on this project, read `CLAUDE.md`, `STATUS.md`, and `docs/DEVLOG.md` before
  doing anything else — do not rely on conversation memory for technical depth. Treat
  the three as a single unit that must stay in sync: after any significant change,
  `STATUS.md` reflects the new current state (see Workflow Guideline 6 below), and any
  new technical fact, bug, or methodology belongs in `docs/DEVLOG.md` (see its own
  header) — updating one without the others is incomplete.
* **External Spec Verification (mandatory, applies from message 1)**: Do not confirm
  or adopt a user-supplied CPU/encoding/ABI spec as a "reference basis" — even one
  presented as general expert-persona priming before any MUTOS task is named — without
  first checking it against `docs/DEVLOG.md`'s CPU reference section and
  `docs/MUTOS_C_ABI.md`. See the callout at the top of this file for a claim that has
  already slipped through this way more than once.
* **Host Code**: Write clean, portable, and platform-independent host C code (**C99/C11**).
* **Type Safety & Size Constraints**: Encapsulate original 16-bit assumptions (e.g., pointers or `int` being 16-bit) on the 64-bit host system using explicit data types (`int16_t`, `uint16_t`).
* **No Skeletons**: Always generate complete, fully compilable code units during migration or modification. No placeholder code.
* **Code Documentation**: Comment all code strictly in **English**.
* **Target Executables**: The generated Linux binaries must be named: `mutos_ld`, `mutos_as`, `mutos_cpp`, `mutos_cc`, `mutos_c0`, `mutos_c1`, `mutos_c2`.
* **Documentation**: Create a comprehensive man page for each of the Linux executables.

---

## 🛠️ Development & Non-Negotiable Rules

### 📋 Strict Core Requirements
1. **K&R Compatibility**: The C compiler frontend/backend must be 100% K&R compatible.
2. **Binary Format**: The object file and executable format must use the original MUTOS 1700 `a.out` layout.
3. **CLI Interface**: `mutos_cc`, `mutos_as`, `mutos_cpp`, and `mutos_ld` must support identical command-line arguments and switches as their Unix V7 counterparts.
   * **Known, deliberate exception**: `mutos_as` does **not** implement `as.1`'s `-L`
     switch (control over whether compiler-internal `L`-prefixed labels appear in the
     written symbol table). Every real hardware-linked golden `.o` this project
     validates against includes those labels unconditionally, with no flag involved in
     how they were produced — implementing `-L`'s documented default (excluded) broke
     byte-for-byte golden parity. Matching the golden files takes priority over literal
     switch-for-switch parity with `as.1` here; `-o`/`-W` and everything else are
     unaffected. Do not "fix" this without re-breaking golden parity — see
     `src/mutos_as/assemble.c`'s `-L` comment and `STATUS.md`.
   * **Known, deliberate exception**: `mutos_cpp` never emits `# N "file"` line-marker
     output, regardless of `-P`. Every real MUTOS 1700 golden reference this tool is
     validated against was itself generated with `cc`'s `-P` flag (which `cc` also
     forwards verbatim to `cpp` — see `v7/cc/cc.c`), so this is simply `mutos_cpp`'s
     only supported mode; `-P` is accepted on the command line purely for
     compatibility. See `src/mutos_cpp/README.md`.
   * **Open question for future `mutos_cc`, not yet decided**: real `cc.c`'s `-P` and
     `-S` are not just independent flags — `-P` makes `cc` exit right after `cpp` runs
     (`if (pflag) { cflag++; continue; }`), unconditionally, before the code that
     checks `-S`'s `sflag` is ever reached. So real `cc -P -S foo.c` silently produces
     only `foo.i`, never `foo.s` — confirmed the hard way generating
     `tests/mutos_cc/`'s golden corpus (see `docs/DEVLOG.md`'s Milestone 4 host-tooling
     findings). Byte-for-byte CLI fidelity would mean `mutos_cc` reproducing this exact
     short-circuit; whether that's worth doing on purpose versus just documenting it as
     a real-`cc` footgun users should avoid is undecided — revisit once `mutos_cc`'s
     driver is actually being written.
4. **Headers & Syscalls**: Provide full support for original MUTOS 1700 header files and system calls (mapping x86 software interrupts/traps instead of PDP-11 traps).
5. **PDP-11 Middle-Endian — scope**: Historic V7/PDP-11 `long` fields use PDP-11
   Middle-Endian byte order (`1 0 3 2`, i.e. the high-order 16-bit word first, each word
   itself little-endian). This does **not** apply blanket-wide across the toolchain —
   confirmed and implemented scope is:
   * **`ar` archive format** (`atime`/`asize`/`cloc` in `src/h/mutos_aout.h`): carried
     over unmodified from V7, **DOES** use PDP-11 middle-endian. Implemented in
     `mutos_get_u32_pdp11()`/`mutos_put_u32_pdp11()`.
   * **MUTOS `a.out` object/executable header and symbol table**: entirely 16-bit
     based — there is **no `long` field anywhere in it at all** — so PDP-11
     middle-endian is simply not applicable there; every multi-byte field is plain
     little-endian (see `mutos_aout.h`'s own top-of-file comment). Do not apply
     middle-endian encoding to the `a.out` header — doing so breaks byte-for-byte
     golden parity.
   * **`long` variables inside C code compiled by `mutos_cc`**: this is where the rule
     will actually matter for K&R `long`-arithmetic correctness — confirmed (not just
     theorized) via real hardware-linked `.o` disassembly, see `docs/MUTOS_C_ABI.md`
     sect. 1.6 — but **not yet implemented**: `mutos_c0`/`mutos_c1`'s current grammar
     coverage has no `long` support yet (see `src/mutos_cc/README.md`'s "Next steps").
   * *Example (for the `ar`-archive and future-`mutos_cc` cases only)* — storing
     `0x0A0B0C0D`:
     ```text
     byte offset        8-bit value     16-bit little-endian value
        0               0Bh             0A0Bh
        1               0Ah
        2               0Dh             0C0Dh
        3               0Ch
     ```
6. **Identifier length limits**: this toolchain's real significant-character limits
   are **8 for internal identifiers, 7 for external (global-linkage) identifiers** —
   confirmed from two independent sources and load-bearing for `mutos_c0`/`mutos_c1`,
   not just a test-corpus style rule:
   * **Internal = 8**: `v7/cc/c0.h`/`c1.h` define `NCPS 8` — the compiler's own
     front-end symbol table (`struct hshtab`) has an 8-byte name field. Applies to
     anything that never leaves the compiler's own symbol table: locals, parameters,
     struct/union members, `typedef` names, `enum` constants, labels. Two internal
     names agreeing on their first 8 characters are the same symbol to `c0`/`c1`.
   * **External = 7**: `man/mutos_aout.h.5`'s `mutos_sym_t.name[8]` shows the
     **object-file** symbol table entry is also 8 bytes — but `v7/cc/c04.c`'s
     `outcode()` (confirmed in `c02.c`'s `EXTERN`/`STATIC`/`CSPACE` handling too)
     unconditionally prepends a leading `_` to any name written there, consuming one
     of those 8 bytes. Applies to any function name and any file-scope variable,
     `static` or not. `mutos_c1` must reproduce this truncation-after-underscore
     behavior exactly (not just avoid overlong names in its own test data) to stay
     byte-for-byte compatible with real MUTOS 1700 object output.
   * See `tests/mutos_cc/README.md` for the derivation in full and two identifiers
     the test corpus itself had to be renamed for after violating the 7-char limit.

### 📋 Workflow Guidelines
1. **Context Alignment**: Before modifying code in `/src/mutos_<tool>/`, always check the corresponding test suite in `/tests/mutos_<tool>/` to understand the expected behavior and existing edge cases.
2. **Golden Master Integrity**: Do not alter files in `/tests/.../kernel_opt/` or `kernel_nonopt/` unless explicitly instructed. These serve as our regression baseline.
   * **Caution — `.o` is ambiguous in this repo**: `tests/mutos1700_libc/*.o`, `tests/mutos1700_crt0/*.o`, and every `*.o.golden` under `tests/mutos_as/` are precious real hardware-linked **reference data**, not regenerable build byproducts, even though they share the `.o` extension with actual build artifacts (e.g. `src/mutos_as/*.o`). A blanket `find . -name "*.o" -delete` or `make clean`-style cleanup run from the repo root will destroy them. Always scope any such cleanup to the specific `src/mutos_<tool>/` build directory being cleaned, never to `/tests/`.
     Extend the same care to `tests/mutos_as/v30_speculative/`'s `*.o.crosschecked` files
     (if/when present) — do not delete them in a cleanup sweep either, but also never
     treat them as equivalent to a real `*.o.golden`: they are cross-checked against
     third-party tooling only, not hardware-confirmed (see that directory's own
     `README.md`).
3. **Reference Material**: Use the `/v7/` directory strictly as historical reference. Do not mix V7 logic directly into `mutos` unless explicitly migrating or fixing compatibility bugs.
4. **Header Files**: When changing structs or definitions in `/src/h/`, verify the impacts across `as`, `cc`, and `ld` simultaneously.
5. **Build Requirement**: Create one top Level Makefile to build all 4 components (`mutos_ld`, `mutos_as`, `mutos_cpp`, `mutos_cc`). **Done** — see the repo-root `Makefile` (`make`/`make test` — see `STATUS.md`'s "Top-level build").
6. **STATUS.md Sync (mandatory)**: `STATUS.md` must be kept in sync with reality at all
   times — treat it as part of the change, not optional follow-up documentation. This
   means:
   * Any change to `mutos_ld` or `mutos_as` (bug fix, new opcode/feature, CLI change,
     regression discovered, etc.) is only "done" once `STATUS.md` reflects it —
     including rebuilding and diffing against the real golden files first (per this
     project's core verification rule) so `STATUS.md` never states an unverified claim
     as fact.
   * Every future milestone (`mutos_cpp` — done, see below — then `mutos_cc`/`mutos_c0`/
     `mutos_c1`, `mutos_c2` + `-mv30`) gets its own status section in `STATUS.md` as soon
     as work on it starts, following the same "verified this session vs. carried from
     prior records" distinction already used there.
   * If a session cannot re-verify a previously-documented claim (e.g. missing golden
     files, as currently the case for `mutos_ld` — see `STATUS.md`), that must be
     stated explicitly in `STATUS.md` rather than silently repeating the old claim.
   * This applies regardless of who or what makes the change — human contributor or AI
     assistant.
   * **`DEVLOG.md` companion**: dense technical detail that doesn't belong in
     `STATUS.md`'s current-state summary (CPU/encoding facts, full bug narratives,
     debugging methodology) belongs in `docs/DEVLOG.md` instead — added there as it's
     discovered, not just held in conversation memory (see Guideline 7 below for why).
   * **Automated backstop**: `make check-docs` (`scripts/check_docs.py`) mechanically
     catches the part of this that's easy to miss by hand — a number restated in more
     than one file that fell out of sync, a stale/renamed filename still quoted
     somewhere, a broken internal link, or a "Last updated" stamp that doesn't match
     the file's actual last commit. It runs in CI on every push/PR touching a `.md`
     file (`.github/workflows/docs-consistency.yml`), and — after a one-time
     `make install-hooks` per clone — automatically before every local commit too
     (`.githooks/pre-commit`, blocks the commit on failure; bypass with
     `git commit --no-verify` if genuinely needed). It is a mechanical backstop, not a
     substitute for the sync discipline above — it only catches drift shaped like its
     existing checks, not a claim that's internally self-consistent but wrong.
7. **Download Delivery Format (mandatory)**: When providing repository file updates,
   default to a **git patch** (a plain `git diff`-format file, applied with `git apply
   patch.file` or `patch -p1 < patch.file` from the repo root), not a zip. Generate it
   from a local clone synced to the person's actual current `HEAD` (fetch/merge
   first — don't assume the sandbox's clone is still current) and verify with
   `git apply --check` (ideally against a fresh clone) before handing it over, so it's
   known to apply cleanly rather than just plausible-looking. State explicitly which
   files the patch touches and why (new vs. modified). This replaced an earlier
   zip-of-changed-files default after a real failure mode: with a zip, new (untracked)
   files are easy for the person to miss when copying by hand — that's exactly how
   `tests/mutos_cc/`'s 11 per-category `Makefile.mutos` files went missing from a
   commit even though everything else in that same delivery was copied correctly. A
   patch applies (or cleanly fails) as one atomic unit, so partial-copy mistakes like
   that can't happen silently. Fall back to a zip only when a patch genuinely isn't
   the right tool — e.g. the person explicitly asks for a zip, or the delivery is
   brand-new binary content that isn't already routed through this project's
   base64-text convention (see the `.o`/`.a` golden files above) and so has nothing
   meaningful to diff.
8. **`dump_temp.py` sync (mandatory)**: `src/mutos_cc/dump_temp.py` (a standalone
   human-readable decoder for any `temp1`/`temp2` — i.e. `.1`/`.2` — file, generated or
   golden) decodes each opcode's argument list from a hardcoded table (`OPCODES`),
   transcribed directly from `c0_parser.c`/`c0_main.c`'s own `outcode()` call sites —
   it does not infer shapes from the byte stream itself. This table goes stale
   silently otherwise, so: any change that adds a new opcode `mutos_c0` emits, changes
   an existing opcode's argument count/order, or adds a new `TY_*`/`SC_*` constant
   (`decode_type()`/`decode_sc()`/`SC_NAMES` in the same file) must update
   `dump_temp.py` in the same change — not as separate follow-up work, the same
   standard Workflow Guideline 6 holds `STATUS.md` to. Verify the update the same way
   `dump_temp.py` was originally verified: re-run it against every affected
   `*.1.golden`/`*.2.golden` (`tests/mutos_cc/**/*.1.golden` covers the full corpus)
   and confirm no new opcode stops the dump with a "no confirmed argument shape"
   message that should now be decodable. A new opcode `mutos_c0` does not yet emit
   (still future grammar/opcode scope) does not need an entry — `dump_temp.py` is
   meant to stop cleanly there, per its own header comment, rather than guess.

---

## 🗺️ Strategic Roadmap & Current Focus
The project follows a strict **Bottom-Up** strategy. See **[`STATUS.md`](./STATUS.md)**
for verified current implementation status per milestone — the summaries below describe
scope and intent, not a snapshot of what's done.

### Milestone 1: The Cross-Linker (`mutos_ld`)
* Porting original V7 `ld.c` to modern C.
* Handling object merging, relocation calculations, and writing the final MUTOS `a.out` header.

### Milestone 2: The Cross-Assembler (`mutos_as`)
* Adapting the V7 assembler to parse MUTOS 1700 assembler syntax and emit 8086 opcodes
  into MUTOS-compatible object files (`.o`).
* NEC V30/80186 instruction-set support: no longer purely future work — a substantial
  set of 80186/V30 opcodes (PUSHA/POPA, PUSH imm, INSB/INSW/OUTSB/OUTSW, ENTER/LEAVE/
  BOUND, IMUL-immediate forms, shift/rotate-with-immediate-count) is implemented; see
  `STATUS.md` for exactly which are real-hardware-confirmed vs. still unconfirmed, and
  which 8086/80186 opcodes remain missing.
* **Status: provisionally complete** — see `STATUS.md`.

### Milestone 3: The C Preprocessor (`mutos_cpp`)
* Re-implementing the observable behavior of the real MUTOS 1700 / V7 "fast cpp"
  (John F. Reiser, 1978): macro expansion (object- and function-like), `#include`
  file resolution, conditional compilation (`#ifdef`/`#ifndef`/`#if`/`#else`/`#endif`
  — no `#elif`, no `#`/`##` operators, matching this K&R-era toolchain's actual
  language level), and its exact line-preserving output conventions.
* **Status: complete for the real corpus** (5/5 golden files byte-for-byte identical)
  — see `STATUS.md` and `src/mutos_cpp/README.md` for the full behavioral
  specification and known, documented simplifications.

### Milestone 4: The C-Compiler (`mutos_cc`, `mutos_c0`, `mutos_c1`) [CURRENT FOCUS]
* Porting frontends and backends. Modifying code generator (`c1`) to emit x86-16 code in MUTOS assembly syntax and enforce PDP-11 middle-endian format for `long`.
* **`c0`/`c1` process split: confirmed, not just inherited.** Deliberately kept as
  two separate executables (matching the "Target Executables" list above) after
  reading `v7/cc`'s actual `temp1`/`temp2` format: it is not a raw memory/pointer
  dump but a small, fully-specified tagged byte stream (`outcode()`/`getree()` in
  `v7/cc/c04.c`/`c11.c`), which makes the split's testability benefit real and
  cheap rather than an artifact of the PDP-11's 64K limit. `mutos_c0` should keep
  that stream format close to the original and add an optional human-readable
  dump mode, so its front-end output (parsing, typing, tree shape) can be verified
  against hand-written expected IR before any x86 code generator exists. Full
  rationale in `docs/DEVLOG.md`'s Milestone 4 section.
* K&R construct-coverage test corpus in place at `tests/mutos_cc/` (source-only,
  62 files across 11 categories, each with its own real-`make`-compatible
  `Makefile.mutos` plus a shell fallback `gen_mutos.sh` — both verified
  against the real MUTOS 1700 `make(1)`/`basename(1)`/`expr(1)`/`find(1)`
  manpages) — its golden-generation pipeline (`.s`/`.i`/`.1`/`.2` on real
  MUTOS 1700 hardware, packaged into `*.golden`/base64 form on the modern
  host) is now **confirmed working end-to-end**, not just designed: getting
  there surfaced and fixed several real bugs in this project's own tooling
  (real `make(1)`'s capacity limits, real `cc`'s `-P`/`-S` interaction, a
  golden-packaging step that tried to rebuild files using MUTOS-only tools
  on the modern host) — full writeups in `docs/DEVLOG.md`'s Milestone 4
  "MUTOS 1700 host-tooling findings" section. Full-corpus goldens (all 62
  files across all 11 categories) are now present in this checkout.
* **`mutos_c0`/`mutos_c1`: implemented and verified byte-exact, end-to-end,
  for 33/62 of the full corpus** (`tests/mutos_cc/00_smoke`'s 3 files, plus
  all of `01_expr`: `01_intarith.c`, `02_bitwise.c`, `03_rellogic.c`,
  `04_shift.c`, `05_incdec.c`, `06_compasgn.c`, `07_ternary.c` and
  `08_castsize.c`, plus `02_long/01_addsub.c` and `02_muldiv.c` (`long`
  `+`/`-`/`*`/`/`/`%`, an implicit `int`→`long` widening conversion
  (`ITOL`), and a `long`-vs-constant relational comparison - see
  `STATUS.md`), plus all 7 of `03_ctrlflow` (`if`/`else`, `while`, `do`/
  `while`, `for` - including v7/cc's deferred-increment-emission trick,
  reproduced via `open_memstream()` since this compiler streams wire
  bytes as it parses rather than building an AST - nested `break`/
  `continue`, `switch`/`case`/`default` via a real jump table, and
  `goto`/labels with forward-reference resolution; full derivation in
  `docs/DEVLOG.md`) — the first
  constructs needing a real symbol table, non-folded expression codegen,
  (for `03_rellogic`) deferred/fused comparison codegen for
  relational, equality and short-circuit logical operators, (for
  `04_shift`) a second working register (`CX`, for a variable shift
  count) and the "repeat a single-bit shift N times" plain-8086
  constant-shift idiom, (for `05_incdec`) postfix/prefix `++`/`--`
  (deferred-vs-immediate codegen ordering), pointer/array declarations,
  array-to-pointer decay, and pointer dereference as an assignment
  target, (for `06_compasgn`) all ten compound-assignment operators
  as their own dedicated opcodes (never a synthesized "a = a + 5"-style
  tree) compiling to a single in-place memory-operand instruction, with
  a genuine multiply-by-constant strength reduction to a shift for
  `*=` by a power of two, (for `07_ternary`) the `?:` conditional
  operator (an inverted-condition-branches-to-false-label codegen
  shape, distinct from a bare comparison's true-label pattern), the
  comma operator (emitting no code of its own - a pure value-discard),
  a parenthesized comma-list allowing embedded `IDENT = expr`
  assignments (not otherwise reachable from expression context), a
  confirmed `+1`-specific `INC` codegen shape, and `ASSIGN` now
  pushing its result value so a comma-list can discard it, and (for
  `08_castsize`) a genuine `char`/`long` type-system extension - three
  new confirmed conversion opcodes (`LTOI`, `ITOC`, and a
  MUTOS-specific `CTOL` absent even from vanilla V7), a `long`
  constant/local matching the already-documented "high word at the
  lower address" ABI convention down to the wire-format level, and
  `sizeof` folding entirely at parse time (never a wire opcode of its
  own), not just
  constant folding), and (for `04_funcs`) K&R-style function parameters
  (`bp+4, bp+6, ...`), direct and indirect (through a function-pointer
  variable) calls with a full right-to-left-push/caller-cleanup sequence,
  local `static` variables (their own dedicated `.bss` block, not the
  stack frame), and function pointers - full derivation in
  `docs/DEVLOG.md`, including a real, previously-unconfirmed compiler
  optimization this surfaced (a call's/multiply's result, when already
  sitting in the return register `AX`, is never redundantly re-moved).
  **`01_expr`, `02_long`, `03_ctrlflow` and `04_funcs` are now all fully
  covered (the two items still open when the detailed walkthrough above
  was first written - `02_long/03_retval.c`'s `DX:AX` return-value
  convention and `04_funcs/06_regclass.c`'s real register-variable
  allocation - were both finished in a later session; see `STATUS.md`/
  `docs/DEVLOG.md`'s Milestone 4 section for their own full derivation,
  not repeated in the walkthrough above). `05_arrptr` is 4 of 7 done -
  see the file list above and `STATUS.md`/`docs/DEVLOG.md` again for that
  derivation.**
  Real `.c` → real `mutos_cpp` → `mutos_c0` → `mutos_c1` → `.s` matches every
  covered golden byte-for-byte (`tests/mutos_cc/run_goldens.sh`, also wired
  into the top-level `Makefile`'s `test` target). Several MUTOS-specific
  deltas from vanilla V7 `cc`'s `temp1`/`temp2` format, and a real
  `mutos_as`-syntax convention (`*`/`#` immediate size markers), were
  discovered and confirmed byte-for-byte in the process — see
  `src/mutos_cc/README.md` and `docs/DEVLOG.md`'s Milestone 4 section for the
  full derivation.
  Grammar/opcode coverage beyond that is explicit, clearly-diagnosed "not
  yet supported" — never silent wrong output — by design (see
  `src/mutos_cc/README.md`'s "Current scope").
* **Next up**: expand `mutos_c0`/`mutos_c1`'s grammar/opcode coverage
  category by category — see
  `src/mutos_cc/README.md`'s "Next steps" for the concrete
  dependency-ordered list: `05_arrptr`'s remaining 3 files
  (`02_array2d.c` - 2-dimensional arrays; `05_arrofptr.c`/`07_strlibc.c` -
  string literals, needing an entirely new data-segment emission
  subsystem that does not exist yet), then `06_struct`
  (structs/unions/enums), the `chkstk` threshold, then the `mutos_cc`
  driver itself.

### Milestone 5: Optimizer (`c2`) & NEC V30
* Enhancing the V7 peephole optimizer for x86 and activating the `-mv30` compiler flag switch.

### 📌 Active Goal
* See `STATUS.md` for the current, verified task-level status — it is updated
  per-change (see Workflow Guideline 6) and is more reliable here than a fixed
  snapshot would be.

