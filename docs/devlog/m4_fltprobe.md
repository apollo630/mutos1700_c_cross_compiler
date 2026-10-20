# Milestone 4 — floating point beyond the corpus: `libc.a`'s compiled C and the `fltprobe` rounds

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). History of the hand-written `mutos_c1` (`c1_gen.c`) against the real-hardware `fltprobe` goldens, rounds 1 to 13 (2026-09-29 to 2026-10-10), and the real assembler on `p19_open3.s`. What the rounds established about the real compiler holds; the code they describe was replaced by the port of `c1`.

---

## Floating shapes from libc.a's compiled C (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-09-29)

`STATUS.md`'s "Next up" item 1 listed the floating shapes `mutos_c1` still
refused, "each waiting for a golden". Most of them turned out to be in the
repository already, as real compiler output: four of `libc.a`'s objects
(`tests/mutos1700_libc/`) are C compiled by the real MUTOS 1700 compiler -
`atof.o` (v7's `atof.c`, plus a MUTOS `_fltrd` and an empty `__fltu3`),
`ecvt.o` (v7's `ecvt.c`: `ecvt`, `fcvt`, the static `cvt`), `gcvt.o` and
`fltpr.o` (MUTOS's `_pgen`/`_pfloat`/`_pscien`) - found by listing every
object whose symbol table names a floating runtime entry point, then read
with a small `a.out` reader that annotates `objdump -D -b binary -m i8086`
output with each relocation's symbol or segment (scratch script, not in
the repo; the sources are v7's, from memory - not in this repo either).

**The caveat: `libc.a` was optimized.** `atof.o`'s `if (negexp<0) fl /=
flexp; else fl *= flexp;` ends both arms in ONE `lea ax,fl` / `call
fstdp` (the `if` arm jumps to the `else` arm's tail) - `c2`'s
cross-jumping, which `c1` alone never does - and `ecvt.o`'s `if (fi != 0)`
is `jne` over a `jmp`. So only shapes the optimizer cannot have changed
are taken: the runtime calls and their operands, the order operands are
loaded in, which entry point is used. Branch structure, label numbers,
where a `.data` block sits relative to code, and whether an `Lnnn:` return
label precedes a function's final `jmp cret` are not readable from it.

**Frames.** `atof(p) register char *p;` with `register c; double fl, flexp,
exp5; double big = 72057594037927936.; int nd; register eexp, exp, neg,
negexp, bexp;`: `p` in DI, `c` in SI (the two register variables), `fl`
at -12, `flexp` -20, `exp5` -28, `big` -36, `nd` -38, then the five
surplus `register` ints as autos from -40 down; `ecvt.o`'s `cvt(arg, ndigits,
decpt, sign, eflag) double arg;` has `arg` at `4(bp)` and `ndigits` at
`12(bp)` - a double parameter takes 8 bytes.

**What the code shows** (`fl`, `big`, ... for their frame slots, `Ln`
for a `.data` constant - the objects hold no `L` symbols):

| Source (v7) | Real code |
|---|---|
| `double big = 72057594037927936.;` (an auto initializer) | `lea ax,L` / `call flds` / `lea ax,big` / `call fstdp` |
| `fl = 0;` (atof), constants `bc a2 31 00` | `lea ax,L` / `call flds` / `lea ax,fl` / `call fstdp` |
| `if (fl<big)` | `lea ax,fl` / `call fldd` / `lea ax,big` / `call fldd` / `call fcmp` / `sahf` / `jge` (false branch) |
| `fl = 10*fl + (c-'0');` | `lea ax,L(10.0)` / `call flds` / `lea ax,fl` / `call fmuld` / `mov ax,si` / `add ax,*-48.` / `call itof` / `call fadd` / `lea ax,fl` / `call fstdp` |
| `flexp *= exp5;`, `exp5 *= exp5;` | `lea ax,exp5` / `call fldd` / `lea ax,flexp` / `call fmuld` / `lea ax,flexp` / `call fstdp` |
| `fl /= flexp;` | `lea ax,fl` / `call fldd` / `lea ax,flexp` / `call fdivd` / (shared) `lea ax,fl` / `call fstdp` |
| `fl *= flexp;` | `lea ax,flexp` / `call fldd` / `lea ax,fl` / `call fmuld` / (shared) ... |
| `fl = ldexp(fl, negexp*bexp);` | `mov ax,negexp` / `imul bexp` / `push ax` / `lea ax,fl` / `call fldd` / `sub sp,*8.` / `mov ax,sp` / `call fstdp` / `call _ldexp` / `add sp,*10.` / `call fldd` / `lea ax,fl` / `call fstdp` |
| `if (neg<0) fl = -fl;` | ... `lea ax,fl` / `call fldd` / `call fneg` / `lea ax,fl` / `call fstdp` |
| `return(fl);` | `lea ax,fl` / `call fldd` / `lea ax,fac` / `call fstdp` / `lea ax,fac` / `jmp cret` |
| `if (arg<0)` (ecvt) | `lea ax,L(0)` / `call flds` / `lea ax,arg` / `call fldd` / `call fcmp` / `sahf` / `jle` - the operands exchanged |
| `if (fi != 0)`, `while (fi != 0)` | the zero loaded first again, `fcmp` / `sahf` / `jne` |
| `else if (arg > 0)` | the zero first, `fcmp` / `sahf` / `jl` (true branch) |
| `fj = modf(fi/10, &fi);` | `lea dx,fi` / `push dx` / `lea ax,fi` / `call fldd` / `lea ax,L(10.0)` / `call fdivs` / `sub sp,*8.` / `mov ax,sp` / `call fstdp` / `call _modf` / `add sp,*10.` / `call fldd` / `lea ax,fj` / `call fstdp` |
| `(int)((fj+.03)*10) + '0'` | `lea ax,fj` / `call fldd` / `lea ax,L(.03, 8 bytes)` / `call faddd` / `lea ax,L(10.0)` / `call fmuls` / `call ftoi` / `add ax,*48.` |
| `while ((fj = arg*10) < 1)` | `lea ax,L(10.0)` / `call flds` / `lea ax,arg` / `call fmuld` / `lea ax,fj` / `call fstd` / `lea ax,L(1.0)` / `call flds` / `call fcmp` / `sahf` / `jl` - and in `.data` the 1.0 comes BEFORE the 10.0 |
| `arg *= 10;` | `lea ax,L(10.0)` / `call flds` / `lea ax,arg` / `call fmuld` / `lea ax,arg` / `call fstdp` |
| `cvt(arg, ndigits, decpt, sign, 1)` (ecvt) | `mov di,*1.` / `push di` / three `push` / `lea ax,*4.(bp)` / `call fldd` / `sub sp,*8.` / `mov ax,sp` / `call fstdp` / `call _cvt` / `add sp,*16.` |
| a store through a pointer (`_fltrd`) | `push s` / `call _atof` / `add sp,*2.` / `call fldd` / `mov bx,pp` / `mov ax,(bx)` / `call fstdp` (or `fstsp`) |

**The runtime's side** (`stkmath.o`, disassembled): `fcmp` pops both
entries, computes second minus top with `dsub`, and returns the flags of
the difference in AH - ZF from its exponent byte (0: zero), SF from its
sign, OF and CF clear (`or ah,ah` / `je` / `or al,7fh` / `lahf`); after
`sahf` the ordinary signed branches read it. `fadd`/`fsub`/`fmul`/`fdiv`
pop the top and combine it into the entry below (second op top - left op
right for operands pushed left first); `fneg` flips the top's sign bit in
place; `ftest` pops the top and returns its flags the same way. `stacks.o`
has `fsts`/`fstd` - store without popping - and `dmath.o` defines `fac`.

**How v7's c1 explains it.** Everything above is v7's own algorithm:

- `optim()` exchanges a relational's operands when `degree(left) <
  degree(right)`, or when the degrees are equal and the left one is a
  NAME and the right one is not (`v7/cc/c12.c`). A DOUBLE NAME and every
  constant have degree 0, a FLOAT NAME 1 (`degree()` gives a FLOAT leaf
  1), anything computed at least 1 - so `arg < 0` becomes `0 > arg`
  (equal degrees, NAME on the left), `fl < big` stays, `(fj = arg*10) <
  1` stays.
- `unoptim()` folds ITOF of a CON into a floating constant (SFCON); the
  MUTOS compiler writes it like a written one, `%.17e` of its value. The
  zeros in `atof.o`/`ecvt.o` are all `bc a2 31 00` - exactly what the
  real assembler makes of `.float 0.00000000000000000e+00`
  (`tests/mutos_as/float_coverage/fltzero.o.golden`), so the text is
  that, and a zero is loaded like any other constant (`flds`), not
  special-cased.
- `cexpr()` prints a constant's `.data` block when the operator using it
  is matched, before any of that operator's code - so for `(fj =
  arg*10) < 1` the comparison's 1.0 is printed before the product's
  10.0, as `ecvt.o`'s data order shows.
- `acommute()` keeps equal-degree operands of `+`/`*` in source order:
  `10*fl` loads the 10 first (`flds` / `fmuld fl`).
- `cbranch()` treats a zero right operand specially (`op += 200`, "special
  for ptr tests" - the PDP-11 tests instead of comparing); the MUTOS
  counterpart is very likely `ftest`, but nothing in `libc.a` reaches it
  (every zero there is exchanged to the left).

**Refuting a guess.** The old "Next up" suggested that an int right
operand (`d + i`) is computed first and combined through `singles.o`'s
and `doubles.o`'s "reversed" entry points (`fsubrs`, `fdivrd`, ...).
`atof.o`'s `10*fl + (c-'0')` does not do that: the left operand is
computed, then the int converted (`itof`), then `stkmath.o`'s
stack-with-stack `fadd`. No object in `libc.a` but the two that define
the reversed entry points refers to one. Where the real compiler uses them is still unknown;
one consistent reading of v7's tables is `-=`/`/=` with a computed right
operand (the right operand on the stack, the target in memory) - v7's
`table.s` has `%ad,nf` templates for exactly that - but that is a
hypothesis, which `fltprobe/p2_arith.c` tests.

**`=*` is not v7's PDP-11 template.** v7's `table.s` compiles `a =* b`
(two addressable doubles) as "load a, multiply by b, store a" (`cr72`
reuses `[addq1a]`); `atof.o` loads the RIGHT operand and multiplies by the
target in memory, for variables and for `arg *= 10` alike, while `/=`
loads the target. So the MUTOS table was changed for `=*` (a commutative
shortcut); nothing in `libc.a` shows `=+` or `=-`, which are not assumed
to follow either.

**Implemented** (`mutos_c0` and `mutos_c1`; all in `c0_parser.c`'s and
`c1_gen.c`'s "Floating point" sections):

- **Comparisons** as conditions (`if`, `while`, `do`, `for`, `!` on
  one): `mutos_c0` converts an int operand to the floating one's type
  and types the node INT, as v7's `build()` does (the right operand of
  a non-floating left one is buffered, so an int left operand's `ITOF`
  can go between them - the same capture `+ - * /` already used);
  `mutos_c1`'s `gen_fp_compare()` exchanges by the degree rule, loads
  both, `call fcmp` / `sahf`, and the branch follows directly
  (`VK_COND` with `cond_is_float`; `gen_cond_branch()` refuses anything
  between).
- **Int constants converted** (`gen_itof()`): a `.float` of `%.17e` of
  the value, zero and negative values included; a written zero
  (`fcon_render()`) too.
- **Unary minus**: `mutos_c0` now writes `NEG`, typed like its operand
  (FLOAT for a float variable - v7's unary `build()`); `mutos_c1`: load,
  `call fneg`.
- **`*=` and `/=`** into a float or double variable (`gen_fp_asop()`),
  the right operand a variable or a constant - an int constant converted
  to the target's own type (ITOF(FLOAT) into a float, as for `=`); a
  computed one, an int variable's `itof` included, is refused.
- **A computed left operand with an int right operand** of `+ - * /`:
  `call itof`, then `call fadd`/`fsub`/`fmul`/`fdiv` - checked on the
  pre-scanned tree (`float_order_check()`) to be `atof.o`'s kind, an int
  variable or a variable plus or minus a constant, so that v7's
  `acommute()` would not reorder it.
- **An assignment whose value is used** (`gen_fp_assign()`, told by
  `scan_consumer()`): `fst<s|d>`, the value left on the stack.
- **Double and float parameters** (a float parameter is a double - v7's
  `funchead()` - 8 bytes of the frame), **double arguments**
  (`push_fp_arg()`), **functions returning a double** - `double f();`
  prototypes and definitions, `return` through `fac` (`gen_fp_rforce()`,
  `|RTYP 3`), and the caller's `call fldd` after the call.

**Still refused, each with its own diagnostic**: a zero as the right
operand after the exchange (a float variable or a computed value compared
with 0), a floating truth test, a comparison as a value, `&&`/`||`/`?:`
around floating code, a constant right operand of a computed value (in a
comparison too - its `.data` block would have to precede the computed
value's code), two computed comparison operands, any other computed right
operand of `+ - * /` (`c * (a+b)`, `(a+b) * (c+d)`) and an int right
operand of a variable (`d + i`), `+=`, `-=`, `*=`/`/=` by a computed
value or an int variable, a
constant that is not exactly a float or is 2\*\*24 or more, a written
constant after an int constant converted in the same expression (v7 numbers
a written constant while reading the tree and a converted one only in
`optim()`; how the MUTOS compiler numbers the converted one is unknown),
a floating constant as a call argument, a computed floating argument other
than the last, an unused double result, a doubly negated value, a function
returning a float, floating globals, statics, arrays, pointers and
members, and `char`/`long` to or from floating. `tests/mutos_cc/fltprobe/`
holds nine programs for real hardware: four that exercise only what is now
implemented (to confirm it byte for byte, including what `libc.a`'s
optimized code cannot show - label numbers, `.data` placement, where the
return sequence goes) and five that exercise the refused shapes above.

**First real-hardware result (the same day).** `cc -S p1_compare.c` stopped
with `p1_compare.c:37: Illegal type of operand` - `if (!e)`, `e` a double -
and nothing else in the file: the real front end rejects `!` on a floating
operand, although v7's `build()` accepts one (`!un` has no `LWORD` in v7's
`opdope[]`; `c01.c`'s EXCLA case just types the node INT). So the MUTOS `c0`
differs from v7 here, and `mutos_c0`'s refusal of `!e` is right - only its
wording ("not yet supported") understates it. The line was removed from
`p1_compare.c`. The second run got past `c0` and failed in `c1`: `37:
Floating point stack underflow` - the line of `while (a)`, `a` a float -
then `38: floating point stack underflow` (lower case: another message
site) for `a = 0;`, and from there one message per stack pop of every
floating statement to the end of the function (two per comparison, four
for the `&&`), so the real `c1` keeps a compile-time model of the runtime
stack's depth, and after the first underflow it stays below zero. Nothing
earlier in the file failed, `if (d)` on line 35 (its CBRANCH carries line
36 - `if` reads one token past its `)`, `while` does not) included, but
whether `if (d)` already left that model one entry short cannot be told.
Both truth tests were removed from `p1_compare.c`; a floating value tested
for truth stays refused, now with a real reason.

**Tooling.** `dump_temp.py` decodes `NEG` (`mutos_c0` writes it now).
`x86sim.py` runs the new calls - `fsts`/`fstd`, `fadd`...`fdiv`, `fneg`,
`fcmp` with `sahf` (only directly after it), `fac` - and no longer divides
eagerly: its table of the four results of a floating operation computed a
division for every `fmuls`/`fmuld` too, so a multiplication by zero
stopped it with Python's `ZeroDivisionError` (no golden multiplies by
zero, so it never showed).

**Verification.** `make test`: 62/62 byte-exact, `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 71/71 compiler goldens assemble, `mutos_cpp`
5/5, zero warnings. Against the previous build (`3c801a0`), on all 71
golden inputs: `mutos_c0` (`.i` -> `.1`/`.2`, diagnostics included) and
`mutos_c1` alone (golden `.1`/`.2` -> `.s`) identical, every file.
`fuzz_c.py` against the previous build: 42,000 programs (18,000 with
arrays, seed 11; 18,000 `--scope`, seed 21; 6,000 scalars only, seed 31):
all identical, 0 WRONG, 0 BAD; 4,500 more through ASan/UBSan builds: 0
WRONG, 0 BAD (the generator makes no floating code - what it checks is
that the integer paths `mutos_c0`'s relational change touched stayed the
same). Thirteen hand-written floating programs -
the four `fltprobe` confirmation files and nine more (every new shape,
`atof.o`'s and `ecvt.o`'s statements among them, loops with floating
conditions, three double functions calling each other) - through
`mutos_cpp`, both passes, `mutos_as` and `x86sim.py`, compared with the
host C compiler: all equal; 27 refusal probes and the five `fltprobe`
open-question files each stop with their diagnostic. ASan/UBSan builds of
`mutos_c0`/`mutos_c1` over all of these and the 71 golden inputs: no
reports, output and exit status identical to the `-O2` build's.
`x86sim.py` gives the same output as before on all 53 goldens it runs.

## The fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-09-29)

The nine `tests/mutos_cc/fltprobe/` programs came back from real hardware
the same day (commit `ac4ba38`). **`mutos_c0` matched every `.1`/`.2` golden
it produced at once** (the four confirmation files, `p1_compare`,
`p4_const`). **`mutos_c1`'s four confirmation files differed in a few lines
each**, and every difference traced to one v7 rule the libc.a-based model
had slightly wrong; the five open-question goldens then answered every
question they asked. All nine are now byte-exact end-to-end; none of the 62
corpus files changed.

**1. A floating constant has degree 1.** v7's `degree()` gives a constant
leaf -3 (`CON`) or, as a DOUBLE leaf, 0 (`FCON`/`SFCON`), and a FLOAT leaf 1 -
and the model gave floating constants 0. The goldens say 1: `03_fltcmp`'s `a
< 1.5` (`a` a float) loads 1.5 first and branches `ble` (exchanged:
degree(a) = 1 is not below degree(1.5) under the old rule, so v7 would not
exchange), `p1_compare`'s `a < 0` and `a == 0` the same, and `1.5 < i` /
`i < 1.5` both keep their order (the constant and the converted int
tie at 1, the left one is no NAME). `04_fltconst`'s `d = e + 10` loads
10.0 first and adds `e` from memory - acommute() puts the constant (1)
before the double (0). The reading that fits: the MUTOS compiler keeps a
constant that is exactly a float as a 4-byte `.float`, typed FLOAT, and
`degree()` does the rest unchanged. `libc.a` could not show it: `atof.o`
and `ecvt.o` never compare a float variable with a constant, and their
`fl < big`, `arg < 0`, `10*fl` fit either rule.

**2. The rest of v7's order, confirmed.** A '+'/'*' chain is acommute()'s -
terms by decreasing degree, equal ones as written, rebuilt left to right:
`p3_global`'s `(int)(gd + gi + ga[2] + arr[0] + gs.y)` starts with the
float `gs.y`, `p2_arith`'s `(d + e) * (i * j)` with `i * j` (an int product
has degree 2 - acommute()'s `d++` for TIMES), `b * (a + e)` with `b`. Every
operator loads its left operand first; a right operand in memory is combined
from there (`lea ax,<it>` / `call fadd<s|d>`), a computed one is computed
onto the runtime stack after it and combined with `stkmath.o`'s
stack-with-stack entry point: `d - i` is `fldd d / itof / fsub`, `d + i` is
`itof / faddd d` (acommute: the conversion first), `i - d` is `itof / fsubd
d`. **No "reversed" entry point** (`singles.o`'s `fsubrs`...) is used
anywhere. A `.data` block is printed where v7's `cexpr()` prints an `FCON`
operand - when its operator is matched, before the operator's code: `(d +
e) * 2.0` is `.data` / `L10004:<TAB>.float 2.0...` / `.text` / `fldd d` /
`faddd e` / `lea ax,L10004` / `fmuls`. `-(-e)` is `e`. `-1.5` is folded into
the constant (`.float -1.50000000000000000e+00`, no `fneg`), as v7's
`unoptim()` folds a negated `FCON`. `(d + e) - 0.5` stays a subtraction:
v7's `optim()` makes `x - c` into `x + -c` only for a `CON`/`SFCON`.

**3. A zero is compared, never tested.** `p1_compare`'s `(d + e) < 0` and
`(d - e) != 0` load a `.float 0` and `fcmp` it - no `ftest`, although v7's
`cbranch()` tests a zero right operand. So the MUTOS compiler makes no
`SFCON` of a zero (or does not test one); `stkmath.o`'s `ftest` is unused
by compiled code.

**4. Labels.** A written constant takes its label when `c1` reads the tree,
an int constant converted (unoptim()'s `ITOF(CON)` fold) one only in
`optim()`: `p4_const`'s `d = 2 * 1.5` numbers 1.5 `L10007` and 2.0
`L10008`, and prints 2.0's block first (it is the left operand). A file-scope
initializer's `FCON` takes a label too, though none is written:
`p3_global`'s first code constant is `L10002` after two initializers.

**5. `x - c` is `x + -c` for an int** (v7's `optim()`, a `CON` right operand):
`05_fltasop`'s `c - '0'` is `add ax,*-48.` - `mutos_c1` wrote `sub`, and
`gen_charx_binop()` already had the rule for a char. `n - 1` stays `dec di`
(`04_funcs`), now as "`+ -1`". The 62 corpus files never subtract another
constant; the fuzzer's programs do (see Verification).

**6. The register an int is converted in.** `itof` takes the int in AX. For
an int variable the real code goes through DI - `mov di,i` / `mov ax,di` -
when the conversion is evaluated first, after a variable or constant was
loaded, or after a computed `+`/`-` (`05_fltasop`'s `(t + fl) / c`); but
straight into AX after a computed floating `*`: `(fl * 2) - c` is `mov ax,c`
/ `call itof`, and `10*fl + (c-'0')` computes `c - '0'` in AX (`mov ax,c` /
`add ax,*-48.`). v7's `c1` hands registers out by number and its `oddreg()`
gives a product the odd register - evidently a floating one too in the
MUTOS compiler - and the next operand's register follows. A product itself
is in AX already (`mov ax,i` / `imul j` / `call itof`), a char loads into AX
(`movb ax,c` / `cbw`), and a register variable in DI goes through SI (`mov
si,di` / `mov ax,si`, `p5_call`). After any other computed operand the
register is not known (refused; `fltprobe/p7_itofreg.c` asks).

**7. Smaller shapes.** A double argument is `sub sp,*8` - no decimal point,
unlike every other constant (the caller's `add sp,*8.` has one), and
`lconvert.o`'s `ltof` is called with the long pushed low word first and
popped with `add sp,*4`. A call's unused double result is left in `fac` (no
`fldd`); a function returning a float returns through `fac` like a double
(`|RTYP 2`) and its caller loads it with `fldd`. A comparison used as a value
is v7's usual 0/1 (`blt L10007` / `mov di,*0.` / `jmp L10008` / `L10007:mov
di,*1.`), and `r + (d < e)` then adds `r` to it (`add di,*-28.(bp)`: the
comparison has the higher degree). `+=`, `-=`, `/=` load the target and
combine the right operand (a computed one: `fldd d` / `fldd e` / `faddd e` /
`fdiv`), `*=` loads the right operand and multiplies the target in from
memory (`itof` / `fmuld d`), a used value is `fstd` (no pop); `i *= e` into
an int is `call ftoi` / `mov ax,ax` / `imul i` / `mov i,ax` - `mutos_c0`
already converts `e` first (`FTOI`, then `ASTIMES(INT)`), v7's own semantics:
`i * (int)e`, not `(int)(i * e)`. In a function with a register variable in
DI, a constant argument goes through SI (`mov si,*3.` / `push si`) and a long
lives in SI(high):DX(low) - `l = 7` is `mov ax,*7.` / `cwd` / `mov si,dx` /
`mov dx,ax`, stored from DX and SI - the next register pair v7's allocation
takes. Floating data: `.comm _gd,8`, `.bss` / `_sd:.blkb 8.`, and for
`double gi = 2.5;` a `SYMDEF`, `.data` and `_gi:<TAB>.double<TAB>2.5...` (a tab
after the directive, unlike a code constant's space); an element or member is
addressed as one operand (`lea ax,16.+_ga`, `8.+_gs`, `*-12.(bp)`), a value
through a pointer as `mov di,p` / `lea ax,(di)` - loaded after the right-hand
side (`*p = 2.0` is `flds <2.0>` / `mov di,*-22.(bp)` / `lea ax,(di)` / `fstdp`),
and `gp = &gd` is `mov _gp,#_gd`.

**8. The text of a constant that is not exactly a float** (`p4_const`): an
8-byte `.double`, loaded with `fldd` - `0.1` is `.double
1.00000000000000000e-01` (glibc's `%.17e` would give `...06e-01`),
`3.14159265358979` is `3.14159265358979001e+00`, `1e30`
`1.00000000000000005e+30`, `16777217.` `1.67772170000000000e+07`; `2**56`
(`72057594037927936.`), exactly a float, is a `.float`
`7.20575940379279360e+16`. The real `c1` is a MUTOS program linked with
`libc.a`: it converts with `libc.a`'s `atof()` and prints with `printf`,
whose `%e` is `fltpr.o`'s `_pscien()` - and its disassembly shows that
calling `_ecvt` (`libc.a`'s `ecvt.o`, v7's `cvt()` compiled; 18 digits for
`%.17e`), then writing the first digit, `.`, the rest, `e`, the sign and two
digits of `decpt - 1`. Running exactly that - `libc.a`'s `atof` then
`libc.a`'s `ecvt` on `libc.a`'s runtime, under the 8086 emulator that
`tests/mutos_as/float_coverage/libcatof.py` already had - reproduces all
seven texts. `mutos_c1` now reproduces them without the emulator:
`src/mutos_cc/c1_fltdec.c` takes the value from `mutos_as`'s own
`fltconst.c` (libc.a's `atof()` exactly, for every nonzero value - compiled
into `mutos_c1` as it is) and runs `cvt()` on exact 56-bit arithmetic.
`cvt()` only ever multiplies by 10.0, divides by 10 and adds its own `.03`
(`c3 f5 28 5c 8f c2 75 7b`), and 10.0's mantissa words below the top one are
zero, so `dmul`'s wrong partial product never contributes: every operation
is the exact result rounded to nearest-even, `modf.o` splits a value
exactly (it shifts the integer bits out), `ftoi` truncates. Against the
emulated `libc.a` - the new `libcatof.py ecvt` subcommand, part of `make
check-libcatof` - 28,188 random C literals (fractions, exponents from e-37
to e+37, long integers, leading and trailing points): 0 differences in text
or size. The 4- or 8-byte choice is "the double's low four bytes are zero".
An 8-byte constant's degree (0, as a DOUBLE leaf?) has no golden yet: one
where it would decide an order - in a `+`/`*` chain or a comparison - is
refused (`fltprobe/p6_dblcon.c` asks); elsewhere (an assignment, an
argument, `d - 0.1`) it is compiled.

**How `mutos_c1` does it now.** Every expression with a floating node is
planned (see "Evaluation order": `plan_expression()` pre-scans the tree): a
new part of that planner, `plan_fvalue()`, rebuilds v7's order - `fdeg()`
(v7's `degree()` as above, with `optim()`'s rules for `-`, `/`, `*=`, `/=`,
relationals, conversions and calls), `plan_fchain()` (acommute()'s
`insert()` order and the rebuilt nodes' `.data` blocks, outermost first), a
relational's exchange, `SEG_FLOAD` (a leaf left operand loaded before a
computed right one), `SEG_FDATA` (a constant's block where `cexpr()` prints
it), `SEG_FSWAP` (the handler mirrors the relation), `SEG_FITOF` (point 6's
register), `SEG_FLEAF` (an element or member at a constant offset, `ffold()`
- `v7`'s `optim()` makes `*(&x + c)` one NAME, so its opcodes are never
streamed). `fplan_constants()` numbers the expression's constants when it is
planned, written ones first. The handlers then just generate each node with
its operands arriving in that order - `gen_fp_binop()` no longer guesses
anything. `&&`/`||`/`!` over floating comparisons go through the existing
`plan_cbranch()`. What is still refused is listed in `src/mutos_cc/README.md`'s
"Floating point" and `tests/mutos_cc/fltprobe/`'s round 2 (`p6`..`p9`) asks
about most of it.

**`mutos_c0`.** `+=`/`-=` into a float or double (like `*=`/`/=`: the right
operand converted to the target's type), `i *= e` into an int (`FTOI`, then
`ASTIMES(INT)`), an embedded compound assignment whose value is used (`e =
(d *= e)` - `parse_comma_item()`), a char to floating (`ITOC(INT)`, then
`ITOF`) and back (`FTOI`, then `ITOC(CHAR)`), functions returning a float,
floating struct members, local floating arrays and pointers, local `static
double`/`float`, file-scope floating variables, arrays, pointers and
initializers (`SYMDEF`, `DATA`, `NLABEL`, `FCON`, `INIT(type)`, `EXPR` - v7's
`cinit()`), file-scope struct variables of any kind (`mutos_c1` refuses an
int member's access as it refuses an int file-scope array's element), and
`(int) (<floating expression>)`. `dump_temp.py` decodes `INIT` and an empty
symbol name (v7's `SYMDEF("")` every 64 initializers - `11_kernel/08_tty`
now decodes up to a `GREATQP`). `11_kernel/03_mem`'s diagnostic moved from
line 617 to 610 (`'y' undeclared` - the K&R `register y,x;` with no type):
its file-scope struct is accepted now.

**Tooling.** `x86sim.py` runs `ltof` (the long from the machine stack),
`.double` constants and floating initializers, and `N.+_sym` addresses;
every round-1 golden now runs under it and returns its program's value
(`p2_arith` 16, v7's `i *= e`; the C value is 17). `libcatof.py` links
`ecvt.o` and `modf.o` too and gained the `ecvt` subcommand;
`src/mutos_cc/fltdec_test` feeds it.

**Verification.** `make test`: 62/62 corpus files and 9/9 `fltprobe` files
byte-exact (71 in `run_goldens.sh`'s list), 0 genuine mismatches, zero
warnings; `mutos_as`, `check_floatdat.sh`, `assemble_cc_goldens.sh` and
`mutos_cpp` unchanged. `fuzz_c.py` against the previous build (`3c801a0`):
30,000 programs (12,000 with arrays, seed 111; 12,000 `--scope`, seed 121;
6,000 scalars only, seed 131) - 0 WRONG, 0 BAD, 398 changed but still
correct (`x - c` is now `add ...,*-c.`, and `p[i - 1]` through a pointer
takes `p[i + 1]`'s path, the -2 a displacement: `mov di,*-2.(di)`), 1
formerly refused now correct; 3,000 more through
ASan/UBSan builds: 0 WRONG, 0 BAD, no reports. ASan/UBSan builds over all
71 golden inputs and the nine `11_kernel` refusals: clean. Seven more
hand-written floating programs (calls with three double arguments, a float
function, compound assignments, `&&`/`||`, loops with floating conditions,
globals, arrays, pointers, members, `long`/`char` conversions, inexact
constants) through `mutos_cpp`, both passes, `mutos_as` and `x86sim.py`,
compared with the host C compiler: all equal, but for `k *= a` where the
difference is v7's `i * (int)a`. `libcatof.py ecvt`: 28,188 literals, 0
differences; `libcatof.py goldens`, `check` and `ops` still pass with the two
extra objects linked in.

## The round-2 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)

The four round-2 programs (`p6_dblcon`, `p7_itofreg`, `p8_misc`,
`p9_init`) came back from real hardware in commit `4c668c2` - their `.s`,
`.1` and `.2`, **but not their `.i`** (`make goldens` packages whatever is
on disk; the `.i` files stayed on the MUTOS machine). `run_goldens.sh` keyed
on `.i.golden` and skipped all four without a word - `make test` would have
gone on reporting 71 files byte-exact while these four were never compared.
It now checks a file with a `.1.golden` but no `.i.golden` from `mutos_c0`
on (`mutos_cpp`'s own output feeding it, nothing to diff that against) and
lists a match apart, as category 7. `mutos_c0` matched `p6`'s and `p7`'s
`.1`/`.2` at once - so `mutos_cpp`'s `.i` and the real one tokenize alike -
and refused `p8` (`i += d`) and `p9` (its initializers); `mutos_c1` refused
`p6` and `p7`. All four are byte-exact now, the 71 others unchanged.

**1. An 8-byte constant has degree 0** (`p6_dblcon`). v7's `degree()` gives
a DOUBLE leaf 0, and a `.double` constant is one: `d + 0.1` and `0.1 + d`
both keep their order (equal degrees), `a * 0.1` and `0.1 * a` both load
the float `a` first, `d < 0.1` is exchanged (equal degrees, only `d` a
NAME: `fldd <0.1>` / `fldd d` / `fcmp` / `ble`), `0.1 < d` is not, `a >
0.1` is not and `0.1 > a` is (`a` has 1). So only a constant that is
exactly a float has degree 1 - it is kept as a FLOAT `.float` (round 1's
finding); the rule is simply `degree()` on the constant's real type.
`fdeg()` no longer refuses one.

**2. The register an int is converted in** (`p7_itofreg`). After the
operand computed before it (each statement `f + (<computed> op <int>)`):

| Computed first | Register | Golden |
|---|---|---|
| `d / e` | AX | `mov ax,*-30.(bp)` / `call itof` |
| `tw(d)` (a call, its result loaded with `fldd`) | AX | the same |
| `-d` | DI | `mov di,*-30.(bp)` / `mov ax,di` / `call itof` |
| `d * e`, int `i + 1` | AX | `mov ax,*-30.(bp)` / `inc ax` |
| `d * e`, int `j - 1` | AX | `mov ax,*-32.(bp)` / `dec ax` |
| `d * e`, int `i + j` | AX | `mov ax,*-30.(bp)` / `add ax,*-32.(bp)` |
| nothing (a chain's first term), int `i + j` | DI | `mov di,i` / `add di,j` / `mov ax,di` |
| nothing, int `i * 3` | AX | `mov ax,i` / `mov cx,*3.` / `imul cx` |

With round 1 (DI after a leaf and after a computed `+`/`-`, AX after a
computed `*`) the reading that fits: the next operand takes the register
the previous one's result was given, and the MUTOS compiler gives a
product, a quotient and a call's result AX - the register the 8086's
`imul`/`idiv` and a function's return use; an addition, subtraction or
negation stays in the register it was computed in. The operands of a
floating `*` itself are still in DI (`p2_arith`'s `d * i`: `mov di,i` /
`mov ax,di`), so this is the result's register, not v7's `oddreg()` applied
to the node. `fafter()` gives AX after `*`, `/` and a call and keeps the
context after `+`, `-` and a negation; in DI any int computed the ordinary
way is then moved, `mov ax,di` (`gen_itof()`); in AX, `x + 1` / `x - 1` are
`inc ax` / `dec ax` and a sum of two variables `mov ax,i` / `add ax,j` (the
`OP_PLUS` handler, for an `ITOF` consumer). After any other computed
operand - a conversion, an element, an assignment - the register is still
not known, and refused.

**3. `p8_misc`.** The `long` converted with DI free goes through DI, as
inferred (`mov di,<high>` / `push di` / `mov di,<low>` / `push di` / `call
ltof` / `add sp,*4`). `*p + 1.5` loads `*p` first: v7's `unoptim()` gives a
STAR `max(islong(type), degree(operand))` = 1, the constant's degree, so
the operands stay as written (`fdeg()` computes it now; ITOP as optim()'s
TIMES). `i += d` and `i -= d` into an int are, like round 1's `i *= e`,
the right operand converted first (`.1`: NAME i, NAME d, `FTOI(INT)`,
`ASPLUS(INT)`/`ASMINUS(INT)` - v7's `build()` converts an assignment
operator's right operand to the left's type, so the value is `i + (int)d`)
and then a single in-place instruction, `call ftoi` / `add *-44.(bp),ax`
(`sub` for `-=`). A double array subscripted by a variable is addressed as
an int array is, with the constant-shift rule for the element size 8:
`lea di,*-38.(bp)` / `mov si,*-44.(bp)` / `mov cx,*3.` / `sal si,cl` / `add
di,si`, then used through DI, `lea ax,(di)` / `call fldd`; as an
assignment's target (`arr[i] = d`) the address is computed after the
right-hand side is loaded (`fldd d` first), as a pointer variable is loaded
only after it (`*p = 2.5`). `return i + (int) d` is `call ftoi` / `add
ax,*-44.(bp)`.

**4. `p9_init`: v7's `doinit()`.** The real `c1` writes an initializer as
v7's `doinit()` does, after `optim()`: the value as a double for a
`double` variable - `double gi = 2;` is `.double<TAB>2.00000000000000000e+00`
(a code constant 2.0 would be a `.float`), `double gx = 0.1;` `.double
1.00000000000000000e-01` (the MUTOS `ecvt()`'s text, as for a code
constant), `double gn = -1.5;` `.double -1.50000000000000000e+00` (`.1`:
`FCON 1.5`, `NEG(DOUBLE)`, `INIT` - v7's `c0` folds a negated int constant,
not a floating one; `unoptim()` folds it) - and for a `float` variable
`sfval = fval; printf(...)` of that: `float gy = 0.1;` is `.float
9.99999940395355225e-02`, the double **truncated** to a float, not rounded
(0.1 rounded would be `1.00000001490116119e-01`). The MUTOS `c1` is a
MUTOS program, so `sfval = fval` is `fldd` / `fstsp` on `libc.a`'s runtime;
run under the emulator (`libcatof.py`'s new `fecvt` subcommand), `fstsp`
stores the double's high four bytes for every one of 5,649 literals (and
3,000 random doubles, 1,429 of which rounding would change), and the text
`fdec_render_single()` writes for each matches `ecvt()` of the stored float
exactly. `static double gs = 2.5;` is `DATA` / `NLABEL _gs` (no `SYMDEF`, no
`.globl`) - `mutos_c0` already wrote that. Every initializer takes a `c1`
label though none is written - the int one too (`unoptim()`'s `ITOF(CON)`
fold, as in code): the first code constant after five initializers is
`L10005`. `mutos_c0` now accepts an int constant (`CON`, `ITOF(type)`), a
negated one (`CON -n`) and a negated floating literal (`FCON`, `NEG`);
`mutos_c1`'s new `gen_finit()` reads the whole initializer, runs before any
expression planning, and replaces the old `FCON INIT` special cases in
`gen_fcon()` and `plan_expression()`.

**5. What `mutos_c1` now compiles by inference** (no golden yet - round 3
asks): an element subscripted by a variable as the second operand after a
leaf (`1.5 + arr[i]`: the leaf loaded, then the address, `lea ax,(di)` /
`call faddd`) or after another element (`arr[i] < arr[j]`: the first
loaded before the second's address is computed - `plan_fload()`); any int
computed in DI converted (`d + (i << 2)`: `mov di,i` / `sal di,*1` / `sal
di,*1` / `mov ax,di`), a quotient converted from AX; an element of any
power-of-two size subscripted by a variable (`mov cx,*N.` / `sal si,cl`
from 8 bytes up, by the constant-shift rule - a struct array's too); `i +=
f()` / `i -= f()` (`add i,ax`, the call's result where `ftoi`'s is);
`float` initializers from an int constant (`ITOF(FLOAT)`), negated
constants, `static float`; a shift's degree (optim()'s "def:").
`tests/mutos_cc/fltprobe/`'s round 3 (`p10_elem`, `p11_itof2`, `p12_init2`)
asks about each, `p13_open` about what is still refused (a conversion after
a conversion, an element or an assignment; a difference or a shift for AX;
`x + 0` for AX; `i /= d`; a floating zero added or subtracted).

**Tooling.** `run_goldens.sh`'s category 7 (above); `libcatof.py fecvt`
and `fltdec_test -f` (`make check-libcatof`); `fltprobe/Makefile.mutos`'s
`round3`. `x86sim.py` needed nothing new: all four round-2 goldens run
under it and return their programs' values (9, 62, 6, 6).

**Verification.** `make test`: 62/62 corpus and 9/9 round-1 `fltprobe`
files byte-exact (category 1, 71), the four round-2 files byte-exact from
`mutos_c0` on (category 7), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 84/84 compiler goldens assemble, `mutos_cpp`
5/5. With a scratch `.i.golden` from `mutos_cpp` for each, all 75 are
byte-exact in category 1. Against the previous build (`4c668c2`) over all
84 inputs (the 71, the four round-2 files, the nine `11_kernel` refusals):
identical output and diagnostics but for the four. `fuzz_c.py` against
`4c668c2`: 30,000 programs (12,000 with arrays, seed 411; 12,000
`--scope`, seed 421; 6,000 scalars only, seed 431), all identical, 0 WRONG,
0 BAD; 3,000 more (seed 441, `--scope`) through ASan/UBSan builds: 0 WRONG,
0 BAD. ASan/UBSan builds over the 84 inputs: no reports, output identical
to the `-O2` build's. Seven hand-written floating programs (elements in
every position, struct arrays of 8 and 16 bytes, ints computed in DI and
AX converted, `i += d`, `i += f()`, every initializer form - the round-3
probes among them) through `mutos_cpp`, both passes, `mutos_as` and
`x86sim.py`, compared with the host C compiler: equal, but where v7's
semantics differ (`i -= d` is `i - (int)d`) or a float initializer is
truncated (`p12_init2`: 6, the host's 7); ten refusal probes each stop with
their diagnostic. `make check-libcatof`: `goldens` 34/38 identical (4 not
emulated), `check` 8,357 texts 0 differences, `ops` 0, `ecvt` 5,630
literals compared 0 differences, `fecvt` 5,649 compared 0 differences.

## The round-3 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)

The four round-3 programs (`p10_elem`, `p11_itof2`, `p12_init2`,
`p13_open`) came back from real hardware in commit `33ec944`, all four
kinds of file this time; round 2's four `.i` files followed in commit
`34c9195`. Those four `.i` goldens are byte-identical with `mutos_cpp`'s
output, so `p6`..`p9` moved from category 7 into category 1 with no
change, and category 7 is empty again. Of round 3, `p11` and `p12` matched
at once - every inference they asked about holds - `p10` differed in two
places of its `.s`, and `mutos_c0` refused `p13` (`j /= e`). All four are
byte-exact now, the 75 others unchanged.

**1. An element of a local array has degree 2** (`p10_elem`'s `d = 1.5 +
arr[i]`). The golden computes the element's address, loads the element
and adds the constant from memory - `lea di,*-28.(bp)` / ... / `lea
ax,(di)` / `call fldd` / `lea ax,L10003` / `call fadds` - where `mutos_c1`
had loaded 1.5 first. By v7's own numbers the two would tie: `degree()`
gives an `AMPER` -2, `acommute()` puts `i*8` (degree 1, `TIMES` by a power
of two) ahead of `&arr` and gives the sum 1, the `STAR` `max(1, 1)` = 1,
the `.float` constant's degree - so `insert()` would keep the source order,
constant first. The element going first means its degree is at least 2.
The same golden's address code says why: `lea di,<arr>` comes BEFORE the
index is scaled, which `insert()` only does when the address term's degree
is not below the index's 1. One change fits both: a local's address is not
a link-time constant on the 8086 but a computed `lea`, and counts as
computed - degree 1; the two terms tie (the `lea` first, as written) and
their sum has degree 2. `fdeg()` now gives an `AMPER` of an `SC_AUTO` name
1; a static or file-scope object's address stays -2 (a constant: `_ga(si)`,
`16.+_ga`). The integer planner's `enode_degree()` already gave such an
element 2 (its "anything computed at least 1" clamp). The rest of `p10`'s
`main()` matched before and after - `d < arr[i]` exchanged (`ble`), `arr[i]
< arr[j]` and `arr[i] + arr[j]` the first element loaded before the
second's address, `fa[i] * arr[i]` as written.

**2. A dereferenced left operand of an int `*` is loaded first** (`p10`'s
`ps[i].c * qs[i].g`, arrays of 8- and 16-byte structs subscripted by a
variable): `lea di,*-20.(bp)` / `mov si,*4.(bp)` / `mov cx,*3.` / `sal
si,cl` / `add di,si` / `mov di,*4.(di)` / `lea si,*-52.(bp)` / `mov dx,*4.
(bp)` / `mov cx,*4.` / `sal dx,cl` / `add si,dx` / `mov ax,di` / `imul
*12.(si)`. v7's `F` then `S1`, as for `+`/`-` (`02_bubsort`, `05_nestst`):
`load_now()` now includes `*`, and the multiply's ordinary `mov ax,<left>`
moves the loaded value. `mutos_c1` had left the element as `*4.(di)` and
loaded it into AX at the end. This is also the first golden of a product
of two 1-D elements: it is NOT spilled - the right operand fits in the
registers left over - so the "Evaluation order" sections' guess that the
real compiler "very probably spills `a[i] * b[j]`" like `05_matmul`'s 2-D
elements is wrong for these. (`a[i] * b[j]` of int arrays itself was no
longer refused - it compiled with the late load; `p14_axint` asks.)

**3. The register an int is converted in, completed** (`p13_open`):

| Computed first | Register | Golden |
|---|---|---|
| `(double) i` - a conversion, through DI | DI | `mov di,*-40.(bp)` / `mov ax,di` / `call itof` / `call fadd` |
| `arr[i]` - an element, its address in DI | DI | the same |
| `d * e`, int `i - j` | AX | `mov ax,*-38.(bp)` / `sub ax,*-40.(bp)` / `call itof` |
| `d / e`, int `i << 1` | AX | `mov ax,*-38.(bp)` / `sal ax,*1` / `call itof` |
| `d * e`, int `j + 0` | AX | `mov ax,*-40.(bp)` / `call itof` - no `add` |

The reading from round 2 holds: the next operand takes the register the
previous one's result was given - an int converted through DI leaves DI,
and so does an element whose address was computed there. `fafter()` keeps
the context after an `ITOF`, and after an element when the context is DI
(after a `*`, `/` or call no golden shows an element, and its address needs
a base register anyway - still refused there). In AX the `OP_PLUS` handler
forms a difference as it forms a sum (`mov ax,i` / `sub ax,j`), and
`OP_LSHIFT`/`OP_RSHIFT` shift a variable there by a constant - `sal ax,*1`;
a count of 3 or more by CL and a right shift are inferred from DI's rule
(`emit_const_shift()`; `p15_fltinf` asks).

**4. An int `+ 0` is dropped.** `(j + 0)` converted is just `mov ax,j`:
v7's `acommute()` "toss[es] out +0" (`c12.c`, for `PLUS`/`OR` whose last
term is a constant 0 - `isconstant()`, an int `CON`) before any code is
chosen. `mutos_c1` refused it converted in AX and everywhere else emitted
`add di,*0.`, a shape no golden had; now the `OP_PLUS` handler pushes the
other operand unchanged for any int `x + 0` - and `x - 0`, which v7's
`optim()` makes `x + -0` first - so `s = j + 0` is `mov di,j` / `mov s,di`
(`p14_axint` asks).

**5. `j /= e` into an int** (`p13`): `.1` - NAME j, NAME e, `FTOI(INT)`,
`ASDIV(INT)`, as `i *= e` and `i += d` (v7's `build()` converts the right
operand to the target's type first, so the value is `j / (int)e` = 3 where
C gives 2 - the golden returns 42, the host 41); `.s` - `lea ax,*-20.(bp)` /
`call fldd` / `call ftoi` / `mov cx,ax` / `mov ax,*-40.(bp)` / `cwd` / `idiv
cx` / `mov *-40.(bp),ax`: the converted divisor moved to CX out of the way
of the dividend. `mutos_c0` accepts `/=` into an int; `mutos_c1`'s
`ASDIV`/`ASMOD` take a right operand in AX that way - a call's result
(`x /= f()`, `y %= f()`) by inference (`p14_axint` asks).

**6. A floating zero is an ordinary constant** (`p13`): `d + 0.0` is
`.data` / `L10004:<TAB>.float 0.00000000000000000e+00` / `.text` / `lea
ax,L10004` / `call flds` / `lea ax,*-12.(bp)` / `call faddd` - kept (the
toss only sees an int `CON`), and loaded first (degree 1, `d` 0); `d - 0.0`
is `fldd d` / `lea ax,L10005` / `call fsubs`. Both refusals are gone; a
negated zero is still refused (`p16_open2` asks).

**7. A value in AX plus a constant stays in AX** (`p13`'s `return r + (int)
d - 30`): `call ftoi` / `add ax,*-42.(bp)` / `add ax,*-30.` (`optim()`'s
`- 30` as `+ -30`), no `mov ax,di` - the sum never left AX. v7's templates
compute into the register their left operand's code returned, and the MUTOS
`c1` returns AX for `ftoi`, `imul`, `idiv` and a call;
`kernel_nonopt/amx.s` shows the same for a product (`imul cx` / `add ax,di`
/ `add ax,*22.`). The `OP_PLUS` handler's AX branch now takes a constant
too, `+ 1` / `- 1` as `inc ax` / `dec ax` (as in DI, and as `p7_itofreg`'s
int converted in AX). By inference: that value stored to memory (`mov
s,ax`) or to a register variable (`mov di,ax`, where `mutos_c1` had `mov
di,ax` / `add di,*3.`); `kernel_opt/clock.s`'s `idiv cx` / `add ax,bx` /
`mov si,ax` / `add si,*-20.` is optimized code from a source not in this
repository, so it settles neither (`p14_axint` asks).

**8. Confirmed as inferred** (`p11`, `p12`, the rest of `p10`): ints
computed in DI converted (a shift, `i - 6` as `add di,*-6.`, `0 - i` as
`mov di,*0.` / `sub di,i`), a quotient
converted from AX (`idiv *-24.(bp)` / `call itof`), a product by a constant
after a `/` (`mov ax,i` / `mov cx,*3.` / `imul cx`), `i += f()` / `i -=
f()` (`call _seven` / `add *-22.(bp),ax`); an int constant into a float
(`.float<TAB>4.0...`), a negated int into a double, `-0.1` into a float
truncated (`-9.99999940395355225e-02`), a `static float`, `0.3` truncated
(`2.99999982118606567e-01`) - `p12`'s golden returns 6, the host 7;
elements of 8- and 16-byte structs subscripted by a variable (`mov cx,*3.`
/ `sal si,cl`, `mov cx,*4.`), an element as a second operand and next to
another.

**Round 4** (`tests/mutos_cc/fltprobe/`, `make -f Makefile.mutos round4`)
asks about what this round's changes infer and what is still refused:
`p14_axint` (no floating point - a value in AX plus a constant stored,
`f() + 1`, `x * y - 1`, `j + 0` / `j - 0` in DI, `a[i] * b[j]` and `a[i] &
b[j]` of int arrays, `x /= f()`, `y %= f()`), `p15_fltinf` (`(d * d) +
arr[i] + i` - the element first by its degree, i through DI; `i << 3` and
`j >> 1` converted in AX; a call's result then an element) and `p16_open2`
(refused: an int converted after `*p` and after an assignment, a remainder
converted, `-0.0`, `(int) 2.5`, `i = 2.5`, `d += c` with a char, an
`unsigned` converted - none is known to stop the real compiler, so they
share one file; a floating truth test, which does, stays out).

**Tooling.** `x86sim.py` needed nothing new: all seventeen goldens run
under it and return their programs' values (`p10` 21, `p11` 109, `p12` 6,
`p13` 42). `fltprobe/Makefile.mutos`'s `round4`.

**Verification.** `make test`: 79 files byte-exact at all four stages
(62/62 corpus, 17/17 `fltprobe`), category 7 empty, 0 mismatches, zero
warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13, 88/88 compiler
goldens assemble, `mutos_cpp` 5/5. Against the previous build (`33ec944`)
over all 88 inputs (the 79, the nine `11_kernel` refusals): identical
output and diagnostics but for `p10` and `p13`. `fuzz_c.py` against
`33ec944`: 8,000 programs (3,000 seed 1; 3,000 seed 7 `--scope`; 2,000
seed 3 scalars only), 0 WRONG, 0 BAD, 37 changed and still correct (the
dropped `+ 0` and a value kept in AX), the rest identical; 500 more (seed
11) through ASan/UBSan builds: 0 WRONG, 0 BAD. ASan/UBSan builds over the
88 inputs: no reports, the 79 byte-exact. Round 4 through `mutos_cpp`, both
passes, `mutos_as` and `x86sim.py`: `p14_axint` 129 and `p15_fltinf` 45,
the host C compiler's values; `p16_open2` stops with four `mutos_c0`
diagnostics.

## The round-4 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-02)

The three round-4 programs (`p14_axint`, `p15_fltinf`, `p16_open2`) came
back from real hardware in commit `27d0cea`, all four kinds of file. Every
`.1`/`.2` of `p14` and `p15` matched at once; their `.s` differed in two
places and one, and `mutos_c0` refused `p16` (an `unsigned` local, a cast
of a constant, `d += c`). All three are byte-exact now, the 79 others
unchanged.

**1. A right element at offset 0 is computed first** (`p14_axint`). `s =
a[i] * b[j]` (int arrays, both subscripted by a variable): `lea di,*-16.
(bp)` / `mov si,*-20.(bp)` / `sal si,*1` / `add di,si` / `mov di,(di)` /
`push di` - `b[j]`'s value onto the machine stack - then `a[i]` the same
way into DI, `mov ax,di` / `pop cx` / `imul cx`: v7's `%n,n` (SS, F), as
`05_matmul`'s 2-D elements, NOT round 3's `ps[i].c * qs[i].g` (`p10`: the
left loaded, the right's address into SI, `imul *12.(si)` - `%n,ew*`). And
`s = a[i] & b[j]`: `b[j]`'s ADDRESS pushed (`push di`), `a[i]` computed and
loaded, `pop bx` / `and di,(bx)` - v7's `%n,nw*` (SS*, F, `I *(sp)+,R`),
which cr40 shares between `+`, `-`, `|` and `&` (`ANDN` on the PDP-11).

What separates the two kinds: every right operand computed after the left
in a golden has a constant offset (`p10`'s `qs[i].g` - 12, `02_bubsort`'s
`a[j + 1]` - 2, `03_starray`'s `pts[i].y` - 2, `05_nestst`'s `*(rp + 2)`),
every one computed first has none (`p14`'s `b[j]`, `05_matmul`'s
`b[k][j]`). By v7's `degree()` both kinds have the same degree - the
constant term never raises it, and `dcalc()` compares only the degree with
the registers left - so the mechanism is not understood. A context rule
also fits `*` (`p10`'s product is returned, `p14`'s assigned; `oddreg()`
makes `*` cost one register more than `+`), but not `&` beside
`03_starray`'s `+` (both assigned, both of degree 2, only the offset
differs). `mutos_c1` follows the offset: `is_elem_read()` (a word read
through an address whose index reads a variable, with `zero_off` one at
offset 0 - `a[j + 1]`'s `+ 1` counts as an offset, v7's `insert()` makes it
a constant term), `order_right_first()` now `*` of two such reads with the
right one at offset 0 (the 2-D special case it replaces is one of them),
and `is_pushaddr()` / `ORD_PUSHADDR` for `+`, `-`, `&`, `|`: the right
element, `SEG_ADDRPUSH` (`push <its register>`), the left, `SEG_DEFPOP`
(the `x - *p` step: load the left, `pop bx`, `(bx)`), the operator; a `+`
chain's first link the same way. `fltprobe/p17_elem2` asks `return a[i] *
b[j]` (offset rule: pushed; context rule: not) and `s = ps[i].c * qs[i].d`
(the reverse), and the inferred `+`, `-`, `|`.

**2. The context in a chain is the previous term's** (`p15_fltinf`'s `(d *
d) + arr[i] + i`): the element (degree 2) first, `d * d` onto the stack,
`fadd`, then `i` straight into AX (`mov ax,*-46.(bp)` / `call itof`) - as
after any computed `*`. `plan_fchain()` had given every term after the
second the chain link's context (`+`: unchanged, DI); it now passes each
term the context the term before it leaves. The rest of `p15` matched: the
element first by its degree (round 3's rule), `i << 3` in AX (`mov cx,*3.`
/ `sal ax,cl`), `j >> 1` (`sar ax,*1`), a call's result then an element.

**3. `p16_open2`, every refusal settled:**

| Source | Golden | Now in `mutos_c0`/`mutos_c1` |
|---|---|---|
| `d = *p + i` | `mov di,p` / `lea ax,(di)` / `call fldd` / `mov di,i` / `mov ax,di` / `itof` / `fadd` | `*p` loaded before the right operand's code (`plan_fload()`), DI after it (`fafter()`) |
| `d = (e = d * 2) + i` | `flds 2.0` / `fmuld d` / `lea ax,e` / `fstd` / `mov ax,i` / `itof` / `fadd` | after an assignment, its right-hand side's context (AX after the `*`) |
| `d = d * (i % j)` | `mov ax,i` / `cwd` / `idiv j` / `mov ax,dx` / `itof` / `fmuld d` | a remainder in DX converted with `mov ax,dx` |
| `e = -0.0` | `.float 0.00000000000000000e+00` | a negated zero written as the zero (the real `printf` writes no sign) |
| `(int) 2.5`, `i = 2.5` | `.float 2.5...` / `flds` / `ftoi` | not folded: `mutos_c0` writes FCON, FTOI(0) for the cast, `mutos_c1` converts at run time |
| `d += c` (char) | `movb ax,c` / `cbw` / `itof` / `lea ax,d` / `faddd` / `fstdp d` | `+=` with a computed right operand computes it first and adds the target from memory (v7's efftab `%a,n`) |
| `unsigned u; d = u` | `mov si,u` / `sub di,di` / `push si` / `push di` / `call ltof` / `add sp,*4` | ITOF of an unsigned is v7's LTOF(ITOL): high word cleared, the long pushed as a long variable is |

`.1`: `unsigned u;` is NAME typed `UNSIGN` (7), `u = 5` an `ASSIGN(7)` of
`CON 5`; `d += c` is NAME c, `ITOC(0)`, `ITOF`, `ASPLUS(3)`; `d = u` NAME u,
`ITOF(3)`. `mutos_c0` accepts a plain `unsigned` local (not `register`,
not a pointer or array), a floating constant cast to an integer, and a
char right operand of a floating compound assignment. `-=` with a computed
right operand is now refused: by v7's table `=-` computes it first too,
which leaves `target - value` to a reversed subtraction (singles.o has
`fsubrs`), and no golden shows which. `p18_fltop3` and `p19_open3` ask.

**4. Bugs found testing `unsigned` locals** (no golden involved - each was
silently wrong code, now a refusal or the conversion v7 writes):

- **A `long` combined with an int-class variable.** `mutos_c0` typed `l +
  i` `PLUS(LONG)` but wrote no `ITOL` for `i`, and `mutos_c1` read `i` as
  the first word of a long - `add si,*-4.(bp)` / `adc di,*-6.(bp)`, two
  words at `i`'s address. The same for `-`, `*`, `/`, `%`, the comparisons
  (`l > i`), and `&`/`|`/`^` (typed `INT` with a long operand). `x = l`
  stored `l`'s HIGH word, `x = 40000` an unrendered long constant. Now:
  an int or unsigned target of a long variable gets `LTOI` (as `(int) l`
  writes it - `08_castsize.1.golden` - and v7's `build()` converts); a
  long constant into a word, an int-class variable with a long in an
  arithmetic operator or a comparison, `&`/`|`/`^` with any long, a
  compound assignment of a long into a word, and an unsigned into a long
  (which `mutos_c1`'s `ITOL` would sign-extend) are refused.
  `fltprobe/p19_open3` asks for the real shapes.
- **Octal and hex constants.** `mutos_c0` made every constant above 32767
  long; v7's `getnum()` does so only for a decimal one - an octal or hex
  constant is long above 0177777 (`(lcval>>1)>MAXINT`), K&R sect. 2.4.1.
  `ip->i_mode = 0100000` and `ip->i_mode&0170000` in the `11_kernel`
  sources are ints: their goldens have `CON -32768` and `CON -4096`. The
  lexer now records the base (`Token.is_octhex`).

**Round 5** (`make -f Makefile.mutos round5`): `p17_elem2` (no floating
point: the offset-or-context question, `+`, `-`, `|`, `^` of two elements,
a `+` chain, a comparison of two elements), `p18_fltop3` (`d += e * 2`, `f
+= i` into a float, a remainder converted after a `*`, `(long) 3.75`, a
negated zero as an initializer) and `p19_open3` (refused: the `long`
mixing above, `u = 40000`, an unsigned compared, `a[i] * b[j - 2]`, `x -
b[j]`, `d -= c`, `d -= e * 2`, an unsigned converted after a `*`).

**Tooling.** `x86sim.py` needed nothing new: all twenty goldens run and
return their programs' values (`p14` 129, `p15` 45, `p16` 79).
`fltprobe/Makefile.mutos`'s `round5`.

**Verification.** `make test`: 82 files byte-exact at all four stages
(62/62 corpus, 20/20 `fltprobe`), 0 mismatches, zero warnings; `mutos_as`
76/76, `check_floatdat.sh` 13/13, 91/91 compiler goldens assemble,
`mutos_cpp` 5/5. Against the previous build (`27d0cea`) over all 94
inputs (the 82, the nine `11_kernel` refusals and round 5): identical
output but for the round-4 and round-5 files and, in four kernel files, the
new refusals (`long` `&` in `03_mem`) and the octal constants' `.1` (`04_pipe`,
`08_tty`), a diagnostic gone where an `unsigned` local is now accepted
(`07_v24`) and one replaced (`09_amx`'s `register unsigned`); every
kernel file still refuses. `fuzz_c.py` against `27d0cea`: 14,000 programs
(seeds 1, 7 `--scope`, 3 scalars only, 21), 0 WRONG, 0 BAD, 53 changed and
still correct, 353 now correct that the old build refused (the pushed
element and the spilled product free the registers the occupancy guard
had refused on); 1,000 more (seed 31) through ASan/UBSan builds: 0 WRONG,
0 BAD. ASan/UBSan builds over the 94 inputs: no reports. Round 5 through
the whole chain: `p17_elem2` 84 and `p18_fltop3` 27, the host C
compiler's values; `p19_open3` stops with its diagnostics. Hand-written
`unsigned` programs (assignment, `+`, `*`, `&`, `==`, a call argument, to
char, to double, from int) give the host's values; the rest refuse.

## The round-5 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-03)

The three round-5 programs (`p17_elem2`, `p18_fltop3`, `p19_open3`) came
back from real hardware in commit `434c320`, all four kinds of file.
`p18` matched at every stage at once; `p17`'s `.1`/`.2` matched and its
`.s` differed in two places; `mutos_c0` refused `p19`. Now `p17` and `p18`
are byte-exact, and `p19` at `.i`/`.1`/`.2` and in its `.s` up to line
223 - after which the golden is not valid assembly (item 5). The 82 others
are unchanged.

**1. The offset decides, not the context** (`p17_elem2`). Round 4 left
open whether a right element at offset 0 is computed first because of its
offset or because of the context (`p10`'s product was returned, `p14`'s
assigned). `return a[i] * b[j]` (`f1`) pushes `b[j]` (`mov di,j` / `sal
di,*1` / `add di,b` / `mov di,(di)` / `push di` / ... / `mov ax,di` / `pop
cx` / `imul cx`), and `s = ps[i].c * qs[i].d` (`f2`) does not (`mov
di,*4.(di)` / `lea si,qs` / `mov dx,i` / ... / `mov ax,di` / `imul *6.(si)`):
the offset. Why v7's `c1` treats the two differently is still not
understood - both have the same `degree()`; in cr40/cctab order a right
operand matching `%n,ew*` is taken first, and one at offset 0 evidently is
not `ew*` on this port. `+`, `-`, `|` and a `+` chain's first link pushed
`b[j]`'s address exactly as inferred.

**2. `^` and a comparison** (`p17_elem2`): `a[i] ^ b[j]` pushes the
address as `&` does (`pop bx` / `xor di,(bx)`) - the PDP-11 rule that
`^`'s right operand be in a register (`acommute()`'s "rt. op of ^ must be
in a register", which inserts a LOAD) has no effect here. `if (a[i] >
b[j])` pushes the LEFT element's address, computes and loads the right
one, and compares through BX in the order written:

    lea di,*-10.(bp) / mov si,i / sal si,*1 / add di,si / push di
    lea di,*-16.(bp) / mov si,j / sal si,*1 / add di,si / mov di,(di)
    pop bx / cmp (bx),di / ble L10

This is cctab's `%nw*,nw*` ([move11]: FS*, S*, compare through both) -
`02_bubsort`'s `a[j] > a[j + 1]` (right one with an offset) matches the
earlier `%n,ew*` instead (`cmp di,*2.(si)`). `mutos_c1`: `is_pushleft()`
(a relational of two element reads at offset 0), `ORD_PUSHLEFT` -
`SEG_KEEPIND` (the left element stays "(di)" although `load_now()` would
load a comparison's left operand), `SEG_ADDRPUSH`, the right operand,
`SEG_DEFPOPL` (it loaded, `pop bx`, the left "(bx)" marked `memleft`), and
`emit_cmp_and_branch()` comparing `(bx)` with the register as is. A
comparison with one element at an offset keeps the old shape (no
golden - `p23_elem3` asks).

**3. `p18_fltop3`, all as inferred**: `d += e * 2` (`flds 2.0` / `fmuld
e` / `lea ax,d` / `faddd` / `fstdp d` - the right operand first), `f += i`
into a float (`mov di,i` / `mov ax,di` / `itof` / `fadds f` / `fstsp f`),
`d = (d * e) + (i % j)` (the remainder first, by its degree: `mov ax,i` /
`cwd` / `idiv j` / `mov ax,dx` / `itof`, then `fldd d` / `fmuld e` /
`fadd`), `(long) 3.75` (`flds` / `ftol` / `mov di,dx` / `mov si,ax`),
`double gz = -0.0` (`_gz: .double 0.0...`).

**4. `p19_open3`** - every refusal settled but one:

| Source | Golden | Now in `mutos_c0`/`mutos_c1` |
|---|---|---|
| `l + i`, `i + l` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `add si,l+2` / `adc di,l` (both) | `.1`: NAME l, NAME i, ITOL, PLUS(6) - and NAME i, ITOL, NAME l, PLUS; `c1`: the widened int first (acommute(): ITOL of a variable has degree 2, a NAME 0) |
| `r + (int) (l - 100000)` | `mov di,l+2` / `add di,r` / `add di,#31072.` | `.1`: NAME l, LCON, MINUS(6), LTOI(0); `c1`: `VK_LOWADD` - v7's `unoptim()` distributes the LTOI (low word of l, low word of the LCON); the low word first, the variable, the constant last |
| `if (l > i)` | `mov ax,i` / `cwd` / `cmp dx,l` / `bgt L4` / `blt L10000` / `cmp ax,l+2` / `bhis L4` / `L10000:` | v7's optim() swaps (ITOL degree 2 > NAME 0): `i < l`, branch when false: `longrel()`'s `lrtab[0]` for GREATEQ |
| `if (l == i)` | ... `cmp dx,l` / `bne L5` / `cmp ax,l+2` / `bne L5` | NEQUAL: `bne` both |
| `l += i` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `add l+2,si` / `adc l,di` | `.1`: NAME l, NAME i, ITOL, ASPLUS(6) |
| `l = u` | `mov si,u` / `sub di,di` | ITOL of an unsigned: the high word cleared (`itolu[]` - which ITOLs take an unsigned, from the pre-scan) |
| `l = m & 255`, `m \| 6` | `mov ax,#255.` / `cwd` / `push ax` / `push dx` / `mov si,m+2` / `mov di,m` / `pop bx` / `pop cx` / `and si,cx` / `and di,bx` | `.1`: CON 255, ITOL, AND(6) - the constant widened at run time, `c + 1L`'s shape (`gen_long_constop()`, "pop cx" with its space) |
| `u = 40000` | `mov u,#-25536.` | `.1`: LCON(0, -25536), LTOI(7) |
| `r + (u > 39999)` | `mov si,u` / `sub di,di` / `push si` / `push di` / `mov si,#-25537.` / `mov di,*0.` / `pop cx` / `pop bx` / `cmp di,cx` / `blt` / `bgt` / `cmp si,bx` / `blo` / ... 0/1 | `.1`: NAME u, ITOL, LCON, GREAT; swapped (ITOL of an unsigned variable: degree -1 < 0) to `39999 < u`, the widened u pushed, the constant loaded, u popped into CX:BX (cctab's `%nl,nl`: SS, F); a value, branching when true |
| `a[i] * b[j - 2]` | `mov di,(di)` / `lea si,b` / `mov dx,j` / `sal dx,*1` / `add si,dx` / `mov ax,di` / `imul *-4.(si)` | the index through DX, added to the array's address, the constant term the displacement (not folded into the `lea`, as v7's acommute() would) |
| `x - b[j]` | `lea di,b` / ... / `push di` / `mov di,x` / `pop bx` / `sub di,(bx)` | `is_pushaddr()` for `-` with a variable on the left; `SEG_DEFPOP` loads it |
| `d -= c` | `lea ax,d` / `call fldd` / `movb ax,c` / `cbw` / `call itof` / `call fsub` / `lea ax,d` / `call fstdp` | the target loaded FIRST (as for `/=`), no reversed subtraction - `+=` alone computes its right operand first |
| `d -= e * 2` | `fldd d` / `flds 2.0` / `fmuld e` / `fsub` / `fstdp d` | the same |

`mutos_c0` writes the conversions where v7's `build()` does - right after
the int-class operand's own bytes (`mix_rhs_end()`/`emit_itol()`), a
constant too (`m & 255`'s CON, ITOL - v7's `c0` folds nothing for a long;
`c1`'s `lconst()` folds later); a char with a long is refused (no
golden). A parenthesized long cast to int (`(int) (l - 100000)`) is the
tree and LTOI. `mutos_c1`'s long comparison (`gen_long_relop()`/
`gen_long_cmp()`) is v7's `longrel()`/`xlongrel()` table for a comparison
not with an ITOL of 0 (`lrtab[0]`), which also reproduces `01_addsub`'s
`c > 0L`; the operand shapes are the three goldens' only.

**5. The real compiler's output is invalid for `d = (d * e) + u`.** After
`fldd d` / `fmuld e` the golden has

    mov  ax,*-36.(bp)
    mov  <118 bytes>,ax
    sub  ax,ax
    push <the same 118 bytes>
    push ax
    call ltof

where the 118 bytes are `08 08 08 08`, eighteen spaces, `08`, `10` x15,
`04` x10, `10` x7, `AAAAAA`, `01` x20, `10` x6, `BBBBBB`, `02` x20, `10`
x4, a space - libc's `_ctype_` character-class table (the entries from
`'\n'` up), read as a string where a register name should be. The intended
code is `p16_open2`'s `mov si,u` / `sub di,di` / `push si` / `push di` for
an unsigned made a long (v7's `unoptim()`: ITOF of an unsigned is
LTOF(ITOL)); in AX context (after a computed `*`, `/` or call) the code
table asks for the register pair starting at AX, and the register-name
table has no entry after AX. `mutos_as` stops at line 229 ("could not
classify operands"); whether the real `as` assembles it is not known
(asked in `STATUS.md`'s "Next up"; settled 2026-10-07: it does not, see
"The real assembler on `p19_open3.s`" below). There is nothing meaningful to
reproduce: `mutos_c1` refuses an unsigned converted after a computed
operand, with a diagnostic that says why, for good.

The golden itself stays as it came back (CLAUDE.md's Golden Master
Integrity). `tests/mutos_cc/invalid_goldens.txt` lists it with the
number of valid leading lines (223); `run_goldens.sh` checks a listed file
at `.i`/`.1`/`.2` as usual, then requires `mutos_c1` to refuse it and its
output up to the refusal to be exactly those lines - category 8 -, and
`tests/mutos_as/assemble_cc_goldens.sh` requires `mutos_as` to refuse it
(a listed file that assembles is a failure: the list is out of date).

**6. Silent miscompiles found while testing `long`** (no golden involved;
found by hand-written programs run through `x86sim.py` against the host
C compiler):

- two long variables compared (`l > m`, `l == m`) - `c0`'s
  `long_mix_refused()` only looked at mixed pairs, and `c1` compared the
  high words only (`mov di,m` / `cmp l,di`). `mutos_c1` now notes every
  comparison with a long operand in the pre-scan (`lrel[]` - a Val keeps
  no type) and sends it to `gen_long_relop()`, which refuses the shapes
  no golden shows;
- a long tested for truth (`if (l)`, `!l`, `l && i`, `l ? a : b`): the
  high word only (`cmp l,*0`); refused in the pre-scan (v7's `longrel()`
  would test both words with `tst`);
- `~l`, `l << n`, `l >> n` and `x ? l : m`: typed int by `mutos_c0`, one
  word each; refused in `mutos_c0`;
- `l = -65536;` stored 0: `mutos_c0` folded `-` of a long constant to a
  truncated int. v7's `fold()` folds CONs only, so its `c0` writes LCON,
  NEG(LONG); `mutos_c0` does the same now, and `mutos_c1` folds NEG and
  COMPL of an LCON (v7's `unoptim()`).

A safety net came with it: `put_insn_ex()` refuses any operand
`render_operand()` writes as a `<...>` placeholder (an unsupported value
reaching an instruction) - such a slip once reached the output unnoticed
(`cmp *-8.(bp),<unmaterialized-long-const>`, 2026-09-26).

**Round 6** (`make -f Makefile.mutos round6`): `p20_fltlv` (floating
lvalues beyond variables: struct members, pointers to double subscripted,
with an offset and incremented, compound assignments into elements,
`&d`, a function returning `double *`), `p21_fltexp` (chained assignment,
`++`/`--`, char operands, casts of computed ints, `-i`, a long sum
converted, `(unsigned) d`, `?:` and comma with doubles, an assignment as
an argument), `p22_long2` (the `long` shapes still refused or inferred)
and `p23_elem3` (one element with an offset compared, `x * b[j]`, a leaf
minus an element, `b[j + 1]` of a local array). All four refused today;
their results (128, 60, 83, 136) are the host's.

**Tooling.** `x86sim.py` executes `adc`/`sbb` (the carry of the `add`/
`sub` immediately before - anything else between is an error) and the
unsigned branches `blo`/`bhis`: 55 of the 62 corpus goldens run (`02_long/
01_addsub` 11009 and `03_retval` -31067 newly), 22 of the 23 `fltprobe`
goldens (`p17` 84, `p18` 27); `p19`'s first 223 lines completed with `r =
r + (int) d` return 104, the host's value for that program.

**Verification.** `make test`: 84 files byte-exact at all four stages
(62/62 corpus, 22 `fltprobe`), `p19_open3` in category 8, 0 mismatches,
zero warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13, 93/93
compiler goldens assemble and `p19_open3`'s is refused as listed,
`mutos_cpp` 5/5. Against the previous build (`434c320`) over all 98
inputs: identical output but for `p17`, `p19`, the round-6 files'
diagnostics and three kernel files (`03_mem`'s two long `&` converted now
- its partial `.1` changes -, the cast diagnostic reworded in `07_v24` and
`09_amx`); every kernel file still refuses. `fuzz_c.py` against `434c320`:
14,000 programs (seeds 1, 7 `--scope`, 3 scalars only, 21), 0 WRONG, 0
BAD, 69 changed and still correct, 632 now correct that the old build
refused; 1,000 more (seed 31) through ASan/UBSan builds: 0 WRONG, 0 BAD.
ASan/UBSan builds over the 98 inputs: no reports. 79 hand-written
programs (long mixing, element comparisons, negated long constants,
floating shapes): every one compiled gives the host's value.

## The round-6 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-04)

The four round-6 programs (`p20_fltlv`, `p21_fltexp`, `p22_long2`,
`p23_elem3`) came back in commit `2540271`, all four kinds of file - and,
for `p21`, with two messages from the real compiler. All four had been
refused (`mutos_c0` refused `p20`/`p21`/`p22`, `mutos_c1` `p23`). Now all
four are byte-exact at every stage, `p21` together with the real
compiler's own two messages and its exit status; and `11_kernel/
01_delay`, the first of the nine kernel files, is byte-exact too. The 84
files byte-exact before are unchanged.

**1. `p21`'s messages: the real `c1` counts the floating-point stack.**
`cc -S p21_fltexp.c` printed

    56: floating point stack underflow
    57: Floating point stack underflow
    *** Error code 1

and wrote the whole `.s` all the same. Line 56 is `f = half(d = 3.0);` -
an assignment whose value is a floating call argument - and the golden's
code for it is wrong:

    lea ax,L10013 / call flds          3.0 onto the stack
    lea ax,*-12.(bp) / call fstdp      stored into d - and POPPED
    sub sp,*8 / mov ax,sp / call fstdp the argument: popped again
    call _half / add sp,*8. / call fldd / lea ax,*-28.(bp) / call fstdp

A statement-level store pops (`fstdp`), a store whose value is used keeps
it (`fstd` - `d = e = 2.5` is `fstd e` / `fstdp d` in the same golden);
for an assignment under a call the real compiler picks the popping one,
and the argument push then pops a value that is no longer there. At run
time the stack underflows (`x86sim.py` stops there: "'fstdp' with an
empty floating-point stack"; with line 250 made `fstd`, the program
returns 60, the host's value). At compile time the real `c1` noticed:
it keeps a model of the runtime stack's depth and reports a pop below the
bottom. "*** Error code 1" is `make`'s: `c1` exits with status 1 when it
reported errors (v7's `c11.c` `error()` counts them and goes on; `c10.c`
ends with `exit(nerror!=0)`), and `cc -S` keeps the `.s` it wrote, since
with `-S` that file is the output itself, not a temporary that `cc`'s
`dexit()` would remove. So the `.s` is a complete, valid golden - of code
that does not work.

The two messages differ in case, so they come from two places in the
real `c1`. The model `mutos_c1` now runs (`fp_track()`, called for every
`call` it emits) is the simplest one that gives exactly these two lines
and nothing else on any other floating golden:

- a load (`flds`/`fldd`, a call's result `fldd`), `itof`, `ltof` and
  `fdup` push one value;
- a store with a pop (`fstsp`/`fstdp`) pops one and reports "floating
  point stack underflow" (lower case) below 0 - line 56's store into
  `f`;
- `ftoi`/`ftol` pop one and report "Floating point stack underflow" (upper
  case) - line 57's `ftoi` (`r = r + f * 2.0 + d`), a balanced statement
  reporting because the model was still one short after line 56;
- the argument push (`sub sp,*8` / `mov ax,sp` / `fstdp`) pops one
  without a check: line 56 has one message, not two;
- `fadd`/`fsub`/`fmul`/`fdiv` pop one, `fcmp` two, checked with the
  upper-case message (an inference: round 1's `p1_compare` notes "two
  per comparison" for `if (d)`, which the real `cc` reported; no golden
  shows the case);
- the model starts at 0 in every function (an inference - `p21`'s
  underflow is in its last function) and is not reset inside one.

`mutos_c1` writes the same code, prints the same two lines (`c1_error()`:
"<line>: <text>", the line from the statement's EXPR), finishes the `.s`
and exits with status 1. `tests/mutos_cc/c1_errors.txt` lists the
expected messages; `run_goldens.sh` requires, for a listed file, a
nonzero exit status, exactly those messages and the golden `.s` - its new
category 9. A correct program never goes below 0, so `mutos_c1` reports
nothing for one; at the end of each statement it checks that the model is
back at 0 (or below 0 only after a reported underflow) and stops with an
internal error otherwise, so the model is checked against every floating
golden by `make test`. The same wrong code with the call's result unused
(`half(d = 3.0);` as a statement: one below the bottom and nothing
reported yet) is refused with a diagnostic, not an internal error - what
the real `c1` reports later is what `p28_fltstk` asks.

**2. Floating lvalues beyond variables** (`p20_fltlv`):

| Source | Golden |
|---|---|
| `q->x += 0.5`, `a[i] += 1.5` | `mov di,q` / `lea ax,(di)` / `\|` / `push ax` / `call fldd` / `lea ax,L10004` / `call fadds` / `pop ax` / `call fstdp` - the target's address kept on the machine stack, an empty `\|` comment line before it (v7's code table for a compound assignment through a register) |
| `a[i] *= s.y` | `lea ax,s.y` / `call fldd` - the right operand first - then the element's address, `lea ax,(di)` / `\|` / `push ax` / `call fmuld` / `pop ax` / `call fstdp` |
| `*p = *p * 2.0` | `mov di,p` / `lea ax,(di)` / `call fldd` / ... / `call fmuls` / `mov ax,p` / `mov bx,ax` / `lea ax,(bx)` / `call fstdp` - after a `*` the pointer goes through AX (the register an int is converted in after a `*` - `fafter()`) |
| `q->y = q->x + 1.5` | ... `call fadds` / `mov di,q` / `lea ax,*8.(di)` / `call fstdp` - DI after a `+` |
| `p++` (`double *p`) | `add *-48.(bp),*8.` - a statement-level `++` in place |
| `return a + i` (`double *pick()`) | `mov di,i` / `mov cx,*3.` / `sal di,cl` / `add di,a` / `mov ax,di` |
| `*pick(a, 2)` | `call _pick` / `add sp,*4.` / `mov bx,ax` / `lea ax,(bx)` / `call fldd` |
| `twice(&d)` | `lea di,d` / `push di` / `call _twice` |

`mutos_c0` accepts a pointer to a double (as a parameter, a local, a
function's return type - one more degree stays refused), floating
compound assignments to members and elements (`ASPLUS(3)` after `NAME q,
CON 0, PLUS, STAR(3)`) and a double stored through a pointer
(`ASSIGN(3)` - a float target written DOUBLE, as for a variable).
`mutos_c1`: `SEG_FSTREG` (the register a store through a pointer is
addressed in), `gen_fp_asop()`'s `FM_IND` path, a statement-level
`++`/`--` in place.

**3. Floating expression forms** (`p21_fltexp`, all as inferred but the
messages): `d = e = 2.5` (`fstd e` / `fstdp d`); `d++` and `++d` as
statements (`fldd d` / `lea ax,L10002` / `fadds` / `fstdp d` - the 1.0 a
`.float`, `mutos_c0`'s `ITOF` of `CON 1`); `e = d--` (`fldd d` / `call
fdup` / `fsubs` / `fstdp d` / `fstdp e` - the old value kept under the new
one); `d + c` (`movb ax,c` / `cbw` / `itof` / `faddd d` - the char
first), `c * 2.5`; `(double) (i + j)` (`mov di,i` / `add di,j` / `mov ax,di`
/ `itof`); `d = -i` (`mov di,i` / `neg di` / `mov ax,di` / `itof` - `NEG`
typed INT); `e = l + 1` (the long sum in DI:SI, then `push si` / `push di`
/ `call ltof` / `add sp,*4`); `u = (unsigned) d` (`fldd d` / `ftol` /
`mov u,ax` - v7's `unoptim()`: FTOI to an unsigned is LTOI(FTOL)); `x ? d
: e` (each arm `fldd`, `jmp` between - `plan_quest()`'s third kind of
arm); `g = (i = 2, d - 40000.0)` (a comma typed DOUBLE: the left operand
for its effect, the constant's `.data` block where its `-` is matched);
`r + u - 39990` (an unsigned widened, then only the low words:
`mov di,r` / `add di,u` / `add di,#25546.` - Val's `lowonly`).

**4. `long` shapes** (`p22_long2`, every refusal and inference settled):

| Source | Golden |
|---|---|
| `l - i` | `mov ax,i` / `cwd` / `push ax` / `push dx` / `mov si,l+2` / `mov di,l` / `pop bx` / `pop cx` / `sub si,cx` / `sbb di,bx` - v7's `%nl,nl`, the widened int pushed (`c + 1L`'s shape, "pop cx" with its space) |
| `i - l` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `sub si,l+2` / `sbb di,l` |
| `l + 1`, `l ^ 7` | `mov ax,*1.` / `cwd` / `push ax` / `push dx` / ... / `add si,cx` / `adc di,bx` - a constant that fits an int is ITOL(CON) (v7's `getree()`) |
| `l - 2` | `add si,*-2.` / `adc di,*-1.` - "x - c" is "x + -c" (`optim()`), and a negative constant folds back into an LCON (`unoptim()`) |
| `l -= i` | `mov ax,i` / `cwd` / `mov di,dx` / `mov si,ax` / `sub l+2,si` / `sbb l,di` |
| `l *= i` | `mov ax,i` / `cwd` / `push ax` / `push dx` / `lea di,l` / `push di` / `call almul` / `add sp,*6.` - libc's in-place multiply |
| `l / i`, `l % i` | `mov ax,i` / `cwd` / `push ax` / `push dx` / `mov di,l+2` / `push di` / `mov di,l` / `push di` / `call ldiv` (`lrem`) / `add sp,*8.` |
| `x = 40000` | `mov x,#-25536.` |
| `l < 0L`, `l >= 0L`, `l > 2`, `l > 0` | `cmp l,*0` / `bgt`/`blt` / `cmp l+2,*0.` (or `*2.`) / `bhis`/`blo`/`blos` - v7's `lrtab[0]` for all four: no `tst` for a widened 0 |
| `x = l > 0L` | the same branches, `mov di,*0.` / `jmp` / `mov di,*1.` |
| `i < l` | `mov ax,i` / `cwd` / `cmp dx,l` / `bgt` / `blt` / `cmp ax,l+2` / `bhis` |
| `if (l)`, `if (!l)` | `mov si,l+2` / `mov di,l` / `cmp di,*0` / `bne` / `cmp si,*0` / `beq` (`!l`: `bne` both) - v7's `cbranch()` on a long |
| `u < l` | `mov si,u` / `sub di,di` / `push si` / `push di` / `mov si,l+2` / `mov di,l` / `pop cx` / `pop bx` / `cmp di,cx` ... - swapped to `l > u` |
| `l > m`, `l == m` | `mov si,l+2` / `mov di,l` / `cmp di,m` / ... / `cmp si,m+2` |
| `l = -100000` | `mov si,#31072.` / `mov di,*-2.` (LCON, NEG folded) |
| `~l` | `not di` / `not si` |
| `l << 2`, `l >> 1` | `sal si,*1` / `rcl di,*1` (twice); `sar di,*1` / `rcr si,*1` |
| `x ? l : m` | each arm into DI:SI |
| `x = (int) (l - 5)` | `mov di,l+2` / `add di,*-5.` / `mov x,di` |
| `(long) u`, `l & m` | `mov si,u` / `sub di,di`; `and si,m+2` / `and di,m` after loading l |

`mutos_c0` types `~l`, `l << n`, `l >> n` and a `?:` with long arms LONG
now (they were refused as silent int miscompiles in round 5), widens a
shift count (ITOL) and writes `(long) e` as ITOL. The expectation in
`p22`'s header that `l > 0` would be a `tst` (v7's `longrel()` for an
ITOL of 0) was wrong: the real compiler compares a widened constant like
any other.

**5. Element shapes** (`p23_elem3`): a left element with an offset
compared with one at offset 0 keeps the offset as the displacement off BX
(`pop bx` / `cmp *4.(bx),di`); `a[i] > ps[i].c` is cctab's `%n,ew*` (`cmp
di,*4.(si)`); `x * b[j]` loads the element into AX and multiplies by the
variable (`mov ax,(di)` / `imul x`); `x - *p` pushes the pointer (`push p` /
`mov di,x` / `pop bx` / `sub di,(bx)`), and so do `5 - b[j]` and `g - b[j]`
the element's address; `b[j + 1]` is `mov di,*2.(di)` after the index,
`a[i] + b[j + 1]` takes the index through DX (`lea si,b` / `mov dx,j` /
`sal dx,*1` / `add si,dx` / `add di,*2.(si)`); `(a[i] < b[j]) + (a[i] >
b[j]) * 2` computes the right term first and pushes it (`sal di,*1` / `push
di` / ... / `pop bx` / `add di,bx` - `is_relsum()`, `ORD_SPILL`); and `b[j]
> 0` under `&&` loads the element and tests it, `mov di,(di)` / `or di,di`
/ `ble` (v7's `rcexpr()` makes `x > 0` a test, and a STAR is computed into
a register for it - Val's `cond_ortest`). Through a pointer variable or at
a constant offset the dereference stays an operand (`cmp (di),*0`, as in
the kernel) - whether an element subscripted by a variable tested for
truth (`if (b[i])`) is also loaded first is what `p26_elem4` asks.

**6. `11_kernel/01_delay` byte-exact.** Its `i++;` (a statement of its
own) is `inc *-6.(bp)` - v7's `efftab`: an increment whose value is
unused, in place; `while (d--)` is `mov di,d` / `dec d` / `or di,di` /
`beq`. `mutos_c0` did not accept a bare `i++;`/`++d;` statement before
(`p21`'s `d++;` needed it too), and `mutos_c1` would have computed the
unused value; with both settled, the first kernel file is byte-exact at
all four stages.

**7. Inferred, and round 7.** `mutos_c1` compiles, with no golden yet:
`e = ++d` (`fstd`, the new value kept), a `?:` with constant arms or a
comparison as its condition, `-=`/`/=` through a pointer to a struct, `l
| m`/`l ^ m`, `x = (int) (l + 5)`, `l += 1`, `l = c`, `l / 7`, `x * *ip`,
`if (b[i])` and other element tests, and the stack model's inferred
parts. Round 7 (`make -f Makefile.mutos round7`) asks: `p24_fltinf2`,
`p25_long3` and `p26_elem4` (those inferences, and the refusals left:
`(x ? d : e) * 2.0`, `q->x += d * e`, a double element by `i + 1`,
`l << 3`, `-l`, a long under `&&`/`||`/`?:`, `l++`, `m = l = 5`, `l * 3`,
int compound assignments into elements - `b[i] += b[j]` - and through
pointers), `p27_frame` (frames of 82 to 127 bytes: `sub sp` or `chkstk`
- the gap no golden has shown) and `p28_fltstk` (not a program to run: the
floating-stack model after `half(d = 3.0)` - reset per function, the
argument push and an unused result, `fmul`/`fcmp` below the bottom,
`return d`).

**Tooling.** `x86sim.py` models `libc.a`'s `long` helpers (`lmul`,
`ldiv`, `lrem`, `almul` - operands on the machine stack, the result in
DX:AX), `fdup`, `neg`, and a long shifted through the carry (`rcl`/`rcr`
right after a one-bit `sal`/`sar`): 56 of the 62 corpus goldens run
(`02_long/02_muldiv` 24 newly), and 25 of the 27 `fltprobe` goldens (`p20`
128, `p22` 83, `p23` 136; `p21` 60 with its doubly popped store made
`fstd`).

**Verification.** `make test`: 88 files byte-exact at all four stages
(62/62 corpus, 25 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp` in category 9 (two messages, exit status 1, the
`.s` byte-exact), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 97/97 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`2540271`) over all 98 golden inputs: identical output but for the
round-6 files, `11_kernel/01_delay` and, in six other kernel files, later
diagnostics and partial `.1` output (constructs accepted now further
down; each still refuses, at the same first error). `fuzz_c.py` against
`2540271`: 22,000 programs (seeds 1, 7 `--scope`, 3 scalars only, 21), 0
WRONG, 0 BAD, 172 changed and still correct, 104 now correct that the old
build refused; 1,500 more (seed 31) through ASan/UBSan builds: 0 WRONG, 0
BAD. ASan/UBSan builds over the 98 golden inputs, the five round-7 probes
and the hand-written programs: no reports, output identical. 158
hand-written programs (members and pointers to double, compound
assignments into elements, `++`/`--`, conversions, `?:`, comma, every
`long` shape above and its neighbours, element orders): the 133 compiled
give the host C compiler's value; the rest refuse with a diagnostic.

## The round-7 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-04)

The five round-7 programs (`p24_fltinf2`, `p25_long3`, `p26_elem4`,
`p27_frame`, `p28_fltstk`) came back in commit `bdb2728`, all four kinds of
file, with the real compiler's messages for `p28` (`round7.log`). All five
had been refused (`mutos_c0` refused `p24`/`p25`/`p26`, `p27` differed in one
line number and was refused for its frames, `mutos_c1` refused `p28`). Now
`p24`..`p27` are byte-exact at every stage, and `p28` together with all
twelve of the real compiler's messages; the 88 files byte-exact before are
unchanged.

**1. An `EXPR`'s line is the line of the token that ends the expression.**
`p27_frame.1.golden`'s `return f82() + ... +` / `f127();` (lines 86 and 87)
has `EXPR 87`; `mutos_c0` wrote 86, the line the statement starts on. v7's
`statement()` and `doret()` call `rcexpr(tree())` - and `rcexpr()` writes
`EXPR` with the lexer's current `line`, which is where `tree()` stopped:
at the `;` it peeked. `mutos_c0` now writes the `;`'s line for every
expression statement (`return`, an assignment, a store through a pointer,
a call or `++`/`--` statement), the `;` after a `for`'s initialization and
the `)` after its increment (`forstmt()`'s `sline = line` right after
`tree()` - the increment's `EXPR` keeps that line although it is written
after the body). A condition's `CBRANCH` already took the lexer's line (the
token after `)` for an `if` - `p28`'s line-65 messages are the `if` on line
64). No other golden had an expression spanning lines, so nothing else
changed.

**2. Frames from 82 to 127 bytes** (`p27_frame`): 82 and 90 bytes are `sub
sp,*82.` / `sub sp,*90.`; 100, 110, 120, 124 and 126 are `mov ax,*100.` /
`call chkstk` (the immediate as any other: `*` up to 127); `char buf[127]`
is rounded to 128, `mov ax,#128.`. The switch lies in `(90,100]`:
`MCC_SUBSP_MAX` is 90 and `MCC_CHKSTK_MIN` 100 now, and the frames 92, 94,
96 and 98 (a frame is even) are refused - round 8's `p29_frame2` asks.

**3. A sum of calls.** `p27`'s `main()` returns `f82() + f90() + ... +
f127()`:

    call _f127 / push ax / call _f126 / push ax / ... / call _f90 / push ax
    call _f82 / pop bx / add ax,bx / pop bx / add ax,bx / ... (seven times)

`acommute()` keeps calls (equal degree) in source order and rebuilds the
chain left-deep; each `+` then has a call on its right, which no register
can hold across the next call, so v7's cr40 `%n,n` computes it first and
pushes it (`SS`), computes the left operand (`F` - the call's value stays in
AX) and adds the popped one (`I (sp)+,R`: `pop bx` / `add ax,bx`).
`is_callsum()` makes such a `+` (its left operand a call or such a sum)
`ORD_SPILL`; `OP_PLUS`'s pushed-right-operand path takes a left operand in
AX as well as in DI. `p28`'s `main()` is the same chain of four int calls,
then `call itof` / `call _f5` / `call fldd` / `call fadd` / `call ftoi` - a
double function's result added on the floating-point stack.

**4. `p28`'s twelve messages: the stack model is one per file, and a store
under a call is not always wrong.** `cc -S p28_fltstk.c` printed

    49: floating point stack underflow
    50: Floating point stack underflow       (twice)
    59: floating point stack underflow
    60: floating point stack underflow
    61: floating point stack underflow
    62: floating point stack underflow
    65: Floating point stack underflow       (twice)
    73: floating point stack underflow
    79: Floating point stack underflow       (twice)

and the code says how the model goes on. In `f1`, `x = half(d = 3.0);`
stores `d` WITHOUT a pop (`fstd`) and the argument push pops the value -
right code, no message; in `f3`, `f4` and `f5` the same assignment under an
unused call (`half(d = 3.0);`) is stored with a pop and popped again, as
`p21`'s `f = half(d = 3.0)` was. Counting with the round-6 model, every
message falls out once three things change:

- the model is NOT reset between functions: `f3` ends two below the bottom
  (line 48 silent - the unused result pops nothing, the argument push is
  unchecked - line 49's store reports, line 50's `fmul` and `ftoi` report),
  and `f4`'s `d = 1.0;` at line 59 - a balanced statement - reports because
  it starts two short; `main()`'s line 79 (`fadd`, `ftoi`) is still short;
- a double function's returned value, stored into `fac` (`lea ax,fac` /
  `call fstdp`), pops without a check, like the argument push: `f5`'s
  `return d;` at -5 is silent (`half()` must pop it, or every count after
  it is one off);
- `fmul`, `fadd` and `fcmp` (twice) below the bottom report with the
  upper-case message, as round 6 inferred for them (lines 50, 65, 79).

`fp_track()` no longer resets at `SAVE`, `gen_fp_rforce()` sets
`fp_nocheck`, and the end-of-statement check compares the model with where
the statement started, one lower per double pop (`fstart`,
`stmt_argpops`) - a statement may now end below 0 silently, as line 48 does.
Which store the real compiler picks under a call is inferred from the
three statements there are: `fstdp` when the statement's own value is
floating (`p21`'s `f = ...`, typed DOUBLE; `half(...);`, a double call),
`fstd` when it is not (`f1`'s `x = ...`, an int `ASSIGN` of an `FTOI`) -
`stmt_ftop`. The other reading is a state the previous floating statement
leaves behind: `f1`'s line 29 follows `half()`'s `return`, a value
computed into the register table. Round 8's `p30_fltstk2` separates the
two (an int statement right after a floating one, a call compared, a
double function's `return half(d = 3.0)`, two such arguments).
`tests/mutos_cc/c1_errors.txt` lists all twelve messages;
`run_goldens.sh` checks them in its category 9.

**5. Int elements** (`p26_elem4` - all its inferences checked, these corrected
or settled):

| Source | Golden |
|---|---|
| `if (b[i])`, `if (ps[i].c)` | `lea di,b` / ... / `add di,si` / `mov di,(di)` / `or di,di` / `beq` (`mov di,*4.(di)` with the member's offset) - an element read through a computed address tested for truth is loaded and tested, as one compared with 0 (`b[j] != 0`, round 6); `mutos_c1` had inferred `cmp (di),*0`, the shape through a pointer variable |
| `x = b[i] * b[j] + b[j - 1]` | the product (`b[j]` pushed first) / `mov di,ax` / `lea si,b` / `mov dx,j` / `sal dx,*1` / `add si,dx` / `add di,*-2.(si)` - the left operand moved into the working register before the right element's address is computed (v7's `%n,ew*`: `F`, then `S*` into the next register) |
| `x = (b[i] + 1) * b[j]` | `b[j]` loaded and pushed / `mov di,(di)` / `inc di` / `mov ax,di` / `pop cx` / `imul cx` - the element at offset 0 first, as when the left operand is an element |
| `x = b[j] / b[i]`, `b[j] % b[i]` | `b[i]` loaded and pushed / `b[j]` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx` (the remainder `mov x,dx`) |
| `r = r + (b[i] < b[j]) + (b[i] > b[j]) * 2 + (b[i] == b[j]) * 4` | C (pushed in both arms) / B (pushed in both arms) / A / `sal di,*1` / `pop bx` / `add di,bx` / `sal di,*1` / `pop bx` / `add di,bx` / `add di,r` - see below |
| `b[i] += b[j]`, `b[j] -= x` | `b[i]`'s address / `push di` / `b[j]` loaded, or `mov di,x` / `pop bx` / `add (bx),di`, `sub (bx),di` |
| `b[0] *= b[i]` | `b[i]` / `mov di,(di)` / `mov ax,di` / `imul *-28.(bp)` / `mov *-28.(bp),ax` |
| `*ip += 2` | `mov di,ip` / `add (di),*2.` |
| `ps[i].c += x` | `mov di,x` / `lea si,ps` / `mov dx,i` / `mov cx,*3.` / `sal dx,cl` / `add si,dx` / `add *4.(si),di` - the right-hand side first, as a store through "pointer + constant" (`is_disp_store()`) |
| `r + b[0] + b[1] + b[2] + ps[i].c` | the member first (`mov di,*4.(di)`), then `add di,*-28.(bp)` / `*-26.` / `*-24.` / `add di,r` |

The sum of three comparisons is v7's `distrib()` at work. `acommute()`
collects the terms by degree - the comparisons ahead of `r`, as written:
C, B\*2, A\*4, r - and `distrib()` divides A\*4 by B\*2's constant ("c1\*y +
c1\*c2\*x -> c1\*(y + c2\*x)"): C, (A\*2 + B)\*2, r. That list, rebuilt
left-deep, would compute the factored term first (the right operand of the
first `+`); the golden computes C first. The second `optim()` explains it:
`rcexpr()`'s `reorder()` optimizes the tree again before any code, and that
`acommute()` sees (A\*2 + B)\*2 with a higher degree than C - its inner sum
of two equal-degree terms is one more (the MUTOS degree that already
explained `03_starray`'s `sum` added last), and a TIMES by a power of two
keeps its operand's - so the list becomes (A\*2 + B)\*2, C, r. Each `+`
then takes cr40: a right operand computed with branches goes first, onto
the stack (`%n,n`), a variable is added from memory. Pushing a comparison's
0/1 onto the stack puts the `push di` into each arm (`cexpr()` compiles a
relational value as `cbranch()` and then `czero`/`cone` through the
requested table - `sptab`): `cmp (bx),di` / `blt L10002` / `mov di,*0.` /
`push di` / `jmp L10003` / `L10002:mov di,*1.` / `push di` / `L10003:`.
`mutos_c1` reproduces it for an int `+` chain of comparisons, two or more of
them times a power of two, and int variables (`is_distrib()`,
`plan_distrib()`: insert(), distrib() and the second acommute() on a small
tree of the terms; `SEG_SHIFT` for the rebuilt multiples, `plan_spill()`
pushing a comparison in both arms). `p23`'s two-term `(a[i] < b[j]) + (a[i]
> b[j]) * 2` is consistent with it: equal degrees, as written, the scaled
term on the right pushed first.

Constant-index elements are leaves: `enode_degree()` gives `*(&b + 0)` -
`b[0]` - degree 0, as v7's `optim()` folds it into the NAME, so the last
row's elements are added in order after the member and before `r`.
`mutos_c0` accepts `*p op= expr` (`NAME ip, STAR(0), CON 2, ASPLUS(0)` -
the golden's tree); `mutos_c1` the in-place compound assignments into an
element or through a pointer (`is_aspush()`/`SEG_ASPOP`, `is_asdisp()`), and
`*=` into a constant-index element with an element on the right.

**6. `long` shapes** (`p25_long3`):

| Source | Golden |
|---|---|
| `l += 1`, `l++`, `++l` | `add *-6.(bp),*1.` / `adc *-8.(bp),*0` - in place; `l--`: `sub` / `sbb ...,*0` (the high word's constant written `*0`, as in `cmp di,*0`) - `mutos_c0` writes the 1 widened (`CON 1, ITOL, INCAFT(6)`) |
| `l = -l` | `mov si,l+2` / `mov di,l` / `neg di` / `neg si` / `sbb di,*0` |
| `l = l << 3`, `l = l >> 4` | `mov cx,*3.` / `sal si,*1` / `rcl di,*1` / `loop .-4` (`sar di,*1` / `rcr si,*1` for `>>`) - from 3 up, as an int is shifted by CL |
| `if (l && i)`, `if (l \|\| j)`, `r + (l ? 2 : 50)` | both words tested through `cbranch()`, the high one first - `cmp di,*0` / `bne L10000` / `cmp si,*0` / `beq L4` / `L10000:` (false-branch), `bne` / `bne` (true-branch); the `?:`'s value then `add di,r` |
| `r + (int) (l - 9990)` | `mov di,r` / `add di,l+2` / `add di,#-9990.` - the other term first, unlike `(int) (l - 69990)`'s `mov di,l+2` / `add di,r`: v7's `optim()` makes "x - c" "x + -c" only for a CON or an ITOL of one, not an LCON |
| `r + 10 - (int) (l - 4995)` | `mov di,r` / `add di,*10.` / `mov si,l+2` / `add si,#-4995.` / `sub di,si` |
| `l * 3` | `mov ax,*3.` / `cwd` / `push ax` / `push dx` / ... / `call lmul` (as `l / 7`) |
| `l + i * j` | `mov ax,i` / `imul j` / `cwd` / ... (no `mov ax,ax`) |
| `m = l = 5` | `mov ax,*5.` / `cwd` / `mov di,dx` / `mov si,ax` / both stores |

`mutos_c0` writes `-l` as `NEG(6)`. The inferences `l | m`, `l ^ m`, `x =
(int) (l + 5)`, `l = c` and `l / 7` matched as inferred. The `?:`'s DI
value plus a variable, stored, is now `add di,r` (`acommute()`: the
computed term first) - inside a larger expression it stays `mov si,r` / `add
si,di` (a later sibling may need DI; no golden). `mutos_as` assembled `loop
.-4` already (`E2 FA`).

**7. Floating shapes** (`p24_fltinf2`):

| Source | Golden |
|---|---|
| `e = ++d` | `fldd d` / `fadds 1.0` / `fstdp d` / `fldd d` / `fstdp e` - the prefix value stored WITH a pop and the variable loaded again (`mutos_c1` had inferred `fstd`) |
| `f++` (a float) | `flds f` / `fadds 1.0` / `fstsp f` - `mutos_c0`: `CON 1, ITOF(2), INCAFT(3)` |
| `r + (x ? d : e) * 2.0` | the `.data` of 2.0 first, the `?:` (each arm `fldd`), `lea ax,L` / `fmuls`, then `mov ax,r` / `itof` / `fadd` (the int in AX after a `*`) - a `?:` has degree 2 (`optim()`'s default) |
| `q->y -= e`, `q->x /= e` | `... push ax` / `fldd` / `fsubd e`; but `/=` loads the divisor: `fldd` / `lea ax,e` / `fldd` / `fdiv` |
| `q->x += d * e` | `mov di,q` / `lea ax,(di)` / `\|` / `push ax` / `fldd` / `fldd d` / `fmuld e` / `fadd` / `pop ax` / `fstdp` - the target pushed and loaded first (`SEG_FLOADTP`) |
| `a[i + 1] = 2.0` | `flds 2.0` / `lea di,a` / `mov si,i` / `mov cx,*3.` / `sal si,cl` / `add di,si` / `lea ax,*8.(di)` / `fstdp` - the index scaled by 8 through CL |
| `d = q->k` | `mov di,q` / `mov di,*16.(di)` / `mov ax,di` / `itof` |
| `*pick(a, 1) = 2.5` | `flds 2.5` / the call / `mov di,ax` / `lea ax,(di)` / `fstdp` - through DI for a store (BX for a read, `*pick(a, 2)` in `p20`) |

Its other inferences matched: `e = --d`, `x ? 1.5 : 2.5` (each constant's
`.data` block inside its arm), `(i > j) ? e : d`, `-i * e`, `x = q->y *
4.0`, `q->x > e`, `a[i] < q->y`, `a[i] + q->x * s.y`, `d + i * j`, `d * (i
+ 1)`. `mutos_c0` writes a store through a function's result (`NAME _pick,
..., CALL(11), STAR(3), FCON, ASSIGN(3)`) and `++`/`--` on a float.

**8. Round 8** (`make -f Makefile.mutos round8`): `p29_frame2` (frames of
92 to 98 bytes), `p30_fltstk2` (not a program to run: which store an
assignment under a call gets - see 4), `p31_long4`, `p32_elem5` and
`p33_fltinf3` (what `mutos_c1` now infers from round 7, and what it still
refuses: a long `++`'s value, `l += 70000`, `l << i`, `x = !l`, a constant
minus a long's low word; `*ip += x`, `b[3] ^= b[i]`, `x + f(1) + g(2)`, a
quotient plus a remainder of elements; a float's `++` as a value, the value
of a compound assignment through a pointer, `++d` as an operand).

**Tooling.** `x86sim.py` runs `loop .-4` (back over a one-bit long shift
pair) and takes CF from `neg` (for `sbb ...,*0`): 29 of the 32 `fltprobe`
goldens now run (`p24` 174, `p25` 153, `p26` 238, `p27` 24 - their C
sources' values; `p19`, `p21` and `p28` as before or not meant to run).

**Verification.** `make test`: 92 files byte-exact at all four stages
(62/62 corpus, 29 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp` and `p28_fltstk` in category 9 (2 and 12 messages,
exit status 1, the `.s` byte-exact), 0 mismatches, zero warnings;
`mutos_as` 76/76, `check_floatdat.sh` 13/13, 102/102 compiler goldens
assemble and `p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against
the previous build (`bdb2728`) over all 103 golden inputs: identical output
but for the five round-7 files. `fuzz_c.py` against `bdb2728`: 22,000
programs (seeds 1, 7 `--scope`, 3 scalars only, 21), 0 WRONG, 0 BAD, no
regression; 2,636 changed and still correct - an element tested for truth
loaded and `or`ed (most), `/`, `%` and `*` of elements with the right one at
offset 0 computed first, a computed value plus a variable stored as `add
di,x` - and 401 now correct that the old build refused (`/` and `%` of two
2-D elements). Two regressions found on the way were fixed first: a left
operand ending in AX (`(a * b) & 17`) no longer takes the right-first order
(`ends_in_axdx()`), and a computed value plus a variable keeps the old
register order inside a larger expression. 1,500 more (seed 31) through
ASan/UBSan builds: 0 WRONG, 0 BAD. ASan/UBSan builds over the 103 golden
inputs, the five round-8 probes and the hand-written programs: no reports,
output identical. 66 hand-written programs (sums of calls, frames from 81
to 127 bytes, elements tested, multiplied and divided, sums of scaled
comparisons, compound assignments into elements and through pointers,
every `long` shape above and its neighbours, the floating shapes): the 55
compiled give the host C compiler's value; the rest refuse with a
diagnostic.

## The round-8 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-07)

The five round-8 programs (`p29_frame2`, `p30_fltstk2`, `p31_long4`,
`p32_elem5`, `p33_fltinf3`) came back in commit `2eaa65f`, all four kinds of
file, with `round8.log`. The log has no compiler message at all: `cc -S
p30_fltstk2.c` - written to make the real `c1` report its floating-stack
underflow again - printed nothing and `make` reported no error code. All five
had been refused (`mutos_c0` refused `p33`, `mutos_c1` the other four); now
all five are byte-exact at every stage, and the 92 files byte-exact before are
unchanged.

**1. Frames of 92 to 98 bytes** (`p29_frame2`): `sub sp,*92.`, `*94.`,
`*96.`, `*98.` - every one. With `p27_frame`'s `mov ax,*100.` / `call chkstk`
the switch is exactly at 100 bytes, `(98,100]` - and since a frame is always
even (`p27`'s 127-byte array is rounded to 128), there is no size left
between the two. `MCC_SUBSP_MAX` is gone: `OP_SETSTK` emits `sub sp,N` below
`MCC_CHKSTK_MIN` (100) and `mov ax,N` / `call chkstk` from there; nothing is
refused any more.

**2. Which store an assignment passed as a floating argument gets**
(`p30_fltstk2`). Round 7 had three statements: `fstdp` (the value popped,
and popped again by the argument push - the real `c1`'s own wrong code and
messages) for `f = half(d = 3.0)` and `half(d = 3.0);`, `fstd` (right code)
for `x = half(d = 3.0)`; `mutos_c1` inferred "the statement's own type".
`p30` stores with `fstd` in all eight places:

| `p30` | statement | type of the statement | the call's value | store |
|---|---|---|---|---|
| `f1` | `x = half(d = 3.0);` after `d = 1.0;` | int | converted (`FTOI`) | `fstd` |
| `f2` | the same after `x = d;` | int | converted | `fstd` |
| `f3` | `e = half(d = 3.0) + 1.0;` | double | an operand of `+` | `fstd` |
| `f4` | `if (half(d = 3.0) > 1.0)` | - | compared | `fstd` |
| `f5` | `x = half(d = 3.0) > 1.0;` | int | compared | `fstd` |
| `f6` | `return half(d = 3.0);` in a double function | double | returned | `fstd` |
| `f7` | `x = two(d = 1.0, e = 2.0);` | int | converted | `fstd`, both |

`f3` and `f6` refute the inference - a floating statement - and so does the
"state the previous floating statement leaves" reading (`f1`/`f2` follow a
floating statement and keep). What the ten statements of `p21`, `p28` and
`p30` share is the CALL's own consumer: the store pops exactly when the
call's value goes nowhere (`half(d = 3.0);`) or straight into the
statement's floating store (`f = half(d = 3.0);`), and keeps the value under
anything else - a conversion, an operator, a comparison, a `return`. Why the
real `c1` takes the popping store in those two places is not known; the rule
is what the goldens show. `plan_expression()` now marks the floating
assignments that are arguments of such a call (`argpop_end`, found on the
pre-scanned tree, up through the argument list's `COMMA`s), and `fp_store()`
pops for those only; `stmt_ftop` is gone. Without a double pop nothing goes
below the bottom, so `p30` reports nothing, as the real `c1` did, and its
code is right: under `x86sim.py` it returns 9 (`f1` 1 + `f2` 1 + `f3` 2 +
`f4` 1 + `f5` 1 + `f7` 3) - the first of these probes that runs. A call
nested in an argument, an int function and other argument stores have no
golden (round 9's `p37_fltstk3` asks).

**3. `long` shapes** (`p31_long4`). Matched as inferred: `l -= 1`, `--l`,
`l += 300` (`add *-6.(bp),#300.` / `adc *-8.(bp),*0`), `m = -l`, `l && m`,
`l << 5`, `l >> 7`, `l * 7`, `l ? x : 7`. Corrected and settled:

| Source | Golden |
|---|---|
| `m = m * i` | `mov di,m+2` / `push di` / `mov di,m` / `push di` / `mov ax,i` / `cwd` / `push ax` / `push dx` / `call lmul` - the long variable pushed FIRST, the int widened after it: `acommute()` puts the `ITOL` (degree 2) ahead of the `NAME`, and cr82's `SS` pushes the right operand first (`mutos_c1` had inferred the int widened and pushed first). An int variable times a long keeps its `ITOL` unwidened (`Val`'s `itolw`) until `gen_long_binop_call()` |
| `x = l++` | `mov di,l+2` / `inc l+2` / `mov x,di` - the LOW WORD incremented as an int, the carry into the high word lost (the real compiler's own wrong code for a low word of 0xffff): the `LTOI` distributed into the `++` as v7's `unoptim()` distributes it into a `+`, `LTOI(l)` the low word and `LTOI(ITOL(1))` the 1. The increment comes right after the load - `cr32`'s `%a,1` - not after the store as an int `x = i++`'s does (v7's `delay()` finds no postfix operator right under the assignment). `x = ++l` and `x = --l` inferred the same way (round 9's `p34_long5` asks) |
| `l += 70000` | `mov si,#4464.` / `mov di,*1.` / `add *-6.(bp),si` / `adc *-8.(bp),di` - the constant into DI:SI as for an assignment, then added. A negative int constant widened (`l += -5`) is an `LCON` to v7's `unoptim()` - inferred the same (`p34`) |
| `l = l << i` | `mov si,l+2` / `mov di,l` / `mov cx,i` / `or cx,cx` / `jz .+8` / `sal si,*1` / `rcl di,*1` / `loop .-4` - v7's `rcexpr()` takes the `ITOL` off a long shift's count, the count goes into CX, and a count of 0 skips the loop (`.+8`: past the pair and the `loop`, two bytes each). `>>` inferred the same with `sar di` / `rcr si` |
| `x = !l` | `mov si,l+2` / `mov di,l` / `cmp di,*0` / `bne L10006` / `cmp si,*0` / `beq L10005` / `L10006:mov di,*0.` / `jmp L10007` / `L10005:mov di,*1.` / `L10007:` - v7's `cexpr()` compiles a logical value with `cbranch()`, both words tested as for `if (!l)` |
| `x = 10 - (int) (l - 35)` | `mov di,*10.` / `mov si,l+2` / `add si,*-35.` / `sub di,si` - the constant loaded first ("F"), then `p25`'s shape |

**4. Int shapes** (`p32_elem5`). Matched as inferred: `if (!b[i])`, `x =
!b[j]`, `b[j] ? 3 : 9`, `if (a[i][j])`, `(b[i] - x) * b[j]`, `~b[i] * b[j]`,
`(b[j] + 3) / b[i]`, `(b[3] & 7) % b[4]`, `b[i] += 7`, `ps[i].d ^= x`, `p->b +=
x`, `a[i][j] += x`, `x = (b[i] < b[j]) * 2 + (b[j] < b[i]) * 2 + x` (the two
equal multiples merged by `distrib()`), `x = f(1) + g(2)`, `x = f(1) + g(2) +
h(3)`. Corrected and settled:

| Source | Golden |
|---|---|
| `x = (x > 2) * 2 + (x < 9) * 4 + (x == 5) + x` | `distrib()`'s `((x < 9)*2 + (x > 2))*2 + (x == 5) + x`, each comparison of a variable with a constant computed AFTER the left operand, into SI: `cmp x,*9.` / `blt` / ... `mov di,*1.` / `sal di,*1` / `cmp x,*2.` / `bgt L10006` / `mov si,*0.` / `jmp L10007` / `L10006:mov si,*1.` / `L10007:add di,si` / `sal di,*1` / [`x == 5` into SI] / `add di,si` / `add di,x`. Its degree is 1, so it fits v7's `%n,e` (F, S1, `add R1,R`); `p26`'s comparisons of two elements (degree 3) are `%n,n` - pushed. `mutos_c1` had pushed every one (`plan_dnode()`'s `hard` now asks for the degree; `materialize_slot()` puts a comparison's 0/1 into SI when it is the right operand and the left one is in DI - `cond_reg()`) |
| `*ip += x` | `push ip` / `mov di,x` / `pop bx` / `add (bx),di` - the pointer variable pushed straight from memory (`%n*,n`: FS* - a pointer NAME through `sptab` is the variable pushed), the right-hand side into DI (`is_asptr()`, `ORD_ASPTR`). A constant still goes through DI (`p26`'s `*ip += 2` - `add (di),*2.`) |
| `b[3] ^= b[i]` | [`b[i]`] / `mov di,(di)` / `xor *-50.(bp),di` - the element loaded, the operation in place into the constant-index element (efftab `%aw,n`: S, `I R,A1`). Any right-hand side computed into DI or SI takes the same shape by inference (`x += y * 2`) |
| `x = x + f(1) + g(2)` | [`g(2)`] / `push ax` / [`f(1)`] / `pop bx` / `add ax,bx` / `add ax,x` / `mov x,ax` - `acommute()` orders by degree, the calls (10) ahead of the variable (0): the calls summed first (`is_callsum()`'s shape), the variable added to AX after them (`is_callacom()`, `ORD_CALLACOM`; one call or more, three terms or more) |
| `x = b[j] / b[i] + b[j] % b[i]` | [`b[j] % b[i]`: ... `idiv cx`] / `push dx` / [`b[j] / b[i]`: ... `idiv cx`] / `pop bx` / `add ax,bx` / `mov x,ax` - neither waits in a register across the other's `idiv`: the right operand computed first and pushed from its own register, DX (cr40's `%n,n` again - `is_callsum()` now takes a quotient or a remainder as the right operand, a quotient or a call as the left one; a remainder on the left, a product and a difference of calls have no golden and stay refused) |

**5. Floating shapes** (`p33_fltinf3`). Matched as inferred: `++f`, `--f`,
`f--` on a float as statements, `(x ? d : e) + 1.5`, `((i > j) ? d : e) * f`,
`q->y /= 2.0`, `q->x -= d * e`, `q->x /= d + e`, `a[i + 1] += 1.0`, `a[i + 2] =
a[j] * 2.0`, `d = q->k * 2.0`, `*pick(a, i) = d + e`. Settled:

| Source | Golden |
|---|---|
| `e = f++` (a float) | `flds f` / `call fdup` / `fadds 1.0` / `fstsp f` / `fstdp e` - as a double's `e = d--` |
| `d = ++f` | `flds f` / `fadds 1.0` / `fstsp f` / `flds f` / `fstdp d` - as a double's `e = ++d` |
| `e = (q->x += d)` | `fldd d` / `mov di,q` / `lea ax,(di)` / `\|` / `push ax` / `call faddd` / `pop ax` / `call fstd` / `fstdp e` - its value used: the right operand loaded FIRST, the target combined from memory and stored without a pop (a statement `q->x += 0.5` loads the target first). `mutos_c0` now writes an embedded compound assignment into a floating member or element (`parse_comma_item()`; `lvalue_asop_ahead()` scans the tokens ahead, the lexer put back where it was): NAME q, CON 0, PLUS, STAR(3), NAME d, ASPLUS(3), then the outer ASSIGN. `*=` inferred the same (its right operand loaded first by the plan, as for a statement); `-=` and `/=` would need the operands the other way round - refused |
| `e = ++d * 2.0` | `.data` / `L10029: .float 1.0` / `.text` / `fldd d` / `fadds L10029` / `fstdp d` / `.data` / `L10028: .float 2.0` / `.text` / `flds L10028` / `lea ax,d` / `fmuld` / `fstdp e` - v7's `rcexpr()` `reorder()`s the `*`'s subtree first: `sreorder()` compiles a prefix operator on a `NAME` for its effect (efftab) and puts the `NAME` in its place, so the increment comes before the `*`'s code, its 1.0's `.data` block with it, and the variable is an operand of degree 0, after the constant (degree 1). The constants' labels are as before: the written 2.0 numbered first, the converted 1.0 after it (`fplan_constants()`). `is_fpreinc()` / `plan_fhoist()` / `SEG_FDROP` do it for an operand of a floating `+`, `*`, `-` or `/` |

**6. Tooling.** `x86sim.py` runs `jz .+8` (past a long shift's pair and its
`loop` when CX is 0, after `or cx,cx`): 34 of the 37 `fltprobe` goldens now
run and return their C sources' values (`p29` 50, `p30` 9, `p31` 140, `p32`
284, `p33` 82; `p19`, `p21` and `p28` as before).

**7. Round 9** (`make -f Makefile.mutos round9`): `p34_long5`, `p35_elem6`
and `p36_fltinf4` - what `mutos_c1` now compiles by inference only (the
prefix `x = ++l`, `l += -5`, `l >> i`, `i * m`; `*ip -= y`, `x -= b[i]`, `x =
x + f(1) + y`, a call plus a quotient, `(y > 2) + (x < 3)`; `e = (q->x *=
2.0)`, `e = (a[i] += d)`, a float's `f--` and `--f` as values, `++d` under `+`,
`-` and `/`, `(d += 1.0) * 2.0`) and what it still refuses (`l <<= 3`, `l /=
m`, `(int) (l * 2)`, `l - m - 1`, `100000 - l`; `c % d + c / d`, `c * d +
f(1)`, `f(1) - g(2)`, `f(1) * g(2)`, `b[i] + f(1)`; `e = (q->y -= d)`, `d++ *
2.0`, `++d > 2.0`) - and `p37_fltstk3` (not a program to run: the
argument-store rule of 2. under a nested call, an int function, two
arguments of an unused call, a float target).

**Verification.** `make test`: 97 files byte-exact at all four stages
(62/62 corpus, 34 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp` and `p28_fltstk` in category 9 (2 and 12 messages,
exit status 1, the `.s` byte-exact), 0 mismatches, zero warnings; `mutos_as`
76/76, `check_floatdat.sh` 13/13, 107/107 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`2eaa65f`) over all 108 golden inputs: identical output but for the
five round-8 files. `fuzz_c.py` against `2eaa65f`: 28,000 programs (seeds
1, 7 `--scope`, 3 scalars only, 21, 41 `--scope`, 55 scalars only), 0 WRONG,
0 BAD, no regression, no program's output changed, 138 now correct that the
old build refused. One regression found on the way was fixed first: a
comparison materialized into SI whenever DI held a pending value made `((--n
|| (11 / y)) % ((x >= 6) + !y))` reach the stacked-`+` path with its left
operand in SI (an internal error) - `cond_reg()` now takes SI only for the
right operand next to a left one in DI. 1,500 more (seed 31) through
ASan/UBSan builds: 0 WRONG, 0 BAD; ASan/UBSan builds over the 108 golden
inputs, the four round-9 probes and the hand-written programs: no reports,
output identical. Hand-written programs (the round-9 shapes, `h1`..`h4`):
every one compiled gives the host C compiler's value; the round-9 probes
with their refused statements taken out give theirs (`p34` 178, `p35`
189, `p36` 124). `make check-libcatof` not run (`unicorn` is not installed
in this sandbox); `c1_fltdec.c` is unchanged.

## The real assembler on `p19_open3.s` (real-hardware finding, 2026-10-07)

Round 5 left one question open. The real compiler's `d = (d * e) + u`
writes 118 bytes of libc's `_ctype_` table where a register name belongs
(golden lines 229, `mov <garbage>,ax`, and 231, `push <garbage>`). Would
the real assembler accept that? If it did, `mutos_as` would have to
reproduce whatever it made of the bytes. If it did not, no MUTOS 1700
binary can ever have contained the code.

The bytes are libc's `_ctype_` class table from the `'\n'` entry on (see
"The round-5 fltprobe goldens"). Read as text (`od -c` of line 229), they
are mostly control characters (`\b`, `020`, `004`, `001`, `002`), plus
runs of spaces and the letters `AAAAAA` and `BBBBBB`, between `mov\t` and
`,ax`.

The run on MUTOS, in `fltprobe` (`tests/mutos_cc/fltprobe/p19_as.log`):

```
# as -o p19.o p19_open3.s
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 229
***ERROR*** syntax error, line 231
***ERROR*** syntax error, line 231
p19_open3.s: 3 errors.
# ls -l p19.o
-rw-r--r-- 1 root     other          0 Oct  7 16:47 p19.o
```

- **Refused.** The real `as` rejects both lines and writes no code; the
  output file it created stays at 0 bytes (in both runs). No other line
  draws a message. That covers the 223 lines `invalid_goldens.txt` counts
  as valid, and also the ordinary lines of the statement and the function
  around 229 and 231 (`mov ax,*-36.(bp)`, `sub ax,ax`, `push ax`, `call
  ltof`, ...).
- **Message form.** The real format is `***ERROR*** <text>, line <n>`,
  followed by a summary `<file>: <n> errors.`. This is the same `***ERROR***`
  prefix as the floating overflow abort (`***ERROR*** floating point
  over/under flow- assembly aborted`, `tests/mutos_as/float_open/`).
  A syntax error does not abort, though: the assembler goes on to line 231
  and prints the summary at the end. There are six messages but the count
  is 3. How the real `as` counts is not known; one message per token it
  trips over is only a guess. Nothing here depends on it.
- **`mutos_as`** refuses the same two lines: `error: could not classify
  operands at line 229` and `... 231`, then `2 error(s) in Pass 1,
  aborting before Pass 2`, exit status 1, and no object file at all. As
  with its other messages, it does not copy the real text. The 0-byte
  object the real `as` leaves behind is not reproduced either; no golden
  or test depends on a failed run's output file.
- **Exit status 2** (a second run, `as -o p19.o p19_open3.s` then `echo
  $?`, the same six messages, appended to `p19_as.log`). The floating
  overflow abort exits with 4. So the real `as` uses a different status
  for each case, and neither is the error count (3). `mutos_as` exits with
  1 for both. That difference is documented, and nothing in this project
  depends on the exact value: every script only checks for a nonzero
  status.
- **A process note for future real-hardware runs.** The first attempt was
  `as -o p19.o p19_open3.s > p19_as.log 2>&1 ; echo "exit=$?" >>
  p19_as.log` followed by `ls -l p19.o a.out >> p19_as.log 2>&1`. It left
  `p19_as.log` at 0 bytes: no message, not even the `echo`'s line, not
  even `ls`'s. `df` showed the disks 13 % and 22 % used, so it was not a
  full file system. The cause is unexplained. The round logs, written
  with `2>&1 | tee`, have always come back complete, so `| tee` is the
  form to ask for. When a log comes back empty, copying from the screen
  works, as it did here.

Consequence: none for the code. `p19_open3` stays in `invalid_goldens.txt`
(category 8), `mutos_c1` keeps refusing an unsigned converted after a
computed operand, and `assemble_cc_goldens.sh` keeps requiring `mutos_as`
to refuse the golden. That is now confirmed real-assembler behaviour, not
just this project's choice.

## The round-9 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-09)

The four round-9 programs (`p34_long5`, `p35_elem6`, `p36_fltinf4`,
`p37_fltstk3`) came back in commits `ee6c08e` (2026-10-07) and `f03faae`
(2026-10-08, completing the set), all four kinds of file, with
`round9.log`: seventeen messages from `cc -S p37_fltstk3.c` (`*** Error code
1 (ignored)`, as planned), none from the other three. Before this session
`mutos_c0` wrote `p34`'s `l <<= 3` without the count's `ITOL`, `mutos_c1`
refused `p35` and `p36` and gave `p37` two wrong stores and wrong messages;
now all four are byte-exact at every stage, `p37` with all seventeen
messages and exit status 1 (`tests/mutos_cc/c1_errors.txt`), and the 96
files byte-exact before are unchanged.

**1. `long` shapes** (`p34_long5`). Matched as inferred: `x = l--` (`mov
di,l+2` / `dec l+2` - round 8's postfix shape), `l = l >> i`, `m = i * m`,
`l *= 3`, `l = l + m * 2`, `l = l * m`. Corrected and settled:

| Source | Golden |
|---|---|
| `x = ++l` | `add *-6.(bp),*1.` / `adc *-8.(bp),*0` / `mov di,*-6.(bp)` / `mov *-16.(bp),di` - the WHOLE long incremented in place, as a statement `++l` is, then its low word read. `mutos_c1` had incremented the low word as an int, as round 8 showed for the postfix `x = l++`. It is what v7's `sreorder()` does with a prefix operator on a NAME (compiled for its effect, the NAME put in its place - `LTOI` of the NAME is then its low word); v7's own `reorder()` does not reach a node under an `LTOI`, and its `unoptim()` distributes `LTOI` into no `++` at all, so the MUTOS compiler differs from v7 in both directions here (`x = l++` distributed, `x = ++l` reordered). `x = --l` the same with `sub`/`sbb` |
| `l += -5`, `l -= -3` | `mov si,*-5.` / `mov di,*-1.` / `add *-6.(bp),si` / `adc *-8.(bp),di` (`sub`/`sbb`) - the negative int constant widened is an LCON (v7's `unoptim()`, its `ITOL` case) and is loaded as two numbers, as `l += 70000` is; `mutos_c1` had widened it with `cwd` |
| `l = -i` | `mov ax,*-14.(bp)` / `neg ax` / `cwd` / `mov di,dx` / `mov si,ax` - negated in AX, where `cwd` needs it (`d = -i` converted to floating stays `neg di` - p21) |
| `l <<= 3`, `l >>= 2` | `mov si,l+2` / `mov di,l` / `mov cx,*3.` / `sal si,*1` / `rcl di,*1` / `loop .-4` / `mov l,di` / `mov l+2,si` - computed in DI:SI and stored back HIGH word first; `>>= 2` is `mov cx,*2.` / `sar di,*1` / `rcr si,*1` / `loop .-4` - the count of 2 looped too, where `l >> 2` repeats the pair (p22). `mutos_c0` now widens the count (`CON 3`, `ITOL`, `ASLSH(6)`), as for a binary shift; a char or long count is refused |
| `l /= m` | `mov di,m+2` / `push di` / `mov di,m` / `push di` / `lea di,l` / `push di` / `call aldiv` / `add sp,*6.` - `l *= i`'s `almul` shape with a long divisor pushed through DI (`gen_long_asop_call()`; `%=` and `alrem` inferred the same way) |
| `x = (int) (l * 2)` | ... `call lmul` / `add sp,*8.` / `mov *-16.(bp),ax` - only the low word used, straight from AX (`dxax_result()`) |
| `l = l - m - 1` | `mov si,l+2` / `mov di,l` / `sub si,m+2` / `sbb di,m` / `add si,*-1.` / `adc di,*-1.` - the computed long plus an LCON, two immediates as for `l - 2` (a non-negative int constant widened has another template - still refused) |
| `l = 100000 - l` | `mov si,#-31072.` / `mov di,*1.` / `sub si,*-6.(bp)` / `sbb di,*-8.(bp)` - the constant loaded as for `l += 70000`, the variable subtracted from memory |

**2. Int shapes** (`p35_elem6`). Matched as inferred: `*ip -= y`, `*ip |=
b[i]`, `*ip &= x + 1`, `*ip += f(1)`, `b[i] += f(1)`, `x += y * 2`, `x -=
b[i]`, `x &= b[i] + 1`, `x = x + f(1) + y`, `x = f(1) + g(2) + y + f(3)`, `x
= f(1) + c / d`, `x = f(1) + c % d` (the remainder pushed from DX), `x = c /
d + c / y`. Corrected and settled:

| Source | Golden |
|---|---|
| `x = c / d + f(2)` | `mov ax,c` / `cwd` / `idiv d` / `push ax` / [f(2)] / `pop bx` / `add ax,bx` - the quotient first and pushed: `acommute()` orders by degree, the call (10) on the LEFT, so cr40's `%n,n` pushes the quotient as the right operand (`mutos_c1` had called first). `is_callsumx()`, `ORD_SPILLX` |
| `x = (y > 2) + (y < 9) * 2 + y` | [y > 2 into DI] / `cmp y,*9.` / `blt` / `mov si,*0.` / ... / `mov si,*1.` / `sal si,*1` / `add di,si` / `add di,y` - the left comparison into DI, the right one (a variable compared with a constant, degree 1) into SI (`%n,e`), its multiple shifted there; one constant multiple leaves `distrib()` nothing to factor. `mutos_c1` had pushed the right one. `is_rele()`, `ORD_RELE`, `SEG_MATDI` |
| `x = (y > 2) + (x < 3)` | the same without the shift: `L10007:add di,si` |
| `x = c % d + c / d` | [c / d] / `push ax` / [c % d: `idiv`] / `pop bx` / `add dx,bx` / `mov x,dx` - the right operand first as for two quotients, the sum formed in DX, where the remainder is |
| `x = c * d + f(1)` | [f(1)] / `mov di,ax` / `mov ax,c` / `imul d` / `add di,ax` / `mov x,di` - the call first (its degree), moved into DI, the product computed into AX after it (`%n,e`: the product of two variables is easy for `dcalc()`). `ORD_LEFTDIX` |
| `x = f(1) - g(2)` | [g(2)] / `push ax` / [f(1)] / `pop bx` / `sub ax,bx` - `%n,n` as for a sum of calls (`is_hardsub()`) |
| `x = f(1) * g(2)` | [g(2)] / `push ax` / [f(1)] / `mov ax,ax` / `pop cx` / `imul cx` - 05_matmul's spilled product, the call's own value "moved" into AX (`is_calltimes()`) |
| `x = b[i] + f(1)` | `lea di,b` / `mov si,i` / `sal si,*1` / `add di,si` / `push di` / [f(1)] / `pop bx` / `add ax,(bx)` - the call left by degree, the element the right operand at offset 0: its address pushed first (`is_pushaddr()`'s `%n,nw*`), added to AX from memory. `ORD_PUSHADDRX` |
| `x = f(1) + g(2) * 3` | [f(1)] / `push ax` / [g(2)] / `mov ax,ax` / `mov cx,*3.` / `imul cx` / `pop bx` / `add ax,bx` - the product (it makes a call) the left operand, the call pushed |

**3. Floating shapes** (`p36_fltinf4`). Matched as inferred: `e = (q->x *=
2.0)`, `e = (q->x *= d + e)`, `e = (a[i] *= 2.0)`, `e = f--`, `d = --f`, `e =
++d + 1.5`, `e = 10.0 - --d`, `e = ++d / 4.0`, `e = ++d * ++e`, `e = ++f *
2.0`, `e = -(++d)`, `e = d * (e = 2.0)`, `e = (d = 2.0) * 3.0`. Corrected and
settled:

| Source | Golden |
|---|---|
| `e = (a[i] += d)` | `fldd d` / `lea di,a` / ... / `add di,si` / `lea ax,(di)` / `\|` / `push ax` / `faddd` / `pop ax` / `fstd` - `d` loaded before the element's address, as `*=` does with a computed target (`mutos_c1` had loaded it after) |
| `e = (d += 1.0) * 2.0` | `.data` / `L10028: .float 1.0` / `.text` / `fldd d` / `fadds L10028` / `fstdp d` / `.data` / `L10029: .float 2.0` / `.text` / `flds L10029` / `lea ax,d` / `fmuld` / `fstdp e` - `d += 1.0` done FIRST, as a statement (a pop), then `d` an operand like any variable: `sreorder()` takes `+=`/`-=` on a NAME as it takes a prefix `++` (`mutos_c1` had computed it in place, `fstd`). `is_fpreinc()`, `plan_fhoist()`, `SEG_FHOIST` |
| `e = (q->y -= d)`, `e = (q->y /= d)` | `mov di,q` / `lea ax,*8.(di)` / `\|` / `push ax` / `fldd` / `fldd d` / `fsub` (`fdiv`) / `pop ax` / `fstd` - the target pushed and loaded first, the right operand loaded too, combined on the stack, stored without a pop (as `q->x /= e` as a statement, which pops) |
| `e = d++ * 2.0` | `.data` / `L10034: .float 2.0` / `.text` / `.data` / `L10035: .float 1.0` / `.text` / `fldd d` / `fdup` / `fadds L10035` / `fstdp d` / `fmuls L10034` / `fstdp e` - the `++` (degree 2 - `optim()`'s default for a binary node) computed first, the `.float` 2.0 (degree 1) multiplied in from memory; both `.data` blocks first, the written constant numbered first |
| `x = ++d > 2.0`, `if (++d > 2.0)` | [the increment, `fstdp d`] / `.data` / `L10036: .float 2.0` / `.text` / `flds L10036` / `fldd d` / `fcmp` / `sahf` / `blt` (`bge L4` for the `if`) - hoisted under a comparison too, the constant (degree 1) loaded first and the relation mirrored |

**4. Which store an assignment passed as a floating argument gets, and
which push the model checks** (`p37_fltstk3`). Round 8 (p30) had found
that the store pops when the call's value goes nowhere or straight into
the statement's floating store. `p37` adds:

| `p37` | statement | store | messages |
|---|---|---|---|
| `f1` | `f = half(half(d = 3.0));` | `fstdp` (the inner call is an argument of a popping call) | 43, 44 |
| `f2` | `e = half(d = 1.0) + half(e = 2.0);` | `fstd`, both | 51, 52 |
| `f3` | `x = ihalf(d = 3.0);` (an int function) | `fstd` | - |
| `f4` | `ihalf(d = 3.0);` | `fstdp` (an int call's value going nowhere) | 68, 69 |
| `f5` | `two(d = 1.0, e = 2.0);` | `fstdp`, both | 76 (2), 77 |
| `f6` | `e = two(d = 1.0, 2.0);` | `fstdp` | 84 (3), 85 |
| `f7` | `return half(d = 3.0);` (an int function) | `fstd` | 92 |
| `f8` | `g = half(d = 3.0);` (a float `g`) | `fstdp` | 100 (2), 101 |

`plan_expression()` now marks the popping calls on the pre-scanned tree,
whatever their type - the root call, the call stored by the statement's
floating `ASSIGN`, and (walking down, postfix order backwards) every call
that is an argument of a marked one - and their floating assignment
arguments go into `argpop_end` as before. Line 84's three messages are in
code order: the 2.0 is pushed first (`flds` / `sub sp,*8` / `mov ax,sp` /
`fstdp` - "84: Floating point stack underflow", upper case), then the
store into `d` ("floating", lower case) and its push (silent), the call,
and the store into `e` ("floating"). So the push of an argument IS checked,
with the upper-case message, unless the value pushed is an assignment's
(p21, p28, p37's other lines) or a call's result (f1's inner `half()`):
`push_fp_arg()` sets `fp_nocheck` to 1 for those, 2 otherwise, and
`fp_track()` reports a checked push in upper case. All seventeen messages
follow (the model, never reset, starts `f1` at 0 - `half`, `two` and
`ihalf` are balanced, a double function's store into `fac` unchecked).

**5. Tooling.** `x86sim.py` models `aldiv` and `alrem` (the target's
address on top, the right operand below it, the result written through
the address): 37 of the 41 `fltprobe` goldens run and return their C
sources' values (`p34` 248, `p35` 856, `p36` 133; `p19`, `p21`, `p28` and
`p37` as before - `p37` stops at its first double pop). `make
check-libcatof` ran again (the Python module `unicorn` installed): 38 real
constants, 8357 texts, 2000 operand pairs and 12,021 literals - 0
differences.

**6. Found by the fuzzer on the way.** A first run of the new orders
against `f03faae` gave one regression and one internal error: `t = (((u =
5, t) == x) + (y != x))` - two variables compared on the right of a sum of
comparisons - went into SI after a value already in DI (refused by the
register guard, where the old build pushed it and was right), and `x = (!(n++
|| t) | ((x != i) + (s <= i)))` materialized the left comparison while DI
held the `!`'s value (`internal:` error). `is_easyrel()` now takes only a
variable compared with a constant - the one shape the golden shows - and
`SEG_MATDI` refuses while DI holds another value or a register variable.
After that: no regression, no output changed.

**7. Round 10** (`make -f Makefile.mutos round10`): `p38_long6`,
`p39_elem7` and `p40_fltinf5` - what `mutos_c1` now compiles by inference
only (`l <<= 1`, `l <<= i`, `l += 5L`, `l /= i`, `l %= m`, `(int) (l / m)`,
`(l + m) - 3`, `5L - l`, `m + -i`; the other written orders of round 9's
call shapes, two remainders, a call minus a quotient, a comparison scaled
by 4, `x - f(1)`, `(y > 2) + (x < y)`; a hoisted `+=` with a computed right
operand or a member target, compared, `*=`/`/=` as operands, the values of
`-=`/`/=`/`+=` through a pointer and into an element with computed right
operands, postfix operands) and what it still refuses (`(l - m) + 3`, `5 -
l`, `l * m * 2`, `l <<= 0`; `f(1) & b[i]`, `c * d + c * y`, `c * d - f(1)`,
`f(1) + c * 3`, `(y > 2) - (x < 3)`, `f(1) + g(2) - f(3)`, `f(1) | g(2)`) -
and `p41_fltstk4` (not a program to run: a variable, a sum and an int
converted pushed as arguments, nested calls, a call's value multiplied,
negated and compared, a constant argument of an unused call).

**Verification.** `make test`: 100 files byte-exact at all four stages
(62/62 corpus, 37 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp`, `p28_fltstk` and `p37_fltstk3` in category 9 (2,
12 and 17 messages, exit status 1, the `.s` byte-exact), 0 mismatches,
zero warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13, 111/111
compiler goldens assemble and `p19_open3`'s is refused as listed,
`mutos_cpp` 5/5. Against the previous build (`f03faae`) over all 112
golden inputs and the four round-10 probes: identical output but for the
four round-9 files and the four new probes. `fuzz_c.py` against
`f03faae`: 28,000 programs (seeds 1, 7 `--scope`, 3 scalars only, 21, 41
`--scope`, 55 scalars only), 0 WRONG, 0 BAD, no regression, no program's
output changed; 1,500 more (seed 31) through ASan/UBSan builds: 0 WRONG,
0 BAD; ASan/UBSan builds over the 112 golden inputs, the round-10 probes
and the hand-written programs: no reports, output identical. Hand-written
programs (`h1`..`h5`: every long, call-order, comparison-sum and floating
shape added, and their neighbours): every one compiled gives the host C
compiler's value (`h5`'s `f(1) & b[i]` refused); the round-10 probes with
their refused statements taken out give theirs (`p38` 448, `p39` 785, `p40`
143 - nothing refused there).

## The round-10 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)

The four round-10 programs (`p38_long6`, `p39_elem7`, `p40_fltinf5`,
`p41_fltstk4`) came back in commit `074b6de`, all four kinds of file, with
`round10.log`: twenty-three messages from `cc -S p41_fltstk4.c` (`*** Error
code 1 (ignored)`, as planned), none from the other three. `.i`, `.1` and
`.2` were byte-exact for all four already; `mutos_c1` refused `p38` and
`p39`, gave `p40` three wrong shapes and `p41` one message too many. Now
all four are byte-exact at every stage, `p41` with all twenty-three
messages and exit status 1 (`tests/mutos_cc/c1_errors.txt`), and the 100
files byte-exact before are unchanged.

**1. `long` shapes** (`p38_long6`). Matched as inferred: `l -= 70000`, `l /=
i` (pushed from DX:AX, `aldiv`), `l %= m` and `l %= 7` (`alrem`), `l *= m`,
`(int) (l / m)` and `(int) (l % 7)` (the low word from AX), `(l + m) - 3`
and `(l - m) + 70000` (two immediates), `l * m - 1`, `l / m + l`, `-5L - l`
(the LCON's two words), `m + -i` (`neg ax` / `cwd`, the variable added),
`m - -i` (pushed from DX:AX, `pop bx` / `pop cx` / `sub si,cx` / `sbb
di,bx`). Corrected and settled:

| Source | Golden |
|---|---|
| `l <<= 1` | `sal *-6.(bp),*1` / `rcl *-8.(bp),*1` / `mov di,*-8.(bp)` / `mov si,*-6.(bp)` - shifted IN PLACE, then the value loaded (high word first, unused - the template's own); `mutos_c1` had looped in DI:SI as for `<<= 3`. `>>= 1` inferred as `sar *-8.(bp),*1` / `rcr *-6.(bp),*1` and the same load |
| `l <<= i`, `l >>= i` | `mov si,*-6.(bp)` / `mov di,*-8.(bp)` / `mov cx,*-14.(bp)` / `or cx,cx` / `jz .+16` / `sal si,*1` / `rcl di,*1` / `loop .-4` / `mov *-8.(bp),di` / `mov *-6.(bp),si` - `.+16` where `l << i` has `.+8`: past the pair, the loop AND the two stores, counted as four bytes each. That is right for a file-scope long (`mov _l,di` is `89 3E lo hi`); a local's stores take three (`89 7E F8`), so with a count of 0 the jump lands two bytes into the next instruction - in `p38`, on the `EE` of `mov di,*-18.(bp)` (`8B 7E EE`), which the 8086 executes as `out dx,al`. The real compiler's own bug, written as it writes it (a count of 0 never occurs in `p38`) |
| `l <<= 0` | no code at all - the next statement's `r = r + ...` follows directly (`>>= 0` inferred the same way) |
| `l += 5L` | `add *-6.(bp),*5.` / `adc *-8.(bp),*0` - in place, as `l += 1`: v7's `c1` reads an LCON whose value fits an int as `ITOL(CON)` (`getree()`), so `5L` is the int `5` widened. `mutos_c1` had loaded `mov si,*5.` / `mov di,*0.`. `OP_LCON` now marks such a constant `fromu`, as `OP_ITOL` of a CON does |
| `l = 5 - l`, `l = 5L - l` | `mov ax,*5.` / `cwd` / `mov di,dx` / `mov si,ax` / `sub si,*-6.(bp)` / `sbb di,*-8.(bp)` - the constant into DI:SI as an assignment of one (`materialize_long()`), the variable subtracted from memory |
| `l = (l - m) + 3` | `mov ax,*3.` / `cwd` / `push ax` / `push dx` / `mov si,*-6.(bp)` / `mov di,*-8.(bp)` / `sub si,*-10.(bp)` / `sbb di,*-12.(bp)` / `pop bx` / `pop cx` / `add si,cx` / `adc di,bx` - the constant (an ITOL of a non-negative CON) computed first and pushed, the template `c + 1L` takes (02_long/01_addsub), here with a computed left operand |
| `l = l * m * 2` | `mov ax,*2.` / `cwd` / `push ax` / `push dx` / [`l`, `m` pushed] / `call lmul` / `add sp,*8.` / `push ax` / `push dx` / `call lmul` / `add sp,*8.` / `mov di,dx` / `mov si,ax` - the same `%n,n`: the constant pushed first, the first product's DX:AX pushed straight after it as the left argument pair |

`is_lspill()` (`ORD_LSPILL`, `SEG_LSPILL`, `plan_lspill()`) plans a `long`
`+`, `*`, `/` or `%` of a computed left operand (a sum, difference,
product, quotient or remainder) and an ITOL constant: the constant pushed
first, `GenState.lkeep_off` telling the left operand's helper call to keep
its result in DX:AX (`long_keep()`), `gen_long_stacked_call()` and the
long `+` handler popping it. Not under an `LTOI` (`(int) ((l - m) + 3)` -
the low words only, `lowonly`): refused, as is `(l + m) * 2` (a sum as a
helper's argument).

**2. Int shapes** (`p39_elem7`). Matched as inferred: `c % d + c % y`, `f(1)
- c / d`, `c / d - f(1)`, `c % d - c / d`, `f(1) - c % d`, `f(1) + c * d`,
`f(1) + b[i]`, `(y > 2) + (x <= 3) * 4` (`sal si,*1` twice), `(y > 2) +
(5 < x)`, `(y < 2) + (y > 1) * 2 + (x == 3)`, `g(2) * 3 + f(1)`, `c % d +
f(1)`, `c * f(1) + d`. Corrected and settled:

| Source | Golden |
|---|---|
| `x = f(2) * f(3) * 2` | ... `mov ax,ax` / `pop cx` / `imul cx` / `sal ax,*1` / `mov x,ax` - the product shifted where it is; `mutos_c1` had moved it into DI first. A value computed into AX (or a remainder in DX) is shifted there now, unless it is a divisor next (`divisor_next()`) |
| `x = x - f(1)` | [f(1)] / `push ax` / `mov di,x` / `pop bx` / `sub di,bx` - `%n,n`: the call first and pushed, the variable loaded after it (`is_hardsub()` widened: a variable, a constant, a sum of calls, an easy product - `is_easyprod()` - or an element on the left) |
| `x = f(1) - x` | [f(1)] / `sub ax,*-16.(bp)` / `mov *-16.(bp),ax` - in AX (`%n,aw`) |
| `x = (y > 2) + (x < y)` | [y > 2 into DI] / `mov si,*-18.(bp)` / `cmp *-16.(bp),si` / `blt` / `mov si,*0.` / ... / `add di,si` - two variables compared through SI, the register the 0/1 goes into (`is_easyrel()` widened; `GenState.cmp_reg`) |
| `x = f(1) & b[i]` | `lea di,b` / ... / `push di` / [f(1)] / `pop bx` / `and ax,(bx)` - `f(1) + b[i]`'s shape for `&` (`is_pushaddr()`; `|`, `^` and `-` inferred) |
| `x = c * d + c * y` | `mov ax,c` / `imul d` / `mov di,ax` / `mov ax,c` / `imul y` / `add di,ax` - the left product moved into DI, the right one computed into AX (`is_leftdi()`) |
| `x = c * d - f(1)` | [f(1)] / `push ax` / `mov ax,c` / `imul d` / `pop bx` / `sub ax,bx` |
| `x = f(1) + c * 3` | [f(1)] / `mov di,ax` / `mov ax,c` / `mov cx,*3.` / `imul cx` / `add di,ax` |
| `x = (y > 2) - (x < 3)` | `is_rele()`'s shape with `sub di,si` |
| `x = f(1) + g(2) - f(3)` | [f(3)] / `push ax` / [g(2)] / `push ax` / [f(1)] / `pop bx` / `add ax,bx` / `pop bx` / `sub ax,bx` |
| `x = f(1) \| g(2)` | [g(2)] / `push ax` / [f(1)] / `pop bx` / `or ax,bx` (`is_callsum()` for `\|`, `&`, `^` of two calls) |

**3. Floating shapes** (`p40_fltinf5`). Matched as inferred: twenty-three of
the twenty-six statements. Corrected:

| Source | Golden |
|---|---|
| `e = (d += 2.0) + (e -= 1.0)` | `.data` / `L10040: .float 1.0` / `.text` / `fldd e` / `fsubs L10040` / `fstdp e` / `.data` / `L10039: .float 2.0` / `.text` / `fldd d` / `fadds L10039` / `fstdp d` / `fldd d` / `faddd e` / `fstdp e` - the RIGHT one hoisted first: v7's `reorder()` of the ASSIGN calls `sreorder()` on the `+` with `recurf`, which `reorder()`s the `+`'s right operand (`tr2`) before its left one (`mutos_c1` had hoisted left to right) |
| `x = ++f > 2.0` (a float f) | [++f: `flds f` / `fadds 1.0` / `fstsp f`] / `.data` / `L10034: .float 2.0` / `.text` / `flds L10034` / `flds f` / `fcmp` / `sahf` / `blt` - the constant first: after the hoist f is a NAME, and of two operands of equal degree (1 and 1) `optim()` exchanges the comparison when only the left one is a NAME (`frel_swap()`) |
| `e = (q->x += d * e)` | `fldd d` / `fmuld e` / `mov ax,q` / `mov bx,ax` / `lea ax,(bx)` / `\|` / `push ax` / `faddd` / `pop ax` / `fstd` - its value used, the right operand computed FIRST and the target added from memory (as `*=` does), where `mutos_c1` had pushed and loaded the target first, as the statement `q->x += d * e;` is; and `q` addressed in the register context the right operand left: after a `*` through AX into BX (`fp_lea_ctx()`). `e = (q->x *= d * e)` the same with `fmuld` (`mutos_c1` had used DI) |

`plan_fchain()` and the new `plan_fsreorder()` now simulate v7's
`reorder()`/`sreorder()` over a floating `+`/`*` chain (`fsim_plus()`,
`fsim_times()`): the chain sorted by `insert()`'s rule as read, each `+`
link's right operand `reorder()`ed before its left one, re-sorted after
each hoist, an operand's degree the unhoisted node's (`optim()`'s "def:",
2 for `d += 1.0`) until its hoist and its NAME's after (`fdeg_live`); a
`*` link hoists only when it is compiled, the innermost link's left term
before its right one. That reproduces every hoist of rounds 7 to 10 (`++d
* 2.0`, `(d += 1.0) * 2.0`, `++d * ++e`, `10.0 - --d`, `(d += 2.0) + (e
-= 1.0)` - rounds 8 to 10). A hoist inside a hoisted `+=`'s right operand
is done while that operand is computed (inferred: the hoisted statement's
`rcexpr()` runs no `reorder()` of its own, its template's `rcexpr()` of
the operand does); two of them there are refused (their order would be a
node's `reorder()`, left to right). The value of `+=` into an element
with a computed right operand - refused on the way, where the previous
build had pushed and loaded the element first - is inferred as `q->x`'s:
the right operand first, then the element's address (as `e = (a[i] +=
d)`, p36).

**4. The argument push the model checks** (`p41_fltstk4`):

| `p41` | statement | store | messages |
|---|---|---|---|
| `f1` | `half(d = 3.0);` | `fstdp` | - (one below the bottom) |
| `f2` | `e = half(d) + 1.0;` | - | 44; 45 twice - the push of `d` (upper case), then the store into `e`; 46 |
| `f3` | `e = half(d + e) + 1.0;` | - | 53, 54; 55 once - the store into `e`, the sum's push NOT checked; 56 |
| `f4` | `e = half(i) + 1.0;` | - | 65 (the store - the conversion's push not checked), 66 |
| `f5` | `x = half(half(d = 3.0));` | `fstd` | 74 |
| `f6` | `two(d = 1.0, half(e = 2.0));` | `fstdp`, both | 82 (2), 83 |
| `f7` | `e = two(half(d = 1.0), half(e = 2.0));` | `fstdp`, both | 90 (3), 91 |
| `f8` | `e = half(d = 3.0) * 2.0;`, `e = -half(d = 3.0);`, `x = half(d = 3.0) > 1.0 ? 1 : 2;` | `fstd`, all three | 99, 100, 101 (2) |
| `f9` | `half(2.0);` | - | 107: the constant's push |

So the push is checked for a value LOADED for it (a variable, a constant -
`flds`/`fldd` right before `sub sp,*8`) and not for one already computed
onto the stack (a sum, a conversion, a call's result, an assignment's
value): `push_fp_arg()` now passes any `VK_FACC` unchecked, and `Val`'s
`fnochk` is gone. Every other store matched round 9's rule.

**5. Tooling.** `x86sim.py` runs `jz .+16` past a compound shift's stores
when they are four-byte ones, and refuses it with a count of 0 past a
local's three-byte stores: 40 of the 45 `fltprobe` goldens run and return
their C sources' values (`p38` 525, `p39` 1141, `p40` 143).

**6. Found by the fuzzer on the way.** A first run against `074b6de` gave
one regression: `y = (y % ((19 / t) - s))` - a quotient minus a variable,
newly computed in AX (`sub ax,s`, as `f(1) - x`), was then a divisor, and
the `%` refused it (AX is the dividend's). The value in AX that is the
divisor of a `/` or `%` is moved into DI first again, as before
(`divisor_next()`, for the new AX shifts too). After that: no regression;
349 programs' output changed, all still correct. The ten among the first
600 of seed 1 are all one change: a quotient, a remainder or a product
shifted by a constant where it was computed (`sal ax,*1`, `sar dx,cl`)
instead of in DI, `p39`'s rule (`fuzz_c.py` generates no calls or longs).

**7. Round 11** (`make -f Makefile.mutos round11`): `p42_long7`, `p43_elem8`
and `p44_fltinf6` - what `mutos_c1` now compiles by inference only (`l
>>= 1`, `l >>= 0`, `l += 300L`, `300 - l`, a sum, product or quotient
plus a constant, a product or quotient times, by or modulo a constant;
a call, quotient, remainder or product shifted in AX/DX, `5 - f(1)`, a
quotient or remainder subtracted from a variable, `b[i] - f(1)`, `f(1) -
b[i]`, `|`/`^`/`&` of a call and an element or two calls, products by
constants summed, two variables compared in a difference; three hoists in
a `+` chain, a hoist sorted by its unhoisted degree, a product of two
hoists, a hoisted sum under a `-`, a returned sum of two hoists, two
hoisted floats compared, `q->y += d * e` at an offset, `q->y = d * e`,
`e = (a[i] += d * e)`, a hoist inside a hoisted `+=`) and what it still
refuses (`(int) ((l - m) + 3)`, `(l + m) * 2`, `(int) (l * m * 2)`; `y %
(c / d - s)`, `f(1) * 2 + c / d`, `f(1) * 3 - f(2)`, `c * d + f(1) * 3`;
`e = (d += ++w + ++y) * 2.0`) -
and `p45_fltstk5` (not a program to run: a float variable, an element and
a member through a pointer, a negation, a conversion, a product and a
call's value multiplied pushed as arguments, two arguments).

**Verification.** `make test`: 103 files byte-exact at all four stages
(62/62 corpus, 40 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp`, `p28_fltstk`, `p37_fltstk3` and `p41_fltstk4`
in category 9 (2, 12, 17 and 23 messages, exit status 1, the `.s`
byte-exact), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 115/115 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`074b6de`) over all 116 golden inputs and the four round-11 probes:
identical output but for the four round-10 files and the four new probes
(`p44` compiled by the previous build, refused now on purpose: its `e =
(d += ++w + ++y) * 2.0`). `fuzz_c.py` against `074b6de`: 28,000 programs
(seeds 1, 7 `--scope`, 3 scalars only, 21, 41 `--scope`, 55 scalars
only), 0 WRONG, 0 BAD, no regression, 112 now correct that were refused,
349 changed and still correct (see 6); 1,500 more (seed 31) through
ASan/UBSan builds: 0 WRONG, 0 BAD; ASan/UBSan builds over the 116 golden
inputs, the four round-11 probes and the hand-written programs: no
reports, output identical. `make check-libcatof`: 0 differences.
Hand-written programs (`h1`..`h6`: every long, int and floating shape
added, and their neighbours; divisors computed in AX; `+=` into an
element as a value; hoists inside a hoisted `+=`): every one gives the
host C compiler's value (`h1` 7795, `h2` 1379, `h3` 148, `h4` 404, `h5`
7, `h6` 43); the round-11
probes with their refused statements taken out give theirs (`p42` 8148,
`p43` 1347, `p44` 114).

## The round-11 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)

The four round-11 programs (`p42_long7`, `p43_elem8`, `p44_fltinf6`,
`p45_fltstk5`) came back in commit `12731f2`, all four kinds of file, with
`round11.log`: twenty-three messages from `cc -S p45_fltstk5.c` (`*** Error
code 1 (ignored)`, as planned), none from the other three. `.i`, `.1` and
`.2` were byte-exact for all four already; `mutos_c1` refused `p42`, `p43`
and `p44` (each over the refused statements it was written with) and gave
`p45` two messages too many. Now all four are byte-exact at every stage,
`p45` with all twenty-three messages and exit status 1
(`tests/mutos_cc/c1_errors.txt`), and the 103 files byte-exact before are
unchanged. Of some seventy shapes compiled by inference, three were wrong
- round 10 had corrected seven, round 9 seven - and the floating file had
none.

**1. `long` shapes** (`p42_long7`). Matched as inferred: twenty-two of the
twenty-three (`l >>= 1` - `sar *-8.(bp),*1` / `rcr *-6.(bp),*1` and the
load -, `l >>= 0` - no code -, `l >>= i`, `l += 300L`, `l -= 300`, `300 -
l`, `(l + m) + 300`, `l * m + 3`, `l / m + 7`, `l * m * 3`, `l / m * 2`,
`(l * m) / 3`, `(l * m) % 5`, `l * m + l` and round 10's shapes again).
Corrected and settled:

| Source | Golden |
|---|---|
| `l = -300` | `mov si,#-300.` / `mov di,*-1.` - the constant's two words; `mutos_c1` had gone through `cwd`, as for `l = 300` (`mov ax,#300.` / `cwd` / `mov di,dx` / `mov si,ax`, which the same golden confirms). v7's `getree()` (c11.c) reads an LCON that fits an int as `ITOL(CON)`, and `optim()` (c12.c) turns an ITOL of a NEGATIVE CON back into an LCON - the assignment template then loads two words. `OP_ITOL` now makes a negative constant an LCON for every consumer but the operand of a `+`, `-`, `&`, ... (whose handlers already tell the two apart by the sign), and `materialize_long()` uses `cwd` only for a non-negative one; a negative long constant argument (`f(-300L)`) is inferred to be pushed as two words the same way (`push_call_arg()`) |
| `x = (int) ((l - m) + 3)` | `mov di,*-6.(bp)` / `sub di,*-10.(bp)` / `add di,*3.` / `mov *-16.(bp),di` - one word: v7's `unoptim()` distributes an LTOI over a long `+`, `-`, `&`, `\|`, `^`, `-`, `~`, turns LTOI of a long NAME into its low word (offset + 2), of an ITOL into the int, of an LCON into a CON. `lowword_region()` finds an LTOI over a region of long `+`/`-` whose leaves are local long variables, ITOLs and LCONs, with at least one `+`/`-` of two non-constant terms (the shapes `VK_LOWADD` and `itollow` already handle keep their paths), and the planner records its nodes (`GenState.lowword`): a long NAME there pushes its low word, an ITOL passes the int, an LCON is its low word, a `+`/`-` is handled as the int one, the LTOI passes the value. The pre-scan's nodes are retyped INT too, so the order decisions see the int expression the real compiler compiles |
| `l = (l + m) * 2` | `mov ax,*2.` / `cwd` / `push ax` / `push dx` / `mov si,*-6.(bp)` / `mov di,*-8.(bp)` / `add si,*-10.(bp)` / `adc di,*-12.(bp)` / `push si` / `push di` / `call lmul` / `add sp,*8.` / `mov di,dx` / `mov si,ax` - `%n,n` with a sum on the left: the 2 first, the sum computed into DI:SI and pushed from there, low word first (`is_lspill()` takes a sum or difference on the left of a `*`, `/`, `%` - `/` and `%` inferred; `gen_long_stacked_call()` pushes SI, DI) |
| `x = (int) (l * m * 2)` | `mov ax,*2.` / `cwd` / `push ax` / `push dx` / [`l`, `m` pushed] / `call lmul` / `add sp,*8.` / `push ax` / `push dx` / `call lmul` / `add sp,*8.` / `mov *-16.(bp),ax` - as `l * m * 2`: `unoptim()` does not distribute an LTOI into a `*`, so `is_lspill()` no longer stops at an LTOI parent for `*`, `/`, `%` (for `+` it still does - that one is distributed) |

**2. Int shapes** (`p43_elem8`). Matched as inferred: twenty-eight of the
twenty-nine (a call, a quotient, a remainder or a product shifted in
AX/DX, by CL too; `5 - f(1)`, `x - c / d`, `x - c % d`, `c * 3 - f(1)`,
`b[i] - f(1)`, `f(1) - b[i]`, `c * d - x`, `c / d - x`, differences of
comparisons, `|`/`^`/`&` of a call and an element or two calls, `c * 3 +
c * 5`, `c * d + c * 3`). Corrected and settled:

| Source | Golden |
|---|---|
| `x = c % d - x` | `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `sub dx,*-16.(bp)` / `mov *-16.(bp),dx` - in DX, the remainder's working register (v7's F leaves it in R+1; `%n,aw`: `sub A2,R`); `mutos_c1` had moved it into DI first. The `-` handler's AX case takes DX too |
| `y = y % (c / d - s)` | `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `sub ax,*-24.(bp)` / `push ax` / `mov di,*-18.(bp)` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx` / `mov *-18.(bp),dx` - `%n,n`: the divisor computed first where it ends (AX - not moved into DI) and pushed, the dividend loaded into DI (F), moved into AX for `idiv`. `is_harddiv()` (`ORD_SPILL`): an int `/` or `%` whose divisor makes a division or a call (v7: a CALL's degree is 10, a DIVIDE's operands count two more - beyond the registers left), its dividend a variable or an element; such a divisor is exempt from `divisor_next()` (`GenState.spilldiv`), the spilled `/`/`%` loads a variable dividend into DI. A product or a sum of variables as the divisor keeps the older path (computed into DI, `idiv di` - no golden either way) |
| `x = f(1) * 2 + c / d` | `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `push ax` / [f(1)] / `sal ax,*1` / `pop bx` / `add ax,bx` - the quotient pushed first, the call's product (degree 10, the higher) on the left (`is_callsum()` with `is_callprod()` on the left of a quotient or remainder) |
| `x = f(1) * 3 - f(2)` | [f(2)] / `push ax` / [f(1)] / `mov ax,ax` / `mov cx,*3.` / `imul cx` / `pop bx` / `sub ax,bx` (`is_hardsub()` with a call's product on the left) |
| `x = c * d + f(1) * 3` | [f(1)] / `mov ax,ax` / `mov cx,*3.` / `imul cx` / `mov di,ax` / `mov ax,*-20.(bp)` / `imul *-22.(bp)` / `add di,ax` - `acommute()` puts the call's product first, moved into DI (`is_leftdix()` with a call's product on the right) |

**3. Floating shapes** (`p44_fltinf6`). All fifteen inferred shapes
matched: three hoists in a `+` chain (`f`, then `d`, then `e` - the
`sreorder()` simulation), a hoist next to a variable sorted by its
unhoisted degree (`e = w + ++d` -> `fldd d` / `faddd w`), the product of
two hoists times a constant, a hoisted sum under a `-` (both hoisted
before `y` is loaded), `d -= (e += 1.0) + 2.0`, a returned sum of two
hoists (left to right, a node's `reorder()`), two hoisted floats compared
(`x = ++f > g` - not exchanged), `q->y += d * e` through a pointer after
a `*` (`lea ax,*8.(bx)`), `q->x *= d * e;`, `q->y = d * e`, `q->x = d +
e` (in DI), `e = (a[i] += d * e)`, and a hoist inside a hoisted `+=`. The
refused one settled the order there:

| Source | Golden |
|---|---|
| `e = (d += ++w + ++y) * 2.0` | `.data` / `L10047: .float 1.0` / `.text` / `fldd w` / `fadds L10047` / `fstdp w` / `.data` / `L10048: .float 1.0` / `.text` / `fldd y` / `fadds L10048` / `fstdp y` / `fldd w` / `faddd y` / `faddd d` / `fstdp d` / `.data` / `L10046: .float 2.0` / `.text` / `flds L10046` / `fmuld d` / `fstdp e` - `++w` first. The hoisted statement is compiled by `rcexpr(optim(p), efftab, ~reg)` from `sreorder()`; the negative register sets `recurf`, and `rcexpr()` calls `reorder()` only when `recurf` is 0 - so the hoisted `+=` gets no `reorder()` of its own; its template's `rcexpr()` of the right operand (a non-negative register) does `reorder()` the `+` - as a node: `sreorder()` of its left operand first, then of its right one. `fchain_direct()` now counts a `+` chain that is a hoisted `+=`/`-=`'s right operand as compiled by `rcexpr()` itself; two hoists under any other operator there are still refused. (The 2.0, a written constant, has the first label; the two 1.0s, int constants converted, are numbered after every written one, in tree order - as `fplan_constants()` already did) |

**4. The argument push the model checks** (`p45_fltstk5`):

| `p45` | statement | messages |
|---|---|---|
| `f1` | `half(d = 3.0);` | - (one below the bottom) |
| `f2` | `f = 1.0;`, `e = half(f) + 1.0;` | 47; 48 twice - the float variable's push (upper case), then the store into `e`; 49 |
| `f3` | `a[1] = 2.0;`, `s.y = 4.0;`, `e = half(a[i]) + 1.0;`, `e = half(q->y) + 1.0;` | 59, 60; 62 once, 63 once - the store into `e` only: the element's and the member's push NOT checked; 64 |
| `f4` | `d = 1.0;`, `e = half(-d) + 1.0;`, `e = half((double) i) + 1.0;`, `e = half(d * 2.0) + 1.0;` | 72; 74, 75, 76 - the stores only; 77 |
| `f5` | `d = 1.0;`, `e = half(half(d) * 2.0) + 1.0;` | 84; 85 twice - the inner push of `d`, the store; 86 |
| `f6` | `d = 1.0;`, `e = 2.0;`, `e = two(d, d + e) + 1.0;` | 93, 94; 95 twice - `d`'s push, the store; 96 |

So a value read through an address - an element subscripted by a
variable, a member through a pointer, v7's `*` node - is pushed without a
check, as a computed one is, where a NAME (a variable, a float one
included) is checked: `push_fp_arg()` passes a `VK_FMEM` with `FM_IND`
unchecked. A constant-indexed element of a file-scope array and a local
struct's member are NAMEs to v7's `optim()` (the address folded) and are
inferred checked - `p49_fltstk6` asks.

**5. Round 12** (`make -f Makefile.mutos round12`): `p46_long8`,
`p47_elem9` and `p48_fltinf7` - what `mutos_c1` now compiles by inference
only (`l = -5L`, `l = -1`, a negative long constant passed and returned,
an LTOI over other sums and differences, a sum or difference times, by or
modulo a constant, a quotient or product under an LTOI; a quotient,
remainder or call as a divisor, an element as the dividend, `y % (c - s)`
and `y % (c * d - s)`, `c % d + x` and `c % d + 3`, a call's product next
to a remainder, a quotient, a variable or another call; `d += ++w + ++y;`
as a statement - `++y` first, its `reorder()` `sreorder()`ing the `+` as
an operand -, a `-=`, three terms, `--`, a `+=` operand, the value
compared, a `+=` through a pointer or into an element with a hoist in its
right operand) and what it still refuses (`(int) (l & m)`, `(int) (l | m)
+ 1`, `(l + m) * m`, `(l << 2) + 3`; `y % (c * d)`, `c % d - b[i]`, `c *
3 + f(1) * 5`, `f(1) * 2 + f(2) * 3`; two hoists under a `*` or a `-` in a
hoisted `+=`) - and `p49_fltstk6` (not a program to run: a file-scope
double, a constant-indexed element of a file-scope array, a local
struct's member, a local static, `*p`, `p[1]` and a float element pushed
as arguments). Planned as the last round of the series: the corrections
per round have fallen (seven, seven, three), and the rules each round
adds widen what is inferred next to them, so the series narrows without
ending by itself. After round 12 the shapes left are taken from real
compiled code - the `11_kernel` goldens and `kernel_nonopt/`, which need
no hardware run. One found on the way: an int divided by a constant (`x
/ 4`, `x % 3`) is still refused, although `09_amx.s.golden` and
`kernel_nonopt/lp_AC.s` show its shape (`mov ax,x` / `cwd` / `mov
cx,*4.` / `idiv cx`) - `STATUS.md`'s "Next up" 2.

**Verification.** `make test`: 106 files byte-exact at all four stages
(62/62 corpus, 43 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp`, `p28_fltstk`, `p37_fltstk3`, `p41_fltstk4` and
`p45_fltstk5` in category 9 (2, 12, 17, 23 and 23 messages, exit status
1, the `.s` byte-exact), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 119/119 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`12731f2`) over all 120 golden inputs: identical output but for
the four round-11 files; `p46`..`p48` refused by both builds, `p49` the
same `.s` with four messages fewer. `fuzz_c.py` against `12731f2`: 28,000
programs (seeds 1, 7 `--scope`, 3 scalars only, 21, 41 `--scope`, 55
scalars only), 0 WRONG, 0 BAD, no regression, 5 now correct that were
refused (all five a divisor that divides - `a[j][k] / (19 / a[j][k])`,
`x / (7 % a[j][k])`), 7 changed and
still correct (a remainder minus a variable in DX, a divisor that divides
pushed first); 1,500 more (seed 31) through ASan/UBSan builds: 0 WRONG, 0
BAD; ASan/UBSan builds over the 120 golden inputs, the four round-12
probes and the hand-written programs: no reports, output identical.
`x86sim.py`: 43 of the 49 `fltprobe` goldens run (`p42` 9554, `p43` 1100,
`p44` 120). `make check-libcatof`: 0 differences. Hand-written programs
(`h1`..`h3`: every long, int and floating shape added and their
neighbours - negative long constants assigned, passed and returned, LTOI
over sums, sums times, by or modulo a constant, every divisor kind, call
products, two and three hoists in a hoisted `+=`): every one gives the
host C compiler's value (`h1` 1340, `h2` 119, `h3` 503); the round-12
probes with their refused statements taken out give theirs (`p46` 1142,
`p47` 412, `p48` 548).

## The round-12 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)

The four round-12 programs (`p46_long8`, `p47_elem9`, `p48_fltinf7`,
`p49_fltstk6`) came back in commit `2cab774`, all four kinds of file, with
`round12.log`: twenty-six messages from `cc -S p49_fltstk6.c` (`*** Error
code 1 (ignored)`, as planned), none from the other three. `.i`, `.1` and
`.2` were byte-exact for all four already; `mutos_c1` refused `p46`, `p47`
and `p48` (each over the refused statements it was written with) and gave
`p49` exactly the real `.s` and all twenty-six messages - every argument
check as round 11 had inferred, so `p49` only needed its entry in
`tests/mutos_cc/c1_errors.txt`. Now all four are byte-exact at every
stage, and the 106 files byte-exact before are unchanged. Of some fifty
shapes compiled by inference, eleven were wrong - round 11 had corrected
three of seventy, round 10 seven, round 9 seven - and they come from eight
rules.

**1. `long` shapes** (`p46_long8`). Matched as inferred: sixteen of the
eighteen (`l = -5L` and `l = -1` as two words, `lsub(-300L, 7L)` - `mov
di,#-300.` / `push di` / `mov di,*-1.` / `push di` -, `return -5L;`, an
LTOI over `l - m`, `(l + m) + 3`, `r + (l - m)`, `(l + m) - i`, `l + m +
l`, a sum or difference times, by or modulo a constant, a quotient or
product under an LTOI). Corrected and settled:

| Source | Golden |
|---|---|
| `x = (int) (i + (l - m))` | `mov di,*-6.(bp)` / `sub di,*-10.(bp)` / `add di,*-14.(bp)` / `mov *-16.(bp),di` - the difference first; `mutos_c1` had loaded i into SI and added DI to it. `unoptim()` returns `optim()` of the int `+` it makes (`LTOI(ITOL(i)) + LTOI(l - m)`), and that `optim()`'s `acommute()` puts the difference (degree 1) ahead of i (0). OP_PLUS already exchanged a variable and a value computed into DI when the sum is stored (`p25_long3`'s `r + (l ? 2 : 50)`); the consumer is now looked for through a distributed LTOI too (`lowword_then_assign()`) |
| `x = (int) (l - (m - l))` | `mov di,*-6.(bp)` / `mov si,*-10.(bp)` / `sub si,*-6.(bp)` / `sub di,si` / `mov *-16.(bp),di` - the left word into DI first, the difference into SI: v7's cr40 `%n,e` (F, S1, `sub R1,R`), the right operand's degree (1) within the registers left. `mutos_c1` had computed the difference first into DI and the left word into SI. `is_sube()` (`ORD_SUBE`): an int `-` whose left operand is a leaf or a call and whose right one is a sum or difference of two leaves or an easy comparison; `SEG_LEFTDI` loads the left one into DI (a call's value `mov di,ax`), and the int `+`/`-` tail computes into SI while DI holds a pending value - the "next free register" `pick_addr_reg()` already used for addresses |
| `x = (int) (l & m)`, `x = (int) (l \| m) + 1` | `mov di,*-6.(bp)` / `and di,*-10.(bp)` / `mov *-16.(bp),di`; `mov di,*-6.(bp)` / `or di,*-10.(bp)` / `inc di` - `unoptim()` distributes the LTOI over `&`, `\|`, `^` as over `+`/`-` (on the PDP-11 `&` is ANDN by then; the MUTOS compiler has `and`). `lowword_region()` takes them (`^` inferred), the int handler sees them retyped |
| `l = (l + m) * m` | `mov di,*-10.(bp)` / `push di` / `mov di,*-12.(bp)` / `push di` / `mov si,*-6.(bp)` / `mov di,*-8.(bp)` / `add si,*-10.(bp)` / `adc di,*-12.(bp)` / `push si` / `push di` / `call lmul` / `add sp,*8.` / `mov di,dx` / `mov si,ax` - `%n,n` as for `(l + m) * 2`, the right operand a long variable pushed through DI first (low word first, as `gen_long_binop_call()` pushes one): `is_lspill()` takes a long variable on the right of a `*`, `/`, `%` whose left operand is a sum or a helper's result (`/`, `%` and the helper inferred), `plan_lspill()` pushes it |
| `l = (l << 2) + 3` | `mov ax,*3.` / `cwd` / `push ax` / `push dx` / `mov si,*-6.(bp)` / `mov di,*-8.(bp)` / `sal si,*1` / `rcl di,*1` / `sal si,*1` / `rcl di,*1` / `pop bx` / `pop cx` / `add si,cx` / `adc di,bx` - the constant pushed first, as for a sum on the left (`is_lspill()` with a shift; a right shift and a shift by a variable inferred) |

**2. Int shapes** (`p47_elem9`). Matched as inferred: twelve of the
nineteen (`y / f(1)`, `y % f(2)`, `y / (c / d)`, `b[i] / (c / d - s)`, `y
% (f(1) - s)` - the divisor pushed -, `f(1) * 2 + c % d`, `f(1) * 3 - c /
d`, `c * d + f(1) * 2`, `f(1) * 3 - x`, `f(2) * 4 - f(1)`, `f(1) % c`, `x *
(c / d)`). Corrected, by four rules, and settled:

| Source | Golden |
|---|---|
| `x = c % d + x;`, `x = c % d + 3;` | `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `add dx,*-16.(bp)` / `mov *-16.(bp),dx`; ... / `add dx,*3.` - added in DX, the remainder's working register, as p43's `c % d - x` subtracts there; `mutos_c1` had moved it into DI first. OP_PLUS's in-place case for AX takes DX too (`inc dx` / `dec dx` for 1 and -1 inferred, as `inc ax`) |
| `y = y / (c % d + s);` | ... / `idiv *-22.(bp)` / `add dx,*-24.(bp)` / `push dx` / `mov di,*-18.(bp)` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx` - the same sum, as a divisor pushed from DX |
| `y = y % (c - s);`, `y = y % (c * d - s);` | `mov di,*-20.(bp)` / `sub di,*-24.(bp)` / `push di` / `mov di,*-18.(bp)` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx` / `mov *-18.(bp),dx`; `mov ax,*-20.(bp)` / `imul *-22.(bp)` / `sub ax,*-24.(bp)` / `push ax` / ... - every computed divisor is pushed first and popped into CX, not only one that divides or calls (p43); `mutos_c1` had computed these two into DI and divided by it, `idiv di`, which no golden anywhere has. `is_harddiv()` with `is_computed_int()` (a sum, difference, product, shift, `&`, `\|`, `^`, negation, complement, call); a dividend computed into DI is inferred to be taken the same way (`is_computed_left()`) - one in AX or DX (a call, a product, a quotient) keeps the older path, `f(1) / (c - d)` still `idiv di`. An element as the divisor keeps its own path |
| `x = x - f(1) * 3;` | [f(1)] / `mov ax,ax` / `mov cx,*3.` / `imul cx` / `push ax` / `mov di,*-16.(bp)` / `pop bx` / `sub di,bx` - the call's product (degree 11) pushed, `%n,n`; `mutos_c1` had subtracted AX from DI directly. `is_hardsub()` takes a call's product on the right |
| `x = f(1) - (x < y);` | [f(1)] / `mov di,ax` / `mov si,*-18.(bp)` / `cmp *-16.(bp),si` / `blt L10000` / `mov si,*0.` / `jmp L10001` / `L10000:mov si,*1.` / `L10001:sub di,si` - `%n,e` again: the call's value moved into DI first (F), the comparison's 0/1 into SI (S1 - `cond_reg()`), as for `(y > 2) - (x < 3)`; `mutos_c1` had computed the comparison into DI and moved the call's value into SI. `is_sube()` |
| `x = y % (c * d);` | `mov ax,*-20.(bp)` / `imul *-22.(bp)` / `push ax` / `mov di,*-18.(bp)` / `mov ax,di` / `cwd` / `pop cx` / `idiv cx` / `mov *-16.(bp),dx` - pushed from AX (refused before: the product held AX) |
| `x = c % d - b[i];` | `lea di,*-12.(bp)` / `mov si,*-14.(bp)` / `sal si,*1` / `add di,si` / `push di` / `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `pop bx` / `sub dx,(bx)` / `mov *-16.(bp),dx` - `%n,nw*` with the remainder on the left: `is_pushaddr()` takes a quotient or remainder on the left of a `-` (and a `+` - inferred), the handler subtracts from DX |
| `x = c * 3 + f(1) * 5;` | [f(1)] / `mov ax,ax` / `mov cx,*5.` / `imul cx` / `mov di,ax` / `mov ax,*-20.(bp)` / `mov cx,*3.` / `imul cx` / `add di,ax` - `acommute()` puts the call's product first, as p43's `c * d + f(1) * 3`: `is_leftdix()` takes any `is_easyprod()` on the left |
| `x = f(1) * 2 + f(2) * 3;` | [f(1)] / `sal ax,*1` / `push ax` / [f(2)] / `mov ax,ax` / `mov cx,*3.` / `imul cx` / `pop bx` / `add ax,bx` - the LEFT product computed first and pushed: a power of two as the multiplier leaves the call's degree (10 - `acommute()`'s `ispow2()` case), the other product has 11, so `acommute()` exchanges them and the `%n,n` template pushes what is then the right operand. `is_callsumx()` with `callprod_degree()`; two call products whose left one's degree is not lower are inferred to be pushed right first (`is_callsum()`) |

**3. Floating shapes** (`p48_fltinf7`). Matched as inferred: eight of the
ten (`d += ++w + ++y;` as a statement - `++y` first, as asked -, `-=`,
three terms, a `+=` operand, a `+=` through a pointer or into an element
with a hoist in its right operand, `*=`, a product of two hoisted sums).
Corrected and settled:

| Source | Golden |
|---|---|
| `e = (d += --w + --y) + 1.0` | `.data` / `L10027: .float 1.0` / `.text` / `fldd y` / `fsubs L10027` / `fstdp y` / `.data` / `L10026: .float 1.0` / `.text` / `fldd w` / `fsubs L10026` / `fstdp w` / `fldd w` / `faddd y` / `faddd d` / `fstdp d` / [`+ 1.0`] - `--y` FIRST, its 1.0 keeping the label of its place in the tree. The outer `+`'s `sreorder()` (recurf 1) `reorder()`s its operand, the `+=`; `reorder()` of the `+=` `sreorder()`s ITS operands first - the right one, a `+`, right to left - and only then the `+=` itself, which is hoisted. Under a `*` or a comparison the `+=` is reached by a `sreorder()` alone, hoisted by `rcexpr(..., efftab, ~reg)` with no `reorder()` of its own, and its template's `rcexpr()` reorders the `+` as a node, left to right (p44, and p48's other shapes). `fsim_reorder()` now `sreorder()`s a hoistable `+=`'s right operand before hoisting it; `mutos_c1` had hoisted `--w` first |
| `x = (d += ++w + ++y) > 2.0` | [`++w`] / [`++y`] / `fldd w` / `faddd y` / `faddd d` / `lea ax,*-54.(bp)` / `call fstd` / `.data` / `L10029: .float 2.0` / `.text` / `flds L10029` / `fldd d` / `fcmp` / ... - d stored WITHOUT a pop, the value left on the runtime stack and used by nothing (the comparison reloads d); `cc -S` printed no message for `p48`. `x = (d += 1.0) > 2.0` (p40) and every hoist under a `*` are stored with a pop. Read with v7's `rcexpr()`: when `cexpr()` fails with efftab it retries with regtab, whose template keeps the value in its register - so the efftab template for a floating `+=` evidently does not match here, the unhoisted right operand's degree (3, two hoists of degree 2 each) beyond what a comparison's operand leaves (inferred - the MUTOS table is not known). Taken narrowly: a hoist from a comparison's operand whose right operand holds two or more hoists (`plan_fhoist()`'s `keepv`, `SEG_FHOIST` with a = 1, `fp_store()`'s `hoistkeep`); the stack model then ends the statement one higher (`stmt_fleft`, OP_EXPR's check) |
| `e = (d += ++w * ++y) * 2.0`, `e = (d += ++w - ++y) * 2.0` | [`++w`] / [`++y`] / `fldd w` / `fmuld y` / `faddd d` / `fstdp d` / ...; [`++w`] / [`++y`] / `fldd w` / `fsubd y` / `faddd d` / `fstdp d` - left to right, the `*` or `-` `reorder()`ed as a node by the hoisted `+=`'s template: the refusal is lifted, the existing simulation was right |

**4. The argument push the model checks** (`p49_fltstk6`, every one as
inferred):

| `p49` | statement | messages |
|---|---|---|
| `f1` | `half(d = 3.0);` | - (one below the bottom) |
| `f2` | `gd = 1.0;`, `ga[1] = 2.0;`, `s.y = 4.0;`, `e = half(gd) + 1.0;`, `e = half(ga[1]) + 1.0;`, `e = half(s.y) + 1.0;` | 51, 52, 53; 54, 55, 56 twice each - the push (upper case: a file-scope double, a constant-indexed element of a file-scope array and a local struct's member are NAMEs), then the store; 57 |
| `f3` | `sd = 2.0;`, `e = half(sd) + 1.0;` | 65; 66 twice (a local static, a NAME); 67 |
| `f4` | `a[0] = 1.0;` ... `fa[1] = 3.0;`, `e = half(*p) + 1.0;`, `e = half(p[1]) + 1.0;`, `e = half(fa[i]) + 1.0;` | 77, 78, 79; 81, 82, 83 once each - the store only: `*p`, `p[1]` and a float element are `*` nodes, unchecked; 84 |
| `f5` | `s.y = 1.0;`, `a[1] = 2.0;`, `e = two(s.y, a[i]) + 1.0;` | 94, 95; 96 twice - the member's push, the store (the element's unchecked); 97 |

**5. Round 13.** The plan was to stop here unless round 12 corrected more
than a handful; it corrected eleven inferences by eight rules, and the
rules widen what is inferred next to them (the `%n,e` order and SI as the
next working register reach every int `-` of a leaf and an easy operand,
and turned several refusals into code - a comparison or a sum minus a
sum, two computed sums under one LTOI; every computed divisor; DX for a
remainder; the hoist order through `reorder()`; `fstd`). So a thirteenth
round asks about the neighbours: `p50_long9` (an int or a constant minus
a distributed difference, two computed sums under one LTOI, `^`, `&` and
`|` with an int, a product, difference or sum times, by or modulo a
variable, a right shift or a shift by a variable plus a constant, a shift
plus a variable - and four refusals), `p51_elem10` (a variable or a
constant minus a sum or a comparison, a comparison or a sum minus a sum,
`c % d + 1` / `- 1`, a quotient or remainder next to an element, sums,
shifts and negations as divisors, a computed dividend, call products by
powers of two, and the older `x - c * d` and `f(1) / (c - d)` - `idiv di`
- with three refusals), `p52_fltinf8` (the hoist order through `reorder()`
under a `-`, a `*`, a `+` with a hoist on either side, `fstd` against
`fstdp` for one hoist, a `-=`, the constant on the left, a condition, an
`==` with a hoist on each side, no hoist, a computed right operand) and
`p53_fltstk7` (not a program to run: whether the model counts the value
`fstd` leaves - inferred: it does, so in `x = (d += ++w + ++y) > 2.0;`
at one below the bottom the two hoists' stores report and `fcmp` does
not, and nothing after it does).

**Verification.** `make test`: 109 files byte-exact at all four stages
(62/62 corpus, 46 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp`, `p28_fltstk`, `p37_fltstk3`, `p41_fltstk4`,
`p45_fltstk5` and `p49_fltstk6` in category 9 (2, 12, 17, 23, 23 and 26
messages, exit status 1, the `.s` byte-exact), 0 mismatches, zero
warnings; `mutos_as` 76/76, `check_floatdat.sh` 13/13, 123/123 compiler
goldens assemble and `p19_open3`'s is refused as listed, `mutos_cpp` 5/5.
Against the previous build (`2cab774`) over all 124 golden inputs:
identical output but for `p46`..`p48` (refused before). `fuzz_c.py`
against `2cab774`: 28,000 programs (seeds 1, 7 `--scope`, 3 scalars only,
21, 41 `--scope`, 55 scalars only), 0 WRONG, 0 BAD, no regression, 156 now
correct that were refused (the int `+`/`-` computing into SI while DI
holds a value, every computed divisor pushed), 56 changed and still
correct; 1,500 more (seed 31) through ASan/UBSan builds (UBSan halting on
error): 0 WRONG, 0 BAD, no sanitizer report;
ASan/UBSan builds over the 124 golden inputs, the four round-13 probes
and the hand-written programs: no reports, output identical.
`x86sim.py`: 46 of the 53 `fltprobe` goldens run (`p46` 1682, `p47` 563,
`p48` 737 - the leftover of its `fstd` on the simulated stack). `make
check-libcatof`: 0 differences. Hand-written programs (`h1`..`h3`: the
neighbours of every new rule) each give the host C compiler's value (`h1`
1265, `h2` 476, `h3` 232); the round-13 probes give theirs (`p50` 1870,
`p51` 434, `p52` 711; with the refused statements taken out `p50` 1515,
`p51` 397).

## The round-13 fltprobe goldens (`mutos_c0`/`mutos_c1` extended and verified this session, 2026-10-10)

The four round-13 programs (`p50_long9`, `p51_elem10`, `p52_fltinf8`,
`p53_fltstk7`) came back in commit `1b97d3e`, all four kinds of file, with
`round13.log`: five messages from `cc -S p53_fltstk7.c` (as planned) and -
not planned - twenty-two from `cc -S p52_fltinf8.c`, "Floating point
stack overflow; simplify expression" (its `Makefile.mutos` line got its
`-` with the goldens, so `make` went on). `.i`, `.1` and `.2` were
byte-exact for all four already; `mutos_c1` refused `p50` and `p51` (over
the refused statements they were written with), gave `p52` four wrong
stores and a wrong hoist order and none of its messages, and `p53` the
right `.s` with two of its five messages in the wrong case. Now all four
are byte-exact at every stage, `p52` and `p53` with every message
(`tests/mutos_cc/c1_errors.txt`), and the 124 golden inputs before them are
unchanged. Of some sixty shapes compiled by inference, twelve were wrong,
from seven rules - round 12 had corrected eleven by eight - so the series
goes on (5 below).

**1. `long` shapes** (`p50_long9`). Matched as inferred: thirteen of the
fourteen (`(int) (i - (l - m))` and `(int) (5 - (l - m))` - the left word
or constant into DI first, the difference into SI -, `(int) (l - (m - n) -
i)`, `(int) ((l + m) - (l - n))`, `(int) (l ^ m)` - `xor di,m` -, `(int) ((l
& m) + i)`, `(int) (i + (l | m))`, `(l * m) * n`, `(l - m) / n`, `(l + m)
% n` - n pushed first -, `(l >> 2) + 3`, `(l << i) + 7`, `(l << 2) + m`).
Corrected and settled:

| Source | Golden |
|---|---|
| `x = (int) (l - (m + i))` | `mov di,*-6.(bp)` / `mov si,*-18.(bp)` / `add si,*-10.(bp)` / `sub di,si` / `mov *-20.(bp),di` - i FIRST, m's low word added to it; `mutos_c1` had `mov si,m` / `add si,i`. v7's `unoptim()` starts with `tree->tr1 = optim(tree->tr1)`: the long tree under the LTOI is `optim()`ed - acommute()d - BEFORE the LTOI is distributed, and in the long `m + ITOL(i)` the ITOL has degree 2 (`islong()`), m 0, so `insert()` puts the ITOL first; the int `optim()` after the distribution keeps that order, the degrees then being equal (0 and 0). `lowword_swap()` marks a commutative node of the region (`+`, `&`, `\|`, `^`) with a long variable on the left and an int variable widened on the right (`GenState.lowswap`), and the int handler exchanges its two memory operands (`&`, `\|`, `^` inferred) |
| `x = (int) (-l + m)` | `mov di,*-6.(bp)` / `neg di` / `add di,*-10.(bp)` / `mov *-20.(bp),di` - one word: `unoptim()` distributes the LTOI over a NEG (and a COMPL) as over a `+`. `lowword_region()` takes a unary `-` or `~` of a long variable or of a region (`~` inferred), OP_NEG/OP_COMPL treat it as the int one (refused before) |
| `l = lsub(l, m) * n` | `mov di,*-14.(bp)` / `push di` / `mov di,*-16.(bp)` / `push di` / `mov di,*-10.(bp)` / `push di` / `mov di,*-12.(bp)` / `push di` / `mov di,*-6.(bp)` / `push di` / `mov di,*-8.(bp)` / `push di` / `call _lsub` / `add sp,*8.` / `push ax` / `push dx` / `call lmul` / `add sp,*8.` / `mov di,dx` / `mov si,ax` - n pushed first (`%n,n`), the call's DX:AX pushed from there as a helper's is: `is_lspill()` takes a long function's result on the left of a `*`, `/`, `%` (`/`, `%` and a constant on the right inferred), `finish_call()` keeps it in DX:AX for SEG_LSPILL's `keep` (the CALL's offset now travels in SEG_CALL's `end`). And the arguments: a long VARIABLE is pushed in two words through DI, low word first - `mutos_c1` had pushed `*-12.(bp)`, its high word, alone (`add sp,*4.`): silently wrong code for any long variable passed, no golden had one. `push_call_arg()` now takes a VK_MEM marked `longv` (OP_NAME of TY_LONG) that way |
| `l = n * (l + m)` | `mov di,*-14.(bp)` / `push di` / `mov di,*-16.(bp)` / `push di` / [l + m into DI:SI] / `push si` / `push di` / `call lmul` - `acommute()` puts the sum (degree 2) ahead of n (0), so it is `(l + m) * n`: `is_lspillx()` (`ORD_LSPILLX`) pushes the variable first and exchanges the two for the handler. A quotient, a remainder, a difference and a call on the right inferred the same; a constant on the left of a long `*` stays refused, as `3 * m` is |
| `l = (l + m) << 2` | [l + m into DI:SI] / `sal si,*1` / `rcl di,*1` / `sal si,*1` / `rcl di,*1` / `mov *-6.(bp),si` / `mov *-8.(bp),di` - shifted where it was computed: `gen_long_shift()` takes a long in DI:SI too (a right shift and a count in a variable inferred) |

**2. Int shapes** (`p51_elem10`). Matched as inferred: twenty-five of the
thirty (`a - (c - d)`, `a - (c + 7)`, `5 - (c + d)`, `a - (c < d)`, `a - (c
> 2) * 4`, `f(1) - (c + d)`, `(c < d) - (a - s)`, `(a + c) - (d - s)` - all
`%n,e` -, `c % d + 1` and `c % d - 1` - `inc dx`, `dec dx` -, `c % d +
b[i]`, `c / d - b[i]`, `c / d + b[i]`, `y / (c + d)`, `y % (c << 1)`, `b[i]
/ (c - d)`, `y / (-c)`, `(c + d) % (a - s)` - every divisor pushed -, `x -
f(1) * 2`, `f(1) * 3 + f(2) * 2`, `c * 5 + f(1)`, `c * d + f(1) * 4`, `c %
d * 3`, `c % d + c % s`, `f(1) * 3 - f(2) * 2`). Corrected, by four rules,
and settled:

| Source | Golden |
|---|---|
| `x = x - c * d;` | `mov di,*-16.(bp)` / `mov ax,*-20.(bp)` / `imul *-22.(bp)` / `sub di,ax` / `mov *-16.(bp),di` - `%n,e`: x into DI first (F), the product (degree 2, within the registers left) after it, subtracted from AX; round 12 had inferred the product first. `is_sube()` takes a product of two variables on the right (`is_leafprod()`) |
| `x = c % d - (a - s);` | `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `mov di,dx` / `mov si,*-24.(bp)` / `sub si,*-26.(bp)` / `sub di,si` / `mov *-16.(bp),di` - `%n,e` with a remainder on the left: computed, moved into DI (F), the difference into SI. `is_sube()` takes a quotient or remainder of two leaves on the left (a quotient inferred, `mov di,ax`; and a product of two variables, `c * d - (a - s)`), `plan_leftdi()` moves DX as it moves AX |
| `x = a - (c - d) * 2;` | `mov di,*-24.(bp)` / `mov si,*-20.(bp)` / `sub si,*-22.(bp)` / `sal si,*1` / `sub di,si` / `mov *-16.(bp),di` - `%n,e` again: a difference of two leaves times a power of two keeps its degree (1). `is_sube()` takes it; OP_TIMES shifts a sum computed into SI there while DI is busy |
| `x = f(1) / (c - d);` | `mov di,*-20.(bp)` / `sub di,*-22.(bp)` / `push di` / [f(1)] / `mov ax,ax` / `cwd` / `pop cx` / `idiv cx` / `mov *-16.(bp),ax` - the divisor pushed first, as every other computed divisor; round 12 had left a call's value as the dividend on the older path (`idiv di`). `is_harddiv()` takes a call (and a product of two variables, a quotient or a remainder - inferred: `mov ax,ax`, `mov ax,dx`), and the spilled division takes its dividend in AX or DX |
| `x = f(1) * 4 + f(2) * 8;` | [f(1)] / `push ax` / [f(2)] / `sal ax,*1` / `pop bx` / `add ax,bx` / `sal ax,*1` / `sal ax,*1` / `mov *-16.(bp),ax` - v7's `distrib()` factors the two multiples - `f(2) * 8` divided by `f(1) * 4`'s 4: `(f(2) * 2 + f(1)) * 4`, the calls' degrees equal (10, a power of two keeping the call's), so in that order, the right one pushed (`%n,n`). `is_distrib()` takes a chain of int calls (and variables), two or more of them times powers of two, ahead of `is_callsum()` (`is_distrib_call()`); `plan_shift()` shifts AX. Mixing calls and comparisons is still refused |
| `x = c % d & b[i];` | `lea di,*-12.(bp)` / `mov si,*-14.(bp)` / `sal si,*1` / `add di,si` / `push di` / `mov ax,*-20.(bp)` / `cwd` / `idiv *-22.(bp)` / `pop bx` / `and dx,(bx)` - as `c % d - b[i]` (p47): `is_pushaddr()` takes a quotient or remainder on the left of `&`, `\|`, `^` (`\|`, `^` inferred) |
| `x = c % d \| f(1);`, `x = c % d + f(1) * 2;` | [c % d] / `push dx` / [f(1)] / `pop bx` / `or ax,bx`; [c % d] / `push dx` / [f(1)] / `sal ax,*1` / `pop bx` / `add ax,bx` - `acommute()` puts the call (or its product - 10 or 11) ahead of the remainder, so the remainder, by then the right operand, is computed and pushed first: `is_callsumx()` takes a quotient or remainder left of a call's product, `is_callbitx()` the same for `&`, `\|`, `^` (`&`, `^` inferred) |

**3. Floating shapes** (`p52_fltinf8`). Matched as inferred: eleven of the
sixteen (`e = (d += ++w * ++y) + 1.0` and `e = (d += ++w - ++y) + 1.0` -
left to right -, `e = (d += ++w + ++y) - 1.0`, `e = 1.0 + (d += ++w +
++y)`, `e = (d += --w + --y + 1.0) + 2.0`, `e = (d += ++w + ++y) + (v -=
1.0)`, `x = (d -= ++w + ++y) < 0.0` - d loaded first, `fstd` -, `x = 2.0 <
(d += ++w + ++y)`, `x = (d += ++w + ++y) == (v += 1.0)` - d kept, v popped
-, `e = (d += ++w + ++y) / 2.0`, `e = (d *= ++w + ++y) + 1.0` - not
hoisted, `++y` first). Corrected and settled:

| Source | Golden |
|---|---|
| `x = (d += ++w + 1.0) > 2.0` | [++w] / `flds 1.0` / `faddd w` / `faddd d` / `fstd d` / `flds 2.0` / `fldd d` / `fcmp` - kept (`fstd`) with one hoist inside; round 12 had taken only two hoists that way |
| `x = (d += w + y) > 2.0`, `x = (d += a[i] * e) > 2.0` | `fldd w` / `faddd y` / `faddd d` / `fstd d` / ...; [a[i]] / `fmuld e` / `faddd d` / `fstd d` / ... - kept with no hoist inside at all |
| `if ((d += ++w + ++y) > 2.0)` | [++w] / [++y] / `fldd w` / `faddd y` / `faddd d` / `fstdp d` / `flds 2.0` / `fldd d` / `fcmp` / `sahf` / `bge L4` - popped under a condition, the same expression as p48's kept one |

So it is not the hoists. Every comparison whose value is taken keeps the
`+=` of a non-leaf right operand, and a condition pops it, as `x = (d +=
1.0) > 2.0` (p40 - a constant) does. Read with v7's code: `cexpr()`
compiles a comparison's value by `cbranch()` - but only after the
`rcexpr(..., regtab)` that reached it has run `reorder()`, which hoists the
`+=` (`rcexpr(optim(p), efftab, ~reg)`); a condition goes to `cbranch()` and
`cctab` directly. On the value path the efftab template evidently fails
for a right operand beyond an addressable one (v7's `dcalc()` above 12)
and `rcexpr()` falls back to regtab, which keeps the value in its
register. `plan_fhoist()`'s `keepv` is now: the `+=`'s parent a floating
comparison that `frel_branch()` does not take for `cbranch()` (a CBRANCH's
condition - `Plan.cbr_root` -, an operand of `!`, `&&`, `||`, a `?:`'s
condition), and a right operand `fdcalc_leaf()` does not take (a NAME, a
constant, `*p` - inferred). The mechanism is an inference (the MUTOS code
table is not known); the shapes are not.

| Source | Golden |
|---|---|
| `e = -(d += ++w + ++y)` | [++w] / [++y] / `fldd w` / `faddd y` / `faddd d` / `fstdp d` / `fldd d` / `fneg` / `fstdp e` - hoisted out of the negation (v7's `reorder()` of the NEG `sreorder()`s its operand), its right operand left to right (hoisted by a `sreorder()` alone - `plan_fhoist()`); `mutos_c1` had computed it in place (`fstd d` / `fneg`) after `++y` first. `is_fpreinc()` takes a NEG parent, OP_NEG's plan `sreorder()`s its operand |

**4. The model's top, and which stores report in upper case** (`p52`,
`p53`). `p52`'s twenty-two messages, lines 74 to 88: its value-comparisons
on lines 57, 60, 63, 71, 74 and 78 each leave one value (`fstd`, used by
nothing), and the model counts them - four by line 74, whose `fldd d`
before `fcmp` is the seventh value: one message; line 78 (`flds 2.0` at 7,
`fldd d` at 8) two; from then on the model is at six between statements,
and every push of every statement reports (`itof` included - `r = r + x + d
* 4.0;` at 79 twice, `d = 1.5;` once, `e = (d += ++w + ++y) / 2.0;` four
times: the two increments' loads, the hoisted `+=`'s, d's). The depth goes
on past six (78's second message). So the model holds six values - v7's c1
has the PDP-11's six floating registers - and `fp_track()` reports
"Floating point stack overflow; simplify expression" for a push beyond
`FP_STACK_MAX`.

| `p53` | statement | messages |
|---|---|---|
| `f1` | `half(d = 3.0);` | - (one below the bottom) |
| `f2` | `d = 1.5;`, `w = 4.0;`, `y = 10.0;` | 38, 39, 40 (lower case - assignments' stores) |
| `f2` | `x = (d += ++w + ++y) > 2.0;` | 41 twice, UPPER case - the stores of the hoisted `++w` and `++y` (inferred: lower case); then `fstd d` leaves its value, counted (as inferred), so the model is back at the bottom: `fcmp` and everything after it silent |

A compound assignment's or increment's store reports in upper case, as a
stack-with-stack operator's pop does; an assignment's in lower case
(`fp_compound`, set around `gen_fp_asop()` and `gen_fp_incdec()`). Whether
it is the operator or the hoist that decides is an inference - no golden
has an unhoisted compound store below the bottom; `p57_fltstk8` asks.
(`p53`'s golden runs under `x86sim.py`: `main()` calls f2 before f1, and
f2's leftover absorbs f1's double pop - 5.)

**5. Round 14.** Round 12 was to end the series unless it corrected more
than a handful, and round 13 corrected twelve inferences by seven rules,
with a silent wrong-code bug (a long variable passed as an argument) and
the model's top besides. So a fourteenth round asks about the neighbours:
`p54_long10` (other commutative nodes of a distributed LTOI with an int
widened, a negation or complement of a region, a long call's result under
`/`, `%` and a constant, a variable times a quotient, remainder,
difference or call, shifts of a computed long by a variable - and four
refusals), `p55_elem11` (`%n,e` with a product, quotient or remainder on
the left or a product or scaled sum on the right, a call, product,
quotient or remainder as a dividend, `distrib()` over three calls, with a
variable, of equal multiples and under a `-`, quotients and remainders
next to calls and elements under `&`, `|`, `^` - and one refusal),
`p56_fltinf9` (the value path against `cbranch()` - a leaf right operand,
`!`, `?:`, `&&`, `while`, an int converted, a comparison's value in a sum
and stored into a double - and negations of other hoists) and `p57_fltstk8`
(not a program to run: an unhoisted compound store, an increment's and
`fdup`'s pushes, below the bottom and at the top).

(Prepared, then put aside before any hardware run - see "Plan after round
13: read the original `c1`" below.)

**Verification.** `make test`: 111 files byte-exact at all four stages
(62/62 corpus, 48 `fltprobe`, `11_kernel/01_delay`), `p19_open3` in
category 8, `p21_fltexp`, `p28_fltstk`, `p37_fltstk3`, `p41_fltstk4`,
`p45_fltstk5`, `p49_fltstk6`, `p52_fltinf8` and `p53_fltstk7` in category
9 (2, 12, 17, 23, 23, 26, 22 and 5 messages, exit status 1, the `.s`
byte-exact), 0 mismatches, zero warnings; `mutos_as` 76/76,
`check_floatdat.sh` 13/13, 127/127 compiler goldens assemble and
`p19_open3`'s is refused as listed, `mutos_cpp` 5/5. Against the previous
build (`1b97d3e`) over all 128 golden inputs: identical output but for
`p50`..`p53`. `fuzz_c.py` against `1b97d3e`: 28,000 programs (seeds 1, 7
`--scope`, 3 scalars only, 21, 41 `--scope`, 55 scalars only), 0 WRONG, 0
BAD, no regression, 44 now correct that were refused (a quotient or
remainder `&`, `|` or `^` an element - its address now pushed first; all
seven of seed 1's are this), 3 changed and still correct (a remainder as
the dividend of a computed divisor, now pushed first); 1,500 more (seed
31) through ASan/UBSan builds: 0 WRONG, 0 BAD, no sanitizer report;
ASan/UBSan builds over the 128 golden inputs, the four round-14 probes and
the hand-written programs: no reports, output identical. `x86sim.py`: 50
of the 57 `fltprobe` goldens run (`p50` 1870, `p51` 434, `p52` 711, `p53`
5). `make check-libcatof`: 0 differences. Hand-written programs (`h1`..`h3`:
the neighbours of every new rule - region swaps and negations, long calls
under every helper, variables times computed longs, shifted sums; every
new `%n,e` and divisor shape, `distrib()` over calls, quotients and
remainders with calls and elements; value-comparisons against conditions,
negated hoists) each give the host C compiler's value (`h1` 639, `h2` 499,
`h3` 359); the round-14 probes give theirs with their refused statements
taken out (`p54` 701, `p55` 686; `p56` compiles whole, 179).
