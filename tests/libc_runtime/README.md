# `tests/libc_runtime/` — the runtime of `libc.a` that compiled code calls

Step F3 of `docs/TOOLCHAIN_PLAN.md` (section 7.3, done 2026-11-09): every
routine of `libc.a` that `c1`'s code tables call without the programmer naming
it, measured against Python's arithmetic. These are the routines the corrected
copy of `libc.a` (step F3b, decision 14) is made of where they compute wrongly;
the bugs found are in `docs/MUTOS_ORIGINAL_BUGS.md` (L-01 to L-05, L-08 to
L-14).

## What is measured

The fourteen members of the plan, with the entry points compiled code calls:

| Member | Entry points | What compiled code uses them for |
|---|---|---|
| `lmul.o`, `ldiv.o`, `lrem.o` | `lmul`, `ldiv`, `lrem` | `l * m`, `l / m`, `l % m` on longs: both operands by value, the result in DX:AX |
| `almul.o`, `aldiv.o`, `alrem.o` | `almul`, `aldiv`, `alrem` | `l *= m`, `l /= m`, `l %= m`: a pointer to the long and the operand by value, the result stored and in DX:AX (`docs/MUTOS_C_ABI.md` 1.8) |
| `chkstk.o` | `chkstk` | the frame of a function with 100 bytes or more of locals (AX the size) |
| `convert.o` | `itof`, `ftoi` | int to a new top of the floating-point stack; the top, popped, to an int in AX |
| `lconvert.o` | `ltof`, `ftol` | a long on the stack to a new top; the top, popped, to DX:AX (also for `unsigned`) |
| `dmath.o` | `dadd`, `dsub`, `dmul`, `ddiv` | the arithmetic everything below calls: `[si]` op `[di]` into `fac` |
| `stacks.o` | `fldd`, `flds`, `fdup`, `fstd`, `fsts`, `fstdp`, `fstsp`, and `incfpsp`/`decfpsp` inside them | pushing a double or float (AX its address), storing the top, popping it |
| `stkmath.o` | `fadd`, `fsub`, `fmul`, `fdiv`, `fneg`, `fcmp`, `ftest` | the operations on the stack's top two, its top; `fcmp` and `ftest` pop and set the flags (and AH, for `sahf`) |
| `singles.o`, `doubles.o` | `fadds` ... `fdivrs`, `faddd` ... `fdivrd` | the top op a float or double in memory (AX its address) - the reverse forms (`r`) are not used by `c1` |

They pull in `fperr.o` (`__ovfl`, `__div0`: kill(getpid(), SIGFPE)), `cret.o`,
and through `stacks.o`'s `fltused` `atof.o` and `fltpr.o` (the conversions
between text and doubles that programs call by name - `atof` is measured by
`tests/mutos_as/float_coverage/libcatof.py`).

## How

`rtcheck.py` links the members (from `tests/mutos1700_libc/`, or with
`--archive` from another archive), `crt0.o` and a two-instruction `main` with
`mutos_ld -i`, loads the image into `tests/realbin/mutosemu.py`'s emulator, runs
`crt0` up to `main`, and then calls each entry point directly: the arguments
set up as compiled code sets them up, a HLT as the return address, the
registers it must preserve (SI, DI, BP; BX for the long helpers) filled with a
guard value. A call that reaches `__ovfl` or `__div0` is stopped there
("SIGFPE"); an 8086 divide error is "divide error".

The right result is C's on 32-bit longs and 16-bit ints, and for the floating
routines the exact rational result rounded to the nearest 56-bit MUTOS double
(ties to even; below the smallest double 0, above the largest SIGFPE). For
every bug of the original a model in `rtcheck.py` predicts its exact wrong
output; a case counts for a bug only if the original gives what the model
says, and a difference no model predicts is `UNEXPLAINED` (exit status 1).

Each entry point gets two sets of cases: **ordinary** (values as programs have
them - floating magnitudes from 2\*\*-40 to 2\*\*40, ints and longs of every
length and sign, divisors of every length) and **edge** (the limits of each
range, the smallest and largest doubles, zeros of every byte form, nearly
equal values, powers of two next to operands 55 to 59 binary orders below,
full and empty floating-point stacks, frames larger than the room below the
stack pointer). The `layout` group links the runtime sixteen times, with up to
36K of data before `fpstk`, to place the floating-point stack around address
0x8000 (L-12).

## Usage

