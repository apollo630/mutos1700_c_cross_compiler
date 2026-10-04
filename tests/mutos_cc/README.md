# mutos_cc Milestone 4 — K&R C Test Corpus

Purpose: give `mutos_cc`/`mutos_c0`/`mutos_c1` an incremental, construct-by-
construct verification corpus **before any code generator exists**, using
exactly the same methodology that already got Milestone 3 (`mutos_cpp`) to
"complete, byte-for-byte identical to real hardware" — see `STATUS.md` and
`tests/mutos_cpp/run_goldens.sh` in the main repo:

1. This corpus is compiled with the **real, unmodified `cc`** on real MUTOS
   1700 hardware (or an accurate emulator) — and, separately, run through
   `cpp` and `/lib/c0` directly, to also capture `c0`'s raw `temp1`/`temp2`
   intermediate-code output for each test case, not just the final `.s`.
2. Every resulting `.s`/`.i`/`.1`/`.2` file is saved as a golden reference
   (`*.s.golden`/`*.i.golden`, parallel to Milestone 3's `*.i.golden`
   naming; `*.1.golden`/`*.2.golden` plus base64 companions for the binary
   `temp1`/`temp2` streams).
3. Once `mutos_c1` exists, its output is diffed byte-for-byte against the
   `.s` goldens, exactly like `mutos_as`/`mutos_cpp` are diffed today —
   and, separately, once `mutos_c0` exists, *its* output can be diffed
   against the `.1`/`.2` goldens on its own, before `mutos_c1` even needs
   to be ready. That's the whole point of capturing this boundary
   directly instead of only the final assembly: see `docs/DEVLOG.md`'s
   Milestone 4 "c0/c1 process split" section for why an inspectable IR
   boundary was worth keeping in the first place.

Nothing here is meant to *run* or be linked (though most of it could be);
the deliverable is the generated **assembly text**, not an executable.

## Two naming constraints this corpus is checked against

**Every directory and file name is ≤14 characters, including the
extension.** This is not an arbitrary style choice: it is MUTOS 1700's real
filesystem limit, inherited unmodified from V7 Unix —
`tests/mutos_cpp/h/dir.h` / `h/param.h` in the main repo both define
`DIRSIZ 14` (`char d_name[DIRSIZ]`). A real V7 filesystem doesn't reject an
over-length name, it **silently truncates** it — two files whose first 14
characters collide would clobber each other on the real hardware without
any error. Every `.c`/`.s` pair below stays within that limit (several are
exactly at 14, e.g. `01_wordcount.c`, `05_breakcont.c`); see the Makefile
comment for why the longer `*.s.golden` suffix is deliberately applied only
on the modern host, never on MUTOS itself. The `.i`/`.1`/`.2` extensions
used for `cpp`'s output and `c0`'s `temp1`/`temp2` streams (see Workflow
below) are the same 2-character width as `.c`/`.s`, so every base name that
already fits `.c`/`.s` automatically fits those too — no separate audit
needed for them.

**Every identifier respects this toolchain's real significant-character
limits: 8 for internal names, 7 for external (global-linkage) names.**
This traces to two independent, corroborating facts already found in the
repo: `v7/cc/c0.h`/`c1.h` define `NCPS 8` (the compiler's own symbol-table
name field is 8 bytes — two internal names agreeing on their first 8
characters are the same symbol as far as `c0`/`c1` are concerned), and
`man/mutos_aout.h.5`'s `mutos_sym_t.name[8]` shows the **object-file**
symbol table entry is also exactly 8 bytes — but every name that reaches
that table first gets a mandatory leading `_` prepended (confirmed
directly in `v7/cc/c04.c`'s `outcode()` and echoed in `c02.c`'s handling of
`EXTERN`/`STATIC`/`CSPACE` declarations), which consumes one of those 8
bytes. So a name with real linkage — any function, and any file-scope
variable whether `static` or not — has exactly 7 characters of its own
name available; a purely local/automatic name (locals, parameters, struct/
union members, `typedef` names, `enum` constants, labels) has the full 8,
since those never leave the compiler's own front-end symbol table at all.

Two identifiers in the first draft of this corpus violated the 7-character
external limit and were renamed (functionally identical otherwise):

| File | Old name | New name |
|---|---|---|
| `04_funcs/03_recfact.c` | `factorial` (9 chars) | `fact` |
| `10_integ/04_strrev.c` | `swapchar` (8 chars) | `swapch` |

