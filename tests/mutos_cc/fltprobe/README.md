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
  round-5 fltprobe goldens". The real `as` refuses those lines as well
  (`***ERROR*** syntax error`, `p19_as.log`, see "The real assembler on
  `p19_open3.s`" below).
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
- **Round 8** (goldens 2026-10-07, commit `2eaa65f`, with `round8.log`):
  the frames of 92 to 98 bytes (`p29_frame2`), which store an assignment
  under a call gets (`p30_fltstk2`, written to make the real `c1` report
  again - it reported nothing) and what `mutos_c1` compiled by inference
  only or still refused after round 7: `long` (`p31_long4`), int
  (`p32_elem5`) and floating (`p33_fltinf3`) shapes - all five refused
  before, now all **5/5 byte-exact** end-to-end. The goldens settled the
  frames (`sub sp,N` up to 98 bytes - no size is left open), corrected
  three inferences (the store under a call depends on the CALL's own
  consumer, not the statement's type: `fstd` in all of `p30`, whose code
  is right and runs; `m * i` pushes the long first; a comparison of a
  variable with a constant in a `distrib()` sum is computed into SI, not
  pushed) and settled every refusal: `x = l++` (the low word as an int),
  `l += 70000`, `l << i` (`jz .+8`), `x = !l`, `10 - (int) (l - 35)`, `*ip
  += x` (the pointer pushed), `b[3] ^= b[i]`, `x + f(1) + g(2)` (the calls
  first), a quotient plus a remainder (the remainder pushed from DX), a
  float's `f++`/`++f` as values, `e = (q->x += d)`, `e = ++d * 2.0` (the
  increment first). See `../../../docs/DEVLOG.md`'s "The round-8 fltprobe
  goldens".
