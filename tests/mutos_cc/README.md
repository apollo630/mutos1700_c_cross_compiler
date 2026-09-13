# mutos_cc Milestone 4 — K&R C Test Corpus

Purpose: give `mutos_cc`/`mutos_c0`/`mutos_c1` an incremental, construct-by-
construct verification corpus **before any code generator exists**, using
exactly the same methodology that already got Milestone 3 (`mutos_cpp`) to
"complete, byte-for-byte identical to real hardware" — see `STATUS.md` and
`tests/mutos_cpp/run_goldens.sh` in the main repo:

1. This corpus is compiled with the **real, unmodified `cc`** on real MUTOS
   1700 hardware (or an accurate emulator).
2. Every resulting `.s` file is saved as a golden reference (`*.s.golden`,
   parallel to Milestone 3's `*.i.golden` naming).
3. Once `mutos_c1` exists, its output is diffed byte-for-byte against these
   golden files, exactly like `mutos_as`/`mutos_cpp` are diffed today.

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
on the modern host, never on MUTOS itself.

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
| `02_long` | `long`: locals, `*`/`/`/`%` (→ `almul`/`aldiv`/`alrem`), return value, parameter passing | `01_addsub`, `02_muldiv`, `03_retval`, `04_params` |
| `03_ctrlflow` | `if`/`while`/`do`/`for`/`switch`/`goto` | `01_ifelse`, `02_while`, `03_dowhile`, `04_for`, `05_breakcont`, `06_switch`, `07_goto` |
| `04_funcs` | K&R old-style definitions, >4 args, recursion, `static`/`register`, function pointers | `01_call`, `02_manyargs`, `03_recfact`, `04_mutrec`, `05_staticvar`, `06_regclass`, `07_funcptr` |
| `05_arrptr` | Arrays, pointers, strings via libc | `01_arrbasic`, `02_array2d`, `03_ptrbasic`, `04_ptrarreq`, `05_arrofptr`, `06_ptrptr`, `07_strlibc` |
| `06_struct` | struct/union/enum/typedef, incl. whole-struct assignment (`STRASG`) and bit-fields | `01_stbasic`, `02_stptr`, `03_starray`, `04_stassign`, `05_nestst`, `06_union`, `07_bitfield`, `08_enum`, `09_typedef` |
| `07_scope` | file-scope `static` vs. extern, block-scope shadowing, forward `extern` | `01_globstat`, `02_shadow`, `03_externdef` |
| `08_float` | `float`/`double` (compile-only, see file headers) | `01_floatbas`, `02_dblconv` |
| `09_abiprobe` | Targeted ABI probes, see below | `01_argvmain`, `02_frame080` … `07_frame300` |
| `10_integ` | Small realistic programs | `01_wordcount`, `02_bubsort`, `03_linklist`, `04_strrev`, `05_matmul` |

### `09_abiprobe` — resolving a documented open question

`docs/DEVLOG.md`'s Milestone 4 section has an explicit **open item**: the
real `chkstk` stack-probe threshold is only bounded to `(76, 256]` bytes by
the existing `libc.a` corpus (largest plain `sub sp,N` seen: `N=76`;
smallest `call chkstk` seen: `N=256` — nothing in between has been
observed). `frame080.c` through `frame300.c` are six otherwise-identical
files whose *only* difference is a local buffer of 80/128/176/224/256/300
bytes. Compiling all six on real hardware and checking which ones emit
`call chkstk` instead of a plain `sub sp,N` pins the real cutoff down for
the first time.

## Workflow

```sh
# on real MUTOS 1700 hardware / an accurate emulator, cc already installed:
make
# transfer the resulting */*.s files back to the modern host, THEN:
make goldens
```

Ship the resulting `*.s.golden` tree back; it drops straight in as a
sibling to `tests/mutos_cpp/`'s `*.i.golden` corpus once `mutos_c0`/
`mutos_c1` exist to diff against.

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
- This corpus does not exercise the MUTOS **kernel-specific** macros
  (`M7100`/`ASK`/`IFSS`/`V24`/`V30IDE`) used by Milestone 3's kernel-source
  golden corpus (`tests/mutos_cpp/c/`) — those are board-configuration
  `#ifdef`s for kernel code, not general C-language constructs, so plain
  userland compiles here don't need them.
