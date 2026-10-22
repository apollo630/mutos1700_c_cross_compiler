# `mutos_bin/` — the original MUTOS 1700 toolchain binaries

The real compiler, assembler and linker, brought from MUTOS 1700 hardware
(added in commit `e8c7516`), stored as base64 text like every other
binary reference in this repo - `<name>.golden.base64.txt`, decoded with
`base64 -d`. They are reference data of the same kind as the `*.o.golden`
files (`CLAUDE.md`'s Golden Master Integrity rule): never regenerate,
re-encode or alter them.

| Binary | Magic | Text | Data | BSS | Symbols | Notes |
|---|---|---|---|---|---|---|
| `as` | 0411 | 26944 | 27564 | 2220 | - | the assembler |
| `c0` | 0411 | 23168 | 2878 | 13900 | - | compiler pass 1 |
| `c1` | 0411 | 32768 | 17166 | 1476 | - | compiler pass 2 - v7's `c1` ported to the 8086 |
| `c2` | 0411 | 17792 | 1476 | 2908 | - | the optimizer (`cc -O`) |
| `cc` | 0407 | 8026 | 894 | 2776 | - | the driver |
| `cpp` | 0411 | 14080 | 2770 | 40166 | 267 | the preprocessor - the only one with symbols |
| `ld` | 0411 | 12480 | 952 | 36382 | - | the linker |

Sizes in bytes, from each file's `a.out` header (`src/h/mutos_aout.h`);
all have relocation stripped (flag 1). 0411 is separate instruction and
data space: the text in one 64K segment, data, BSS and stack in another.

`tests/realbin/` runs them under an 8086 emulator: the real `cpp`, `c0`,
`c1` and `as` reproduce the project's goldens there (see its
`README.md`), which makes them an oracle for any input; `c2` gives the
optimized kernel sources of `tests/mutos_as/kernel_opt/` from the
`11_kernel` goldens, and `ld` and `cc` run too (`cc` with `fork`/`exece`/
`wait` faked by the emulator, traced by `tests/realbin/cctrace.py`, which
also runs the whole original chain - 2026-10-20). `c1`'s code tables, read from its data segment, are
in `tests/realbin/c1_tables.txt`. `c0`, `c1` are ported
(`src/mutos_cc/`), `cpp`, `ld`, `cc` and `c2` are to be
(`docs/TOOLCHAIN_PLAN.md`).

To follow from the hardware (decided 2026-10-20): `make`, `ar`, `ranlib`,
and `lorder`, `tsort`, `nm`, `size`, `strip` as MUTOS makefiles need
them - same naming, same rules. Not present, but referred to by the
original `cc`: `c0f`, `c1f`, `asf` (its `-f` passes), and in `/lib`
`mcrt0.o`, `crt2.o`, `crt20.o`, `libf.a`, `lib2.a`.