- **Round 9** (goldens 2026-10-07 and 2026-10-08, commits `ee6c08e` and
  `f03faae`, with `round9.log`): what `mutos_c1` compiled by inference
  only or still refused after round 8 - `long` (`p34_long5`), int
  (`p35_elem6`) and floating (`p36_fltinf4`) shapes - and, on purpose not
  a program to run, which store an assignment passed as a floating
  argument gets where `p30` did not ask (`p37_fltstk3`) - now all **4/4
  byte-exact** end-to-end, `p37` together with all seventeen of the real
  compiler's messages (`../c1_errors.txt`). The goldens corrected seven
  inferences: `x = ++l` increments the whole long in place first (`add` /
  `adc`, then the low word read - v7's `sreorder()`, not the postfix
  form's distributed `LTOI`), `l += -5` loads the constant's two words
  (`mov si,*-5.` / `mov di,*-1.`, no `cwd`), `l = -i` is negated in AX,
  `c / d + f(2)` computes the quotient first and pushes it (`acommute()`
  puts the call on the left), `(y > 2) + (x < 3)` computes the left
  comparison into DI and the right one into SI (`%n,e`), `e = (a[i] +=
  d)` loads `d` before the element's address, `e = (d += 1.0) * 2.0`
  compiles `d += 1.0` first as a statement; and the argument store of
  `p37`: an int function's unused call and a call nested as an argument
  of such a call pop too, and a constant argument is pushed with a
  checked pop (the upper-case message). They settled every refusal: `l
  <<= 3` / `l >>= 2` (the count into CX and the pair looped - 2 too - the
  long stored back high word first), `l /= m` (`aldiv`), `(int) (l * 2)`
  (the low word stored from AX), `l - m - 1` (`add si,*-1.` / `adc
  di,*-1.`), `100000 - l`; `c % d + c / d` (`add dx,bx`), `c * d + f(1)`
  (the call first, `mov di,ax`), `f(1) - g(2)`, `f(1) * g(2)` (`mov
  ax,ax` / `pop cx` / `imul cx`), `b[i] + f(1)` (`add ax,(bx)`), `f(1) +
  g(2) * 3` (the product left); `e = (q->y -= d)` and `/=` (the target
  pushed and loaded first, `fstd`), `d++ * 2.0` (the `++` first, degree
  2), `++d > 2.0` (hoisted, the constant loaded first). See
  `../../../docs/DEVLOG.md`'s "The round-9 fltprobe goldens".
- **Round 10** (`p38`..`p41`, goldens 2026-10-09, commit `074b6de`, with
  `round10.log`): what `mutos_c1` compiled by inference only or still
  refused after round 9 - `long` (`p38_long6`), int (`p39_elem7`) and
  floating (`p40_fltinf5`) shapes - and, on purpose not a program to run,
  the real `c1`'s
  floating-stack model where `p37` left it open (`p41_fltstk4`) - now all
  **4/4 byte-exact** end-to-end, `p41` together with all twenty-three of
  the real compiler's messages. The goldens corrected seven inferences: `l
  <<= 1` shifts the variable in place and then loads it (`sal *-6.(bp),*1`
  / `rcl *-8.(bp),*1` / `mov di,*-8.(bp)` / `mov si,*-6.(bp)`); `l <<= i`
  skips with `jz .+16`, not `.+8` (the template's own - with a count of 0
  it jumps two bytes into a local's store); `l += 5L` is in place (`add` /
  `adc ...,*0`: a long constant that fits an int is an int widened to
  v7's `c1`); `f(2) * f(3) * 2` shifts the product in AX (`sal ax,*1`);
  `e = (d += 2.0) + (e -= 1.0)` hoists `e -= 1.0` FIRST (v7's `sreorder()`
  of a `+` takes its right operand first); `x = ++f > 2.0` loads the
  `.float` 2.0 first (the hoisted f is a NAME); `e = (q->x += d * e)`
  computes the right operand first and addresses `q` through AX into BX
  after the `*`; and the push of a SUM as a floating argument is not
  checked (`p41`'s line 55), where a variable's is. They settled every
  refusal: `l <<= 0` is no code, `5 - l` goes through `cwd`, `(l - m) + 3`
  and `l * m * 2` push the constant first (`mov ax,*3.` / `cwd` / `push
  ax` / `push dx` / ... / `pop bx` / `pop cx` / `add si,cx` / `adc di,bx`;
  the first `lmul`'s DX:AX pushed as the second's left argument); `x -
  f(1)` and `f(1) + g(2) - f(3)` push the right call, `f(1) - x` is `sub
  ax,x`; `f(1) & b[i]` pushes the element's address (`and ax,(bx)`), `f(1)
  | g(2)` the right call (`or ax,bx`); `c * d + c * y` and `f(1) + c * 3`
  move the left value into DI; `(y > 2) - (x < 3)` is `sub di,si`, `(y >
  2) + (x < y)` compares through SI (`mov si,y` / `cmp x,si`). See
  `../../../docs/DEVLOG.md`'s "The round-10 fltprobe goldens".
- **Round 11** (`p42`..`p45`, goldens 2026-10-10, commit `12731f2`, with
  `round11.log`): what `mutos_c1` compiled by inference only or still
  refused after round 10 - `long` (`p42_long7`), int (`p43_elem8`) and
  floating (`p44_fltinf6`) shapes - and, on purpose not a program to run,
  the real `c1`'s floating-stack model where `p41` left it open
  (`p45_fltstk5`) - now all **4/4 byte-exact** end-to-end, `p45` together
  with all twenty-three of the real compiler's messages. The goldens
  corrected three inferences of some seventy: `l = -300` loads the
  constant's two words (`mov si,#-300.` / `mov di,*-1.` - v7's `optim()`
  makes an ITOL of a negative CON an LCON; `l = 300` keeps `cwd`), `c % d
  - x` subtracts in DX (`sub dx,x`), and the push of an element
  subscripted by a variable or of a member through a pointer is not
  checked (`half(a[i])`, `half(q->y)` - a `*` node, not a NAME), where a
  float variable's is; `p44`'s fifteen inferred shapes all matched. They
  settled every refusal: `(int) ((l - m) + 3)` in one word (`mov di,l` /
  `sub di,m` / `add di,*3.` - `unoptim()` distributes the LTOI), `(l + m)
  * 2` with the 2 pushed first and the sum pushed from DI:SI (`push si` /
  `push di`), `(int) (l * m * 2)` pushed as `l * m * 2` is (`mov x,ax`);
  `y % (c / d - s)` with the divisor computed in AX and pushed (`sub
  ax,s` / `push ax` / `mov di,y` / `mov ax,di` / `cwd` / `pop cx` / `idiv
  cx`), `f(1) * 2 + c / d` and `f(1) * 3 - f(2)` with the right one pushed
  first, `c * d + f(1) * 3` with the call's product moved into DI; `e = (d
  += ++w + ++y) * 2.0` hoisting `++w`, then `++y`, inside the hoisted `+=`
  (its template's `reorder()` of the `+` as a node, left to right). See
  `../../../docs/DEVLOG.md`'s "The round-11 fltprobe goldens".
- **Round 12** (`p46`..`p49`, goldens pending): what `mutos_c1` now
  compiles by inference only or still refuses after round 11 - `long`
  (`p46_long8`), int (`p47_elem9`) and floating (`p48_fltinf7`) shapes -
  and, on purpose not a program to run, the floating-stack model's
  argument checks for the remaining kinds of operand (`p49_fltstk6`: a
  file-scope double, a constant-indexed element of a file-scope array, a
  local struct's member, a local static, a value through a pointer
  variable, a float element). Run them with `make -f Makefile.mutos
  round12`. Planned as the last round of this series - see
  `../../../STATUS.md`'s "Next up".

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
| `p29_frame2.c` | no floating point: frames of 92, 94, 96 and 98 bytes - all `sub sp,*N.` | byte-exact | 50 |
| `p30_fltstk2.c` | written NOT to run: an assignment passed as a floating argument after a floating statement in an int statement, after an int use of a double, under a floating sum, a comparison as a condition and as a value, `return half(d = 3.0)` in a double function, two such arguments - `fstd` everywhere (the call's value used: right code, no message) | byte-exact, no message (as the real `c1`) | 9 |
| `p31_long4.c` | no floating point: `l -= 1`, `--l`, `l += 300`, `m = -l`, `l && m`, `l << 5`, `l >> 7`, `l * 7`, `m * i` (the long pushed first), `l ? x : 7`, `x = l++` (`mov di,l+2` / `inc l+2` - the low word as an int), `l += 70000` (through DI:SI), `l << i` (`mov cx,i` / `or cx,cx` / `jz .+8` / ... / `loop .-4`), `x = !l`, `10 - (int) (l - 35)` (`mov di,*10.` first) | byte-exact | 140 |
| `p32_elem5.c` | no floating point: `if (!b[i])`, `x = !b[j]`, `b[j] ? 3 : 9`, `if (a[i][j])`, `(b[i] - x) * b[j]`, `~b[i] * b[j]`, `(b[j] + 3) / b[i]`, `(b[3] & 7) % b[4]`, `b[i] += 7`, `ps[i].d ^= x`, `p->b += x`, `a[i][j] += x`, `(x > 2) * 2 + (x < 9) * 4 + (x == 5) + x` (each comparison into SI after the left term), `... * 2 + ... * 2 + x`, `x = f(1) + g(2)`, `... + h(3)`, `*ip += x` (`push ip` / ... / `pop bx` / `add (bx),di`), `b[3] ^= b[i]` (`xor *-50.(bp),di`), `x = x + f(1) + g(2)` (the calls first, `add ax,x`), `b[j] / b[i] + b[j] % b[i]` (the remainder first, `push dx`) | byte-exact | 284 |
| `p33_fltinf3.c` | `++f`, `--f`, `f--`, `(x ? d : e) + 1.5`, `((i > j) ? d : e) * f`, `q->y /= 2.0`, `q->x -= d * e`, `q->x /= d + e`, `a[i + 1] += 1.0`, `a[i + 2] = a[j] * 2.0`, `d = q->k * 2.0`, `*pick(a, i) = d + e`, `e = f++` (`fdup`), `d = ++f` (`fstsp f` / `flds f`), `e = (q->x += d)` (`fldd d` first, `fstd` through the popped address), `e = ++d * 2.0` (the increment first, then `flds 2.0` / `fmuld d`) | byte-exact | 82 |
| `p34_long5.c` | no floating point: `x = ++l` (`add *-6.(bp),*1.` / `adc *-8.(bp),*0`, then the low word), `x = --l`, `x = l--`, `l += -5` (`mov si,*-5.` / `mov di,*-1.`), `l -= -3`, `l = l >> i`, `m = i * m`, `l *= 3`, `l = -i` (`mov ax,i` / `neg ax` / `cwd`), `l = l + m * 2`, `l = l * m`, `l <<= 3` and `l >>= 2` (`mov cx,*N.` / ... / `loop .-4`, stored high word first), `l /= m` (`aldiv`), `x = (int) (l * 2)` (`mov x,ax`), `l = l - m - 1`, `l = 100000 - l` | byte-exact | 248 |
| `p35_elem6.c` | no floating point: `*ip -= y`, `*ip \|= b[i]`, `*ip &= x + 1`, `*ip += f(1)`, `b[i] += f(1)`, `x += y * 2`, `x -= b[i]`, `x &= b[i] + 1`, `x = x + f(1) + y`, `x = f(1) + g(2) + y + f(3)`, `x = f(1) + c / d`, `x = c / d + f(2)` (the quotient first, pushed), `x = f(1) + c % d`, `x = c / d + c / y`, `x = (y > 2) + (y < 9) * 2 + y` (the left comparison into DI, the right one into SI), `x = (y > 2) + (x < 3)`, `x = c % d + c / d` (`add dx,bx`), `x = c * d + f(1)` (`mov di,ax`), `x = f(1) - g(2)`, `x = f(1) * g(2)`, `x = b[i] + f(1)` (`add ax,(bx)`), `x = f(1) + g(2) * 3` | byte-exact | 856 |
| `p36_fltinf4.c` | `e = (q->x *= 2.0)`, `e = (q->x *= d + e)`, `e = (a[i] += d)` (`fldd d` first), `e = (a[i] *= 2.0)`, `e = f--`, `d = --f`, `e = ++d + 1.5`, `e = 10.0 - --d`, `e = ++d / 4.0`, `e = ++d * ++e`, `e = ++f * 2.0`, `e = (d += 1.0) * 2.0` (`d += 1.0` first, `fstdp d`), `e = -(++d)`, `e = d * (e = 2.0)`, `e = (d = 2.0) * 3.0`, `e = (q->y -= d)` and `e = (q->y /= d)` (`push ax` / `fldd` / `fldd d` / `fsub` / `pop ax` / `fstd`), `e = d++ * 2.0` (`fdup`, then `fmuls 2.0`), `x = ++d > 2.0` and `if (++d > 2.0)` (`flds 2.0` / `fldd d` / `fcmp`) | byte-exact | 133 |
| `p37_fltstk3.c` | NOT a program to run: an assignment passed as a floating argument of a call nested as an argument (`fstdp`), of two calls in a sum (`fstd`), of an int function (its value stored: `fstd`; unused: `fstdp`), two of them as arguments of an unused call (`fstdp`), one of two with the value stored (`fstdp`, the constant's push checked), the call's value returned from an int function (`fstd`) and stored into a float (`fstdp`) | byte-exact, all seventeen messages reproduced | - (the real program: a stack underflow) |
| `p38_long6.c` | no floating point: `l <<= 1` (`sal *-6.(bp),*1` / `rcl *-8.(bp),*1` / `mov di,*-8.(bp)` / `mov si,*-6.(bp)`), `l <<= i` and `l >>= i` (`jz .+16`), `l += 5L` (`add` / `adc ...,*0`), `l -= 70000`, `l /= i`, `l %= m`, `l %= 7`, `l *= m`, `x = (int) (l / m)`, `x = (int) (l % 7)`, `l = (l + m) - 3`, `l = (l - m) + 70000`, `l = l * m - 1`, `l = l / m + l`, `l = 5L - l` and `l = 5 - l` (`mov ax,*5.` / `cwd`), `l = -5L - l`, `l = m + -i`, `l = m - -i`, `l = (l - m) + 3` (the 3 pushed first), `l = l * m * 2` (the 2 pushed, then the first product's DX:AX), `l <<= 0` (no code) | byte-exact | 525 |
| `p39_elem7.c` | no floating point: `x = c % d + c % y`, `x = f(1) - c / d`, `x = c / d - f(1)`, `x = c % d - c / d`, `x = f(1) - c % d`, `x = f(1) + c * d`, `x = f(1) + b[i]`, `x = (y > 2) + (x <= 3) * 4`, `x = (y > 2) + (5 < x)`, `x = (y < 2) + (y > 1) * 2 + (x == 3)`, `x = g(2) * 3 + f(1)`, `x = c % d + f(1)`, `x = f(2) * f(3) * 2` (`sal ax,*1`), `x = x - f(1)` (the call pushed), `x = f(1) - x` (`sub ax,x`), `x = c * f(1) + d`, `x = (y > 2) + (x < y)` (`mov si,y` / `cmp x,si`), `x = f(1) & b[i]` (`and ax,(bx)`), `x = c * d + c * y`, `x = c * d - f(1)`, `x = f(1) + c * 3`, `x = (y > 2) - (x < 3)` (`sub di,si`), `x = f(1) + g(2) - f(3)`, `x = f(1) \| g(2)` (`or ax,bx`) | byte-exact | 1141 |
| `p40_fltinf5.c` | `e = (d -= 0.5) * 2.0`, `e = 3.0 - (d += 1.0)`, `e = (d += e * 2.0) * 3.0`, `e = (d += i) * 2.0`, `e = (s.x += 1.0) * 2.0`, `e = (f -= 0.5) * 2.0`, `x = (d += 1.0) > 2.0`, `if ((d -= 1.0) < 1.0)`, `x = 1.0 < (d += 1.0)`, `if (--d < 1.0)`, `x = ++f > 2.0` (`flds 2.0` first), `e = (d += 2.0) + (e -= 1.0)` (`e -= 1.0` first), `e = (d *= 2.0) * 3.0` and `e = (d /= 2.0) + 1.0` (not hoisted), `e = (q->y -= 0.5)`, `e = (q->y /= 2.0)`, `e = (q->x += d * e)` (`fmuld` / `mov ax,q` / `mov bx,ax` / `lea ax,(bx)` / ... / `faddd`), `e = (q->y -= d * e)`, `e = (q->x /= d + e)`, `e = (q->x *= d * e)` (through AX into BX), `e = (a[i] += 1.5)`, `e = (a[i] -= 0.5)`, `e = (a[i] /= 2.0)`, `e = d-- * 2.0`, `e = f++ * 2.0`, `e = d++ + e--` | byte-exact | 143 |
| `p41_fltstk4.c` | NOT a program to run: the floating-stack model one below the bottom from the start (`half(d = 3.0);`), then a variable (checked), a sum (not checked) and an int converted pushed as arguments, a call nested in an int conversion (`fstd`), a call nested in an unused call and two of them (`fstdp`), a call's value multiplied, negated and compared (`fstd`), a constant argument of an unused call (checked) | byte-exact, all twenty-three messages reproduced | - (the real program: a stack underflow) |
| `p42_long7.c` | no floating point: `l >>= 1` (`sar *-8.(bp),*1` / `rcr *-6.(bp),*1` / `mov di,*-8.(bp)` / `mov si,*-6.(bp)`), `l >>= 0` (no code), `l = -300` (`mov si,#-300.` / `mov di,*-1.` - an LCON to v7's `optim()`), `l >>= i`, `l <<= i`, `l += 5L`, `l -= 5L`, `l += 300L`, `l -= 300`, `5L - l`, `5 - l`, `300 - l`, `-5L - l`, `(l - m) + 3`, `(l + m) + 300`, `l * m + 3`, `l / m + 7`, `l * m * 2`, `l * m * 3`, `l / m * 2`, `(l * m) / 3`, `(l * m) % 5`, `l * m + l`, `x = (int) ((l - m) + 3)` (`mov di,*-6.(bp)` / `sub di,*-10.(bp)` / `add di,*3.`), `l = (l + m) * 2` (`mov ax,*2.` / `cwd` / `push ax` / `push dx` / [the sum] / `push si` / `push di` / `call lmul`), `x = (int) (l * m * 2)` (`mov *-16.(bp),ax`) | byte-exact | 9554 |
| `p43_elem8.c` | no floating point: a call, quotient, remainder or product shifted in AX/DX (by CL too), `5 - f(1)`, `x - c / d`, `x - c % d`, `c * 3 - f(1)`, `b[i] - f(1)`, `f(1) - b[i]`, `c * d - x`, `c / d - x`, `c % d - x` (`sub dx,*-16.(bp)` / `mov *-16.(bp),dx`), differences of comparisons, `\|`/`^`/`&` of a call and an element or two calls, products by constants summed, `y % (c / d - s)` (`sub ax,*-24.(bp)` / `push ax` / `mov di,*-18.(bp)` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx`), `f(1) * 2 + c / d` (the quotient pushed first, `sal ax,*1` / `pop bx` / `add ax,bx`), `f(1) * 3 - f(2)` (`f(2)` pushed first), `c * d + f(1) * 3` (the call's product moved into DI) | byte-exact | 1100 |
| `p44_fltinf6.c` | three hoists in a `+` chain, a hoist next to a variable, a product of two hoists times a constant, a hoisted sum under a `-`, `d -= (e += 1.0) + 2.0`, a returned sum of two hoists, two hoisted floats compared, `q->y += d * e` and other pointer targets after a `*`, `e = (a[i] += d * e)`, a hoist inside a hoisted `+=`, `e = (d += ++w + ++y) * 2.0` (`++w`, then `++y`, then `fldd w` / `faddd y` / `faddd d` / `fstdp d` - the hoisted `+=`'s right operand reordered as a node) | byte-exact | 120 |
| `p45_fltstk5.c` | NOT a program to run: the floating-stack model one below the bottom from the start, then a float variable pushed (checked, as a double), an element subscripted by a variable and a member through a pointer (NOT checked - a `*` node, not a NAME), a negation, a conversion, a product and a call's value multiplied (not checked), a variable and a sum as two arguments | byte-exact, all twenty-three messages reproduced | - (the real program: a stack underflow) |
| `p46_long8.c` | no floating point; inferred: `l = -5L`, `l = -1` (two words), `lsub(-300L, 7L)` (`mov di,#-300.` / `push di` / `mov di,*-1.` / `push di`), `return -5L;`, `(int) (l - m)`, `(int) (l + m) + 3`, `r + (int) (l - m)`, `(int) ((l + m) - i)`, `(int) (i + (l - m))`, `(int) (l + m + l)`, `(int) (l - (m - l))`, `(l - m) * 3`, `(l + m) / 3`, `(l - m) % 7`, `(int) (l / m * 2)`, `(int) ((l * m) % 9)`, `(l + i) * 3`, `(int) ((l + m) * 3)`; refused: `x = (int) (l & m)`, `x = (int) (l \| m) + 1`, `l = (l + m) * m`, `l = (l << 2) + 3` | refused | 1682 |
| `p47_elem9.c` | no floating point; inferred: `y / (c % d + s)`, `y / f(1)`, `y % f(2)`, `y / (c / d)`, `b[i] / (c / d - s)`, `y % (f(1) - s)`, `y % (c - s)`, `y % (c * d - s)`, `c % d + x`, `c % d + 3`, `f(1) * 2 + c % d`, `f(1) * 3 - c / d`, `c * d + f(1) * 2`, `f(1) * 3 - x`, `x - f(1) * 3`, `f(2) * 4 - f(1)`, `f(1) % c`, `x * (c / d)`, `f(1) - (x < y)`; refused: `x = y % (c * d)`, `x = c % d - b[i]`, `x = c * 3 + f(1) * 5`, `x = f(1) * 2 + f(2) * 3` | refused | 563 |
| `p48_fltinf7.c` | inferred: `d += ++w + ++y;` as a statement (`++y` first?), `e = (d -= ++w + ++y) * 2.0`, `e = (d += ++w + ++y + 1.0) * 2.0`, `e = (d += (w += 1.0) + ++y) * 3.0`, `e = (d += --w + --y) + 1.0`, `x = (d += ++w + ++y) > 2.0`, `e = (q->x += ++w + 1.0)`, `e = (a[i] += ++w + 1.0)`, `e = (d *= ++w + 1.0) * 2.0`, `e = (++w + ++y) * (++d + 1.0)`; refused: `e = (d += ++w * ++y) * 2.0`, `e = (d += ++w - ++y) * 2.0` | refused | 737 |
| `p49_fltstk6.c` | NOT a program to run: the floating-stack model one below the bottom from the start, then a file-scope double, a constant-indexed element of a file-scope array, a local struct's member and a local static pushed as arguments (checked, as NAMEs?), `*p`, `p[1]` and a float element (not checked, as `*` nodes?), a member and an element as two arguments | inferred | - (compiler messages expected) |

"Result" is what `main()` returns under C semantics (the host C compiler's
value). The real compiler's `i *= e` is `i * (int)e`, not `(int)(i * e)` - v7's
`build()` converts the right-hand side of a compound assignment to the
target's type first (`p2_arith.1.golden`: `FTOI`, then `ASTIMES(INT)`) - so
`p2_arith`'s golden returns 16 - and `p13_open`'s `j /= e` gives 42 on
MUTOS for the same reason (its golden: `FTOI`, `ASDIV(INT)`); `p12_init2`'s
golden returns 6, its floats truncated. Every golden of rounds 1 to 11 runs
under `../fuzz/x86sim.py` and returns these values - `p19_open3`'s up to
its invalid statement (the first 223 lines, completed with `r = r + (int)
d`, return 104), and `p21_fltexp`'s with its doubly popped store (line
250, `fstdp`) made the `fstd` it should be (as written, `x86sim.py` stops
there: "'fstdp' with an empty floating-point stack"); `p28_fltstk`,
`p37_fltstk3`, `p41_fltstk4` and `p45_fltstk5` are not meant to run, and `p30_fltstk2`,
written not to, turned out right code (9); `mutos_c1`'s output is
byte-identical to them. (`p38_long6`'s `l <<= i` with `jz .+16` runs
because its count is never 0: with a count of 0 the jump lands inside a
local's three-byte store, which `x86sim.py` refuses to run.)
"inferred" means `mutos_c1` compiles the file - its output runs under
`x86sim.py` and returns the value above - with no golden behind the shapes
yet; "refused" that `mutos_c0` or `mutos_c1` stops with a diagnostic. In
round 12, the shapes each file lists as inferred compile today and give
the host's value when the refused ones are taken out (`p46` 1142, `p47`
412, `p48` 548).