```
python3 tests/libc_runtime/rtcheck.py                 # all groups, 1000 cases per entry and set
python3 tests/libc_runtime/rtcheck.py -n 20000 --seed 2 long float
python3 tests/libc_runtime/rtcheck.py show ldiv 99999 100000
python3 tests/libc_runtime/rtcheck.py show itof 1001
python3 tests/libc_runtime/rtcheck.py --archive x.a --fixed L-04,L-05 long float
python3 tests/libc_runtime/rtcheck.py --copy          # the corrected copy (step F3b)
python3 tests/libc_runtime/rtcheck.py show --archive src/libc_fixed/libc.a ftol 1.5e9
python3 tests/libc_runtime/linkcheck.py               # the copy on whole programs (goldens, triggers/)
python3 tests/libc_runtime/foldcheck.py               # what c1 folds against what the program computes
python3 tests/libc_runtime/hwkit.py --out hwkit       # the kit for the check on the hardware (step F3b)
make check-runtime                                    # the original, the copy, linkcheck, foldcheck; make check-realbin runs it too
```

Groups: `long`, `float` (the core and the sixteen stack operations), `conv`,
`stack`, `cmp`, `chkstk`, `layout`. `--fixed` names the bugs an archive given
with `--archive` has corrected: their models are dropped, so the original's
behaviour counts as `UNEXPLAINED` (step F3b's check of the corrected copy).
`--copy` is `--archive src/libc_fixed/libc.a` with `--fixed` the corrections
the copy's register in `docs/RULES.md` names ("The corrected copy of
`libc.a`"). Requires the Python module `unicorn` and a built tree (`make`).

## Result (2026-11-09)

Four seeds of 20,000 cases per entry point and set (`-n 20000 --seed 1` to
`4`, 3,745,620 calls): every result right or what a model predicts. Wrong in
the ordinary cases: `itof` 34 % (L-01), `ldiv`/`aldiv` 7 % (L-04), the value
`alrem` leaves in DX:AX 35 % (L-08), `dmul` 77 % and the products on the stack
76 to 86 % (L-05), `ftol` 3 % (L-11), the additions and subtractions 5 of
300,000 (L-14). Only in the edge cases: `lrem`/`alrem` (L-04, the divisor
-2\*\*31), `ftoi` (L-03), `fcmp` (L-10), `chkstk` (L-13), the placement of
`fpstk` (L-12). Right everywhere: `lmul`, `almul`, `ddiv` and the divisions on
the stack, `ftest`, `fneg`, the loads and stores (a float store keeps the
double's high four bytes - truncates, as C allows), the registers each routine
preserves, the stack pointer and the direction flag. `ltof(0)` leaves old
bytes below its zero (L-02), which no routine reads. `fsubrs` (L-09) is wrong
almost always, but compiled code does not call it.

## The corrected copy (step F3b)

`rtcheck.py --copy` measures the corrected copy of `libc.a`
(`src/libc_fixed/libc.a`; `src/libc_fixed/README.md`) the same way, with the
models of the bugs it corrects dropped - the corrections its register in
`docs/RULES.md` names. `linkcheck.py` checks it on whole programs:

- every assemblable `.s` golden of `tests/mutos_cc` (the real compiler's
  output), assembled by `mutos_as` and linked as `cc` links it once with the
  original archive and once with the copy: a program that links no corrected
  member gets the same image, the others run under the emulator and must give
  the same exit status and output (the interface: the copy takes the
  original's place under what the original compiler made);
- `triggers/*.c`, one small program per correction, compiled and linked by the
  port's `cc` in the original mode and with `-F` in the MUTOS root (the
  corrected mode: `lib/fixed/libc.a` through `ld`'s rule LIBC-F): each gives
  the original's wrong result - or SIGFPE - with `cc` and C's with `cc -F`
  (`expect: original X, corrected Y` in its comment).

The kernel's `arith.a` (`tests/mutos1700_arith/`) and its corrected copy
(`src/libc_fixed/arith.a`): `rtcheck.py --archive
tests/mutos1700_arith/arith.a.base64.txt long` (the original's long helpers:
L-04 and L-08, as `libc.a`'s) and `rtcheck.py --copy src/libc_fixed/arith.a
long` (`--copy` takes the corrections of the members the archive has).

`make check-runtime` runs `rtcheck.py`, `rtcheck.py --copy`, both on
`arith.a`, `linkcheck.py` and `foldcheck.py`; `make check-realbin` too.

Result (2026-11-09): `rtcheck.py --copy -n 20000`, seeds 1 to 4 (3,745,620
calls): every result right but where a bug the copy does not correct explains
it - L-02 (`ltof(0)`'s old bytes), L-09 (`fsubrs`) and L-13 (`chkstk`) - and
`-2**31 / -1`, undefined in C. `linkcheck.py`: 122 golden programs, 73 the same
image, 49 run, each the same; the nine trigger programs (L-01, L-03, L-04,
L-05, L-08, L-10, L-11, L-12, L-14) give the original's result with `cc` and
C's with `cc -F`.

## What `c1` folds (step F3b, part 2)

`c1` folds constants with re-enactments of these routines
(`src/mutos_cc/c1port/c1lib.c`): a long constant's `/` and `%` with
`aldiv`'s and `alrem`'s code, an int constant converted to a floating one
with `itod`'s, a floating constant converted to an int or a long with
`dtoi`'s and `ftol`'s - in the original mode the original's, in the
corrected mode the copy's (the rules L-01, L-03, L-04 and L-11 of `c1`,
`docs/RULES.md`, "The tools follow the copy"). `foldcheck.py` checks that a
folded constant is what the program computes at run time: for each kind a
program whose initializers `c1` folds and whose `main()` computes the same
from the operands and writes both, compiled and linked by the port's `cc`
in the MUTOS root without a switch (the original `libc.a`) and with `-F`
(the copy), run under the emulator, compared value for value - the edges of
each routine's paths and bugs and random operands (`-n`, `--seed`).
Constants the original's `c1` refuses (`dtoi` from 2\*\*15, `ftol` from
2\*\*30) are only in the corrected mode's program; int to double is
compared in the corrected mode only, because the folded double reaches the
object as `c1`'s 18-digit text read back by the assembler's `atof()`, which
in the original mode does not give `itof`'s bytes back (777: one unit
lower in the last place). The conversion of a constant's text (L-05) is
`tests/mutos_as/float_coverage/libcatof.py copy`'s.

Result (2026-11-11): 300 values per kind and mode (seed 1), and 2,000 (seed
2): every folded value the one computed at run time - in the original mode
the original's wrong quotients included (`99999L / 100000` is 2 both ways),
in the corrected mode C's.

## The check on the hardware (step F3b)

The plan's last check of the copy (`docs/TOOLCHAIN_PLAN.md`, F3b): a
program linked with the copy by the original `ld` on the MUTOS 1700
hardware, before the copy replaces anything. `hwkit.py` makes the kit
for it - a directory (`--out`, default `hwkit`) to copy to the hardware,
and with `--tar FILE` a tar file of it:

- the trigger programs `triggers/*.c` and the copy, `fixlibc.a`;
- `hwcheck.sh`, for the hardware's `sh` (v7's Bourne shell, nothing
  later): each program compiled by the hardware's `cc -c`, linked by its
  `ld` once as `cc` links it (`ld -X /lib/crt0.o -o N.H [-i] N.o -lc`,
  the original `/lib/libc.a`) and once with the copy alone in the place
  of `-lc` (`N.F`), both run; the exit statuses go into a file `status`,
  and every `N.o`, `N.H` and `N.F` is compared with `cmp` against the
  kit's `expect/`. Nothing outside the kit's directory is changed;
- `expect/`: `N.o`, `N.H`, `N.F` and `status`, and a `README`.

`expect/` is the prediction, made by the originals under the emulator:
the original `cc`'s whole chain (`tests/realbin/cctrace.py`'s `--run`)
for `N.o`, the original `ld` for `N.H` and `N.F`, then both images run.
`hwkit.py` writes the kit only if the cross toolchain agrees -
`mutos_cc -c` gives the same `N.o`, `mutos_ld` the same images,
`mutos_cc -F` (the corrected mode, `ld`'s rule LIBC-F) the same `N.F` -,
if the original `ld` links the same `N.F` with the copy's table of
contents out of date (after a copy to the hardware it usually is: `ld`
warns `fixlibc.a(__.SYMDEF): out of date` and reads the archive member
by member), and if each image ends as its trigger's `expect:` line
says. The emulator is an 8086; the owner's machine has a NEC V30, which
computes as the 8086 where the two could differ here (shifts by `CL`
unmasked, `idiv`'s quotient -32768 a divide error - `docs/devlog/m2_as.md`,
"CPU reference").

Prediction (2026-11-11): all of it holds for the nine triggers - with
`/lib/libc.a` the original's results (exit status 0 or 2, or SIGFPE,
which the v7 shell reports as 136), with the copy C's (1, and 0 for
`l04_ldiv`). `l03_ftoi.H` reads the word at address 8000 of its data
segment (L-03): SIGFPE unless that word happens to be 8000 hex.

Result (the owner's A7100 with the NEC V30 - its 8086 is no longer
available - under MUTOS 1700, 2026-11-14, `sh hwcheck.sh >log 2>&1`; the log is `hwlog_2026-11-14.txt`): **as predicted, all of
it.** The nine status lines are the predicted ones, the hardware's `sh`
gives 136 for the four programs SIGFPE kills (`l03_ftoi`, `l10_fcmp`,
`l11_ftol`, `l12_fpstk` with `/lib/libc.a`) and prints "Floating
exception - core dumped" for each (the number before it is the process
id), and the log ends `statuses: as expected` and `files: every .o, .H
and .F as expected` - all 27 files are the emulated originals' byte for
byte. `ld` did not warn "out of date": `fixlibc.a` had evidently kept its
file time, so the hardware's `ld` read it through its table of contents; the
reading member by member is measured under the emulator only. The V30
so computes the nine programs as the emulator's 8086 does.
