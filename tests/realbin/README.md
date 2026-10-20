# `tests/realbin/` — the original MUTOS 1700 binaries as an oracle

`../../mutos_bin/` holds the original toolchain binaries brought from the
hardware (`as`, `c0`, `c1`, `c2`, `cc`, `cpp`, `ld`, as base64 text - see
its `README.md`). This directory runs them on the modern host under an
8086 emulator and reads `c1`'s code tables out of its data segment. It
needs no hardware: the real compiler's answer for any input is one
emulator run away. It is also where `mutos_c1` and `mutos_c0` - since
2026-10-13 and 2026-10-18 ports of the real `c1` and `c0`
(`src/mutos_cc/c1port/`, `c0port/`, `docs/C1_PORT_PLAN.md`,
`docs/C0_PORT_PLAN.md`) - are checked against the originals, on every
golden and on random programs.

Requirements: Python 3 and the module `unicorn` (`pip install unicorn`)
for running the binaries; `capstone` (`pip install capstone`) only for
`c1dis.py` and `c0dis.py`.

## Files

| File | What it is |
|---|---|
| `mutosemu.py` | The emulator: loads a MUTOS `a.out` (0411 or 0407) into Unicorn, builds `crt0`'s argument stack and serves the system calls behind `crt0`'s two far-call trampolines (`Iscadr` → CS:0002, `Isgadr` → CS:0004, see `docs/MUTOS_C_ABI.md` §2.2). Shifts and rotates by CL as the 8086 does them (see "Limits of the emulator"). Usable from the command line and as a module (with hooks on text offsets; `eof_limit` stops a program that reads the end of a file again and again). |
| `check_real.py` | `goldens`: the real `cpp`, `c0`, `c1` and `as` against every golden; `coverage`: which rows of `c1`'s code tables the goldens use. |
| `c1tables.py` | Reads `c1`'s code tables (`regtab`, `efftab`, `cctab`, `sptab`) and `opdope[]`, `opntab[]`, `instab`, `branchtab` from the binary and writes them in `v7/cc/table.s` notation - the inverse of `v7/cc/cvopt.c`. |
| `c1_tables.txt` | Its output, kept in the tree as the readable reference (generated - `make check-realbin` checks it is current). |
| `c1map.txt` | A map of the stripped `c1`: every function's text offset, its `v7/cc` counterpart and how far it has been compared (`same`, `delta`, `x86`, `new`, `unread`), and the data offsets of the tables and globals. |
| `c1dis.py` | An annotated disassembly of `c1` - by function name from `c1map.txt` or by text range - naming calls, data and strings; the `libc.a` functions it names by matching `libc.a`'s objects against the text. |
| `c1tables.py --c` | The same tables as C for the port: `src/mutos_cc/c1port/c1tab.c` (generated, checked in; `--check-c` checks it is current). |
| `c1trees.py` | `mutos_c1` against the original on `.1`/`.2` pairs (default: every golden) at four depths: 1 the expression trees as read, 2 also after `optim()` (both taken out of the emulated memory and the port's `C1PORT_TREES` dump), 3 the `.s` with all of the original's traces on (`rcdab`), 4 the `.s`, the messages and the exit status. A refusal `invalid_goldens.txt` specifies counts apart. |
| `c0map.txt` | A map of the stripped `c0`: its 73 functions (v7's `c00.c`..`c04.c` in source order, then `libc.a` from text 4a1e) with their text offsets, how each compares with v7 (`same`, `delta`, `new`) and what the binary does differently, and the data offsets of the tables and globals - the port's memory image follows them. |
| `c0dis.py` | `c1dis.py` switched to `c0` (`c1dis.use('c0')`): the annotated disassembly by function name from `c0map.txt`. |
| `c0tables.py` | `--c`: the original `c0`'s initialized data segment as C - `src/mutos_cc/c0port/c0data.c` (generated, checked in; `--check-c` checks it is current); `--tables`: its keyword table, `opdope[]`, `cvtab[]`, `cvntab[]` and `ctab[]` in readable form. |
| `c0cmp.py` | `mutos_c0` against the original: the same `.i` to both, their `.1`, `.2`, messages and exit status compared byte for byte - `goldens` (all 137 `.i` goldens) or `files`. A refusal of the port counts apart: it must have written a prefix of the original's output, and where the original does not finish (it reads past the end of a file for ever, or a CPU fault stops it) the port must have refused. |
| `c0fuzz.py` | Differential fuzzing of `mutos_c0` with `c0cmp.py`'s comparison. Generators: its own, declaration-heavy (storage classes, typedefs, structures, unions, enums, bit-fields, initializers, K&R parameters, blocks, labels, switches, the old `=+` operators, `\/` and `/\`, escapes, comments, `# line` lines, SOH markers), `c1fuzz.py`'s (`--gen c1fuzz`) and `fuzz_c.py`'s (`--gen fuzz_c`); `--mutate P` deletes, duplicates, swaps or inserts tokens, for the error paths. |
| `c1fuzz.py` | Differential fuzzing: random programs through `mutos_cpp` and the emulated real `c0`, then the emulated original `c1` against `mutos_c1` (depth 4 by default, 1 and 2 too). Generators: its own (every type, a structure with bit-fields, registers, calls, switches), `fuzz_c.py`'s (`--gen fuzz_c`), and long constant arithmetic at the edges of `lconst()`'s library paths (`--gen lconst`). The port's refusals are checked too: what it wrote before must be the original's, and where the original does not finish, the port must have refused. |

## Running

```sh
make check-realbin            # from the repo root: tables, goldens, coverage,
                              # mutos_c1 at depths 2-4 and a fuzz run,
                              # mutos_c0 on the goldens and two fuzz runs
python3 tests/realbin/check_real.py goldens
python3 tests/realbin/check_real.py coverage --unused
python3 tests/realbin/c1trees.py --depth 4 --show 1
python3 tests/realbin/c1fuzz.py -n 1000 -s 7 --keep /tmp/c1diff
python3 tests/realbin/c1fuzz.py --gen lconst -n 300
python3 tests/realbin/c0cmp.py goldens
python3 tests/realbin/c0fuzz.py -n 2000 -s 7 --mutate 0.7 --keep /tmp/c0diff
python3 tests/realbin/c0dis.py fold
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
  496 rows. See `docs/devlog/originals_and_ports.md`'s "The original binaries" and
  `docs/C1_PORT_PLAN.md`.

## The port against it (2026-10-13)

- Every golden `.1`/`.2` pair: depth 1, 2, 3 and 4 identical on 131 of
  132; `fltprobe/p19_open3` refused after its 228 valid lines, as
  `invalid_goldens.txt` specifies (the original writes bytes past
  `regname[]` there). At depth 3 a STRASG's degree is compared as `#`:
  `getree()` never sets it, so the original's trace shows a leftover
  word.
- Differential fuzzing at depth 4: see `STATUS.md` for the current
  corpus; every difference found was fixed in the port (the last one a
  library routine, `libc.a`'s `aldiv`, which is not C's division -
  `src/mutos_cc/c1port/c1lib.c`).

## The port of `c0` against it (2026-10-18)

- Every `.i` golden (137): `.1`, `.2`, messages and exit status
  identical.
- Differential fuzzing: see `STATUS.md` for the current corpus - no
  difference; the port's refusals each checked against the original's
  prefix, and every program on which the original does not finish
  refused by the port (`docs/C0_PORT_PLAN.md`, section 4;
  `docs/MUTOS_ORIGINAL_BUGS.md`, C0-01 .. C0-12).

## The other originals (2026-10-20)

Run while planning the driver (`../../docs/TOOLCHAIN_PLAN.md`; findings in
`../../docs/devlog/originals_and_ports.md`, "The other originals as
oracles"):

- **`c2`** runs as it is (`mutosemu.py c2 in.s out.s`). On the nine
  `tests/mutos_cc/11_kernel` `.s` goldens it gives
  `tests/mutos_as/kernel_opt/`'s `delay.s`, `prim.s`, `mem.s`, `pipe.s`,
  `nami.s`, `fio.s`, `v24.s`, `tty.s` and `amx.s` byte for byte - the
  optimized kernel sources are its output, which makes it the oracle
  for a port of `c2`.
- **`ld`** runs through a link with the real `crt0.o` and `libc.a`, but
  asks for system calls this emulator does not serve yet (`fstat`,
  `link`, `umask`, `chmod` - answered with `EINVAL`); without `link` its
  output stays in its temporary file `/tmp/lda<pid>`, which it then
  removes, so no `a.out` is left to compare yet (step 0 of the plan
  serves them). What it does is visible all the same: `-lc` is opened as
  `/lib/libc.a`, then `/usr/lib/libc.a` (under `--root`); `-r` and `-d`
  are refused.
- **`cc`** forks and execs its passes, which this emulator cannot. A
  scratch harness built on the emulator's `Emu` class faked them - at `fork` it
  snapshots the process and returns 0, at the child's `exece` it records
  the path and `argv` and resumes the parent as if the child had exited
  0, `wait` returns that child - and so listed every pass `cc` runs with
  its arguments. Serving these calls in `mutosemu.py` and a tracer for
  `cc` in this directory are step 0 of the plan, so that the port of `cc`
  is checked against the original the way the ports of `c0` and `c1`
  are.
- **`as`**: the options it knows (`-o`, `-e`, `-g`, `-C`, `-l`) and its
  mnemonic table (the 8087 set, no 80186 mnemonic) - the basis of the
  planned differential fuzzer for `mutos_as`.

## Limits of the emulator

- Unicorn executes x86 code as an 80386. Where that changes what these
  binaries compute, `mutosemu.py` does what the 8086 (and the V30) does:
  a shift or rotate by CL takes all of CL, not its low five bits (`1 <<
  40` folded by `c0` was 256, not 0) - every shift by CL in the text that
  may run with CL >= 32 gets a hook that shifts as the 8086 for a
  register operand (a memory operand with CL >= 32 stops the run, not
  modelled); and a signed division whose quotient is -32768 (-128) is a
  divide error, as on the 8086, which the 80186 and the 80386 allow
  (`-32768 / 1`) - the run stops there, as at Unicorn's own faults.
  Instructions that cannot reach those cases (a shift after `mov cx,<n>`
  with n < 32, `cwd` / `mov cx,<n>` / `idiv cx` with |n| >= 2) get no
  hook unless a jump can enter them. Other 80386 behaviour that differs
  from the 8086 (`push sp`, the opcodes the 8086 does not have) is not
  modelled: an instruction that depends on it runs as on the 80386.
- System calls served: `exit`, `read`, `write`, `open`, `close`, `creat`,
  `unlink`, `break`, `lseek`, `getpid`, `access`, `kill`, `signal`,
  `ioctl` (always "not a tty"); anything else gets `EINVAL` and a
  warning. No `fork`/`exec`/`wait` yet (see above). A signal is delivered when the
  program `kill()`s itself (libc.a raises SIGFPE that way): a caught one
  through its handler, which is then reset to the default, as v7's
  kernel does; a default one ends the program (status 128 + the
  signal). CPU exceptions (a divide fault) are not turned into signals:
  the run stops with an error.
- On the command line, a program that stops without `exit()` (or runs
  past `--max-insns`) gives status 125 and a `mutosemu:` message.
- `break` is accepted as asked; the memory is the 64K data segment.
- Only `0407` and `0411` files; the binaries in `mutos_bin/` are all one
  or the other (`cc` is `0407`, the rest `0411`).
