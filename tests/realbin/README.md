# `tests/realbin/` — the original MUTOS 1700 binaries as an oracle

`../../mutos_bin/` holds the original toolchain binaries brought from the
hardware (`as`, `c0`, `c1`, `c2`, `cc`, `cpp`, `ld`, as base64 text - see
its `README.md`). This directory runs them on the modern host under an
8086 emulator and reads `c1`'s code tables out of its data segment. It
needs no hardware: the real compiler's answer for any input is one
emulator run away. It is also where `mutos_c1`, `mutos_c0`, `mutos_cpp`,
`mutos_ld`, `mutos_cc` and `mutos_c2` - since 2026-10-13, 2026-10-18,
2026-10-22 (the next two), 2026-10-24 and 2026-10-25 ports of the real `c1`,
`c0`, `cpp`, `ld`, `cc` and `c2` (`src/mutos_cc/c1port/`, `c0port/`,
`src/mutos_cpp/`, `src/mutos_ld/`, `src/mutos_cc/ccport/`, `c2port/`,
`docs/C1_PORT_PLAN.md`, `docs/C0_PORT_PLAN.md`,
`src/mutos_cpp/README.md`, `src/mutos_ld/README.md`,
`src/mutos_cc/README.md`) - are checked against the originals, on every
golden and on random input.

Requirements: Python 3 and the module `unicorn` (`pip install unicorn`)
for running the binaries; `capstone` (`pip install capstone`) only for
`c1dis.py`, `c0dis.py`, `cppdis.py`, `lddis.py`, `ccdis.py` and `c2dis.py`; a C
compiler (`$CC`, default `cc`) for `ccstub.c` (`cccmp.py`, `ccfuzz.py`).

## Files

