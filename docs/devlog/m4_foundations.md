# Milestone 4 — `mutos_cc`/`mutos_c0`/`mutos_c1` (C compiler)

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). The compiler's groundwork: method, ABI findings, the `c0`/`c1` split, and what building the golden corpus on real hardware taught about the MUTOS tools.

---

**Status (2026-10-20):** `mutos_c1` and `mutos_c0` are ports of the original `c1`
and `c0` (since 2026-10-13 and 2026-10-18) and reproduce every golden; the driver is
next - see `STATUS.md` and `docs/TOOLCHAIN_PLAN.md`. (When this part was begun, the
hand-written passes covered 20 of the 62 corpus files.) This section starts with the
groundwork done ahead of any code: ABI/calling-convention research, the `c0`/`c1`
process-split decision, and the K&R test corpus — so `mutos_c1`'s code generator had
a byte-level-accurate target from day one instead of guessing generic
8086-C-compiler conventions, and so a verification path was lined up before it
existed — then continues below with the byte-level wire-format derivation and
host-tooling findings from actually building against it. Full ABI detail, with every rule cited against a
real disassembled `.o`, lives in **`docs/MUTOS_C_ABI.md`** (not duplicated here in
full — the ABI subsection below is the condensed "why/how we found out" pointer
into it, matching this file's usual role).

## Method

Real hardware-linked evidence, primarily two sources: `tests/mutos1700_crt0/crt0.o.base64.txt`
(the real startup object) and ~15 hand-picked files out of `tests/mutos1700_libc/`'s
167 real linked objects (chosen to cover: a trivial 1-arg function, 2-arg functions,
`long`-returning/`long`-parameter functions, the compiler's own long-arithmetic
runtime helpers, a large-local-frame function, and the `exit`/cleanup chain) — this
is the **load-bearing** evidence for the calling convention itself.
`tests/mutos_as/kernel_opt/mch.s` was used as a **third, supplementary** source, but
**not** as compiler output: a mid-session correction (prompted by a reviewer noticing
`mch.s`'s own `_outb`/`_out`/`_in`/`_inb`/`_hdio` use `bx`, not `bp`, as their frame
pointer — see item 9 below) established that `mch.c`
(`tests/mutos_cpp/c/mch.c`), unlike the kernel's other four `mutos_cpp` corpus C
files, opens with the comment `Assemblerteil MUTOS 1700/1834` ("the assembly
portion") and is 100% hand-written MUTOS-assembler source from its first line, using
a `.c` filename only so it flows through the same `cpp` build step (`#include`/
`#ifdef`) as genuine C files — it is **never** passed through `c0`/`c1`/`c2`. It
remains useful because it's the one place `cret`'s exact implementation is available
as literal source, and it contains two hand-written-but-explicitly-`C-Callable`
helper functions (`_co`, `_ci`) that a human deliberately conformed to the real
compiler ABI for interop — corroborating, not independently proving, the same
convention `libc.a`'s genuine compiler output establishes. Text segments were
extracted from each `.o`'s already-understood `mutos_aout.h` header layout and
disassembled with `objdump -D -b binary -m i386 -M intel,i8086`; relocation entries
were decoded with the project's already-established rules (shift-flag/symidx/type
bits) to identify call targets by name.

## Headline findings (see `docs/MUTOS_C_ABI.md` for full derivation and citations)

1. **Pure stack ABI, no register arguments.** Right-to-left push order, caller
   cleans up (`add sp,N`) after every call — confirmed at every single call site
   examined, no exceptions.
2. **Fixed, unconditional prologue/epilogue** for every compiler-generated function,
   regardless of actual register/local usage: `push bp / mov bp,sp / push di /
   push si` … `jmp cret`. `cret` itself — quoted verbatim from `mch.s`'s
   hand-written source (see "Method" above) — is `lea sp,#-4(bp) / pop si / pop di /
   pop bp / ret`; the `lea`-relative-to-`bp` trick is why one shared routine can
   serve every function regardless of local-frame size. Confirmed unconditional via
   genuine compiler output in `libc.a`: `_abs` (uses `di` but not `si`) and
   `_strlen` (uses both) still save/restore both registers unconditionally.
   `mch.s`'s hand-written-but-`C-Callable` `_ci` (zero params, uses neither `di` nor
   `si` internally) independently shows the same shape, as corroborating rather than
   primary evidence (see "Method").
