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
make check-runtime                                    # what make check-realbin runs too
```

Groups: `long`, `float` (the core and the sixteen stack operations), `conv`,
`stack`, `cmp`, `chkstk`, `layout`. `--fixed` names the bugs an archive given
with `--archive` has corrected: their models are dropped, so the original's
behaviour counts as `UNEXPLAINED` (step F3b's check of the corrected copy).
Requires the Python module `unicorn` and a built tree (`make`).

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
