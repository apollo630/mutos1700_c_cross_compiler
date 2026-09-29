# `tests/mutos_cc/fltprobe/` — floating-point probes waiting for real-hardware goldens

Not part of the 62-file corpus (and not counted in its N/62 figure): nine
small K&R programs whose `.s`/`.i`/`.1`/`.2` still have to be generated on
real MUTOS 1700 hardware, with this directory's own `Makefile.mutos`, the same
way as every corpus category (see `../README.md`'s "Workflow"). They settle
the floating-point shapes `mutos_c1` either takes from evidence that is not
a golden - the real compiler's own output in `libc.a` - or still refuses.
Once their goldens are back, `../run_goldens.sh` picks them up with no script
change, and the four confirmation files move into `../08_float/` as
`03_`..`06_` - every name already fits there.

## Why these, and not goldens alone

`libc.a` (`../../mutos1700_libc/`) contains four objects compiled from C by the
real MUTOS 1700 compiler - `atof.o`, `ecvt.o`, `gcvt.o`, `fltpr.o` (v7's
`atof.c`, `ecvt.c`, `gcvt.c` and MUTOS's own float printing) - and their code
shows most of the floating shapes the corpus lacks: comparisons (`fcmp` /
`sahf`), int constants converted to floating constants (including zero,
`bc a2 31 00` = `.float 0.00000000000000000e+00`), `fneg`, `*=` and `/=`, a
computed value combined with an int on the stack (`itof` / `fadd`), `fstd`
for an assignment whose value is used, double parameters and arguments, and a
function returning a double through `fac`. Full derivation in
`../../../docs/DEVLOG.md`'s "Floating shapes from libc.a's compiled C".

But `libc.a` was compiled **with `-O`**: its code went through the MUTOS
`c2` (`atof.o` shares one `fstdp` between the two arms of an `if`/`else`,
`c2`'s cross-jumping). `mutos_c1` takes only what the optimizer cannot have
changed; the goldens here check the rest - label numbers, where a `.data`
block goes, the position of a function's return sequence relative to its
`jmp L<n>` - and show the shapes nothing in `libc.a` has.

## The files

| File | What it probes | `mutos_c1` today | Result |
|---|---|---|---|
| `03_fltcmp.c` | comparisons as conditions: two doubles, a variable against a constant (exchanged by v7's `degree()` rule), a constant on the left, a float variable (degree 1), an int converted, `!(a > b)`, a `while` | compiles | 95 |
| `04_fltconst.c` | int constants converted to floating (`d = 0`, `f = 4`, `10 * d`, `e = -2`), the written zero, unary minus on a variable and on a written constant | compiles | 7 |
| `05_fltasop.c` | `atof.o`'s `10*fl + (c-'0')`, `*=` / `/=` by variables and int constants, a computed value `-` / `/` an int variable, a float target, an assignment's value compared | compiles | 33 |
| `06_fltfunc.c` | double and float parameters, double arguments, three functions returning a double, their results used | compiles | 26 |
| `p1_compare.c` | a float variable or a computed value compared with 0, truth tests (`if (d)`, `while (a)`, `!e`), a comparison as a value, `&&`, a computed value against a constant or another computed value, `1.5 < i`, `i < 1.5` | refused | 32 |
| `p2_arith.c` | `d + i`, `d - i`, `d * i`, `d / i` (does the real compiler use `libc.a`'s `fsubrs`/`fdivrs`...?), computed right operands, a constant right operand of a computed value, `-(-e)`, `+=`, `-=`, `/=` by a computed value, `d *= i`, a compound assignment's value, `i *= e` | refused | 17 |
| `p3_global.c` | file-scope `double`/`float` (plain, `static`, `extern`, initialized), a local `static double`, arrays, pointers, a structure member | refused | 12 |
| `p4_const.c` | constants that are not exactly a float (`0.1`, `.03`, ...: the text of the 8-byte form), 2\*\*24 and more, `2 * 1.5` (label order), `d - 2.0`, constant call arguments | refused | 3 |
| `p5_call.c` | an unused double result, a function returning a float, a computed argument that is not the last one, two double results in one expression, `long` and `char` to and from floating, a `register` variable converted | refused | 12 |

"Result" is what `main()` returns under C semantics (the host C compiler's
value). `mutos_c1`'s output for the four confirmation files runs under
`../fuzz/x86sim.py` and returns exactly these values.

## Generating the goldens

On MUTOS 1700, from inside this directory:

```
make -f Makefile.mutos
```

then bring the `.s`, `.i`, `.1` and `.2` files back to this directory on the
modern host and run `make goldens` in `..` (it packages every category's,
this directory's included).