| File | What it is |
|---|---|
| `mutosemu.py` | The emulator: loads a MUTOS `a.out` (0411 or 0407) into Unicorn, builds `crt0`'s argument stack and serves the system calls behind `crt0`'s two far-call trampolines (`Iscadr` → CS:0002, `Isgadr` → CS:0004, see `docs/MUTOS_C_ABI.md` §2.2) - `fork`/`exece`/`wait` faked, so that `cc` runs (see "Limits of the emulator"). Shifts and rotates by CL as the 8086 does them. Usable from the command line and as a module (with hooks on text offsets; `eof_limit` stops a program that reads the end of a file again and again; `on_exec` decides what an `exece` does). |
| `cctrace.py` | The original `cc`, traced: every `exece` with its `argv`, in order with what is written to stdout and stderr, and the exit status - the oracle for the port of `cc`. Traced, no pass runs (an existing file "exits 0"); with `--run` the original passes run too, as emulated a.outs from a MUTOS root of the originals (`--make-root` writes one), so the whole chain `cpp`, `c0`, `c1`, `c2`, `as`, `ld` runs. `goldens`: `cc -S`, `cc -P` and `cc -O -S` against the goldens they made on the hardware. |
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
| `cppmap.txt` | A map of `cpp` - which kept its symbol table, so the names are the binary's: its functions (v7's `cpp.c`, `yylex.c` and `cpy.y` in source order, one new, then `libc.a` from text 26c6) with their text offsets, how each compares with v7 and what the binary does differently, and its data symbols - the port's memory image follows them. |
| `cppdis.py` | `c1dis.py` switched to `cpp` (`c1dis.use('cpp')`): the annotated disassembly by function name from `cppmap.txt`. |
| `cpptables.py` | `--c`: the original `cpp`'s initialized data segment as C - `src/mutos_cpp/cppdata.c` (generated, checked in; `--check-c` checks it is current); `--symbols`: its symbol table; `--tables`: its strings, `yylex()`'s tables and yacc's tables. |
| `cppcmp.py` | `mutos_cpp` against the original: the same command line in the same directory, the output (stdout or a file named on the command line), messages and exit status compared byte for byte - `goldens` (the source of each of the 137 `.i` goldens with `-P`, also against the golden, without `-P`, and into an output file) or `files`. A refusal of the port must have written a prefix of the original's output, also where the original does not finish (a CPU fault), and there the port must have refused; where both run into the limits (an endless expansion: 1 MB of output or 40M instructions; the port 5 s of CPU) their outputs must agree as far as the shorter goes (`endless`). |
| `cppfuzz.py` | Differential fuzzing of `mutos_cpp` with `cppcmp.py`'s comparison: random programs for every part of the preprocessor (macros with and without parameters, calls across lines with comments and strings in the actuals, conditionals and `#if` expressions with every operator and number base, character constants, includes with `" "` and `< >`, `-I` and a MUTOS root, `#line`, `__LINE__`, `__FILE__`, comments, backslash-newlines, odd bytes), random options (`-P`, `-C`, `-R`, `-D`, `-U`, `-I`, an output file, stdin), programs at cpp's limits (its string space, symbol table, pushback, the yacc stack, long tokens); `--mutate P` for the error paths. |
| `ldmap.txt` | A map of the stripped `ld`: its functions (v7's `ld.c` in source order, `bytrel()` new after `load2td()`, then `libc.a` from text 20c0) with their text offsets, how each compares with v7 (`same`, `delta`, `new`) and what the binary does differently, and the data offsets of its globals - the port's memory image follows them. |
| `lddis.py` | `c1dis.py` switched to `ld` (`c1dis.use('ld')`): the annotated disassembly by function name from `ldmap.txt`. |
| `ldtables.py` | `--c`: the original `ld`'s initialized data segment as C - `src/mutos_ld/lddata.c` (generated, checked in; `--check-c` checks it is current); `--strings`: its strings. |
| `ldcmp.py` | `mutos_ld` against the original: the same command line in two copies of the same directory with the same MUTOS root (`crt0.o`, `libc.a` at its `__.SYMDEF` time, `tmp/`, a case's own libraries); the messages, the exit status and every file left in the directory (bytes and mode; the temporary output name counted as one) compared - `goldens` (every assemblable `.s` golden of `tests/mutos_cc`, assembled by `mutos_as`, linked with twelve command lines), `cases` (hand-written: the messages, the options, `-l` names, archives, bad files, the tables' limits, the original's bugs) or `files`. A refusal of the port must have written a prefix of the original's messages, and where the original does not finish the port must have refused. `-r` and `-d` (the port's extension) are not compared; `tests/mutos_ld/check_rflag.py` checks them. |
| `ccmap.txt` | A map of the stripped `cc` (0407: text and data in one segment): its functions (v7's `cc.c` in source order, then `libc.a` from text 0fc6) with their text offsets, how each compares with v7 and what the binary does differently, and the addresses of its globals in the segment - the port's memory image follows them. |
| `ccdis.py` | `c1dis.py` switched to `cc` (`c1dis.use('cc')`; for a 0407 binary the data is indexed by its address in the segment): the annotated disassembly by function name from `ccmap.txt`. |
| `cctables.py` | `--c`: the original `cc`'s initialized segment - its text and data - as C, `src/mutos_cc/ccport/ccdata.c` (generated, checked in; `--check-c` checks it is current); `--strings`: its strings. |
| `ccstub.c` | A pass for `cccmp.py`: copied to every pass's place in the port's MUTOS root, it logs its path and arguments, marks its place in `cc`'s stdout and stderr, ends with the status a case gives it - or hands itself to `cccmp.py --pass`, which runs the pass for real - and else leaves the files the pass would write. |
| `cccmp.py` | `mutos_cc` against the original: the same command line in two copies of one directory, each with a MUTOS root of its own; the passes run (their MUTOS paths and arguments), what `cc` and the passes wrote to stdout and stderr before, between and after them, the exit status and the files left (in the directory and the root's `tmp/`; `cc`'s temporary names counted as one) compared. The original under the emulator, its passes traced (`cases`, `files`) or run as emulated originals (`run`, `files --run`); the port with `ccstub.c` as every pass, traced likewise, or running the emulated originals (`run`), or the ported tools (`programs`: against the original chain; `goldens`: the ported chain against the real-hardware goldens - `mutos_c2` since 2026-10-25, the original `c2` until then). A refusal of the port must have run and written what the original had up to there. |
| `ccfuzz.py` | Differential fuzzing of `mutos_cc` with `cccmp.py`'s comparison, the passes traced: random command lines of every option the original knows and some it does not, names of every kind (sources, objects, archives, absent files, names of 14 and 15 characters, long paths to short sources, which `cc` copies whole), the limits (`-D`, `-P`, files, `av[]`, `ts[]` and `malloc()`, `-K`, `-B`) and passes that fail or are killed. |
| `c2map.txt` | A map of the stripped `c2`: its functions (v7's `c20.c` and `c21.c` in source order, `reducelit()`, `source()`, `setcc()` gone, twelve new, then `libc.a` from text 3484) with their text offsets, how each compares with v7 (`same`, `delta`, `x86`, `new`) and what the binary does differently, and the data offsets of its globals - the port's memory image follows them. |
| `c2dis.py` | `c1dis.py` switched to `c2` (`c1dis.use('c2')`): the annotated disassembly by function name from `c2map.txt`. |
| `c2tables.py` | `--c`: the original `c2`'s initialized data segment as C - `src/mutos_cc/c2port/c2data.c` (generated, checked in; `--check-c` checks it is current); `--strings`: its strings; `--optab`: its opcode table. |
| `c2cmp.py` | `mutos_c2` against the original: the same command line in two copies of one directory - `c2 in out`, `c2 - in out` (the statistics), `c2 <in >out`, the hand-written ones -, the output, the messages and the exit status compared byte for byte - `goldens` (every `.s` golden of `tests/mutos_cc` three ways; the nine `11_kernel` outputs also against `tests/mutos_as/kernel_opt/`), `cases` (hand-written: the command lines, the input's form, the line buffer's end, long tokens and operands over the fixed buffers, many chunks, a large function, the heap's end, label numbers, the original's bugs) or `files`. A refusal of the port must have written a prefix of the original's output, and where the original does not finish the port must have refused; where both run into the limits their outputs must agree as far as the shorter goes (`endless`). |
| `c2fuzz.py` | Differential fuzzing of `mutos_c2` with `c2cmp.py`'s comparison. Generators: the real compiler's output for `c1fuzz.py`'s programs (`--gen c1fuzz`) and `fuzz_c.py`'s (`--gen fuzz_c`), through the ported chain; assembly in `c1`'s style made to reach every pass of `c2` (`--gen asm`: register moves and renames, `push`/`pop`, index patterns, constants, branches near and far, jumps to jumps, unreachable code, `.data`/`.bss`, several functions); `--gen mix` all three; `--mutate P` damages the cases (long operands and first tokens, odd bytes, lines deleted, doubled, swapped or cut, branches to undefined labels or to code, empty lines, a missing last linefeed, a line past the buffer). |
| `ldfuzz.py` | Differential fuzzing of `mutos_ld` with `ldcmp.py`'s comparison: random objects (text and data words with every relocation kind, the pc-relative and byte-shift bits; external definitions in every segment, absolutes, undefined, commons, locals, "L" locals, file names, shared names, a spare byte, an odd size, no relocation bits, another magic), real `crt0.o` and `libc.a` members, archives plain, current and out of date - by name or through `-l` from the root -, random options; `--mutate P` damages files for the error paths. |
| `c0fuzz.py` | Differential fuzzing of `mutos_c0` with `c0cmp.py`'s comparison. Generators: its own, declaration-heavy (storage classes, typedefs, structures, unions, enums, bit-fields, initializers, K&R parameters, blocks, labels, switches, the old `=+` operators, `\/` and `/\`, escapes, comments, `# line` lines, SOH markers), `c1fuzz.py`'s (`--gen c1fuzz`) and `fuzz_c.py`'s (`--gen fuzz_c`); `--mutate P` deletes, duplicates, swaps or inserts tokens, for the error paths. |
| `c1fuzz.py` | Differential fuzzing: random programs through `mutos_cpp` and the emulated real `c0`, then the emulated original `c1` against `mutos_c1` (depth 4 by default, 1 and 2 too). Generators: its own (every type, a structure with bit-fields, registers, calls, switches), `fuzz_c.py`'s (`--gen fuzz_c`), and long constant arithmetic at the edges of `lconst()`'s library paths (`--gen lconst`). The port's refusals are checked too: what it wrote before must be the original's, and where the original does not finish, the port must have refused. |

## Running

```sh
make check-realbin            # from the repo root: tables, goldens, coverage,
                              # mutos_c1 at depths 2-4 and a fuzz run,
                              # mutos_c0 on the goldens and two fuzz runs,
                              # mutos_cpp on the goldens and two fuzz runs,
                              # the original cc traced (cctrace.py goldens),
                              # mutos_cc against it (cccmp.py cases, run,
                              # programs, goldens; a ccfuzz.py run),
                              # mutos_c2 on the goldens, the cases and
                              # four fuzz runs,
                              # the original ld through mutos_root/
                              # (tests/mutos_root/check_root.py --realbin)
python3 tests/realbin/check_real.py goldens
python3 tests/realbin/check_real.py coverage --unused
python3 tests/realbin/c1trees.py --depth 4 --show 1
python3 tests/realbin/c1fuzz.py -n 1000 -s 7 --keep /tmp/c1diff
python3 tests/realbin/c1fuzz.py --gen lconst -n 300
python3 tests/realbin/c0cmp.py goldens
python3 tests/realbin/c0fuzz.py -n 2000 -s 7 --mutate 0.7 --keep /tmp/c0diff
python3 tests/realbin/c0dis.py fold
python3 tests/realbin/cppcmp.py goldens
python3 tests/realbin/cppfuzz.py -n 2000 -s 7 --mutate 0.3 --show 20 --keep /tmp/cppdiff
python3 tests/realbin/cppdis.py _subst
python3 tests/realbin/ldcmp.py goldens
python3 tests/realbin/ldcmp.py cases --show 100
python3 tests/realbin/ldfuzz.py -n 2000 -s 7 --mutate 0.5 --keep /tmp/lddiff
python3 tests/realbin/lddis.py _bytrel
python3 tests/realbin/cccmp.py cases --show 20
python3 tests/realbin/cccmp.py files -c -O x.c y.o   # one line, traced
python3 tests/realbin/cccmp.py files --run -o x x.c  # one line, passes run
python3 tests/realbin/ccfuzz.py -n 5000 -s 7 --keep /tmp/ccdiff
python3 tests/realbin/ccdis.py _copy
python3 tests/realbin/c2cmp.py goldens
python3 tests/realbin/c2cmp.py cases --show 100
python3 tests/realbin/c2cmp.py files - x.s     # one file, "c2 - x.s out"
python3 tests/realbin/c2fuzz.py -n 500 -s 7 --gen asm --keep /tmp/c2diff
python3 tests/realbin/c2fuzz.py -n 500 -s 7 --gen mix --mutate 0.5
python3 tests/realbin/c2dis.py _pushopt
```

The original `cc` (since 2026-10-20, `cctrace.py`):

```sh
T=tests/realbin/cctrace.py
python3 $T -O -o x x.c               # the passes cc would run, none run
python3 $T --run -o x x.c            # the whole original chain: x is linked
python3 $T --run --cwd dir -S x.c    # x.s in dir, c1's messages and status
python3 $T goldens                   # cc -S, -P, -O -S against the goldens
python3 $T --make-root /tmp/oroot    # a MUTOS root of the originals, to keep
```

Its output: `exec PATH ARGV...` per pass, `1| ...` and `2| ...` for
stdout and stderr, `exit N`; the temporary names `/tmp/ctm<k>XXXXXX`
(`--raw`: as they are). By default a fresh MUTOS root of the originals is
made for the run; `--root DIR` takes another (`mutos_root/`, say).

The passes one by one, the way `cc` runs them:

```sh
E=tests/realbin/mutosemu.py
python3 $E cpp -P x.c > x.i          # headers: relative to the current directory
python3 $E c0 x.i x.1 x.2
python3 $E c1 x.1 x.2 x.s
python3 $E as -o x.o x.s
python3 $E --root R ld -X /lib/crt0.o x.o -lc   # R: a root with lib/crt0.o,
                                     # lib/libc.a and tmp/ (cctrace.py --make-root)
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

## The port of `cpp` against it (2026-10-22)

- The source of every `.i` golden (137), with `-P`, without `-P` and into an output
  file: 411/411 identical - output, messages, exit status (`cppcmp.py goldens`); the
  original's `-P` output is also each golden.
- Differential fuzzing (`cppfuzz.py`), with the final port: 2,000 programs - seed 20
  (1,000 generated) and seed 21 (1,000, `--mutate 0.5`) -, no difference: 1,802
  identical, 33 refused, 130 on which the original does not finish (each refused by the
  port, its output a prefix of the original's), 35 endless in both. `make
  check-realbin` runs 200 of each (seed 1).
- The port's refusals are each checked against the original's prefix; the categories
  (`cppfuzz.py --show`): a frame array run past or read unwritten, an `#include` of a
  directory, a `#if` division the 8086 faults on, a wild pointer into libc.a's state -
  `src/mutos_cpp/README.md` and `docs/MUTOS_ORIGINAL_BUGS.md` (CPP-01 ..).

## The port of `ld` against it (2026-10-22)

- Every assemblable `.s` golden (131), assembled by `mutos_as`, linked with twelve
  command lines (`ldcmp.py goldens`): 1,572/1,572 the same - messages, exit status,
  the files left in the directory, bytes and mode.
- The hand-written cases (`ldcmp.py cases`: 91, 89 the same, 2 refused) and
  differential fuzzing (`ldfuzz.py`, 2,000 cases with and without damaged files:
  1,955 the same, 45 refused; see `STATUS.md`) - no difference; the refusals (an input that is a directory, an `lseek()` to a negative
  offset - `docs/MUTOS_ORIGINAL_BUGS.md`, LD-07) each checked against the original's
  messages. `make check-realbin` runs the goldens, the cases and 300 fuzz cases of
  each kind (seed 1).
- The original's bugs the cases demonstrate: `docs/MUTOS_ORIGINAL_BUGS.md`, LD-01 ..

## The port of `cc` against it (2026-10-24)

- `cccmp.py cases`: 177 command lines, the passes traced - 175 the same
  (the passes with their arguments, stdout and stderr around them, the
  exit status, the files left), 2 refused (`docs/MUTOS_ORIGINAL_BUGS.md`,
  CC-01: a pointer past the break).
- `cccmp.py run`: the construct corpus's programs, two command lines each
  from a cycle of nine, every pass the emulated original in both: 124/124
  the same. `cccmp.py programs`: the port with the ported passes against
  the original chain, `cc x.c` and `cc -O -o x x.c`: 124/124 the same
  (the same `a.out`, the same messages).
- `cccmp.py goldens`: the ported chain - `cc -S` 132/132 (`c1`'s messages
  and exit status included; `p19_open3` refused as
  `invalid_goldens.txt` says), `cc -P` 132/132, `cc -O -S` 9/9 with the
  original `c2`.
- `ccfuzz.py`: see `STATUS.md` (Milestone 4); `make check-realbin` runs
  1,000 cases (seed 1).
- Thirteen mutants of the port, each with one small fault, checked how
  much the comparison sees: every one that can change what `cc` does is
  found; the four it does not find cannot (`strcat(kopt, ...)` for
  `strcat(kopt + 1, ...)`, a removal `dexit()` repeats, two orders of
  evaluation that cannot meet, a `malloc()` arena that ends up the same).
- The original's bugs the cases demonstrate: `docs/MUTOS_ORIGINAL_BUGS.md`,
  CC-01 ..

## The port of `c2` against it (2026-10-25)

- `c2cmp.py goldens`: every `.s` golden, 132 files three ways - `c2 in
  out`, `c2 - in out`, `c2 <in >out` -: 396/396 the same (the output, the
  statistics, the exit status); the nine `11_kernel` outputs are
  `kernel_opt/`'s.
- `c2cmp.py cases`: 66 hand-written cases - 59 the same, 6 where the
  original never ends and the port refuses (C2-01, C2-07: `dest()`'s and
  `iterate()`'s endless loops), 1 refused (a directory as the input).
- `c2fuzz.py`: see `STATUS.md` (Milestone 5); `make check-realbin` runs 100
  cases of each generator and 100 damaged ones (seed 1).
- The original's bugs the cases demonstrate: `docs/MUTOS_ORIGINAL_BUGS.md`,
  C2-01 .. C2-07.

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
- **`ld`** links with the real `crt0.o` and `libc.a`: `-lc` is opened as
  `/lib/libc.a`, then `/usr/lib/libc.a` (under `--root`); `-r` and `-d`
  are refused. Since step 0 of the plan (the same day) the emulator
  serves the `fstat`, `link`, `umask` and `chmod` it asks for, so its
  output - written to `/tmp/lda<pid>`, then linked to the output name -
  is left to compare: on fourteen compiled goldens five were `mutos_ld`'s
  (then a port of v7's `ld.c`) byte for byte, nine differed by the text's
  padding alone (the original appends 6 bytes of `eb fe`, `mutos_ld`
  rounded up to 8 - see
  `docs/devlog/originals_and_ports.md`, "Step 0 of the plan"). With
  `libc.a` newer than its `__.SYMDEF` time it warns `out of date`; at
  that time, as in `mutos_root/`, it does not.
- **`cc`** forks and execs its passes. First traced with a scratch
  harness; since step 0 the emulator fakes `fork`/`exece`/`wait` itself
  (below) and `cctrace.py` traces `cc` - or runs the whole original
  chain with `--run`. Against the goldens it made on the hardware
  (`cctrace.py goldens`): `cc -S` 132/132 `.s` with `c1`'s messages and
  exit status where the hardware's transcripts have them, `cc -P` 132/132
  `.i`, `cc -O -S` 9/9 of `kernel_opt/`'s optimized kernel sources. The
  port of `cc` is to be checked against these traces the way the ports
  of `c0` and `c1` are checked against the emulated originals.
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
- System calls served: `exit`, `fork`, `read`, `write`, `open`, `close`,
  `wait`, `creat`, `link`, `unlink`, `exece` (and `exec`), `chmod`,
  `break`, `stat`, `lseek`, `getpid`, `fstat`, `access`, `kill`,
  `signal`, `ioctl` (always "not a tty"), `umask`; anything else gets
  `EINVAL` and a warning. `stat`/`fstat` give v7's `struct stat` from the
  host's (the longs high word first); `umask` is the emulated process's,
  starting from the host's, and `creat` applies it to a new file's mode.
- `fork`/`exece`/`wait` are faked: one process at a time. `fork` saves
  the parent and goes on as the child; the child's `exece` asks the
  `on_exec` hook (default: ENOENT if the file does not exist, else "exited
  0"; `cctrace.py --run` runs the program), and then - or at the child's
  `exit` - the parent is restored and its `fork` returns. Right for a
  parent that waits at once, as `cc` does; a parent that goes on while
  its child runs (a pipeline, `&`) is not modelled. A signal is delivered when the
  program `kill()`s itself (libc.a raises SIGFPE that way): a caught one
  through its handler, which is then reset to the default, as v7's
  kernel does; a default one ends the program (status 128 + the
  signal). CPU exceptions (a divide fault) are not turned into signals:
  the run stops with an error.
- On the command line, a program that stops without `exit()` (or runs
  past `--max-insns`) gives status 125 and a `mutosemu:` message.
- What a program `write()`s to a file is in the file however the run
  ends, as with the kernel: the host files are buffered and are closed
  at the end of every run. Until 2026-10-22 they were closed only after
  `exit()` or the instruction limit, so a fault lost up to 8 KB of an
  output file (`cppfuzz.py` found it: the port's output was longer than
  the crashed original's).
- `open()` of a directory fails (ENOENT): the host cannot read one as a
  file, where MUTOS hands out its entries. `mutos_cpp` refuses such an
  `#include` rather than follow either.
- `break` is accepted as asked; the memory is the 64K data segment.
- Only `0407` and `0411` files; the binaries in `mutos_bin/` are all one
  or the other (`cc` is `0407`, the rest `0411`).