3. **Frame layout is fixed too**: parameters at `bp+4, bp+6, ...`; locals always
   start at `bp-6` (the `bp-2`/`bp-4` slots are permanently reserved for saved
   `di`/`si`, whether or not a given function has any real locals there). Confirmed
   via 2-word (`atol.o`) and 4-word (`sprintf.o`) local-block examples.
4. **`long` = 2 words, high word at the lower address, everywhere** — locals,
   register-pair returns, by-reference operands, and by-value parameters alike.
   This is the exact same PDP-11 "middle-endian" word order already established for
   `ar` archive `long` fields (see Milestone 1's entry), now directly confirmed to
   also govern compiled C `long` values — resolving `CLAUDE.md`'s note that this
   "hasn't been relevant yet." Confirmed three independent ways (a `long` local in
   `atol.o`, a by-reference `long` in `almul.o`/`aldiv.o`, and the by-value `long`
   parameter of `_lseek`'s `offset`).
5. **Return values**: `AX` for 16-bit scalars, `DX:AX` (`DX`=high) for `long` —
   confirmed via `_atol`'s and `aldiv`/`ldiv`'s final `mov ax,.. / mov dx,..` before
   their epilogues.
6. **A second, *different* internal-only ABI** for the compiler's own
   `long`-arithmetic runtime helpers (`almul`/`aldiv`/`alrem`, `lmul`/`ldiv`/`lrem` —
   note: no leading `_`, i.e. not user-callable C symbols). These use an *extended*
   prologue (`push bp / push bx / mov bp,sp / push di / push si`, shifting params to
   start at `bp+6`) and a matching custom epilogue (not `jmp cret`, since `cret`
   doesn't know about the extra saved `bx`). First parameter is a pointer to a `long`
   that's both an input operand and the in-place result destination; this is a
   private contract between `mutos_c1`'s own codegen and its own runtime support
   library, **not** part of the general C function ABI.
7. **Large stack frames get a guard**: above some threshold, `sub sp,N` is replaced
   by `mov ax,N / call chkstk`, which checks the new `sp` against a stack-bottom
   limit, attempts to grow the stack via a far call through a runtime-initialized
   trampoline pointer on failure, and ultimately does `_kill(_getpid(), SIGSEG)` +
   `__exit` if growth genuinely fails — a real stack overflow terminates with
   `SIGSEGV`-equivalent behavior. Empirically bounded via corpus scan: largest plain
   `sub sp,N` seen is `N=76`; smallest `call chkstk` seen is `N=256` — exact real
   cutoff not pinned down further by this corpus.
8. **`crt0` startup**, fully annotated in `docs/MUTOS_C_ABI.md`: reads `argc`/`argv`
   off the kernel-provided initial `sp`, scans for the `argv[]` NULL terminator
   (**correction worth flagging**: the scan uses `test WORD PTR [bx],0xffff` /
   `jne`, which is an AND-with-all-ones zero-test, i.e. an ordinary NULL check — an
   initial fast read of the raw bytes mis-suggested a `0xFFFF` sentinel value; always
   trace the actual flag semantics, not just the literal immediate, when reading
   `TEST`), sets the global `_environ`, calls `_main(argc, argv, envp)` per the
   standard ABI (§1 above), and — critically — calls **`exit()`** (asm symbol
   `_exit`, defined in `cuexit.o`) after `main` returns, **not** the raw
   `_exit()`/`__exit` syscall wrapper (`exit.o`). `exit()` calls a `_cleanup()` hook
   (asm symbol `__cleanu` — `_cleanup` mangled + K&R-8-char-truncated) before the raw
   syscall; a classic two-object linker trick (`fakcu.o`'s no-op stub vs. the real
   flush routine bundled inside `flsbuf.o`) means a program only pays for stdio
   flush-on-exit if it actually uses stdio.
9. **Not every function-shaped routine is compiler output**, and hand-written
   assembly uses more than one internal style — documented explicitly in
   `docs/MUTOS_C_ABI.md` §1.10 (expanded this session) so a future session doesn't
   mistake any of these for a second valid *compiler* convention:
   - Trivial 1:1 syscall stubs (`libc.a`: `access.o`, …) — `mov ax,N / jmp sysNa`.
   - **`bx`-as-frame-pointer leaf routines, no `bp` at all** — `mov bx,sp` directly,
     params at `[bx+2]`, `[bx+4]`, … This is in fact the **prevailing** style in
     `mch.s` itself (25 occurrences, several with an explicit human comment like
     `| bx is frame pointer`, vs. only 6 using the full `push bp` C-ABI prologue) —
     confirmed via real, `.globl`'d, C-linkage examples `_in`/`_inb`/`_out`/`_outb`/
     `_hdio`. The same idiom, inferred from disassembly alone before these `mch.s`
     examples confirmed the pattern's intent, is also used by a handful of `libc.a`
     syscall wrappers (`dup.o`/`execv.o`/`shutdn.o`/`signal.o`). **This is the
     pattern a reviewer flagged** (correctly) as looking like a contradiction to
     item 2's `bp`-based convention — it isn't a contradiction, but it did expose
     that this document's first version mischaracterized `mch.s` as compiler output
     (see "Method" above); the underlying `bp`-frame facts in items 1–8 were
     unaffected, only the `mch.s` attribution needed correcting.
   - `bp`-based but reordered and not using `cret` (`libc.a`'s `_lseek`): pushes
     `si` before `di` (opposite of item 2) with its own matching inline epilogue —
     internally consistent, just independent of the shared `cret` routine.

## Open item

The exact `chkstk` size threshold (bounded to between 76 and 256 bytes by `libc.a`
above) is not pinned down further by the `libc.a` corpus; if a real object file with a local frame in that
range surfaces later (e.g. from generating more goldens off the kernel source tree
per Milestone 2's `conf/Makefile` mechanism, or from a userland `.c` file not yet in
this checkout), re-check it against this bracket. **Update:** `tests/mutos_cc/
09_abiprobe/02_frame080.c` … `07_frame300.c` now exists specifically to resolve this —
six otherwise-identical files with local buffers of 80/128/176/224/256/300 bytes,
bisecting the gap. Once compiled on real hardware, whichever ones emit `call
chkstk` vs. a plain `sub sp,N` pin the real cutoff down for the first time.
**Resolved (2026-09-23)**: the goldens are in - `02_frame080` uses `sub sp,*80.`,
all of `03_frame128` … `07_frame300` use `mov ax,#N.` / `call chkstk`, so the
real threshold is in `(80,128]`; see "`c1_gen.c` review" below for the details
and what `mutos_c1` now does with it. **Settled (2026-10-07)**: the
`fltprobe` goldens of round 7 (`p27_frame`: 90 bytes `sub sp`, 100 the
guarded call) and round 8 (`p29_frame2`: 92, 94, 96 and 98 bytes all `sub
sp,N`) leave no size open - a frame is even, so `sub sp,N` up to 98 bytes,
`mov ax,N` / `call chkstk` from 100: `(98,100]` (see "The round-8 fltprobe
goldens").

## `c0`/`c1` process split: decision and rationale

**Decision: keep `c0` and `c1` as two separate executables**, communicating
through a `temp1`/`temp2` file pair exactly as V7's own `cc` driver does (see
`v7/cc/cc.c`'s pipeline comment: `c0 source temp1 temp2` / `c1 temp1 temp2
assembly.s`). This also matches `CLAUDE.md`'s "Target Executables" list, which
already named `mutos_c0`/`mutos_c1` as separate deliverables before this
question was raised — the analysis below is the reasoning behind confirming
that choice deliberately rather than by default.

**The question.** The historic split exists because V7's `cc` ran on a PDP-11
with a 64K address space per process: parsing/semantic-analysis (`c0`) and code
generation (`c1`) each needed the full 64K to themselves, so they had to be
separate processes. That constraint doesn't exist on a modern 64-bit host, which
raises the obvious question: keep the two-phase split, or merge `c0`+`c1` into a
single `mutos_cc` binary? The concern driving the question was testability of
the final generated assembly, not implementation convenience.

**What reading the real `v7/cc` source settled it.** The `temp1`/`temp2`
intermediate format is not a raw memory/pointer dump (which would make the split
purely an artifact of the 64K limit, with no inherent value once that limit is
gone) — it is a small, fully-specified, tagged byte stream:

- `outcode()` in `c04.c` (~70 lines) is the *only* place `c0` writes to `temp1`/
  `temp2`. Format characters: `'B'` (opcode) writes `(opcode_low_byte, 0376)` —
  the fixed sentinel byte `0376` marks "this is an opcode", letting the reader
  distinguish an opcode from an ordinary data word (a legitimate 16-bit number
  never has high byte `0376`); `'N'` is a little-endian 16-bit word; `'S'`/`'F'`
  are a nul-terminated symbol name / float ASCII string; `'1'`/`'0'` are
  shorthands for the constants 1 and 0.
- `getree()` in `c11.c` (~270 lines) is the exact inverse: reads that stream,
  rebuilds an expression tree (`struct tnode`) for `EXPR`/`CBRANCH`/`C3BRANCH`
  nodes, and emits PDP-11 text directly for the many administrative pseudo-ops
  (`PROG`/`DATA`/`BSS`/`.globl`/`SETSTK`/`SNAME`/`ANAME`/`RNAME`/…).

**Why this changes the calculus.** Because the format is this well-defined and
small, preserving it (nearly as-is) buys real, cheap benefits independent of
whether `c0`/`c1` are one process or two:

1. An inspectable IR boundary that can be unit-tested on its own — `mutos_c0`'s
   front end (parsing, type promotion, tree shape, scope/storage-class handling)
   can be fully verified against hand-written expected IR *before* a single line
   of x86 code generator exists, by adding an optional human-readable dump mode
   to `mutos_c0` (not present in the original V7 `cc`, but cheap to add on top of
   the same encoding).
2. A clean separation between front-end and back-end bugs during the hardest
   part of this milestone: the code generator itself. `v7/cc`'s back end
   (`c10`–`c13` + `table.s`) is a tree-pattern matcher (`match()`/`cexpr()`/
   `reorder()` in `c10.c`) driven by PDP-11 instruction-template tables
   (`cctab`/`efftab`/`regtab`/`sptab`, defined in `table.s` as literal PDP-11
   assembly-template strings like `%a,n`). This is fundamentally tied to the
   PDP-11's register model (6 orthogonal general registers R0–R5, symmetric
   `mov`-style two-operand instructions, hardware autoincrement/autodecrement
   mapped 1:1 onto C's `++`/`--`). The 8086 has none of that regularity (`AX`
   required for `MUL`/`DIV`, `CX` required for shift counts, only `BX`/`BP`+
   `SI`/`DI` usable for addressing) — porting this is a genuine redesign, not a
   table refresh, **and it costs exactly the same whether `c0`/`c1` are merged
   or split**. The split doesn't shrink that work; it only lets bugs in it be
   isolated from front-end bugs without waiting for a complete `.s` file every
   time.

**What does *not* change.** Final verification of generated assembly is
identical either way: byte-diffing `mutos_c1`'s `.s` output against real
hardware-linked goldens, exactly the methodology already validated for
Milestone 3 (`mutos_cpp`) and now being extended to Milestone 4 via
`tests/mutos_cc/`. The split is a development-time diagnostic aid, not a
verification mechanism in itself — the golden-diffing process would be
identical if `c0`+`c1` were merged into one binary.

**Concrete follow-on**: `tests/mutos_cc/` (62 K&R C files across 11 categories,
`Makefile`, `README.md`) now exists to seed that golden-diffing process once
`mutos_c1` exists; `*.s.golden` references are being generated on real MUTOS
1700 hardware next, category by category. Two identifiers in the corpus
(`factorial`, `swapchar`) turned out to exceed this toolchain's real 7-character
external-identifier limit (see `CLAUDE.md`'s new "Identifier length limits"
rule, derived from `v7/cc/c0.h`'s `NCPS 8` plus the mandatory leading `_` seen
in `outcode()`) and were renamed (`fact`, `swapch`); every file/directory name
in the corpus was also checked against MUTOS 1700's real `DIRSIZ`=14 filename
limit (`tests/mutos_cpp/h/dir.h`, `h/param.h`) and shortened where needed.

## MUTOS 1700 host-tooling findings

Getting the corpus's golden-generation pipeline actually running on real
hardware (rather than just designed against manpages and `v7/cc` source
reading) surfaced several real bugs in this project's own scripts and
Makefiles — none of them about `mutos_cc` design, all of them about the real
V7-heritage tools those scripts have to run correctly under. Recorded here in
the order they were found, since each fix only surfaced the next problem.

**1. `make -f Makefile.mutos`: `Must be a separator on rules line 35. Stop.`**
Turned out to be user error, not a tooling bug — `gen_mutos.sh` (a shell
script) had been passed to `make -f` by mistake, which tried to parse shell
syntax as makefile rules and choked on the `for` loop's bare `do` (no `:`, no
leading tab, so `make` expected a rule separator and found none). Fixed by
making the distinction impossible to miss: a loud warning at the very top of
`gen_mutos.sh` itself, and the two invocations visually separated in
`tests/mutos_cc/README.md`'s workflow section. Real `make(1)`'s own manpage
was checked at this point (person-supplied `basename_1.d`/`expr_1.d`/
`find_1.d`/`make_1.d` troff source) and confirmed: no `%.o: %.c` pattern
rules, no `$(wildcard)`/`$(dir)`/`$(notdir)` functions, no `:=` — only plain
`=` macros and two-suffix rules like `.c.o:`. Also confirmed from the
manpage's own *Fehlerquellen* section: shell state (notably `cd`) does not
carry across separate recipe lines, since each line runs in its own
subshell — every recipe in this project's `Makefile.mutos` files is
deliberately written as one `;`-joined shell line because of this.

**2. `make -f ./Makefile.mutos`: `Make: line too long. Stop.`** The
single top-level `Makefile.mutos` (covering all 62 test files) built its
`all:` target from one backslash-continued dependency line listing all 124
`.s`/`.1` outputs — joined, ~2946 characters across 63 physical lines. The
longest individual recipe line in the same file was only 110 characters, so
this pinned the fault precisely on that one aggregate line, not on any
per-file command. Fixed by replacing the flat list with a chain of small
targets (`chk01: file1.s file1.1`, `chk02: chk01 file2.s file2.1`, …,
`all: chk62`), so no logical line ever exceeds ~110 characters regardless of
how many files the corpus grows to.

**3. Same file, next run: three `Warning: CC/CPP/C0 changed after being
used` warnings, then `Make: out of memory. Stop.`**, partway through the
very *first* category (`00_smoke`, 3 files) despite the chain fix above.
This ruled out per-line length as the remaining cause (already fixed) and
pointed instead at total makefile size: ~490 lines, 62 chain links plus 124
real per-file targets, apparently exceeding some fixed-size internal
table/arena this `make(1)` allocates once for the whole run — a genuinely
different failure mode from #2, not the same bug resurfacing. The warnings
are presumed to be a symptom of the same resource exhaustion (this `make`'s
built-in default macros for `CC` etc. getting corrupted/reused once table
space ran low), not a separate, independent bug. Fixed structurally: one
small `Makefile.mutos` per category directory (2–9 files, 42–84 lines each)
instead of one covering all 62, run from inside each directory (`cd
00_smoke && make -f Makefile.mutos`) so no single invocation's makefile gets
anywhere near whatever the real limit is. `tests/mutos_cc/README.md`'s
workflow section and `CLAUDE.md`'s `/tests/mutos_cc/` bullet were updated to
match; the top-level `tests/mutos_cc/Makefile.mutos` was deleted outright
(no longer represents anything real).

**4. `make -f Makefile.mutos` (per-category, this time): ran clean, but
produced no `.s` files at all** — `.i`/`.1`/`.2` present and correct, `.s`
silently missing for every file. Root cause: `cc`'s `-P` and `-S` cannot be
combined on this compiler. `v7/cc/cc.c`'s own control flow — not just its
flag-parsing `switch`, which just sets `pflag`/`sflag` independently and
gives no hint of an interaction — makes this explicit:

```c
av[1] = tmp4;
tsp = savetsp;
av[0]= "c0";
if (pflag) {
	cflag++;
	continue;          /* <-- for every source file, unconditionally */
}
...
if (sflag)
	assource = tmp3 = setsuf(clist[i], 's');
```

`-P` (`pflag`) makes `cc` `continue` to the next source file immediately
after `cpp` runs, before c0, c1, or the `sflag`-driven `.s`-naming logic a
few lines further down is ever reached. So `cc -P -S foo.c` only ever
produces `foo.i` (cpp's output, since `pflag` also redirects `tmp4` to
`setsuf(clist[i], 'i')` a few lines earlier) — `-S` never gets a chance to
matter. This directly explains why Milestone 3's own established convention
(`cc -P -DM7100 ... file.c`, no `-S`, used to generate the `.i.golden`
corpus) was always correct: for a preprocess-only run, `-P` alone is exactly
the right, and only meaningfully possible, flag. The mistake was assuming
`-P` plus `-S` would compose (both effects together) when designing this
corpus's own `.s`-generating recipes — they don't compose, `-P` simply wins
by exiting first. Fixed by dropping `-P` from every `cc -S` invocation
across `tests/mutos_cc/Makefile`, all 11 `Makefile.mutos` files, and
`gen_mutos.sh`'s final `cc` call; the separate direct `cpp -P` calls used
to produce `.i`/`.1`/`.2` are unaffected, since they don't go through
`cc`'s driver logic and so never hit this interaction at all. Confirmed
this doesn't threaten byte-parity with the rest of the golden set: `c0`'s
own intermediate-code opcode set (`v7/cc/c0.h`) has no line/file-tracking
opcode at all, so whether `cpp` inserted `# N "file"` line markers (the
actual, sole difference `-P` makes to `cpp` itself) has no path into the
resulting `.s` text.

**5. `make goldens` on the modern host (Linux), after transferring the
now-correct `.s`/`.i`/`.1`/`.2` back: `/bin/sh: 1: /lib/c0: not found`,
`make: *** [Makefile:114: 00_smoke/01_emptymain.1] Error 127`.** The
top-level `Makefile`'s `goldens` target depended on `all intermediates`,
which made Make re-check `.s`/`.1` freshness against `.c`/`.i` by mtime
before packaging anything — standard Make behavior, but wrong for this
specific workflow, where the actual generation happens on a *different*
machine (MUTOS) than the packaging step (Linux). A fresh `git checkout`, or
just the mechanics of transferring files off MUTOS, routinely doesn't
preserve the "each stage newer than its input" timestamp ordering Make
assumes; when it doesn't, Make decides an already-correct `.s`/`.1` is stale
and tries to rebuild it via `$(CC)`/`$(CPP)`/`$(C0)` — paths to binaries
that exist only on real MUTOS hardware, not on the packaging host. Fixed by
making `goldens` prerequisite-free: it now only packages whatever
`.s`/`.i`/`.1`/`.2` files already exist on disk (skipping, not failing on,
any that are missing), and never attempts to (re)create anything itself.
Verified against the exact failure scenario (a `.c` file's mtime forced
newer than already-generated `.s`/`.1` siblings, replicating what a fresh
`git checkout` does): `make -n goldens` now shows only `cp`/`base64`
commands, never `cc`/`cpp`/`/lib/c0`.

**End state, confirmed working:** with fixes #1–5 applied, `make -f
Makefile.mutos` inside a category directory on real MUTOS 1700 hardware
correctly produces `.s`/`.i`/`.1`/`.2` for every file in that category
(confirmed for `00_smoke`), and `make goldens` on the modern host, after
transferring those files back, correctly packages them into
`*.s.golden`/`*.i.golden`/`*.1.golden`/`*.2.golden` (+ base64 companions)
without attempting to invoke any MUTOS-only tool. The golden-generation
pipeline designed in the previous subsection is now empirically validated,
not just designed.
