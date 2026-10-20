# Toolchain plan — from the compiler passes to a complete cross toolchain

Written 2026-10-20, from the decisions taken that day with the project
owner. It continues where `docs/C1_PORT_PLAN.md` and `docs/C0_PORT_PLAN.md`
ended: the compiler passes are ports of the originals; the driver, the
optimizer, the archiver and support for MUTOS makefiles are missing, and
three of the existing tools (`mutos_cpp`, `mutos_ld`, `mutos_as`) are
re-implementations whose behaviour beyond the goldens was never compared
with the originals.

**Status (2026-10-20): plan; nothing of it is implemented yet** - step 0
is next. `STATUS.md`'s "Next up" follows this order.

## 1. Where things stand

| Tool | Today | Checked against |
|---|---|---|
| `mutos_cpp` | re-implementation of the observable behaviour, four documented simplifications, no `# N "file"` lines, no default include directory | 137 `.i` goldens (written with `-P`) |
| `mutos_c0` | port of the original `c0` (2026-10-18) | the emulated original: every golden, differential fuzzing |
| `mutos_c1` | port of the original `c1` (2026-10-13) | the emulated original at four depths, differential fuzzing |
| `mutos_c2` | missing | - |
| `mutos_as` | re-implementation, 80186/V30 opcodes always on | 76 `.o` goldens, the floating conversion against `libc.a`'s own `atof` |
| `mutos_ld` | port of v7's `ld.c` with the MUTOS deltas | its goldens (`myhello`/`idhello`) are missing from the tree |
| `mutos_cc` | missing | - |
| `ar`, `ranlib`, `make` | missing | - |

The originals run under `tests/realbin/mutosemu.py`: `cpp`, `c0`, `c1`,
`as` since 2026-10-12, and on 2026-10-20 also `c2`, `ld` and - with
`fork`/`exece`/`wait` faked in a scratch harness - `cc`. What they showed
is in `docs/devlog/originals_and_ports.md`, "The other originals as
oracles (2026-10-20)"; the points that shape this plan are in section 5.

## 2. Decisions (2026-10-20)

1. **Ports as `c0` and `c1` were ported**: from the binary, with the v7
   source as the scaffold where there is one, checked against the
   emulated original on every golden and on random input.
2. **A MUTOS root.** One directory mirrors the MUTOS file system; every
   absolute path an original tool has compiled in is looked up under it
   (`/lib/crt0.o` → `$MUTOS_ROOT/lib/crt0.o`). Its location: the
   environment variable `MUTOS_ROOT`, otherwise a default compiled in at
   build time - the absolute path of the build tree's `mutos_root/`
   (rebuild, or set `MUTOS_ROOT`, after moving the tree). No new
   command-line option, so the command lines stay those of the
   originals. The host's own `/lib` and `/usr/lib` are never used (on
   some distributions `/usr/lib/libc.a` is glibc's).
3. **`mutos_root/`** sits at the top of the repository, is ignored by git
   and removed by `make clean`; the build fills it:

   | Path | What |
   |---|---|
   | `lib/crt0.o` | decoded from `tests/mutos1700_crt0/crt0.o.base64.txt` |
   | `lib/libc.a` | decoded from `tests/mutos1700_libc/libc.a.base64.txt` as it is (not re-archived from the 167 objects), its modification time set to its `__.SYMDEF` time, 1989-01-31 20:41:17 UTC, so that `ld` uses the table without "out of date" |
   | `lib/cpp`, `lib/c0`, `lib/c1`, `lib/c2`, `bin/as`, `bin/ld`, `bin/cc` | symbolic links to `mutos_cpp`, `mutos_c0`, ... - the passes are found through the root, so the driver maps each of the original's paths one to one, and `-B`/`-t` keep their meaning |
   | `usr/lib/` | empty: for the user's own libraries |
   | `usr/include/` | the headers, once there are any |

   `make install` comes later.