## Generating the goldens

On MUTOS 1700, from inside this directory:

```
make -f Makefile.mutos round12 2>&1 | tee round12.log
```

(or plain `make -f Makefile.mutos` for all twelve rounds), then bring the
`.s`, `.i`, `.1` and `.2` files back to this directory on the modern host -
all four kinds - and run `make goldens` in `..` (it packages every
category's, this directory's included). `p49_fltstk6` is meant to make
`cc -S` print messages and exit with status 1 (`make` goes on: its recipe
starts with `-`), as `p28_fltstk`, `p37_fltstk3`, `p41_fltstk4` and
`p45_fltstk5` did; its `.s` is written all the same - please bring back
the messages (`round12.log`) with the files. Should another round-12 file
not compile on MUTOS, make the others by name (`make -f Makefile.mutos
p47_elem9.s p47_elem9.1`) and bring back the failing one's messages
instead. Should `make` itself run out of memory on the larger
`Makefile.mutos` (53 files now; the corpus's one makefile of 62 did, see
`../README.md`), the commands are the ones `round11.log` shows - `cc -S
p46_long8.c`, then `/lib/cpp -P p46_long8.c > p46_long8.i; /lib/c0
p46_long8.i p46_long8.1 p46_long8.2` - for each file.

## The real assembler on `p19_open3.s`

Settled on real hardware on 2026-10-07: `as -o p19.o p19_open3.s` prints
`***ERROR*** syntax error, line 229` four times and `***ERROR*** syntax
error, line 231` twice, then `p19_open3.s: 3 errors.`, exits with status 2
and leaves a `p19.o` of 0 bytes. `p19_as.log` holds the terminal transcript. It was copied from
the screen because a first run redirected with `> p19_as.log 2>&1` left the
file empty. The real compiler's invalid lines are invalid for the real
assembler too, so no MUTOS 1700 binary can have contained this code.
`mutos_as` refuses the same two lines ("could not classify operands at
line 229" and "... 231", exit status 1, no object written), and that is
the right behaviour. It does not copy the real message texts, which is the
same choice made for its other messages.