A handful of other identifiers sit exactly at the boundary and were kept
as-is since they don't exceed it: the functions `counter`, `addlong`,
`reverse` (7 chars, external) and the struct member `botright` (8 chars,
internal).

## Layout

62 small, focused K&R C files across 11 numbered directories, ordered
roughly by increasing difficulty so they can be tackled — and diffed —
one directory at a time. Directory and file names are deliberately short
(see the DIRSIZ=14 constraint above); the table below is the authoritative
mapping from short name to intent.

| Dir | Focus | Files |
|---|---|---|
| `00_smoke` | Absolute minimum programs | `01_emptymain`, `02_retconst`, `03_retexpr` |
| `01_expr` | Every scalar operator | `01_intarith`, `02_bitwise`, `03_rellogic`, `04_shift`, `05_incdec`, `06_compasgn`, `07_ternary`, `08_castsize` |
| `02_long` | `long`: locals, `*`/`/`/`%` (→ the `lmul`/`ldiv`/`lrem` runtime helpers), return value, parameter passing | `01_addsub`, `02_muldiv`, `03_retval`, `04_params` |
| `03_ctrlflow` | `if`/`while`/`do`/`for`/`switch`/`goto` | `01_ifelse`, `02_while`, `03_dowhile`, `04_for`, `05_breakcont`, `06_switch`, `07_goto` |
| `04_funcs` | K&R old-style definitions, >4 args, recursion, `static`/`register`, function pointers | `01_call`, `02_manyargs`, `03_recfact`, `04_mutrec`, `05_staticvar`, `06_regclass`, `07_funcptr` |
| `05_arrptr` | Arrays, pointers, strings via libc | `01_arrbasic`, `02_array2d`, `03_ptrbasic`, `04_ptrarreq`, `05_arrofptr`, `06_ptrptr`, `07_strlibc` |
| `06_struct` | struct/union/enum/typedef, incl. whole-struct assignment (`STRASG`) and bit-fields | `01_stbasic`, `02_stptr`, `03_starray`, `04_stassign`, `05_nestst`, `06_union`, `07_bitfield`, `08_enum`, `09_typedef` |
| `07_scope` | file-scope `static` vs. extern, block-scope shadowing, forward `extern` | `01_globstat`, `02_shadow`, `03_externdef` |
| `08_float` | `float`/`double` (compile-only, see file headers) | `01_floatbas`, `02_dblconv` |
| `09_abiprobe` | Targeted ABI probes, see below | `01_argvmain`, `02_frame080` … `07_frame300` |
| `10_integ` | Small realistic programs | `01_wordcount`, `02_bubsort`, `03_linklist`, `04_strrev`, `05_matmul` |

`fuzz/` is not part of the corpus: it holds host-only random-program
fuzzing tools for `mutos_c0`/`mutos_c1` (`make fuzz` from the repo root -
see `fuzz/README.md`). It contains no `.c` files, so nothing that walks
this corpus (`run_goldens.sh`, `gen_mutos.sh`, the Makefiles) sees it.

`fltprobe/` is not part of the corpus either: floating-point probe programs
with their own `Makefile.mutos` and real-hardware goldens (see
`fltprobe/README.md`) - a first round of nine, a second, a third and a
sixth of four, a fourth and a fifth of three, a seventh of five, all
byte-exact (they stay here rather than joining `08_float`; `p21_fltexp`
and `p28_fltstk` together with the real compiler's own error messages,
see below) but `p19_open3`, whose `.s.golden` ends in the real compiler's
own invalid output (see below), and an eighth round of five
(`p29`..`p33`) whose goldens are still to be generated. Unlike `fuzz/`, it
does hold `.c` files: `gen_mutos.sh` and the Makefiles walk it like a
category, and `run_goldens.sh` skips each file until it has goldens - one
with a `.1.golden` but no `.i.golden` (a set brought back without its `.i`
files, as round 2's first was) it checks from `mutos_c0` on and lists
apart (category 7).

`invalid_goldens.txt` lists the goldens whose `.s` is not valid assembly
from some line on - the real compiler's own broken output, kept as it
came back. At present one: `fltprobe/p19_open3`, whose `d = (d * e) + u`
has 118 bytes of libc's `_ctype_` table where a register name belongs
(the real `c1` asks for the register pair starting at AX). For a listed
file `run_goldens.sh` requires `mutos_c1` to refuse it and its output up
to the refusal to be exactly the golden's leading valid lines (category
8, not a failure); `../mutos_as/assemble_cc_goldens.sh` requires
`mutos_as` to refuse it.

