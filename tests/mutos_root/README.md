# `tests/mutos_root/` — the MUTOS root and its lookup

The MUTOS root (`docs/TOOLCHAIN_PLAN.md`, decisions 2 and 3) is one
directory that mirrors the MUTOS file system: the cross tools look up
every absolute path an original MUTOS 1700 tool names - `/lib/crt0.o`,
`/lib/c0`, the `/lib/libc.a` of `-lc`, `/usr/include`, `ld`'s
`/tmp/ldaXXXXXX` - under
`$MUTOS_ROOT`, by default the build tree's `mutos_root/`, whose absolute
path every Makefile compiles in. `make` fills it
(`scripts/mutos_root.sh`) - since 2026-10-25 also with the original
headers, `mutos/mutos_include/usr/include/` copied into `usr/include/` -,
`make clean` removes it; the lookup is `src/h/mutos_root.h`.

| File | What it is |
|---|---|
| `check_root.py` | The check, part of `make test`: the root's `lib/crt0.o` and `lib/libc.a` against the references in `tests/` (decoded from their base64 text), `libc.a`'s time (its `__.SYMDEF` time, 602282477), the symbolic links of the passes and tools into `src/` (a tool not built yet is listed, not an error), `usr/lib/`, `usr/include/` and `tmp/`, `usr/include/` against `mutos/mutos_include/usr/include/` file for file; `mutos_root.h` with `MUTOS_ROOT` unset, empty, set, with trailing slashes and `/`, and with a buffer too small; `mutos_ld` linking `08_float/01_floatbas` through the root without `out of date`, and with a newer copy of `libc.a` with it; `mutos_cc` compiling and linking a program with `<stdio.h>`, `<sys/types.h>` and `<sys/stat.h>` through the root. `--realbin` (in `make check-realbin`, needs the Python module `unicorn`) does the `libc.a` check with the original `ld`, emulated, and runs that program under the emulator (`hello, 512 30`: `BUFSIZ` and the size of `struct stat`). |
| `rootpath_test.c` | Prints `mutos_root()` and `mutos_path()`'s results for its arguments; `check_root.py` compiles it (with `$CC`, `-Werror`) into a temporary directory and runs it. |

Running it by hand, after `make`:

```sh
python3 tests/mutos_root/check_root.py mutos_root
python3 tests/mutos_root/check_root.py --realbin mutos_root
```
