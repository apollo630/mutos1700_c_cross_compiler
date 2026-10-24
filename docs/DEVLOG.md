# MUTOS 1700 Cross-Compiler Toolchain — Development Log

This log is the durable, version-controlled home for the detailed technical
findings, debugging methodology and session-by-session history behind the
project - the "why" and "how we found out" for what `STATUS.md` and
`CLAUDE.md` state. `STATUS.md` is the authoritative *current-state*
tracker; when the two disagree, `STATUS.md` wins for the current state.
This log is a record: a part written at an earlier date says what was true
and known then, and is not re-verified every session.

Since 2026-10-20 the log is split into parts under [`docs/devlog/`](devlog/)
(the single file had grown past 9,000 lines). Every section kept its title,
so a reference elsewhere of the form "`docs/DEVLOG.md`'s *title*" - in code
comments, test sources or other documents - is found by searching this
index for the title. New findings go into the part they belong to, or into
a new part listed here.

---

## [Milestone 1 — `mutos_ld` (linker)](devlog/m1_ld.md)

The linker until its port (2026-10-22): archive support, the MUTOS deviations from the V7 `a.out`/`ld` format. The port: "The port of `ld`" in the part on the originals.

- Archive / `-l` implementation
- MUTOS-specific `a.out`/`ld` deviations from stock V7

## [Milestone 2 — `mutos_as` (cross-assembler)](devlog/m2_as.md)

The assembler: CPU reference (8086/80186/V30/80188), syntax, encodings, object format, implementation and bug-fix history, CLI, debugging methodology.

- CPU reference (8086/80186/V30/80188)
- Assembly syntax
- Confirmed encoding facts
- Object file / relocation table format
- Implementation history
- Bug-fix history
- Opcode coverage audit
- CLI reference
- Auxiliary deliverables
- Debugging methodology (reusable)

## [Milestone 2 — `mutos_as`: floating constants (2026-09-27 to 2026-09-29)](devlog/m2_as_float.md)

The assembler's floating constants: `.float`/`.double`, the real conversion re-enacted and pinned by real-hardware probes.

- `mutos_as`: `.float`, `lea <reg>,<label>` - `08_float` assembles, links and runs (`mutos_as` extended and verified this session)
- Real-hardware confirmation: `-L` is a manual/binary mismatch, and the two open `.float`/`.double` gaps are closed (2026-09-28)
- `mutos_as`: zero `.float` and `.double` implemented (2026-09-28)
- `mutos_as`: the real conversion, re-enacted (2026-09-28)
- `mutos_as`: rounding pinned, zeros on both paths, and `libc.a`'s own `atof` as a second oracle (2026-09-29)
- `mutos_as`: the real product, zeros at exponent 0, and `libcatof.py` (2026-09-29)
- The overflow probes: `float_open/fltovf.s` and `fltsig.s` (2026-09-29)
- `mutos_as`: out of range - `ldexp` wraps, an overflow aborts (2026-09-29)

## [Milestone 3 — `mutos_cpp` (C preprocessor)](devlog/m3_cpp.md)

The preprocessor until its port (2026-10-22): golden generation command, the re-implementation's two bugs worth remembering, its output-line accounting rule. The port: "The port of `cpp`" in the part on the originals.

- The two bugs worth remembering
- Core output-line-accounting rule

## [Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler)](devlog/m4_foundations.md)

The compiler's groundwork: method, ABI headline findings, the `c0`/`c1` split, what building the corpus on real MUTOS 1700 hardware showed about its tools (`make`, `cc`, `find`).

- Method
- Headline findings (see `docs/MUTOS_C_ABI.md` for full derivation and citations)
- Open item
- `c0`/`c1` process split: decision and rationale
- MUTOS 1700 host-tooling findings

## [Milestone 4 — the hand-written `mutos_c0`/`mutos_c1` (history, until 2026-10-18)](devlog/m4_handwritten.md)

History until 2026-10-18: the hand-written `mutos_c0` (`c0_parser.c`) and `mutos_c1` (`c1_gen.c`), corpus category by category, with the byte-level derivation of the real compiler's output.

