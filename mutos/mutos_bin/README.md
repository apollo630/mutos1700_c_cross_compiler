# `mutos/mutos_bin/` — the original MUTOS 1700 binaries

The real compiler, assembler and linker and the tools around them,
brought from MUTOS 1700 hardware, stored as base64 text like every other
binary reference in this repo - `<name>.golden.base64.txt`, decoded with
`base64 -d`. They are reference data of the same kind as the `*.o.golden`
files (`CLAUDE.md`'s Golden Master Integrity rule): never regenerate,
re-encode or alter them. The toolchain came in commit `e8c7516`, the
other tools in commit `c4e8b6b` (2026-10-25); until 2026-10-25 the
directory was `mutos_bin/` at the top of the repository (the other
originals beside it: [`../README.md`](../README.md)).

| Binary | Magic | Text | Data | BSS | Symbols | Notes |
|---|---|---|---|---|---|---|
| `as` | 0411 | 26944 | 27564 | 2220 | - | the assembler |
| `c0` | 0411 | 23168 | 2878 | 13900 | - | compiler pass 1 |
| `c1` | 0411 | 32768 | 17166 | 1476 | - | compiler pass 2 - v7's `c1` ported to the 8086 |
| `c2` | 0411 | 17792 | 1476 | 2908 | - | the optimizer (`cc -O`) - v7's `c2` rewritten for the 8086 |
| `cc` | 0407 | 8026 | 894 | 2776 | - | the driver |
| `cpp` | 0411 | 14080 | 2770 | 40166 | 3204 | the preprocessor - the only one with symbols (267 entries) |
| `ld` | 0411 | 12480 | 952 | 36382 | - | the linker |
| `make` | 0411 | 16192 | 3288 | 6430 | - | `make` (2026-10-25) |
| `ar` | 0407 | 8930 | 1934 | 1870 | - | the archiver (2026-10-25) |
| `ranlib` | 0407 | 6794 | 424 | 9688 | - | an archive's `__.SYMDEF` (2026-10-25) |
| `nm` | 0407 | 6840 | 354 | 1270 | - | prints symbol tables (2026-10-25) |
| `size` | 0407 | 4530 | 282 | 1206 | - | segment sizes (2026-10-25) |
| `strip` | 0407 | 4588 | 398 | 1208 | - | removes symbols and relocation (2026-10-25) |
| `tsort` | 0407 | 7022 | 604 | 1206 | - | topological sort, for `lorder` (2026-10-25) |
| `ed` | 0407 | 11522 | 234 | 4886 | - | the editor (2026-10-25) |
| `em` | 0411 | 27136 | 2106 | 10966 | - | the extended editor, no v7 source (2026-10-25) |

Sizes in bytes, from each file's `a.out` header (`src/h/mutos_aout.h`);
all have relocation stripped (flag 1), and each file is exactly header,
text, data (and `cpp`'s symbols). 0411 is separate instruction and data
space: the text in one 64K segment, data, BSS and stack in another.

`lorder.golden.base64.txt` is no binary: `lorder` is a 400-byte shell
script (`nm -g`, `sed`, `sort`, `join`), kept here as base64 text like
the rest.

`tests/realbin/` runs the seven toolchain binaries under an 8086
emulator: the real `cpp`, `c0`, `c1` and `as` reproduce the project's
goldens there (see its `README.md`), which makes them an oracle for any
input; `c2` gives the optimized kernel sources of
`tests/mutos_as/kernel_opt/` from the `11_kernel` goldens, and `ld` and
`cc` run too (`cc` with `fork`/`exece`/`wait` faked by the emulator,
traced by `tests/realbin/cctrace.py`, which also runs the whole original
chain - 2026-10-20). `c1`'s code tables, read from its data segment, are
in `tests/realbin/c1_tables.txt`. `c0`, `c1` (`src/mutos_cc/`) and
`cpp` (`src/mutos_cpp/`, 2026-10-22), `ld` (`src/mutos_ld/`, 2026-10-22),
`cc` (`src/mutos_cc/ccport/`, 2026-10-24) and `c2` (`src/mutos_cc/c2port/`,
2026-10-25) are ported; `as` is not (`docs/TOOLCHAIN_PLAN.md`, decision 9).

The binaries of 2026-10-25 are what MUTOS makefiles need (decided
2026-10-20, `docs/TOOLCHAIN_PLAN.md`, decisions 10 and 11): `ar` and
`ranlib` are to be ported, `make` is run through the wrapper
`mutos_make` for now - its built-in rules and macros are v7's
`builtin[]` string for string (`docs/devlog/originals_and_ports.md`,
"The originals of 2026-10-25"). None of them runs under `tests/realbin/`
yet. v7's sources are in `v7/make/`, `v7/ar/`, `v7/ranlib/`, `v7/nm/`,
`v7/size/`, `v7/strip/`, `v7/tsort/`, `v7/ed/`; the manual pages in
`../mutos_man/`.

Not present, but referred to by the original `cc`: `c0f`, `c1f`, `asf`
(its `-f` passes), and in `/lib` `mcrt0.o`, `crt2.o`, `crt20.o`,
`libf.a`, `lib2.a`.
