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
- **Round 2** (goldens 2026-09-30, commit `4c668c2`; their `.i` files
  2026-10-02, commit `34c9195`): four files for what `mutos_c1` refused or
  only inferred, all **4/4 byte-exact** end-to-end. See
  `../../../docs/DEVLOG.md`'s "The round-2 fltprobe goldens".
- **Round 3** (goldens 2026-10-02, commit `33ec944`): three files for what
  `mutos_c1` compiled by inference, one for what it refused - now all **4/4
  byte-exact** end-to-end. `p11` and `p12` matched at once; `p10` corrected
  two inferences, `p13` settled every refusal. See
  `../../../docs/DEVLOG.md`'s "The round-3 fltprobe goldens".
- **Round 4** (goldens 2026-10-02, commit `27d0cea`): two files for what
  `mutos_c1` compiled by inference, one for what it refused - now all
  **3/3 byte-exact** end-to-end. `p14_axint` corrected two inferences (a
  right element at offset 0 is computed first), `p15_fltinf` one (the
  register context in a chain), `p16_open2` settled every refusal. See
  `../../../docs/DEVLOG.md`'s "The round-4 fltprobe goldens".
- **Round 5** (goldens 2026-10-02, commit `434c320`): two files for what
  `mutos_c1` compiled by inference only, one for what was still refused -
  `p17_elem2` and `p18_fltop3` now **byte-exact** end-to-end (`p18` at
  once; `p17` corrected two inferences: '^' pushes the right element's
  address too, and a comparison of two elements pushes the LEFT one's),
  `p19_open3` byte-exact at every stage but the last, and there up to the
  real compiler's own invalid output: its `d = (d * e) + u` (an unsigned
  converted after a computed `*`) has 118 bytes of libc's `_ctype_` table
  where a register name should be, which `mutos_c1` refuses on purpose -
  see `../invalid_goldens.txt` and `../../../docs/DEVLOG.md`'s "The
  round-5 fltprobe goldens".