- `temp1`/`temp2` wire format: byte-level derivation (`mutos_c0`/`mutos_c1` built and verified this session)
- Local variables: byte-level derivation (`01_expr/01_intarith` — `mutos_c0`/`mutos_c1` extended and verified this session)
- Bitwise operators and the `*`/`#` immediate marker: byte-level derivation (`01_expr/02_bitwise` — `mutos_c0`/`mutos_c1` extended and verified this session)
- Relational/equality/logical operators and short-circuit codegen: byte-level derivation (`01_expr/03_rellogic` — `mutos_c0`/`mutos_c1` extended and verified this session)
- Shift operators: byte-level derivation (`01_expr/04_shift` — `mutos_c0`/`mutos_c1` extended and verified this session)
- Increment/decrement, pointers and arrays: byte-level derivation (`01_expr/05_incdec` — `mutos_c0`/`mutos_c1` extended and verified this session)
- Compound assignment operators, and a real lexer bug: byte-level derivation (`01_expr/06_compasgn` — `mutos_c0`/`mutos_c1` extended and verified this session)
- The `?:` conditional and `,` comma operator, plus a confirmed `ASSIGN`-value-consumer: byte-level derivation (`01_expr/07_ternary` — `mutos_c0`/`mutos_c1` extended and verified this session)
- `char`/`long`, casts, `sizeof`, and a new MUTOS-specific opcode: byte-level derivation (`01_expr/08_castsize` — `mutos_c0`/`mutos_c1` extended and verified this session)
- `02_long/02_muldiv` — `long` `*`/`/`/`%` via runtime helpers
- `03_ctrlflow` (all 7 files) — a real recursive-descent statement dispatcher, plus `02_long/01_addsub` falls out for free
- `04_funcs` (6 of 7 files) — function parameters, calls, local `static` variables, and function pointers
- `02_long/03_retval` and `04_funcs/06_regclass` — `long` return values and real register-variable allocation: byte-level derivation (`mutos_c0`/`mutos_c1` extended and verified this session)
- `05_arrptr` (4 of 7 files) — pointer-degree chaining, array subscripting, and two real `c1` register-allocation surprises: byte-level derivation (`mutos_c0`/`mutos_c1` extended and verified this session)
- `c1_gen.c` review: emission layer, register-occupancy guard, and the `09_abiprobe` `SETSTK` threshold (`mutos_c1` changed and verified this session)
- `05_arrptr/02_array2d` — 2-D arrays, `distrib()`, and the real constant-shift threshold (`mutos_c0`/`mutos_c1` extended and verified this session)
- String literals (`05_arrptr/05_arrofptr`, `07_strlibc`), `09_abiprobe/01_argvmain`, and six older `mutos_c1` bugs found on the way (`mutos_c0`/`mutos_c1` extended and verified this session)
- Evaluation order - the plan for `10_integ/05_matmul`
- Evaluation order - implemented for `10_integ/05_matmul` (`mutos_c1` extended and verified this session)
- Constant left operands, and semantic fuzzing (`mutos_c0`/`mutos_c1` fixed, `tests/mutos_cc/fuzz/` added, verified this session)
- Conditional evaluation: `&&`, `||`, `?:`, `,` and postfix `++`/`--` in conditions (`mutos_c1` fixed and verified this session)
- `char` element access, call arguments right to left, and `02_bubsort`'s comparison shapes (`mutos_c0`/`mutos_c1` extended and verified this session)
- `07_scope` - file-scope variables and block scope (`mutos_c0`/`mutos_c1` extended and verified this session)
- `10_integ/01_wordcount` - a file-scope char array, character constants, a char compared with a constant (`mutos_c0`/`mutos_c1` extended and verified this session)
- `06_struct` and `10_integ/03_linklist` - structs, unions, bit-fields, enums, typedefs; more `char` (`mutos_c0`/`mutos_c1` extended and verified this session)
- Test corpus: `tests/mutos_cc/11_kernel` added — real kernel driver golden
- `08_float` — floating point through libc's software floating-point runtime (`mutos_c0`/`mutos_c1` extended and verified this session)

## [Milestone 4 — floating point beyond the corpus: `libc.a`'s compiled C and the `fltprobe` rounds](devlog/m4_fltprobe.md)

History: floating point beyond the corpus - `libc.a`'s compiled C and the `fltprobe` rounds 1 to 13 against the hand-written `mutos_c1`; the real assembler on `p19_open3.s`.

- Floating shapes from libc.a's compiled C (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-09-29)
- The fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-09-29)
- The round-2 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)
- The round-3 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)
- The round-4 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)
- The round-5 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-03)
- The round-6 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-04)
- The round-7 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-04)
- The round-8 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-07)
- The real assembler on `p19_open3.s` (real-hardware finding, 2026-10-07)
- The round-9 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-09)
- The round-10 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)
- The round-11 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)
- The round-12 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)
- The round-13 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)

## [Milestone 5 — Optimizer (`c2`) & NEC V30 (`-mv30`)](devlog/m5_c2_v30.md)

The optimizer and the NEC V30: why `-mv30` code must keep the ABI, and why not `PREPARE`/`DISPOSE`.

- Hard constraint (settled)
- Evidence: `docs/V20_V30_Users_Manual_Oct86.pdf`
- `PREPARE`/`DISPOSE` vs. the discrete instruction sequence — real numbers
- Recommendation

## [The original binaries and the ports of `c1`, `c0`, `cpp`, `ld` and `cc`](devlog/originals_and_ports.md)

The original binaries read and run under an emulator; the ports of `c1` and `c0`; the original `cc`, `ld`, `as`, `c2` and `cpp` as oracles; step 0 of the plan - `fork`/`exece`/`wait` faked, the traced `cc`, the original `ld` against `mutos_ld`, the MUTOS root (2026-10-20); the ports of `cpp` and `ld` (2026-10-22); the port of `cc` (2026-10-24).

- Plan after round 13: read the original `c1` (2026-10-10)
- The original binaries (2026-10-12)
- The port of `c1` (2026-10-13)
- The port of `c0` (2026-10-14, 2026-10-18)
- The other originals as oracles (2026-10-20)
- Step 0 of the plan: the infrastructure (2026-10-20)
- The port of `cpp` (2026-10-22)
- The port of `ld` (2026-10-22)
- The port of `cc` (2026-10-24)

## [Recurring process lessons](devlog/process_lessons.md)

Lessons that apply to every milestone.
