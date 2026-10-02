# `tests/mutos_cc/fltprobe/` — floating-point probes with real-hardware goldens

Not part of the 62-file corpus (and not counted in its N/62 figure): small
K&R programs whose `.s`/`.i`/`.1`/`.2` are generated on real MUTOS 1700
hardware with this directory's own `Makefile.mutos`, the same way as every
corpus category (see `../README.md`'s "Workflow"). They settle floating-point
shapes one round at a time; `../run_goldens.sh` picks up every file that has
a `.i.golden` with no script change, checks one with only a `.1.golden` from
`mutos_c0` on (listed apart, as category 7 - `mutos_cpp`'s output is then
not compared), and skips the others.

- **Round 1** (goldens 2026-09-29): nine files, now all **9/9 byte-exact**
  end-to-end (`mutos_cpp` → `mutos_c0` → `mutos_c1`). Four were written to
  confirm shapes taken from `libc.a`'s own compiled C, five to show shapes
  `mutos_c1` refused; the goldens corrected four details of the first set
  and settled all of the second - see `../../../docs/DEVLOG.md`'s "The
  fltprobe goldens". They stay here rather than moving into `../08_float/`
  (the plan when they were written): the corpus's "62" is restated across
  the documentation, and nothing is gained by renumbering it.
- **Round 2** (goldens 2026-09-30, commit `4c668c2`): four files for what
  `mutos_c1` refused or only inferred, now all **4/4 byte-exact** from
  `mutos_c0` on. Their `.i` files were not brought back, so
  `../run_goldens.sh` checks them from `mutos_c0` on (category 7); copy
  `p6_dblcon.i` ... `p9_init.i` here and run `make goldens` in `..` to move
  them into category 1. See `../../../docs/DEVLOG.md`'s "The round-2
  fltprobe goldens".
- **Round 3** (`p10`..`p13`, goldens pending): three files for what
  `mutos_c1` now compiles by inference only, one for what it still refuses.
  Run them with `make -f Makefile.mutos round3`.

## Why these, and not goldens alone

`libc.a` (`../../mutos1700_libc/`) contains four objects compiled from C by the
real MUTOS 1700 compiler - `atof.o`, `ecvt.o`, `gcvt.o`, `fltpr.o` - and their
code showed most floating shapes before any golden did. But `libc.a` was
compiled **with `-O`** (`atof.o` shares one `fstdp` between the two arms of an
`if`/`else`, `c2`'s cross-jumping), and it cannot show what the real compiler
does with a shape its sources do not contain. Round 1 checked both: it
confirmed the shapes taken from `libc.a` and corrected the model where
`libc.a` had misled it (a floating constant's degree), and it answered every
open question the five other files asked.

## The files

| File | What it probes | `mutos_c1` today | Result |
|---|---|---|---|
| `03_fltcmp.c` | comparisons as conditions: two doubles, a variable against a constant (exchanged by v7's `degree()` rule), a constant on the left, a float variable against a constant and against a double, an int converted, `!(a > b)`, a `while` | byte-exact | 95 |
| `04_fltconst.c` | int constants converted to floating (`d = 0`, `f = 4`, `e + 10`, `10 * d`, `e = -2`), the written zero, unary minus on a variable and on a written constant (folded: `.float -1.5...`) | byte-exact | 7 |
| `05_fltasop.c` | `atof.o`'s `10*fl + (c-'0')` (`c - '0'` computed in AX as `c + -48`), `*=` / `/=` by variables and int constants, a computed value `-` / `/` an int variable, a float target, an assignment's value compared | byte-exact | 33 |
| `06_fltfunc.c` | double and float parameters, double arguments (`sub sp,*8` - no decimal point), three functions returning a double, their results used | byte-exact | 26 |
| `p1_compare.c` | a float variable or a computed value compared with 0 (compared, never tested), a comparison as a value, `&&`, a computed value against a constant or another computed value, `1.5 < i`, `i < 1.5` (no truth test: the real `cc` rejects `!e` on a double, and `if (d)` / `while (a)` made its `c1` report "Floating point stack underflow") | byte-exact | 30 |
| `p2_arith.c` | `d + i`, `d - i`, `d * i`, `d / i` (no "reversed" entry points: `fldd d / itof / fsub`), computed right operands, a constant right operand of a computed value, `-(-e)`, `+=`, `-=`, `/=` by a computed value, `d *= i`, a compound assignment's value, `i *= e` | byte-exact | 17 (16 on MUTOS) |
| `p3_global.c` | file-scope `double`/`float` (plain, `static`, `extern`, initialized), a local `static double`, arrays, pointers, a structure member | byte-exact | 12 |
| `p4_const.c` | constants that are not exactly a float (`0.1`, `.03`, ...: `.double`, the MUTOS `ecvt()`'s digits), 2\*\*24 and more, `2 * 1.5` (label order), `d - 2.0`, constant call arguments | byte-exact | 3 |
| `p5_call.c` | an unused double result, a function returning a float, a computed argument that is not the last one, two double results in one expression, `long` and `char` to and from floating, a `register` variable converted | byte-exact | 12 |
| `p6_dblcon.c` | 8-byte constants (`0.1`) where the operand order depends on their degree: `d + 0.1`, `0.1 + d`, `a * 0.1` with a float, comparisons each way - degree 0, as a double variable | byte-exact | 9 |
| `p7_itofreg.c` | the register an int converted to floating is loaded into after a `/`, a call (AX), a negation (DI); `x + 1`, `x - 1`, `i + j` in AX (`inc ax`, `dec ax`, `add ax,j`), `i + j` in DI, `i * 3` | byte-exact | 62 |
| `p8_misc.c` | a `long` converted with DI free (`ltof`'s pushes through DI), `*p + 1.5` (degree 1 - as written), `i += d` and `i -= d` into an int (`add i,ax`), a double array subscripted by a variable (`mov cx,*3.` / `sal si,cl`) | byte-exact | 6 |
| `p9_init.c` | file-scope initializers: an int constant into a double (`.double 2.0`), `0.1` into a double and into a float (truncated: `9.99999940395355225e-02`), `-1.5`, a `static` one, and a code constant after them (`L10005`) | byte-exact | 6 |
| `p10_elem.c` | elements subscripted by a variable as a second operand, two in one expression, compared, a float array's; arrays of 8- and 16-byte structs by a variable | inferred | 21 |
| `p11_itof2.c` | ints computed in DI (a shift, a difference, `0 - i`) or AX (a quotient, a product by a constant after a `/`) converted; `i += f()` / `i -= f()` | inferred | 109 |
| `p12_init2.c` | an int constant into a float, a negated int, a negated inexact constant into a float, a `static float`, `0.3` into a float | inferred | 7 (6 on MUTOS?) |
| `p13_open.c` | an int converted after a conversion and after an element, a difference and a shift for AX, `x + 0` for AX, `i /= d`, a floating zero added and subtracted | refused | 41 (42 on MUTOS?) |

"Result" is what `main()` returns under C semantics (the host C compiler's
value). The real compiler's `i *= e` is `i * (int)e`, not `(int)(i * e)` - v7's
`build()` converts the right-hand side of a compound assignment to the
target's type first (`p2_arith.1.golden`: `FTOI`, then `ASTIMES(INT)`) - so
`p2_arith`'s golden returns 16 - and `p13_open`'s `j /= e` should give 42 on
MUTOS for the same reason; `p12_init2` 6 if its floats are truncated, as
`mutos_c1` writes them. Every round-1 and round-2 golden runs under
`../fuzz/x86sim.py` and returns these values (16 for `p2_arith`); `mutos_c1`'s
output is byte-identical to them. "inferred" means `mutos_c1` compiles the
file - its output runs under `x86sim.py` and returns the value above - with
no golden behind the shapes yet.

## Generating the goldens

On MUTOS 1700, from inside this directory:

```
make -f Makefile.mutos round3
```

(or plain `make -f Makefile.mutos` for all three rounds), then bring the
`.s`, `.i`, `.1` and `.2` files back to this directory on the modern host -
all four kinds: round 2's `.i` files were left behind - and run `make
goldens` in `..` (it packages every category's, this directory's
included).
