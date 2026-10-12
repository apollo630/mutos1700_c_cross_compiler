# `tests/realbin/` — the original MUTOS 1700 binaries as an oracle

`../../mutos_bin/` holds the original toolchain binaries brought from the
hardware (`as`, `c0`, `c1`, `c2`, `cc`, `cpp`, `ld`, as base64 text - see
its `README.md`). This directory runs them on the modern host under an
8086 emulator and reads `c1`'s code tables out of its data segment. It
needs no hardware: the real compiler's answer for any input is one
emulator run away.

Requirements: Python 3 and the module `unicorn` (`pip install unicorn`)
for running the binaries; `capstone` (`pip install capstone`) only for
`c1dis.py`.

## Files

| File | What it is |
|---|---|
| `mutosemu.py` | The emulator: loads a MUTOS `a.out` (0411 or 0407) into Unicorn, builds `crt0`'s argument stack and serves the system calls behind `crt0`'s two far-call trampolines (`Iscadr` → CS:0002, `Isgadr` → CS:0004, see `docs/MUTOS_C_ABI.md` §2.2). Usable from the command line and as a module (with hooks on text offsets). |
| `check_real.py` | `goldens`: the real `cpp`, `c0`, `c1` and `as` against every golden; `coverage`: which rows of `c1`'s code tables the goldens use. |
| `c1tables.py` | Reads `c1`'s code tables (`regtab`, `efftab`, `cctab`, `sptab`) and `opdope[]`, `opntab[]`, `instab`, `branchtab` from the binary and writes them in `v7/cc/table.s` notation - the inverse of `v7/cc/cvopt.c`. |
| `c1_tables.txt` | Its output, kept in the tree as the readable reference (generated - `make check-realbin` checks it is current). |
| `c1map.txt` | A map of the stripped `c1`: every function's text offset, its `v7/cc` counterpart and how far it has been compared (`same`, `delta`, `x86`, `new`, `unread`), and the data offsets of the tables and globals. |
| `c1dis.py` | An annotated disassembly of `c1` - by function name from `c1map.txt` or by text range - naming calls, data and strings; the `libc.a` functions it names by matching `libc.a`'s objects against the text. |

## Running

```sh
make check-realbin            # from the repo root: tables, goldens, coverage
python3 tests/realbin/check_real.py goldens
python3 tests/realbin/check_real.py coverage --unused
```

The real pipeline on any file, the way `cc` runs it (`cc` itself cannot
run: there is no `fork`/`exec`):

```sh
E=tests/realbin/mutosemu.py
python3 $E cpp -P x.c > x.i          # headers: relative to the current directory
python3 $E c0 x.i x.1 x.2
python3 $E c1 x.1 x.2 x.s
python3 $E as -o x.o x.s
```

`--cwd DIR` resolves the program's relative file names against `DIR`,
`--root DIR` its absolute ones (`/usr/include/...`) under `DIR`.

`c1` takes options after its three file names: `a`, `b`, `c`, `d`, `r`
switch on its own debug traces, written into the `.s` file - `b`: each
expression tree as read from the intermediate file, `a`: the same tree
after `optim()`, `r`: every `rcexpr()` call with its tree, `c`: every
`cexpr()` call with its tree, each emitted line followed by its tree's
operator and type, and the register returned, `d`: every `delay()` call;
`s<n>` sets the frame size from which a function calls `chkstk`
(default 100). For example `python3 $E c1 x.1 x.2 x.s rcd`.

## What it established (2026-10-12)

- The real `cpp` gives all 137 `.i` goldens (132 in `tests/mutos_cc`, 5 in
  `tests/mutos_cpp/c`), the real `c0` all 132 `.1`/`.2` goldens, the real
  `c1` all 132 `.s` goldens - with its messages exactly as
  `c1_errors.txt` and the round logs have them, and exit status 1 where
  it printed any - and the real `as` 75 of the 76 `.o` goldens.
- The 76th, `kernel_opt/mch_insw_outsw`, is not the real `as`'s output
  for its source: it is `mch.s` with two `.byte /6d` / `.byte /6f`
  replaced by `insw` / `outsw`, its `.o.golden` the real `mch.o`. The
  real `as` does not know those 80186 mnemonics and assembles each as an
  undefined external word, silently. `check_real.py` lists it apart.
- `c1` is v7's `c1` ported to the 8086: the same functions in the same
  order, the same data structures, and code tables in v7's own `cvopt`
  encoding with MUTOS assembler text. The goldens use 160 of the tables'
  496 rows. See `docs/DEVLOG.md`'s "The original binaries" and
  `docs/C1_PORT_PLAN.md`.

## Limits of the emulator

- System calls served: `exit`, `read`, `write`, `open`, `close`, `creat`,
  `unlink`, `break`, `lseek`, `getpid`, `access`, `kill`, `signal`,
  `ioctl` (always "not a tty"); anything else gets `EINVAL` and a
  warning. No `fork`/`exec`/`wait`, no signals delivered.
- `break` is accepted as asked; the memory is the 64K data segment.
- Only `0407` and `0411` files; the binaries in `mutos_bin/` are all one
  or the other (`cc` is `0407`, the rest `0411`).