`c1_errors.txt` lists the goldens for which the real compiler's `c1`
reported errors - and wrote the whole `.s` all the same (`cc -S` keeps
it; `c1` exits with status 1). At present two: `fltprobe/p21_fltexp`,
whose `f = half(d = 3.0)` the real compiler compiles into code that pops
the floating-point stack twice, and whose `c1` says so - "56: floating
point stack underflow", "57: Floating point stack underflow" - and
`fltprobe/p28_fltstk`, the same wrong code on purpose, twelve messages
from a model of that stack which is never reset. For a
listed file `run_goldens.sh` requires `mutos_c1` to exit with a nonzero
status, to print exactly the listed messages and to write exactly the
golden `.s` (category 9, not a failure).

## `11_kernel` — real MUTOS kernel driver golden corpus

`11_kernel/` is a second, separate golden corpus alongside the 62-file
table above, not one more entry in it. Where `00_smoke` through
`10_integ` are small, single-construct probes written for this corpus,
`11_kernel` holds nine **real, unmodified MUTOS 1700 kernel driver
source files**, ordered easy to hard: `01_delay.c`, `02_prim.c`,
`03_mem.c`, `04_pipe.c`, `05_nami.c`, `06_fio.c`, `07_v24.c`, `08_tty.c`
and `09_amx.c` (the AMX serial-board driver, the largest at ~40 KB) -
plus the local header tree they `#include` (`a.out.h`, `acct.h`,
`amx.h`, `asp.h`, `buf.h`, `chars.h`, `conf.h`, `dir.h`, `file.h`,
`filsys.h`, `inode.h`, `intr.h`, `mount.h`, `msdos.h`, `param.h`,
`proc.h`, `reg.h`, `systm.h`, `tty.h`, `user.h`, `var.h`) copied in
directly rather than via a separate `h/` subdirectory, and its own
`Makefile.mutos`. This plays the same role here that `tests/mutos_cpp/c/`
already plays for `mutos_cpp` (see the main `CLAUDE.md`): a golden
corpus made of real kernel source instead of constructed test cases,
exercising whatever the kernel actually uses rather than only what this
corpus's constructed cases were written to probe.

Full real-hardware-verified goldens (`*.s.golden`, `*.i.golden`,
`*.1.golden`/`*.2.golden` plus base64 companions) are already present in
this checkout - the golden-capture pipeline (same one described under
"Workflow" below) has already been run on real MUTOS 1700 hardware for
all nine files, and `run_goldens.sh`'s per-category wildcard loop already
discovers them with no script change needed.

**`mutos_c0`/`mutos_c1` coverage of them is tracked apart** from the
62-file corpus's own N/62 pass fraction used throughout this project's
docs: 1/9. `01_delay.c` is byte-exact at every stage since 2026-10-04 (its
bare `i++;` statement - first found here - is accepted and done in place,
`inc *-6.(bp)`). The other eight are refused at the front-end stage, each
with a diagnosed error, never a silent wrong compile: seven stop first at
the same header line, `typedef struct { unsigned short off; ... }
segadr;` (an `unsigned short` member, which `mutos_c0` misreports as "an
unnamed filler field"), `09_amx.c` at a `long` struct member, and each
reports more after that (the last error `run_goldens.sh` shows is often
the generic "external definition syntax"). See `docs/DEVLOG.md`'s
Milestone 4 section for the initial per-file breakdown and
`src/mutos_cc/README.md`'s "Next steps" for where this sits in the
project's roadmap.

### `09_abiprobe` — resolving a documented open question

`docs/DEVLOG.md`'s Milestone 4 section had an explicit **open item**: the
real `chkstk` stack-probe threshold was only bounded to between 76 and 256
bytes by the existing `libc.a` corpus (largest plain `sub sp,N` seen: `N=76`;
smallest `call chkstk` seen: `N=256` — nothing in between has been
observed). `frame080.c` through `frame300.c` are six otherwise-identical
files whose *only* difference is a local buffer of 80/128/176/224/256/300
bytes. Compiling all six on real hardware and checking which ones emit
`call chkstk` instead of a plain `sub sp,N` pins the real cutoff down for
the first time. **Result**: `02_frame080` uses `sub sp,*80.`, the other five
use `call chkstk`, so the threshold was above 80 and at most 128 bytes;
`fltprobe/p27_frame` narrowed it further: frames of 82 and 90 bytes use
`sub sp,N`, from 100 `call chkstk` - the threshold is in `(90,100]` (see
`docs/MUTOS_C_ABI.md` sect. 1.9; `fltprobe/p29_frame2` asks about 92..98).

## Workflow

On the real MUTOS 1700 hardware / an accurate emulator, three
interchangeable ways to produce `.s`/`.i`/`.1`/`.2` for every test case —
pick whichever fits your setup, they produce the same files:

```sh
# A) if you have GNU Make available there:
make all intermediates

# B) the real MUTOS 1700 `make` itself (NOT GNU Make — see Makefile's
#    header for what that means in practice) — one category at a time:
cd 00_smoke && make -f Makefile.mutos && cd ..
cd 01_expr  && make -f Makefile.mutos && cd ..
# ...and so on for each of the 11 category directories, plus 11_kernel
# (see above), which has its own Makefile.mutos too - already run for
# this checkout's present goldens.

# C) no make at all -- a plain shell script, run it DIRECTLY, never
#    with "make -f" (it is not a makefile and make cannot parse it):
sh gen_mutos.sh
```

**(B) is one small `Makefile.mutos` per category directory, not a single
one at the top of `tests/mutos_cc/`.** A single top-level one covering all
62 files (124 targets) hit `Make: out of memory. Stop.` on real MUTOS 1700
hardware, partway through even the very first, smallest category — this
`make(1)` has some fixed-size internal table for macros/targets that a
250-line-plus makefile exceeds, regardless of any one line's length (a
previous fix already ruled out per-line length as the cause — the longest
line anywhere here is under 230 characters). Splitting into one makefile
per category keeps every single `make` invocation small enough to stay
under that ceiling, and happens to match the corpus's own intended
"tackle one category at a time" order anyway.

