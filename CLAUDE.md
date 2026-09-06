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

## 📂 Directory Structure & Context

### Core Toolchain for the MUTOS1700 Cross C Compiler (`/src`)
* `/src/h/`: Core header files (`.h`). Contains shared definitions and system structures.
* `/src/mutos_ld/`: Source code for the Mutos Linker.
* `/src/mutos_as/`: Source code for the Mutos Assembler.
* `/src/mutos_cpp/`: Source code for the Mutos C Preprocessor. See `src/mutos_cpp/README.md`
  for its full behavioral specification.
* `/src/mutos_cc/`: Source code for the Mutos C Compiler frontend/driver. **Planned only —
  this directory does not exist yet** (Milestone 4 has not started; see `STATUS.md`).

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
     will actually matter for K&R `long`-arithmetic correctness — **not yet relevant**,
     since Milestone 4 (`mutos_cc`) has not started (see `STATUS.md`).
   * *Example (for the `ar`-archive and future-`mutos_cc` cases only)* — storing
     `0x0A0B0C0D`:
     ```text
     byte offset        8-bit value     16-bit little-endian value
        0               0Bh             0A0Bh
        1               0Ah
        2               0Dh             0C0Dh
        3               0Ch
     ```

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
5. **Build Requirement**: Create one top Level Makefile to build all 4 components (`mutos_ld`, `mutos_as`, `mutos_cpp`, `mutos_cc`)
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
7. **Download Delivery Format (mandatory)**: When providing repository files as a
   downloadable archive, package **only files that are new or modified relative to
   the person's last-synced state** (i.e. an incremental diff, not a full repository
   dump) into a single zip. Determine the change set via `git status`/`git diff
   --name-only` against the current branch before packaging, and exclude build
   artifacts (compiled `.o`/binaries — see Guideline 2's caution on `.o` ambiguity;
   regenerate via `make`, don't ship them) and `.git/` itself. State explicitly in
   the response which files are included and why (new vs. modified). This replaces
   the earlier default of zipping the entire working tree.

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

### Milestone 3: The C Preprocessor (`mutos_cpp`) [CURRENT FOCUS]
* Re-implementing the observable behavior of the real MUTOS 1700 / V7 "fast cpp"
  (John F. Reiser, 1978): macro expansion (object- and function-like), `#include`
  file resolution, conditional compilation (`#ifdef`/`#ifndef`/`#if`/`#else`/`#endif`
  — no `#elif`, no `#`/`##` operators, matching this K&R-era toolchain's actual
  language level), and its exact line-preserving output conventions.
* **Status: complete for the real corpus** (5/5 golden files byte-for-byte identical)
  — see `STATUS.md` and `src/mutos_cpp/README.md` for the full behavioral
  specification and known, documented simplifications.

### Milestone 4: The C-Compiler (`mutos_cc`, `mutos_c0`, `mutos_c1`)
* Porting frontends and backends. Modifying code generator (`c1`) to emit x86-16 code in MUTOS assembly syntax and enforce PDP-11 middle-endian format for `long`.

### Milestone 5: Optimizer (`c2`) & NEC V30
* Enhancing the V7 peephole optimizer for x86 and activating the `-mv30` compiler flag switch.

### 📌 Active Goal
* See `STATUS.md` for the current, verified task-level status — it is updated
  per-change (see Workflow Guideline 6) and is more reliable here than a fixed
  snapshot would be.