- **Round 6** (goldens 2026-10-03, commit `2540271`): floating lvalues
  beyond variables (`p20_fltlv` - struct members, pointers to double,
  elements as compound-assignment targets, `&d`), floating expression
  forms (`p21_fltexp` - chained assignments, `++`/`--`, char operands,
  casts of computed ints, `?:`, comma), and the `long` (`p22_long2`) and
  element (`p23_elem3`) shapes round 5 left open - all four refused
  before, now all **4/4 byte-exact** end-to-end. `p21_fltexp` came back
  with two compiler messages: the real compiler's code for `half(d =
  3.0)` pops the value it passes twice, and its `c1`, which counts the
  floating-point stack at compile time, said so - "56: floating point
  stack underflow" / "57: Floating point stack underflow", exit status 1,
  the `.s` complete all the same (`cc -S` keeps it). `mutos_c1` writes the
  same code and the same two messages and exits with 1 too - see
  `../c1_errors.txt` and `../../../docs/DEVLOG.md`'s "The round-6
  fltprobe goldens".
- **Round 7** (goldens 2026-10-04, commit `bdb2728`, with `round7.log`):
  what `mutos_c1` compiled by inference only or still refused after round
  6 - floating (`p24_fltinf2`), `long` (`p25_long3`) and int element
  (`p26_elem4`) shapes, the frames from 82 to 127 bytes (`p27_frame`) and,
  on purpose, more of the real compiler's floating-stack messages
  (`p28_fltstk`) - all five refused before, now all **5/5 byte-exact**
  end-to-end, `p28` together with all twelve of the real compiler's
  messages (`../c1_errors.txt`). The goldens corrected three inferences
  (an element tested for truth is loaded and `or`ed; `e = ++d` stores
  with a pop and reloads `d`; the stack model is not reset between
  functions) and settled the rest: `sub sp,N` up to 90 bytes, `call
  chkstk` from 100; a sum of calls pushes each right call's value; an
  assignment under a call is stored without a pop in an int statement
  (`x = half(d = 3.0)` - right code); v7's `distrib()`; compound
  assignments into elements and through pointers; every `long` shape
  asked. See `../../../docs/DEVLOG.md`'s "The round-7 fltprobe goldens".
- **Round 8** (`p29`..`p33`, goldens pending): the frames of 92 to 98
  bytes (`p29_frame2`), which store an assignment under a call gets - the
  statement's type, or what the previous floating statement left
  (`p30_fltstk2`, on purpose not a program to run, like `p28`) - and what
  `mutos_c1` now compiles by inference only or still refuses: `long`
  (`p31_long4`), int (`p32_elem5`) and floating (`p33_fltinf3`) shapes.
  Run them with `make -f Makefile.mutos round8`.

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
| `p10_elem.c` | elements subscripted by a variable as a second operand (`1.5 + arr[i]` loads the element first - degree 2), two in one expression, compared, a float array's; arrays of 8- and 16-byte structs by a variable, `ps[i].c * qs[i].g` (the left loaded first, not spilled) | byte-exact | 21 |
| `p11_itof2.c` | ints computed in DI (a shift, a difference, `0 - i`) or AX (a quotient, a product by a constant after a `/`) converted; `i += f()` / `i -= f()` | byte-exact | 109 |
| `p12_init2.c` | an int constant into a float, a negated int, a negated inexact constant into a float, a `static float`, `0.3` into a float (truncated) | byte-exact | 7 (6 on MUTOS) |
| `p13_open.c` | an int converted after a conversion and after an element (DI), a difference and a shift for AX, `x + 0` for AX (the `+ 0` dropped), `j /= e` (`mov cx,ax`), a floating zero added and subtracted (kept) | byte-exact | 41 (42 on MUTOS) |
| `p14_axint.c` | no floating point: a value in AX plus a constant stored (`x / y + 3`, `f() + 1`, `x * y - 1`), an int `+ 0` / `- 0` in DI, `a[i] * b[j]` (spilled: `push di` / `pop cx` / `imul cx`) and `a[i] & b[j]` (the address pushed: `pop bx` / `and di,(bx)`) of int arrays, `x /= f()`, `y %= f()` | byte-exact | 129 |
| `p15_fltinf.c` | `(d * d) + arr[i] + i` (the element first by its degree, then i in AX - after d * d), a shift by 3 and a right shift converted in AX, a call's result then an element | byte-exact | 45 |
| `p16_open2.c` | an int converted after `*p` (DI) and after an assignment (AX, its `*`), a remainder converted (`mov ax,dx`), `-0.0` (written as 0.0), `(int) 2.5` and `i = 2.5` (`flds` / `ftoi`), `d += c` with a char (the char first, `faddd d`), an `unsigned` converted (`sub di,di` / `ltof`) | byte-exact | 79 |
| `p17_elem2.c` | no floating point: `return a[i] * b[j]` and `s = ps[i].c * qs[i].d` (the offset decides, not the context), `+`, `-`, `\|`, `^` of two elements (the right one's address pushed - `^` too), a `+` chain, two elements compared (the LEFT one's address pushed: `pop bx` / `cmp (bx),di`) | byte-exact | 84 |
| `p18_fltop3.c` | `d += e * 2` (computed first), `f += i` into a float, `(d * e) + (i % j)` (the remainder first, `mov ax,dx`), `(long) 3.75`, a negated zero as an initializer | byte-exact | 27 |
| `p19_open3.c` | `long` with int-class variables (`l + i` and `i + l` - the int widened first, `l > i` and `l == i` - swapped, in DX:AX, `l += i`, `l = u` - `sub di,di`), `&` / `\|` of longs (an int constant widened at run time), `u = 40000`, an unsigned compared with a long constant; `a[i] * b[j - 2]` (`imul *-4.(si)`), `x - b[j]` (pushed); `d -= c`, `d -= e * 2` (the target loaded first, `fsub`); an unsigned converted after a `*` - the real compiler's output invalid there | byte-exact to line 223, then refused on purpose | 111 |
| `p20_fltlv.c` | a double struct member (target, operand, through a pointer, `+=` - `lea ax,(di)` / `\|` / `push ax` / ... / `pop ax` / `fstdp`), `+=` / `*=` into a double element by a variable, a pointer to double subscripted, with an offset and incremented (`add p,*8.`), `&d` passed, a function returning `double *` (`mov bx,ax` / `lea ax,(bx)`) | byte-exact | 128 |
| `p21_fltexp.c` | `d = e = 2.5` (`fstd e` / `fstdp d`), `d++`, `++d`, `e = d--` (`fdup`), `d + c`, `c * 2.5`, `(double) (i + j)`, `(double) (i * j) + 0.5`, `d = -i` (`neg di`), `e = l + 1` (the sum, then `ltof`), `(unsigned) d` (`ftol`, the low word), `x ? d : e`, `(i = 2, d - 40000.0)`, `half(d = 3.0)` (the value popped twice - the real `c1`'s two messages) | byte-exact, both messages reproduced | 60 (the real program: a stack underflow) |
| `p22_long2.c` | no floating point: `l - i`, `i - l`, `l + 1`, `l - 2`, `l ^ 7`, `l -= i`, `l *= i` (`almul`), `l / i`, `l % i`, `x = 40000`, `l < 0L`, `l >= 0L`, `x = l > 0L`, `i < l`, `l > 2`, `l > 0` (no `tst`), `if (l)`, `!l`, `u < l`, `l > m`, `l == m`, `-100000`, `~l`, `l << 2` (`sal si,*1` / `rcl di,*1`), `l >> 1`, `x ? l : m`, `x = (int) (l - 5)`, `(long) u`, `l & m` | byte-exact | 83 |
| `p23_elem3.c` | no floating point: `ps[i].c > b[j]` (`cmp *4.(bx),di`), `a[i] > ps[i].c`, `x * b[j]` (the element in AX, `imul x`), `x - *p`, `5 - b[j]`, `g - b[j]` (a file-scope `g`), `b[j + 1]` of a local array, `a[i] + b[j + 1]`, two elements compared as values (the right term first, pushed) and under `&&` (`or di,di`) | byte-exact | 136 |
| `p24_fltinf2.c` | inferred: `e = ++d`, `e = --d`, `x ? 1.5 : 2.5`, `(i > j) ? e : d`, `-i * e`, `q->y -= e`, `q->x /= e`, `x = q->y * 4.0`, `q->x > e`, `a[i] < q->y`, `a[i] + q->x * s.y`, `d + i * j`, `d * (i + 1)`; refused: `(x ? d : e) * 2.0`, `q->x += d * e`, `a[i + 1]` of doubles, `d = q->k`, `*pick(a, 1) = 2.5`, `f++` on a float | byte-exact | 174 |
| `p25_long3.c` | no floating point; inferred: `l \| m`, `l ^ m`, `x = (int) (l + 5)`, `l += 1`, `l = c`, `l / 7`, `l + i * j`; refused: `l << 3`, `l >> 4`, `10 - (int) (l - 4995)`, `-l`, `l && i`, `l \|\| j`, `l ? 2 : 50`, `l++`, `++l`, `l--`, `m = l = 5`, `l * 3` | byte-exact | 153 |
| `p26_elem4.c` | no floating point; inferred: `x * *ip`, `*ip * j`, `if (b[i])`, `b[j] != 0`, `x = b[j] > 0`, `b[j] > 5` under `&&`, `if (ps[i].c)`, `ps[i].c > 2`, `b[i] * b[j] + b[j - 1]`, `g * b[j]`, `b[j] - g`, `(b[i] + 1) * b[j]`, `b[3] = b[j] + b[0]`, `b[j] / b[i]`, `b[j] % b[i]`; refused: three comparisons of two elements summed, `b[i] += b[j]`, `b[j] -= x`, `b[0] *= b[i]`, `*ip += 2`, `ps[i].c += x` | byte-exact | 238 |
| `p27_frame.c` | no floating point: eight functions with frames of 82, 90, 100, 110, 120, 124, 126 and 127 bytes (`char buf[N]`): `sub sp,*82.`/`*90.`, from 100 `mov ax,*N.` / `call chkstk` (127 rounded: `#128.`); `main()`'s sum of eight calls (each right call pushed: `push ax` / ... / `pop bx` / `add ax,bx`) | byte-exact | 24 |
| `p28_fltstk.c` | NOT a program to run: `half(d = 3.0)` (p21's double pop) on purpose, to see the real `c1`'s floating-stack model go on: never reset (not between functions), the argument push, an unused result and a returned double's store into `fac` unchecked, `fmul`/`fadd`/`fcmp` below the bottom upper case; `x = half(d = 3.0)` stored with `fstd` (right code) | byte-exact, all twelve messages reproduced | - (the real program: a stack underflow) |
| `p29_frame2.c` | no floating point: frames of 92, 94, 96 and 98 bytes - `sub sp,N` or `call chkstk`? | refused (the frame size) | 50 |
| `p30_fltstk2.c` | NOT a program to run: an assignment passed as a floating argument after a floating statement in an int statement (`fstd` or `fstdp`?), after an int use of a double, under a floating sum, a comparison as a condition and as a value, `return half(d = 3.0)` in a double function, two such arguments | inferred (by the statement's type) | - (compiler messages expected) |
| `p31_long4.c` | no floating point; inferred: `l -= 1`, `--l`, `l += 300`, `m = -l`, `l && m`, `l << 5`, `l >> 7`, `l * 7`, `m * i`, `l ? x : 7`; refused: `x = l++`, `l += 70000`, `l << i`, `x = !l`, `10 - (int) (l - 35)` | refused | 140 |
| `p32_elem5.c` | no floating point; inferred: `if (!b[i])`, `x = !b[j]`, `b[j] ? 3 : 9`, `if (a[i][j])`, `(b[i] - x) * b[j]`, `~b[i] * b[j]`, `(b[j] + 3) / b[i]`, `(b[3] & 7) % b[4]`, `b[i] += 7`, `ps[i].d ^= x`, `p->b += x`, `a[i][j] += x`, `(x > 2) * 2 + (x < 9) * 4 + (x == 5) + x`, `... * 2 + ... * 2 + x`, `x = f(1) + g(2)`, `x = f(1) + g(2) + h(3)`; refused: `*ip += x`, `b[3] ^= b[i]`, `x = x + f(1) + g(2)`, `b[j] / b[i] + b[j] % b[i]` | refused | 284 |
| `p33_fltinf3.c` | inferred: `++f`, `--f`, `f--`, `(x ? d : e) + 1.5`, `((i > j) ? d : e) * f`, `q->y /= 2.0`, `q->x -= d * e`, `q->x /= d + e`, `a[i + 1] += 1.0`, `a[i + 2] = a[j] * 2.0`, `d = q->k * 2.0`, `*pick(a, i) = d + e`; refused: `e = f++`, `d = ++f`, `e = (q->x += d)`, `e = ++d * 2.0` | refused | 82 |

"Result" is what `main()` returns under C semantics (the host C compiler's
value). The real compiler's `i *= e` is `i * (int)e`, not `(int)(i * e)` - v7's
`build()` converts the right-hand side of a compound assignment to the
target's type first (`p2_arith.1.golden`: `FTOI`, then `ASTIMES(INT)`) - so
`p2_arith`'s golden returns 16 - and `p13_open`'s `j /= e` gives 42 on
MUTOS for the same reason (its golden: `FTOI`, `ASDIV(INT)`); `p12_init2`'s
golden returns 6, its floats truncated. Every golden of rounds 1 to 7 runs
under `../fuzz/x86sim.py` and returns these values - `p19_open3`'s up to
its invalid statement (the first 223 lines, completed with `r = r + (int)
d`, return 104), and `p21_fltexp`'s with its doubly popped store (line
250, `fstdp`) made the `fstd` it should be (as written, `x86sim.py` stops
there: "'fstdp' with an empty floating-point stack"); `p28_fltstk` is not
meant to run; `mutos_c1`'s output is byte-identical to them. "inferred" means `mutos_c1` compiles the file -
its output runs under `x86sim.py` and returns the value above - with no
golden behind the shapes yet; "refused" that `mutos_c0` or `mutos_c1`
stops with a diagnostic. In round 8, the shapes each file lists as
inferred compile today and give the host's value when the refused ones
are taken out.

## Generating the goldens

On MUTOS 1700, from inside this directory:

```
make -f Makefile.mutos round8 2>&1 | tee round8.log
```

(or plain `make -f Makefile.mutos` for all eight rounds), then bring the
`.s`, `.i`, `.1` and `.2` files back to this directory on the modern host -
all four kinds - and run `make goldens` in `..` (it packages every
category's, this directory's included). `p30_fltstk2` is meant to make
`cc -S` print messages and exit with status 1 (`make` goes on: its recipe
starts with `-`), as `p28_fltstk` did; its `.s` is written all the same -
please bring back the messages (`round8.log`) with the files. Should
another round-8 file not compile on MUTOS, make the others by name (`make
-f Makefile.mutos p31_long4.s p31_long4.1`) and bring back the failing
one's messages instead.

One more question for the real toolchain, if convenient: `as p19_open3.s`
- what the real assembler makes of the real compiler's invalid lines 229
and 231 (`mutos_as` stops with "could not classify operands"). An error
there settles that no MUTOS 1700 binary could ever have contained this
code.