**`gen_mutos.sh` is a shell script, not a makefile — run it with `sh` (or
`./gen_mutos.sh` if its execute bit survived the transfer to MUTOS), never
as `make -f gen_mutos.sh`.** Feeding it to `make -f` makes `make` try to
parse shell syntax as makefile rules, which fails as soon as it hits
something that isn't a comment, a macro definition, or a `target:`/tab
-recipe line — e.g. `Make: Must be a separator on rules line 35. Stop.`
is `make` choking on that line's bare `do` (the `for` loop's `do`,
sitting on its own line with no `:` and no leading tab, so `make`
expected a rule separator and didn't find one). `sh gen_mutos.sh` sidesteps
any execute-bit question entirely and is the safest way to run it.

**(A) needs GNU Make specifically.** The real MUTOS 1700 `make(1)` has no
`%.o: %.c` pattern rules, no `$(wildcard)`/`$(dir)`/`$(notdir)` functions,
and no `:=` — confirmed directly from its own manpage. Every
`<category>/Makefile.mutos` (option B) is a from-scratch rewrite using
only what that manpage actually documents (plain `=` macros, per-file
targets, and every recipe kept to a single shell line, since the
manpage's own *Fehlerquellen* section warns that `cd` and other shell
state don't carry across separate recipe lines — each line gets its own
subshell; not that it matters here, since each per-category makefile runs
from inside its own directory and never needs `cd` at all).
`gen_mutos.sh` (option C) was checked the same way against the real
`basename(1)`/`expr(1)`/`find(1)` manpages, which caught one real bug:
V7 `find`'s predicates (`-name` included) are pure tests with no implicit
default action, so `find . -name '*.c'` without a trailing `-print`
produces **zero output**, silently — unlike GNU `find`, which defaults to
printing. Fixed to `find . -name '*.c' -print`. `expr "$f" : '\(.*\)/'`
for splitting off the directory part turned out to mirror this system's
*own* documented idiom for the inverse (extracting a basename via `expr`
is shown directly in `expr(1)`'s examples), and `basename "$f" .c` matches
`basename(1)`'s documented form exactly — both were already correct.

Then, after transferring every resulting `*/*.{s,i,1,2}` file back to the
modern host:

```sh
make goldens
```

This runs `cpp -P <name>.c > <name>.i` followed by
`/lib/c0 <name>.i <name>.1 <name>.2` for every test case — feeding `c0`
`cpp`'s *output*, never the raw `.c`, exactly like the real `cc` driver
does internally (see the Makefile comment for why that distinction
matters for getting byte-identical `temp1`/`temp2`). `temp1`/`temp2` are
raw binary tagged byte streams (`v7/cc/c04.c`'s `outcode()`), not text, so
`make goldens` gives them the same binary+base64 treatment already used
elsewhere in this project for `tests/mutos_as/kernel_opt/`'s `*.o.golden`/`*.o.golden_base64.txt` pairs:
`<name>.1.golden`/`<name>.2.golden` (raw bytes) plus
`<name>.1.golden.base64.txt`/`<name>.2.golden.base64.txt` (base64 text, so
the content stays inspectable without a binary-capable viewer).

Ship the resulting `*.s.golden`/`*.i.golden`/`*.1.golden`/`*.2.golden`
tree back; the `.s.golden` part drops straight in as a sibling to
`tests/mutos_cpp/`'s `*.i.golden` corpus once `mutos_c0`/`mutos_c1` exist
to diff against — and the `.1.golden`/`.2.golden` part lets `mutos_c0`
alone be verified well before `mutos_c1` is ready.

## Caveats

- Every file was syntax-checked with a modern host compiler
  (`gcc -fsyntax-only -w`, tolerant of K&R old-style definitions) purely
  to catch typos — this is **not** a guarantee of acceptance by the real
  V7-heritage `cc`, only a basic sanity net. Real-hardware compilation is
  still the authoritative check, exactly as for the Milestone 3 corpus.
- No standard headers beyond an occasional reliance on already-ported
  `libc.a` routines are assumed; pointer-returning library functions
  (`malloc`, `strcpy`) are explicitly forward-declared per K&R convention,
  since this compiler has no prototypes to catch the mistake for you.
- `float`/`double` files are compile-only probes of the front end's type
  handling; nothing here assumes a working FPU emulation path end-to-end.
  (The real compiler turns them into calls of `libc.a`'s software
  floating-point runtime - see `src/mutos_cc/README.md`'s "Floating point".)
- This corpus does not exercise the MUTOS **kernel-specific** macros
  (`M7100`/`ASK`/`IFSS`/`V24`/`V30IDE`) used by Milestone 3's kernel-source
  golden corpus (`tests/mutos_cpp/c/`) — those are board-configuration
  `#ifdef`s for kernel code, not general C-language constructs, so plain
  userland compiles here don't need them.
- `/lib/cpp` and `/lib/c0` are the paths `v7/cc/cc.c` itself hardcodes;
  override the Makefile's/`Makefile.mutos`'s `CPP`/`C0` macros (or edit
  `gen_mutos.sh` directly) if your installation keeps them elsewhere.
- None of the three approaches (Makefile's `%.1` rule, `Makefile.mutos`,
  `gen_mutos.sh`) distinguish "c0 rejected this file" from "c0 crashed/
  hung" — check that `<name>.1`/`<name>.2` actually exist and look
  non-empty before trusting them, especially for anything that fails
  silently under `make`'s default error handling.
- **`cc`'s `-P` and `-S` cannot be combined** — found on real hardware,
  where an earlier version of this corpus's tooling produced every
  `.i`/`.1`/`.2` correctly but no `.s` at all. `v7/cc/cc.c`'s own control
  flow (not just its flag-parsing) makes `-P` stop cc dead right after
  `cpp`, before `-S`'s effect is ever reached, for every file, no matter
  what else is on the command line. The `.s`-producing recipes now use
  plain `cc -S`, no `-P`; see the top-level `Makefile`'s header for the
  full explanation of why this doesn't affect byte-parity with the rest
  of the golden set.
- **`make goldens` must be run with no expectation that it (re)builds
  anything** — an earlier version depended on `all`/`intermediates`, which
  made a plain `make goldens` on the modern host re-check `.s`/`.1`
  freshness against `.c`/`.i` by timestamp. After a fresh `git checkout` or
  a file transfer from MUTOS, those timestamps routinely don't land in the
  order Make expects, so it decided already-correct files were stale and
  tried to rebuild them with tools (`cc`/`cpp`/`/lib/c0`) that don't exist
  on the modern host. `goldens` is now prerequisite-free: it only packages
  whatever `.s`/`.i`/`.1`/`.2` already exist on disk, skipping (not
  failing on) anything missing. If you see `/lib/c0: not found` or similar
  from a `make goldens` run, you're on an older copy of this Makefile.

**All of the above are confirmed fixed, not just diagnosed**: the full
`.c` → `.s`/`.i`/`.1`/`.2` → `*.golden` pipeline has been run end-to-end on
real MUTOS 1700 hardware plus a modern Linux host (verified for the
`00_smoke` category) with no remaining tooling errors.