4. **`mutos_cpp` becomes a port** of the original `cpp` (Reiser's, SCCS
   `cpp.c 1.6 12/12/86`; the binary has its symbol table). That brings
   the `# N "file"` lines a compile without `-P` writes - the real `c0`
   takes its messages' file names and line numbers from them -, the
   default include directory (`/usr/include` → `$MUTOS_ROOT/usr/include`),
   and the four cases `mutos_cpp` simplifies.
5. **`mutos_ld` becomes a port** of the original `ld`. `-lX` is
   `$MUTOS_ROOT/lib/libX.a`, then `$MUTOS_ROOT/usr/lib/libX.a`, never the
   current directory (the original's order). `-r` and `-d`, which the
   original refuses, stay as a documented extension.
6. **`mutos_cc` is a port of the original `cc`** (0407, text 8,026
   bytes), not of `v7/cc/cc.c`, which it differs from (section 5). It
   passes `$MUTOS_ROOT/lib/crt0.o` and `-lc` to `ld`, as the original
   passes `/lib/crt0.o` and `-lc`. Files the root does not have
   (`mcrt0.o` for `-p`, `crt2.o`/`crt20.o`/`lib2.a` for `-2`/`-20`, the
   `-f` passes and `libf.a`) fail as on a MUTOS system without them.
7. **`mutos_c2` is a port** of the original `c2`, for the 8086 only (the
   original has no 80186/V30 transformation).
8. **`mutos_c1` and `mutos_c2` stay without 80186/V30 code generation**
   until the 8086 toolchain works completely; whether and how it comes is
   decided then.
9. **`mutos_as` is not ported now.** It accepts `-g` and `-C` (which the
   original `cc` always passes) and ignores them - no effect of either is
   known yet. No 8087 support is needed. A differential fuzzer against the
   emulated original measures the differences; a port is decided after
   that. The 80186/V30 instructions move behind the switch `-1` (section
   4).
10. **MUTOS makefiles are supported** without a `make` port for now: a
    wrapper `mutos_make` runs the host's `make` with its built-in rules
    off (`-r`), a rules file with the MUTOS `make`'s own built-in rules and
    macros (read from its binary), and `$MUTOS_ROOT/bin` first in `PATH`,
    so that `cc`, `as`, `ld`, `ar` in a recipe are the MUTOS tools. Whether
    `make` is ported is decided later. **`ar` and `ranlib` are ported**
    (from their binaries): a makefile that builds a library needs them,
    and the host's `ar` writes a format `mutos_ld` cannot read. A recipe
    with an absolute path (`/lib/c0`) cannot be redirected by any `make`;
    such makefiles need the path as a macro (`make CC=$MUTOS_ROOT/bin/cc
    CPP=$MUTOS_ROOT/lib/cpp C0=$MUTOS_ROOT/lib/c0` will work for
    `tests/mutos_cc/*/Makefile.mutos` once the driver exists).
11. **Binaries and manual pages from the hardware**: the project owner
    brings `make`, `ar`, `ranlib` (and `lorder`, `tsort`, `nm`, `size`,
    `strip` as makefiles need them) into `mutos_bin/`, and the MUTOS `make`
    manual page as reference material (`docs/mutos_man/make_1.d`).
12. **Manual pages** (`man/`) are written at the end of the project; the
    existing ones stay as they are until then, also where they no longer
    describe the tools. Requirements: English, no development details,
    references to the other tools, consistent wording and structure
    throughout.

## 3. Order of work

| Step | What | Checked by |
|---|---|---|
| 0 | Infrastructure: `mutosemu.py` serves `fork`/`exece`/`wait` (faked as above), `fstat`, `chmod`, `umask`, `link`, and a tracer for `cc` joins `tests/realbin/`; `mutos_root/` and the `MUTOS_ROOT` lookup (one header in `src/h/` for every tool); a `.gitignore` (the repository has none) for `mutos_root/` and the build products; `mutos_as` accepts `-g`/`-C` | the real `cc` and `ld` run as oracles; `make test` unchanged |
| 1 | Port of `cpp` | the emulated original on the 137 `.i` goldens with and without `-P`, differential fuzzing (macros, conditionals, includes, comments) |
| 2 | Port of `ld` | the emulated original: every compiled golden linked with `crt0.o` and `libc.a`, with `-i`, `-n`, `-s`, `-x`, `-X`, `-u`, `-e`, `-o`, archive search, messages and exit status; this replaces the missing `myhello`/`idhello` goldens |
| 3 | Port of `cc` | the tracer: every pass's path and arguments, messages and exit status over a matrix of options, the original's against the port's; whole programs through the chain |
| 4 | Port of `c2` | the emulated original on all 132 `.s` goldens and on `c1fuzz.py`'s programs; `tests/mutos_as/kernel_opt/`'s nine sources from `11_kernel` (the original gives them byte for byte) |
| 5 | `mutos_as`: differential fuzzer, the `-1` switch | the emulated original; then the decision on a port |
| 6 | MUTOS makefiles: ports of `ar` and `ranlib`, the `mutos_make` wrapper, `bin/` in the root | `libc.a` rebuilt from its 167 objects against the original archive; the corpus's `Makefile.mutos` files |

Later: the 80186/V30 decision for `c1`/`c2`, `make install`, the manual
pages.

## 4. The switch `-1` (decided 2026-10-20)

`mutos_as -1` accepts the 80186 instructions `mutos_as` implements
(`pusha`, `popa`, `push` with an immediate, `insb`/`insw`/`outsb`/`outsw`,
`enter`, `leave`, `bound`, `imul` with an immediate, shifts and rotates
by an immediate count) - the set the 80186, 80188, NEC V20 and V30 share;
no NEC-only instruction. The name: the original reads its options letter
by letter (`-mv30` would be four unknown options to it, and to a later
port), `-1` is free in `as`, `cc` and `ld`, `cc` already has digit
options (`-2`, `-20`), and the 1 stands for the 80186.

Without `-1`, `mutos_as` does what the original does with such a line:
an identifier it does not know as an instruction starts an expression,
which assembles to a word referring to an undefined external of that
name (AS-02 in `docs/MUTOS_ORIGINAL_BUGS.md`); the link then fails with
`Undefined: pusha` - a C program cannot define a name without the
leading `_`. That keeps the differential fuzzer exact. The tests that use
those instructions (`tests/mutos_as/kernel_opt/mch_insw_outsw.s`,
`tests/mutos_as/v30_speculative/`) get `-1`. Implemented with step 5.

## 5. What the originals showed (2026-10-20)

Details and evidence: `docs/devlog/originals_and_ports.md`.

- **`cc`**: link step `/bin/ld -X /lib/crt0.o [-o out] <files> -lc`;
  `-p`, `-2`, `-20` and `-f` change the startup file and libraries as
  listed in decision 6; `-f` runs `c0f`, `c1f`, `asf`; `as` is called
  with `-gC`; a compile runs `cpp` without `-P`; `-P` stops after `cpp`,
  with or without `-S`; unknown options go to `ld`; `-o name.c` or `-o
  name.o` compiles or uses that file and writes `name` (no "Would
  overwrite").
- **`ld`**: `-l` searches `/lib`, then `/usr/lib`, never `./`; `-r` and
  `-d` refused.
- **`as`**: options `-o`, `-e`, `-g`, `-C`, `-l`; the manual's `-L`,
  `-W`, `-D`, `-T` unknown to the binary; the 8087 set in its table, no
  80186 mnemonic; an unknown mnemonic assembles silently as a word with
  an undefined external.
- **`c2`**: gives `kernel_opt/`'s nine kernel sources from the
  `11_kernel` `.s` goldens byte for byte.
- **`cpp`**: writes `# N "file"` lines without `-P`; the real `c0`'s
  messages depend on them (`e.c:3:` where the present chain says `4:`).

## 6. Open questions

- What `as`'s `-g`, `-C` and `-l` do, and what switches on `cc`'s
  "Executing ..." messages: to be read in the binaries (steps 3 and 5).
- Whether `cc -o name.o` writing `name` is meant (listed as suspect in
  `docs/MUTOS_ORIGINAL_BUGS.md`).
- What `c0f`, `c1f`, `asf` and `libf.a` are (probably the passes for
  8087 code): not on hand; `-f` fails until they are.
