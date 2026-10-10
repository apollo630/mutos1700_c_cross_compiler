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
  62/62 of `tests/mutos_cc/`'s goldens - all of them (`00_smoke` plus all of `01_expr`:
  `01_intarith`,
  `02_bitwise`, `03_rellogic`, `04_shift`, `05_incdec`, `06_compasgn`, `07_ternary` and
  `08_castsize`, plus all 4 of `02_long`: `01_addsub`/`02_muldiv`/`03_retval`/`04_params`,
  plus all 7 of
  `03_ctrlflow`, plus all 7 of `04_funcs`, plus all 7 of `05_arrptr` (string
  literals included), plus all 7 of `09_abiprobe` (`char` element access), plus
  all 3 of `07_scope` (file-scope variables, block scope), plus all 9 of `06_struct`
  (structs, unions, bit-fields, enums, typedefs), plus all 5 of `10_integ`, plus both
  of `08_float` (floating point through libc's software floating-point runtime)) — see
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
* `/tests/mutos1700_libc/`: MUTOS1700 libc.a, including all `*.o.base64.txt` object files and `libc.a.base64.txt`.
* `/tests/mutos_as/kernel_nonopt/`: Golden Master test cases for the assembler (non-optimized builds), including `*.golden_base64.txt`. Also has `Makefile.mutos` (`noL`/`L` targets), written for the real-hardware `-L` experiment described below — see that entry for the outcome.
* `/tests/mutos_as/kernel_opt/`: Golden Master test cases for the assembler (optimized builds), including `*.golden_base64.txt`. Also has `Makefile.mutos` (`noL`/`L` targets), same purpose.
* `/tests/mutos_as/README.md` + `/tests/mutos_as/Makefile`: the real-hardware "-L" experiment (see `kernel_opt`/`kernel_nonopt` above and Workflow Guideline 3's "Known, deliberate exception") — meant to assemble every real kernel `.s` both with and without `-L` to settle whether the kernel build actually used it. **Concluded 2026-09-28, without needing a full comparison run**: the real `as` does not implement `-L` at all (`Unknown option L ignored`), so no `.oL` goldens were generated — there is nothing to diff. See `STATUS.md`'s open item 6 for the finding and the new source-level angle it points to instead.
* `/tests/mutos_as/float_coverage/`: real-hardware goldens (generated on real MUTOS 1700 hardware 2026-09-28/29) from eight hand-written `.float`/`.double` sources — `lea`+`.float` in whole objects, zeros, a `.double`, an inexact `.float` (`fltopen.s`), the probes `fltmode.s`, `fltmul.s` and `fltovf.s` — which pinned down the real assembler's conversion itself: v7 `atof()` on the 56-bit double, `.float` = its high half, truncated, every step rounded to nearest-even, the multiplication rounding `libc.a`'s own (inexact) product, a zero keeping what the accumulator `fac` held, a result outside the exponent range stored with `ldexp`'s wrapped exponent byte (see `src/mutos_as/fltconst.h`). All eight are byte-identical and part of `make test`, as is `fltmodel.py`, an independent Python model of that conversion that re-derives its unknowns from every golden here and checks `fltconst.c` against it. `libcatof.py` there is a second oracle: `libc.a`'s own `atof` run on `libc.a`'s own floating-point runtime under an 8086 emulator (Unicorn; `make check-libcatof`, not part of `make test`). See that directory's own `README.md` and `STATUS.md`'s open item 7.
* `/tests/mutos_as/float_open/`: hand-written probes for the real `as` whose constants
  `mutos_as` still refuses — deliberately **not** in `make test`; a probe moves to
  `float_coverage/` once `mutos_as` reproduces its golden (`fltopen.s` did,
  2026-09-28, `fltmode.s`, `fltmul.s` and `fltovf.s` 2026-09-29). **No open
  probe** at present: `fltsig.s` stays here as evidence without a golden - an
  overflow inside `atof` (5\*\*55), on which the real `as` aborts (2026-09-29:
  `***ERROR*** floating point over/under flow- assembly aborted`, exit status 4,
  no object), as `mutos_as` refuses it (`FLT_RANGE`). Its `README.md` has the
  real output and how to add, run and read a new probe (`fltmodel.py
  survivors`).
* `/tests/mutos_as/libc_recon/`: real `libc.a` objects as assembler goldens, with
  sources **reconstructed** from their disassembly (`ldexp.s` → the real `ldexp.o`,
  byte for byte), plus `check_floatdat.sh`, which compares `.float` output with real
  `libc.a` constants. `tests/mutos_as/assemble_cc_goldens.sh` checks that every real
  compiler `.s` in `/tests/mutos_cc/` assembles - but those listed in
  `tests/mutos_cc/invalid_goldens.txt` (the real compiler's own invalid output),
  which `mutos_as` must refuse. See that directory's `README.md`.
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
  `00_smoke/` … `10_integ/`, plus a further `11_kernel/` real-kernel-source
  golden corpus described below — 71 `.c` files across 12 directories in
  total). Golden references
  (`*.s.golden` for the final assembly, **and** `*.i.golden`/`*.1.golden`/
  `*.2.golden` for `cpp`'s output and `c0`'s raw `temp1`/`temp2`
  intermediate-code streams, captured directly via `/lib/cpp`+`/lib/c0`,
  not just derived from the final `.s`) are captured per-category as they're
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
    matching `tests/mutos_as/kernel_opt/`'s `*.o.golden`/`*.o.golden_base64.txt`
    convention) is meant to run
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
  `fuzz/` is the one non-corpus directory in it: host-only random-program
  fuzzing (`make fuzz`, see `tests/mutos_cc/fuzz/README.md`) - `fuzz_c.py`
  computes what each generated program must do under C semantics and
  checks every variable at every statement boundary by executing
  `mutos_c1`'s output with `x86sim.py`; `--baseline` classifies every
  difference against an earlier build. It holds no `.c` files, so nothing
  that walks the corpus sees it.
  `fltprobe/` is the other non-corpus directory: floating-point probe
  programs with their own `Makefile.mutos` and real-hardware goldens (see
  its `README.md`) - round 1's nine files, the four each of rounds 2, 3,
  6, 9, 10, 11, 12 and 13, the three each of rounds 4 and 5 and the five
  each of rounds 7 and 8 all byte-exact (`p19_open3` up to the real
  compiler's own invalid output, `p21_fltexp`, `p28_fltstk`,
  `p37_fltstk3`, `p41_fltstk4`, `p45_fltstk5`, `p49_fltstk6`,
  `p52_fltinf8` and `p53_fltstk7` together with the real compiler's own
  `c1` error messages, see below), round 14's four (`p54`..`p57`) waiting
  for their goldens. It does hold `.c` files, so `run_goldens.sh`, `gen_mutos.sh` and
  the Makefiles do walk it - `run_goldens.sh` skips a file with no goldens
  and checks one with a `.1.golden` but no `.i.golden` from `mutos_c0` on,
  listed apart (a set brought back without its `.i` files) - and it is not
  counted in the N/62 figure.
  `invalid_goldens.txt` lists every golden whose `.s` stops being valid
  assembly at some line - the real compiler's own broken output, never
  edited (at present `fltprobe/p19_open3`: 118 bytes of libc's `_ctype_`
  where a register name belongs - the real `as` refuses those lines too,
  `***ERROR*** syntax error`, see `fltprobe/p19_as.log`). `run_goldens.sh` requires `mutos_c1` to
  refuse such a file and to have written exactly the golden's lines
  before that point (its category 8).
  `c1_errors.txt` lists every golden for which the real compiler's `c1`
  reported errors - and still wrote the whole `.s`, which is the golden,
  never edited (at present `fltprobe/p21_fltexp`: "56: floating point
  stack underflow", "57: Floating point stack underflow" - its own code
  pops the floating-point stack twice - and `fltprobe/p28_fltstk`,
  `p37_fltstk3`, `p41_fltstk4`, `p45_fltstk5`, `p49_fltstk6` and
  `p53_fltstk7`, twelve, seventeen, twenty-three, twenty-three, twenty-six
  and five messages, the same wrong code on purpose, and
  `fltprobe/p52_fltinf8`, twenty-two "Floating point stack overflow;
  simplify expression" - its own `fstd`s pile up past the model's six
  values). `run_goldens.sh` requires
  `mutos_c1` to print exactly those messages, exit with a nonzero status
  and write exactly the golden `.s` (its category 9).
  `11_kernel/` is a second, separate golden corpus alongside the 62-file
  construct table above — nine real, unmodified MUTOS 1700 kernel driver
  source files (`01_delay.c` … `09_amx.c`, ordered easy to hard) plus the
  local header tree they `#include`, mirroring what `/tests/mutos_cpp/c/`
  below already does for `mutos_cpp`. Its own real-hardware-verified
  goldens (`.s`/`.i`/`.1`/`.2` plus base64 companions) and `Makefile.mutos`
  are already present in this checkout; `mutos_c0`/`mutos_c1` coverage
  against it is tracked apart from the 62-file corpus's own N/62 pass
  fraction: 1/9 (`01_delay` byte-exact since 2026-10-04; the other eight
  refuse at the front end, each with a diagnosed reason, never silent
  wrong output) — see `STATUS.md`'s "Next up" and `docs/DEVLOG.md`'s
  Milestone 4 section.
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
     **Real-hardware finding (2026-09-28), settles one part of the open question**:
     the real MUTOS 1700 `as` binary does not implement `-L` at all —
     `as -L -o v30ide.oL v30ide.s` prints `Unknown option L ignored` and proceeds —
     despite `docs/MUTOS1700_Assembler_as.pdf` sect. 3.1 documenting it (a
     manual/binary mismatch this project cannot resolve). This rules out "the
     kernel build invoked `as -L`" as the explanation for why none of `libc.a`'s
     167 objects (compiled C included) has an `L`-number symbol while every kernel
     golden does — the flag is a no-op on this hardware either way, so no
     per-invocation switch can produce that difference. The underlying
     kernel-vs-libc discrepancy is still open; see `STATUS.md`'s open item 6 for
     the new source-level angle (whether `L`-labels come from the compiler's C
     sources, not the assembler's symbol-table policy).
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
     sect. 1.6 — and **implemented** for `long` locals, constants, parameters,
     return values and `+`/`-`/`*`/`/`/`%` (all of `tests/mutos_cc/02_long`, byte-exact);
     a `long` read or written through a pointer or subscript is still refused (see
     `src/mutos_cc/README.md`'s "Current scope").
   * **`float`/`double` values do NOT follow it either**: MUTOS 1700's floating
     format keeps the whole value little-endian, the excess-128 exponent in the
     HIGHEST byte and the sign in the top bit of the byte below it (v7's
     "0.1xxx" mantissa otherwise) - read off real `libc.a` data bytes
     (`atof.o`'s `2**56` is `00 00 00 b9`, `ecvt.o`'s `10.0` is `00 00 20 84`),
     not PDP-11 word order. See `docs/DEVLOG.md`'s `08_float` section.
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
   * **Caution — `.o` is ambiguous in this repo**: every `*.o.golden` under `tests/mutos_as/` is precious real hardware-linked **reference data**, not a regenerable build byproduct, even though it shares the `.o` extension with actual build artifacts (e.g. `src/mutos_as/*.o`). A blanket `find . -name "*.o" -delete` or `make clean`-style cleanup run from the repo root will destroy those. `tests/mutos1700_libc/*.o.base64.txt`, `tests/mutos1700_libc/libc.a.base64.txt`, and `tests/mutos1700_crt0/crt0.o.base64.txt` are the same kind of precious real hardware-linked reference data — stored as base64 *text* (the raw `.o`/`.a` binaries were deleted 2026-09-27) specifically so a `.o`-glob cleanup can't catch them by accident, but they must never be regenerated, re-encoded, or otherwise altered either. Always scope any build-artifact cleanup to the specific `src/mutos_<tool>/` build directory being cleaned, never to `/tests/`.
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
     the file's actual last commit (for a file with uncommitted changes: today's date,
     the date the pending commit will carry). It runs in CI on every push/PR touching a `.md`
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
  for 62/62 of the full corpus - every file** (`tests/mutos_cc/00_smoke`'s 3 files, plus
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
  not repeated in the walkthrough above). `05_arrptr` is now fully covered
  too (including `02_array2d`'s 2-D arrays, whose factored address
  arithmetic follows v7/cc/c12.c's `distrib()`, and string literals -
  `05_arrofptr`/`07_strlibc` - the first use of the `temp2` data stream,
  rendered as `.data` / `.byte` lines), as is `09_abiprobe/01_argvmain` -
  see `STATUS.md`/`docs/DEVLOG.md` again for those derivations. So is
  `10_integ/05_matmul`, the first evaluation-order codegen: `mutos_c1`
  pre-scans each expression and, where the real compiler evaluates the
  right operand first (v7's `%n,n` template - computed onto the stack,
  then the left), replays temp1's subtrees in that order through its
  ordinary handlers. `09_abiprobe`'s six large-frame probes, `10_integ/
  04_strrev` and `02_bubsort` followed (2026-09-26): `char` element
  access - the MUTOS front end's explicit char conversions (opcode 109,
  `ITOC`, its type the result type) inserted where v7's `build()`
  converts, and byte loads/stores (`movb`/`cbw`, DX/BX for a char pointer)
  in `mutos_c1` - call arguments evaluated right to left (v7's
  `comarg()`), and a relational's operands swapped by v7's `degree()`
  rule. All three `07_scope` files followed the same day: file-scope
  variables (`CSPACE`/`BSS`+`NLABEL`+`SSPACE` in `temp1`, referenced by
  `NAME(SC_EXTERN, type, "_name")`, rendered `.comm`/`_name:.blkb` and
  as a bare `_name` operand) and nested blocks with their own scope.
  `10_integ/01_wordcount` completed `10_integ`'s char files the same day:
  a file-scope `char text[] = "..."` (`SYMDEF`/`DATA`/`NLABEL`, the
  string's `BDATA` runs in `temp1`, `EVEN` - rendered `.data` /
  `_text:.byte ...`), character constants, and a char compared with a
  constant in 0..127 - the constant typed char, no `ITOC` (v7 `optim()`'s
  CHAR retyping, done by the MUTOS front end) - as a byte compare,
  `cmpb _text(bx),*10.`, or against 0 `movb dx,#_text(bx)` / `orb dx,dx`,
  the element addressed with the index in BX and the array's symbol as
  displacement. All nine `06_struct` files and `10_integ/03_linklist`
  followed (2026-09-27): structs, unions, bit-fields, enums and typedefs in
  `mutos_c0` (v7's layout and its `build()`/`setype()` retyping of member
  chains, plus the new `FSEL` and `STRASG` opcodes), and in `mutos_c1` the
  bit-field codegen, a store through "pointer + offset" computing its
  right-hand side first, `x - *p` pushing the pointer first and v7's
  `acommute()` order for `+` chains - three more evaluation-order plan
  decisions - with registers handed out in order (DI, SI, DX); the
  remaining `char` shapes (a char with an int operand, as a call argument
  or condition, stored into a file-scope array element) came first, from
  kernel evidence. Both `08_float` files completed the corpus
  (2026-09-27): `float`/`double` locals, floating constants (`FCON`,
  the source text), `+ - * /` and the `ITOF`/`FTOI`/`FTOL` conversions
  in `mutos_c0`, and in `mutos_c1` calls into libc.a's software
  floating-point runtime - an operand's address in AX (`lea ax,<x>` /
  `call flds`, `fadds`, `fstsp`, `itof`, `ftoi`, `ftol`, ...), each
  constant a `.float` in `.data`, `.globl fltused` at the end; the
  floating format itself read off real `libc.a` data bytes (see
  `STATUS.md`/`docs/DEVLOG.md`). `mutos_as` assembles that output
  (`.float`, `lea <reg>,<label>`), and linked with the real `crt0.o`/
  `libc.a` it runs: both goldens return their C sources' values under an
  8086 emulator. Beyond the corpus, 56 of the 57 probes of
  `tests/mutos_cc/fltprobe/`'s first thirteen rounds are byte-exact against
  their own real-hardware goldens (v7's operand order for every floating
  operator, constants of any value and their degree, floating globals and
  their initializers, pointers, members, elements, calls, conversions and
  the register an int is converted in, `long` mixed with int-class
  values and every `long` operator asked, compound assignments of a
  `long`, sums and differences of calls, of quotients and of
  comparisons, compound assignments into elements and through pointers
  and their values, v7's `distrib()` and `sreorder()`, which store an
  assignment passed as a floating argument gets, the `chkstk` threshold
  `(98,100]` - `p21_fltexp`, `p28_fltstk`, `p37_fltstk3`, `p41_fltstk4`,
  `p45_fltstk5`, `p49_fltstk6`, `p52_fltinf8` and `p53_fltstk7` with the
  real compiler's own `c1` error messages), the 57th up to the real
  compiler's own invalid output; and
  `11_kernel/01_delay`, the first of the nine real kernel files.**
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
  `src/mutos_cc/README.md`'s "Current scope"). The semantic fuzzer
  (`tests/mutos_cc/fuzz/`) has found where that did not hold: `mutos_c0`
  compiling `7 - x` as `x - 7`, and two `mutos_c1` bugs - side effects in
  conditionally evaluated operands, and a postfix `++`/`--` in a
  condition (all three fixed 2026-09-24; `&&`/`||`/`?:`/`,` are now
  generated through `mutos_c1`'s evaluation-order plan, in v7's
  `cexpr()`/`cbranch()` order - see `STATUS.md`).
* **Next up**:
  * **`mutos_as`'s floating conversion is settled** (2026-09-29):
    `fltmul.o.golden` showed that the real multiplication rounds
    `libc.a`'s own inexact product (one partial product from the wrong
    word), so every constant the model covers is determined, and
    `mutos_as` accepts every `%.17e` constant `mutos_c1` writes from
    `e-37` up. The range is settled too (`fltovf.o.golden`, `fltsig.s`,
    2026-09-29): a result only `ldexp` takes out of range gets a wrapped
    exponent byte, as `mutos_as` now writes it; an overflow inside the
    arithmetic (5\*\*55 for text from `e-38` down) aborts the real `as`,
    and `mutos_as` refuses it (exit status 1 where the real `as` exits
    with 4). Unobserved and refused (`FLT_UNKNOWN`): LOGHUGE after a
    dropped digit. No real-hardware float probe is pending. All of `atof.o`'s and
    `ecvt.o`'s floating constants are reproduced, `ecvt.o`'s inexact
    `.03` included, so only the `L`-label question below keeps them from
    being `libc_recon/` goldens.
  * **The `libc.a`-vs-kernel `L`-label discrepancy is still open, but the
    flag-level explanation is now ruled out.** Real hardware confirmed
    2026-09-28 that `as -L` is not implemented at all (`Unknown option L
    ignored`) — see this file's "Known, deliberate exception" above and
    `STATUS.md`'s open item 6. The next angle is source-level, not
    flag-level: whether `L`-labels are emitted by the *compiler* only under
    certain conditions, so kernel `.s` sources contain them in the text while
    `libc.a`'s lost sources never did — not a symbol-table policy the
    assembler applies per invocation. `mutos_as`'s own always-emit-`L`-labels
    behavior remains the pragmatic choice for golden parity either way.
  * **Floating point beyond the corpus: `tests/mutos_cc/fltprobe/`'s nine
    round-1, four round-2, four round-3 and three round-4 goldens are
    byte-exact**
    (2026-09-29, 2026-10-02). `libc.a`'s compiled C
    (`atof.o`, `ecvt.o`, `gcvt.o`, `fltpr.o` - optimized) gave the first
    shapes; the probes' real-hardware goldens corrected them (a floating
    constant exactly a float has v7 degree 1) and settled the rest: the
    real compiler is v7's `c1` with calls into the software runtime, so
    `mutos_c1` plans every floating expression in v7's order on the
    pre-scanned tree, and writes a constant's text as the real `c1`'s
    `printf` does - `libc.a`'s `ecvt()` of `libc.a`'s `atof()`, modelled
    exactly in `src/mutos_cc/c1_fltdec.c` and checked against the emulated
    `libc.a` (`make check-libcatof`). Round 2 settled an 8-byte constant's
    degree (0), the register an int is converted in (AX after a `*`, `/`
    or a call), elements subscripted by a variable, `i += d` and v7's
    `doinit()` (a float initializer truncated, as `libc.a`'s `fstsp`
    does). Round 3 settled an element's degree (2 for a local array's: its
    `lea` counts as computed), an int `*` loading its left operand first, a
    dropped int `+ 0`, a value kept in AX plus a constant, `j /= e` and a
    floating zero (kept). Round 4 settled the order of two int elements (a
    right one at offset 0 computed first - its value pushed for `*`, its
    address for `&`), the register context in a chain and after an
    assignment or `*p`, a remainder, a negated zero, a constant converted
    to an int, `d += c` (the right operand first) and an unsigned
    converted (`mutos_c0` accepts `unsigned` locals now). See
    `docs/DEVLOG.md`'s "The fltprobe goldens", "The round-2 fltprobe
    goldens", "The round-3 fltprobe goldens" and "The round-4 fltprobe
    goldens". Round 5 settled that the offset (not the context) decides
    the order of two int elements, `^` and a comparison of two elements
    (the left one's address pushed), `-=` with a computed right operand
    (the target loaded first), and `long` mixed with int-class values
    (`mutos_c0` writes v7's `ITOL`; v7's long comparisons) - and showed
    the real compiler emitting invalid assembly for an unsigned converted
    after a computed `*` (see "The round-5 fltprobe goldens"; the real `as`
    refuses it too - 2026-10-07, "The real assembler on `p19_open3.s`"). Round 6
    settled floating lvalues beyond variables (members and pointers,
    compound assignments into them), floating expression forms (`++`/`--`,
    chained assignments, `?:`, comma), nearly every `long` operator and
    the element orders left - and showed the real compiler's `c1`
    reporting its own wrong code: `half(d = 3.0)` pops the floating-point
    stack twice, and its compile-time model of that stack said so in two
    messages, which `mutos_c1` reproduces (see "The round-6 fltprobe
    goldens"). Round 7 settled the frames (`sub sp` up to 90 bytes, `call
    chkstk` from 100), a sum of calls (each right call pushed), that stack
    model (one per file, twelve messages reproduced; an assignment under a
    call popped only in a floating statement - inferred), an element
    tested for truth (loaded and `or`ed), compound assignments into
    elements, v7's `distrib()` of scaled comparisons, `long` `++`/`--`,
    negation, shifts by CX and truth tests, and the floating shapes left
    (see "The round-7 fltprobe goldens"). Round 8 settled every frame
    size (`sub sp,N` up to 98 bytes), that the CALL's own consumer decides
    whether an assignment passed as its floating argument is stored with a
    pop (`p30_fltstk2` - no message, right code), and round 7's inferences
    and refusals (`x = l++` the low word as an int, `l << i` through CX,
    `*ip += x` the pointer pushed, calls summed before variables, a
    remainder pushed from DX, a comparison computed into SI, `e = ++d *
    2.0` the increment first - see "The round-8 fltprobe goldens"). Round
    9 settled round 8's new inferences and refusals: a prefix `++` of a
    long truncated (`x = ++l`) and a floating `+=` as an operand are done
    first, as statements (v7's `sreorder()`), a negative constant added to
    a long is two words (`mov si,*-5.` / `mov di,*-1.`), long shifts and
    divisions in place (`loop .-4`, `aldiv`), a call ordered by
    `acommute()` (`c / d + f(2)`, `b[i] + f(1)`), two comparisons summed
    (the left into DI, the right into SI), and the floating-stack model
    further (`p37_fltstk3`: seventeen messages; a constant argument's push
    is checked) - see "The round-9 fltprobe goldens". Round 10 settled
    round 9's: a long shifted by 1 in place, `jz .+16` for a compound
    shift by a variable, a non-negative long constant an int widened, a
    constant pushed first next to a computed long (`%n,n`), a product in
    AX shifted there, v7's `reorder()` hoisting a floating `+`'s right
    operand first, and the push of a computed floating argument unchecked
    (`p41_fltstk4`: twenty-three messages) - see "The round-10 fltprobe
    goldens". Round 11 settled round 10's, three of some seventy inferences
    corrected: a negative int constant widened is an LCON (two words), a
    remainder's working register is DX (`sub dx,x`), and the push of a
    floating operand read through an address (an element, a member through
    a pointer) is unchecked (`p45_fltstk5`: twenty-three messages); LTOI
    distributed over a long `+`/`-`, a divisor that makes a division or a
    call pushed first, hoists inside a hoisted `+=` left to right - see
    "The round-11 fltprobe goldens". Round 12 settled round 11's, eleven of
    some fifty inferences corrected (by eight rules): an LTOI'd `i + (l -
    m)` takes the difference first, an int `-` whose right operand is
    easy loads its left one into DI first and computes the right one into
    SI (v7's `%n,e` - `(int) (l - (m - l))`, `f(1) - (x < y)`), a
    remainder plus a variable or constant stays in DX, every computed
    divisor is pushed first (no `idiv di`), a call's product on the right
    of a `-` is pushed, a hoisted `+=` that is an operand of a `+` hoists
    its right operand right to left, and one hoisted from a comparison
    with two hoists inside is stored without a pop (`fstd` - the real
    compiler's own wrong code, unreported; `p49_fltstk6`: twenty-six
    messages, all as inferred) - see "The round-12 fltprobe goldens".
    Round 12 had been planned as the series' last unless it corrected
    more than a handful, so round 13 asked about those rules' neighbours,
    and it settled twelve of some sixty inferences by seven rules: an
    LTOI distributed in the long tree's own `acommute()` order (`(int) (l
    - (m + i))` adds m's low word to i) and over a negation, v7's `%n,e`
    with a remainder on the left (`mov di,dx`) or a product or scaled
    difference on the right, a call's value over a pushed divisor,
    `distrib()` over call products, a hoisted `+=` kept (`fstd`) under
    every comparison whose value is taken - its right operand not a leaf
    - and popped under a condition, a negation's hoists, and a compound
    store's message in upper case; besides, a `long` variable passed as
    an argument - pushed as its high word alone until then, silently
    wrong - and the stack model's top (six values: `p52_fltinf8`'s
    twenty-two "Floating point stack overflow; simplify expression") -
    see "The round-13 fltprobe goldens". That is more than a handful
    again, so round 14 (`p54`..`p57`) asks about the new rules'
    neighbours; running it is the first "Next up" in `STATUS.md`.
  * **Beyond those**: expand `mutos_c0`/`mutos_c1`'s grammar/opcode coverage
    category by category — see
    `src/mutos_cc/README.md`'s "Next steps" for the concrete
    dependency-ordered list: the floating shapes still refused (see
    `tests/mutos_cc/fltprobe/README.md`), then growing coverage
    into `tests/mutos_cc/11_kernel`'s real kernel driver sources (currently
    1/9 — see that directory's own paragraph above and `docs/DEVLOG.md`'s
    Milestone 4 section for the initial assessment), then the `mutos_cc`
    driver itself; the shapes still refused (see `src/mutos_cc/README.md`'s
    "Current scope") each wait for evidence of the real compiler's output.

### Milestone 5: Optimizer (`c2`) & NEC V30
* Enhancing the V7 peephole optimizer for x86 and activating the `-mv30` compiler flag switch.

### 📌 Active Goal
* See `STATUS.md` for the current, verified task-level status — it is updated
  per-change (see Workflow Guideline 6) and is more reliable here than a fixed
  snapshot would be.

