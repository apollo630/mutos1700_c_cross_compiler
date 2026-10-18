# mutos_c0 / mutos_c1 - MUTOS 1700 Cross C Compiler (Milestone 4)

`mutos_c0` (front end: lex/parse/typecheck, emits the `temp1`/`temp2`
intermediate-code stream) is, since 2026-10-18, a **port of the original
MUTOS 1700 `c0`** - v7's `c0`, ported to the 8086 - read from its binary
(`c0port/`, `../../docs/C0_PORT_PLAN.md`): v7's functions with the
binary's deltas, run on an image of the original's data segment, its
initialized data generated from the binary. It is checked against the
original itself, run under an 8086 emulator (`../../tests/realbin/`), on
every golden and on random programs - see "The port of the real `c0`"
below. Until then `mutos_c0` was a from-scratch C11 front end
(`c0_parser.c` and its companions - their last version is in the git
history, commit `ff1e589`); the grammar and wire-format explanations in
"Current scope" come from that time.

`mutos_c1` (back end: reads `temp1`/`temp2`, emits `mutos_as`-syntax
assembly text) is, since 2026-10-13, a **port of the original MUTOS 1700
`c1`** - v7's `c1`, ported to the 8086 by hand - read from its binary
(`c1port/`, `../../docs/C1_PORT_PLAN.md`): v7's functions with the
binary's deltas, its code tables generated from the binary, and the
library routines its output passes through re-enacted. It is checked
against the original itself, run under an 8086 emulator
(`../../tests/realbin/`), on every golden and on random programs - see
"The port of the real `c1`" below. Until then `mutos_c1` was `c1_gen.c`,
a generator of the shapes the goldens had shown (its last version is in
the git history, commit `e36c67f`); the code-generation explanations in
"Current scope" come from that time.

**Status: verified byte-exact, end-to-end (`.c` → real `mutos_cpp` →
`mutos_c0` → `mutos_c1` → `.s`), for 62/62 of the full corpus - every
file:**
`tests/mutos_cc/00_smoke/`'s three files, plus all of `01_expr`:
`01_intarith.c`, `02_bitwise.c`,
`03_rellogic.c`, `04_shift.c`, `05_incdec.c`, `06_compasgn.c`,
`07_ternary.c` and `08_castsize.c`, plus all 4 of `02_long`:
`01_addsub.c`, `02_muldiv.c`, `03_retval.c` and `04_params.c`, plus all 7
of `03_ctrlflow`, plus all 7
of `04_funcs`: `01_call.c`, `02_manyargs.c`, `03_recfact.c`,
`04_mutrec.c`, `05_staticvar.c`, `06_regclass.c` and `07_funcptr.c`, plus
all 7 of `05_arrptr`: `01_arrbasic.c`, `02_array2d.c`, `03_ptrbasic.c`,
`04_ptrarreq.c`, `05_arrofptr.c`, `06_ptrptr.c` and `07_strlibc.c`, plus
all 7 of `09_abiprobe` (`01_argvmain.c` and the six `0N_frameNNN.c`
large-frame probes), plus all 3 of `07_scope`: `01_globstat.c`,
`02_shadow.c` and `03_externdef.c` (file-scope variables and block
scope), plus all 9 of `06_struct` (structs, unions, bit-fields, enums,
typedefs), plus all 5 of `10_integ`: `01_wordcount.c`, `02_bubsort.c`,
`03_linklist.c`, `04_strrev.c` and `05_matmul.c`, plus both of
`08_float`: `01_floatbas.c` and `02_dblconv.c` (floating point - see
"Floating point" under "Current scope") - and, beyond the corpus, all 61
probes of `tests/mutos_cc/fltprobe/`'s fourteen rounds with their own
real-hardware goldens: 50 byte-exact, ten more together with the real
compiler's own `c1` error messages (`p21_fltexp`, `p28_fltstk`,
`p37_fltstk3`, `p41_fltstk4`, `p45_fltstk5`, `p49_fltstk6`,
`p52_fltinf8`, `p53_fltstk7`, `p56_fltinf9` and `p57_fltstk8` - two,
twelve, seventeen, twenty-three, twenty-three, twenty-six, twenty-two,
five, eighteen and eighteen of them, `tests/mutos_cc/c1_errors.txt`),
and `p19_open3` up to the real compiler's own invalid output, which
`mutos_c1` refuses on purpose (`tests/mutos_cc/invalid_goldens.txt`) -
and all nine real kernel driver files of `tests/mutos_cc/11_kernel`,
`01_delay.c` to `09_amx.c`. See STATUS.md for the currently-verified
details and `tests/mutos_cc/run_goldens.sh` for a full-corpus run (which
reports a refusal of either port - where the original traps or reads
state the port does not model - apart from a mismatch, never as a
silent wrong output).

## The `temp1`/`temp2` wire format

`c0`/`c1` communicate through the exact `temp1`/`temp2` tagged byte
stream V7's own `cc` pipeline comment documents (`v7/cc/cc.c`:
`c0 source temp1 temp2` / `c1 temp1 temp2 assembly.s`) - see
`docs/DEVLOG.md`'s Milestone 4 "`c0`/`c1` process split" section for
why this project deliberately keeps that boundary. The format itself
(`v7/cc/c04.c`'s `outcode()`, which `mutos_c0` writes with the real
`c0`'s own, `c0port/c04.c` - with `stdarg.h` instead of the original's
varargs-pointer walk; `mutos_c1` reads it with the real `c1`'s own
`getree()`, `c1port/c11.c`):

| Tag | Meaning | Bytes written |
|---|---|---|
| `B` | one opcode/operator tag | `(value & 0xFF, 0xFE)` - `0xFE` (octal `0376`) marks "this is a tag", never a data word |
| `N` | one 16-bit word | little-endian `(low, high)` |
| `S` | one symbol name | `'_'` (iff non-empty) + up to `NCPS` (8) significant chars, 7-bit-masked + a terminating `NUL` |
| `1` / `0` | the literal word 1 / 0 | shorthand constants |
| `F` | a floating constant's text (`FCON` only) | up to 1000 characters, 7-bit-masked, then a `NUL` - no `_` |

`temp1` carries everything but string-literal data, which goes to `temp2`
(v7's `putstr()` switches `outcode()`'s destination with `strflg`), one
block per literal: `LABEL <n>`, then `BDATA`, `(1, byte)` per byte and
`(1, 0)` for the NUL, ended by a lone `0` - a new `BDATA` run starting
before every 15th byte. `mutos_c1` reads `temp2` after `temp1`'s `EOFC`,
behind the `.data` it always emits, rendering each run as `.byte` lines of
at most 9 hex values (`L4:.byte\t/6f,/6e,/65,/0`) - confirmed against
`05_arrptr/05_arrofptr` and `07_strlibc`, and against the line layout of
all 208 literals in `tests/mutos_as/kernel_nonopt|kernel_opt/*.s`; see
`docs/DEVLOG.md`'s Milestone 4 string-literal section. The same runs,
without the `LABEL`, initialize a file-scope `char name[] = "..."` - in
`temp1`, where the declaration is, after `DATA` and `NLABEL`
(`10_integ/01_wordcount`, see "Current scope").

All opcode/type/storage-class values are v7's (`v7/cc/c0.h`), with the
MUTOS additions, in `c0port/c0.h` and `c1port/c1.h` - load-bearing
wire-format tag values, which both ports take from the binaries.

### Confirmed MUTOS-1700-specific deltas from vanilla V7 `cc`

Reverse-engineered byte-for-byte (`od -c`/`od -t x1z`) from
`tests/mutos_cc/00_smoke/*.1.golden`/`*.s.golden` against
`v7/cc/c02.c`'s/`c04.c`'s algorithm shape - full derivation with
every byte offset cited in `docs/DEVLOG.md`'s Milestone 4 "temp1/temp2
wire format" section:

1. **`STAUTO` is `-4`, not V7 PDP-11's `-6`.** MUTOS's fixed prologue
   only callee-saves 2 registers (`di`, `si` - see `MUTOS_C_ABI.md`
   sect. 1.2/1.4), vs V7's 3, so the first local slot sits 4 bytes
   below `bp`, not 6.
2. **`cfunc()`'s header sequence emits an extra `EVEN` between `PROG`
   and `RLABEL`** (`PROG, EVEN, RLABEL, name`, not V7's
   `PROG, RLABEL, name`) - presumably for the 8086's entry-point
   alignment.
3. **`RETRN` carries one extra numeric argument** (the function's
   return type), rendered by `c1` as a `|RTYP n` comment immediately
   before the `jmp cret` epilogue tail-jump.
4. **The initial register-variable budget (`SETREG`'s first emitted
   value) is `4`, not V7's `5`.**

### The local-variable wire format (confirmed via `01_intarith`)

Reverse-engineered the same way, against
`tests/mutos_cc/01_expr/01_intarith.1.golden`/`.s.golden` (`int a, b,
c;` plus assignments and `+ - * / %`):

- **`ANAME`** (`outcode("BSN", ANAME, name, offset)`) is emitted once
  per declared `AUTO` local, in declaration order, immediately after
  the function's `L<sloc+1>` body-entry label and before any
  statement code. `mutos_c1` renders each as a `"| name=offset."`
  comment line - confirmed against `01_intarith.s.golden`'s
  `"L2:| _a=-6.\n| _b=-8.\n| _c=-10."` (the leading `_` comes from
  `outcode()`'s `'S'` format itself, not from anything `AUTO`-specific
  - see the wire-format table above).
- **`NAME`** (`outcode("BNNN", NAME, hclass, type, hoffset)` for a
  non-`EXTERN` name - the `hoffset`-vs-symbol-name branch `treeout()`
  itself makes) is emitted for every variable *reference* (as opposed
  to declaration). Offset assignment matches `v7/cc/c03.c`'s
  declarator loop exactly: starting from `MCC_STAUTO` (`-4`, MUTOS's
  own value - see delta #1 above), each new local first subtracts its
  own size, *then* takes that as its offset - so `a`, `b`, `c` (each
  2 bytes) land at `-6`, `-8`, `-10`, confirming `MCC_STAUTO`'s role
  as a *starting subtrahend*, not a first-local offset directly.
- **The `SETSTK`-vs-`.s` local-frame threshold is now implemented for
  real** (previously always "not yet supported" for any `extra > 0`):
  `01_intarith`'s three 2-byte locals give `SETSTK 10` → `extra = 6`
  bytes beyond the fixed 4-byte register-save area, rendered as
  `"sub\tsp,*6."` - confirmed byte-for-byte, and consistent with
  `docs/MUTOS_C_ABI.md` sect. 1.9's bound (largest real `libc.a`
  example using plain `sub sp,N`: `N=76`). The `09_abiprobe` goldens
  and the `fltprobe/p27_frame`/`p29_frame2` goldens have since confirmed
  both shapes and settled the threshold of the `chkstk` call, `(98,100]` -
  the real `c1`'s option `s<n>`, default 100 (`docs/DEVLOG.md`'s "The
  original binaries").
- **Per-operator instruction shapes** (`c1_gen.c`, confirmed against
  `01_intarith.s.golden`): `+`/`-` both load the left operand into
  `DI` then `add`/`sub` the right operand in place (`DI` is evidently
  the generic two-operand working register - the same register
  `00_smoke`'s `RFORCE` handler already used for a bare return value);
  `*` loads the left operand into `AX` then `imul`s the right operand
  (single-operand `IMUL`, matching plain-8086 - no `-mv30` 3-operand
  form); `/` and `%` both do `mov ax,<left> / cwd / idiv <right>`,
  the *same* two-instruction sequence, differing only in which
  register the result comes from afterward (`ax` for `/`, `dx` for
  `%`) - `01_intarith.s.golden` genuinely repeats the whole `mov/cwd/
  idiv` sequence for both, with no shared sub-expression reuse,
  confirming this is unoptimized per-statement codegen (peephole
  cleanup is `c2`'s job, Milestone 5). Both `IMUL`/`IDIV`'s reg/mem
  operand can currently only be a `NAME`/memory location - an
  immediate right-hand operand (`8086` has no imm-operand `IMUL`/
  `IDIV`) is an explicit "not yet supported" rather than a guessed
  workaround sequence.

### Bitwise operators and the immediate size marker (confirmed via `02_bitwise`)

`AND`(`0x2F`)/`OR`(`0x30`)/`EXOR`(`0x31`) follow the exact same
NAME/NAME/`op`/`ASSIGN`/`EXPR` shape as `+ - * / %`, and `c1` renders
each with the same "load left into `DI`, then operate in place" shape
(`and`/`or`/`xor\tdi,<right>`). Unary `COMPL`(`~`, `0x26`) is the
first *unary* operator confirmed on a non-constant operand: `treeout()`'s
single-child walk emits just the one operand (no second `treeout()`
call, since `COMPL` isn't `BINARY`), and `c1` renders it as "load into
`DI`, then `not\tdi`" in place - the same generic `DI` working
register as every other operator confirmed so far.

**A real, confirmed source-syntax distinction surfaced here for the
first time: `mutos_as`'s `*`/`#` immediate size markers.** `a = 0xF0;`
(240) renders as `"mov *-6.(bp),#240."` while `b = 0x0F;` (15) renders
as `"mov *-8.(bp),*15."` - i.e. `mutos_c1` picks `*value.` (byte-sized
marker) when the value fits a signed byte (`-128..127`) and
`#value.` (word-sized marker) otherwise. Cross-checked two ways: (1)
`man/mutos_as.1`'s "Operand size markers" section confirms `*`/`#`
are honored literally for an immediate operand regardless of magnitude;
(2) actually assembling both lines with the real, already-verified
`mutos_as` and disassembling the result (`objdump -D -b binary -m i386
-M intel,i8086`) shows they produce the *identical* 5-byte `C7 /0 iw`
MOV-word-immediate encoding either way - 8086's plain `MOV` has no
byte-immediate form at all, so the marker choice here is a pure
source-text convention the real compiler's code generator applies
uniformly (presumably shared with the arithmetic/logical instructions
that *do* have a genuine 3-byte `imm8`-with-sign-extension encoding,
where the choice would actually matter), not something that changes
`02_bitwise`'s generated machine code. `c1_gen.c`'s `render_operand()`
implements this for every immediate it renders - and, since the
`09_abiprobe` frame goldens, for every memory-operand displacement too:
`movb #-132.(bp),*1.` for the 128-byte buffer at `bp-132`, while `*-5.
(bp)` in the same file keeps `*` (`man/mutos_as.1`: the marker "has no
effect" on a displacement's encoding - a pure source-text convention
again; every earlier golden's displacements fitted a byte, which is
why this used to be an unconditional `*`). The `-128` lower bound is the natural symmetric
completion of "fits in a sign-extended byte" but, unlike the `127`
upper bound, is not yet independently confirmed by a golden with a
negative large-magnitude constant.

## Current scope

This section grew with the old `mutos_c0`'s grammar (`c0_parser.c`, the
from-scratch front end until 2026-10-18) and with `c1_gen.c`, the
shape-by-shape generator that was `mutos_c1` until 2026-10-13. What it
says about the real compiler's output - the `temp1`/`temp2` stream and
the code each construct gets, as the goldens show them - still holds:
`mutos_c0` and `mutos_c1` now produce that output by the original's own
mechanism. What it says about `c0_parser.c` and `c1_gen.c` themselves
(their grammar subsets, functions, tables and refusals: `parse_expr()`,
`Val`, `gen_call()`, ...) is history; the ports refuse only where the
original traps or reads state they do not model (see "The port of the
real `c0`" and "The port of the real `c1`").

`mutos_c0`'s grammar coverage (`c0_parser.c`) is deliberately narrow -
exactly `tests/mutos_cc/00_smoke`'s three programs plus all of
`tests/mutos_cc/01_expr`: `01_intarith.c`, `02_bitwise.c`,
`03_rellogic.c`, `04_shift.c`, `05_incdec.c`, `06_compasgn.c`,
`07_ternary.c` and `08_castsize.c`:

```
translation-unit  := extdef*
extdef            := IDENT '(' ')' compound-stmt
compound-stmt     := '{' decl* stmt* '}'
decl              := ('int'|'char'|'long'|'float'|'double') declarator (',' declarator)* ';'
declarator        := '*'* IDENT ('[' ICON ']' ('[' ICON ']')?)?
stmt              := assign-stmt | star-assign-stmt | call-stmt | return-stmt
call-stmt         := IDENT '(' ... ')' ... ';'
                    | IDENT ('++'|'--') ';' | ('++'|'--') IDENT ';'
                      (an expression statement - since 2026-10-04,
                      fltprobe/p21_fltexp's "d++;", 11_kernel/01_delay's
                      "i++;"; the parser's parse_call_stmt())
assign-stmt       := IDENT assign-op expr ';'
assign-op         := '=' | '+=' | '-=' | '*=' | '/=' | '%='
                    | '<<=' | '>>=' | '&=' | '|=' | '^='
star-assign-stmt  := '*' ('++'|'--')? IDENT ('++'|'--')? '=' expr ';'
return-stmt       := 'return' expr? ';'
expr              := LOGOR ('?' LOGOR ':' LOGOR)?
LOGOR             := LOGAND ('||' LOGAND)*
LOGAND            := BITOR ('&&' BITOR)*
BITOR             := BITXOR ('|' BITXOR)*
BITXOR            := BITAND ('^' BITAND)*
BITAND            := EQUALITY ('&' EQUALITY)*
EQUALITY          := RELATIONAL (('=='|'!=') RELATIONAL)*
RELATIONAL        := SHIFT (('<'|'<='|'>'|'>=') SHIFT)*
SHIFT             := ADD (('<<'|'>>') ADD)*
ADD               := MUL (('+'|'-') MUL)*
MUL               := UNARY (('*'|'/'|'%') UNARY)*
UNARY             := ('-'|'+'|'~'|'!') UNARY | ('++'|'--') IDENT | POSTFIX
POSTFIX           := PRIMARY ('++'|'--')?
PRIMARY           := ICON | CCON | FCON | IDENT | STRING | cast-expr | sizeof-expr
                    | '(' comma-item (',' comma-item)* ')'
cast-expr         := '(' ('int'|'char'|'long'|'float'|'double') ')' IDENT
sizeof-expr       := 'sizeof' '(' ('int'|'char'|'long'|IDENT) ')'
comma-item        := (IDENT '=' expr) | expr
```

Every function is implicitly `int`-returning (K&R default) and takes
no parameters. A block may declare any number of `int`, `int *`
(single-degree pointer), `int[N]` (single-dimension array), `char`, or
`long` locals
(no initializers; `char`/`long` support only the plain-`IDENT`
declarator, no `*`/`[` forms) before its statements, matching K&R's
declarations-before-statements rule; each statement is a
single-variable assignment (with `=` or any of the ten
compound-assignment operators), a dereferenced-pointer assignment
(`*<ptr-expr> = expr;`, `<ptr-expr>` being a plain pointer variable
with an optional leading/trailing `++`/`--`), or a `return`.
Expression evaluation
(`c0_parser.c`'s `parse_expr()`/`parse_bitor()`/.../`parse_add()`
family) uses the `ExprVal` fold-or-emit representation: a
subexpression stays an unmaterialized compile-time constant for as
long as every operand feeding it is also constant (matching real K&R
`cc`'s own per-operation `build()`-time folding - the golden for
`return 6 * 7;` still contains a single folded `CON(42)` node, never
a `TIMES` opcode), and is emitted as a real `NAME`/operator tree the
moment a variable enters the picture, materializing any constant
sibling as a genuine `CON` leaf at that point - IN ITS OWN PLACE: while a
constant left operand is still pending, the right operand is parsed into
a memory buffer, and the `CON` is written ahead of it once the right
operand turns out not to be constant (`rhs_begin()`/`rhs_end()`), so `7 -
x` is `CON 7, NAME x, MINUS`, v7's order. (Until 2026-09-24 it was
written after the right operand - `7 - x` compiled as `x - 7`, for every
binary operator; see `docs/DEVLOG.md`.) Folding uses 16-bit
truncation (`trunc16()`), matching the target's 16-bit `int` - except
an integer literal too large for a plain `int` (K&R/C89 promotion
rule: `ExprVal` gains an `is_long` flag, carrying the full untruncated
value; see the char/long/cast/sizeof derivation below). A
postfix/prefix `++`/`--` operand and a bare array name used as an
rvalue are never treated as compile-time constants (both always
emit real code - see the increment/decrement/pointer/array
derivation below). A `?:` folds only when its condition AND both
branches are constants (v7/cc/c01.c's `fold(QUEST)`); any other `?:` -
a constant condition included - is a real `QUEST` tree, its parts in
source order (`c1` refuses a constant condition: no golden shows its
shape). A comma list is never folded either (v7 never folds `SEQNC`):
each item, a constant last one included, is written before the `SEQNC`
that consumes it. Nor are `!`, `&&` and `||` of constants (v7's
`fold()` has no EXCLA, LOGAND or LOGOR, and the real `c0` writes `CON 1
/ EXCLA` for `!(12 >> 3)` - `mutos_c0` folded them until 2026-10-14,
when `fuzz_c.py`'s comparison with the emulated real `c0` showed it). A parenthesized comma-list's embedded `IDENT '=' expr`
items are never constants (an assignment always has a side effect, so
it is always emitted for real, exactly like `assign-stmt`'s own
handling).
`sizeof(...)` is the one exception to the fold-or-emit model
entirely: it emits its own `CON(TY_UNSIGN, <size>)` node immediately
and unconditionally, never deferred as a further-foldable `ExprVal`
constant (see that derivation below for why).

`mutos_c1`'s opcode coverage (`c1_gen.c`) matches exactly what the
above grammar can produce: `SYMDEF`, `PROG`, `EVEN`, `RLABEL`, `SAVE`,
`SETREG`, `BRANCH`, `LABEL`, `ANAME`, `NAME`, `CON`, `PLUS`, `MINUS`,
`TIMES`, `DIVIDE`, `MOD`, `AND`, `OR`, `EXOR`, `COMPL`, `LSHIFT`,
`RSHIFT`, `LESS`, `LESSEQ`, `GREAT`, `GREATEQ`, `EQUAL`, `NEQUAL`,
`LOGAND`, `LOGOR`,
`EXCLA`, `AMPER`, `ITOP`, `STAR`, `INCBEF`, `DECBEF`, `INCAFT`,
`DECAFT`, `ASPLUS`, `ASMINUS`, `ASTIMES`, `ASDIV`, `ASMOD`, `ASLSH`,
`ASRSH`, `ASSAND`, `ASOR`, `ASXOR`, `COLON`, `QUEST`, `SEQNC`,
`LCON`, `LTOI`, `ITOC`, `CTOL`,
`ASSIGN`, `RFORCE`, `EXPR`,
`RETRN`, `SETSTK`, `EOFC` (the later categories added their own - calls,
`SWIT`, the data and BSS opcodes, and for structs `STRASG` and `FSEL`,
see the sections below). Its
own value stack (`Val`/`push_val`/`pop_val` in `c1_gen.c`) tracks,
for each pending intermediate value, whether it's an immediate, a
`bp`-relative memory location, a value already in a specific
register, or - new for relational/logical support - an
unmaterialized `VK_COND` (a deferred "L <op> R" comparison, not yet
turned into a real 0/1 value or branch). Deferring a comparison
instead of immediately emitting code for it is what lets `LOGAND`/
`LOGOR` fuse two comparisons into classic short-circuit "jumping
code" without ever materializing an intermediate 0/1 in between -
confirmed byte-for-byte against `03_rellogic.s.golden`; see that
file's derivation in `docs/DEVLOG.md`'s Milestone 4 section. A
`VK_COND` is materialized into a real `di` value (`cmp` + conditional
branch + `mov di,*0./*1.`) the moment anything other than `LOGAND`/
`LOGOR`/`EXCLA` needs an actual value from it (`ASSIGN`'s right-hand
side, `RFORCE`'s operand, and defensively every arithmetic/bitwise
operator, in case a future grammar extension ever feeds a comparison
into arithmetic - e.g. chained relational like `a < b < c`, not
itself confirmed by any golden yet). `LSHIFT`/`RSHIFT` reuse the same
`load_into_di()` working-register convention for the value being
shifted, plus a new `load_into_cx()` (mirroring `load_into_di()`)
for a *variable* shift count specifically - the 8086's "shift by a
register" opcode shape only accepts `CL`. A *constant* shift count
doesn't use an immediate-count opcode at all: plain 8086 has none (it's
an 80186-only extension - `04_shift.c`'s own header comment says this
compiler targets plain 8086 only), so the real compiler repeats the
single-bit-shift form, confirmed against `04_shift.s.golden`'s
`"r >> 2"` (two `sar\tdi,*1` lines) and `"a << 1"` (one `sal\tdi,*1`
line) - but only up to a count of 2: from 3 up it loads the count into
`CX` and shifts by `CL` (`"mov cx,*3."` / `"sar di,cl"` - confirmed later
from the real non-optimized kernel sources, see the `02_array2d`
section below). This is still the minimum
needed to pick a legal 8086 instruction shape (e.g. `IMUL`/`IDIV`
cannot take an immediate operand directly, and an immediate's `*`/`#`
size-marker choice depends on whether it fits a signed byte) without
a general table-driven register allocator (see "Next steps" below).

**Postfix/prefix `++`/`--`, pointer declarations, array declarations,
array-to-pointer decay and pointer dereference all confirmed against
`05_incdec`.** `c0`'s tree shape for an increment/decrement is the
lvalue's `NAME`, then a literal `CON(1)` (the "1" every `++`/`--`
means), then - only when the lvalue is a pointer - `CON(MCC_SZINT)`
and an `ITOP` node that scales the two, then the
`INCAFT`/`INCBEF`/`DECAFT`/`DECBEF` tag itself (`c0_parser.c`'s
`emit_incdec()`); `c1` folds the `CON`/`ITOP` subtree into a plain
immediate at compile time (`OP_ITOP`'s handler) rather than emitting
a runtime multiply - confirmed via `"add *-18.(bp),*2."`, never an
`imul`. Postfix vs. prefix is purely a `c1`-side codegen-ordering
choice, not a different wire shape: `gen_incdec()` in `c1_gen.c`
commits a *prefix* op's `inc`/`dec` (or `add`/`sub`, for the
scaled-pointer case) immediately, then loads the new value into `DI`;
a *postfix* op loads the OLD value into `DI` first and queues the
fixup instruction to be flushed at the enclosing statement's `EXPR`
node - confirmed via `"j = i++;"` emitting `"mov di,*-6.(bp)"` /
`"mov *-8.(bp),di"` / `"inc *-6.(bp)"` in that exact order (the `inc`
strictly after the assignment's own store). A bare array name used
as an rvalue (`"p = a;"`) decays via `NAME` (the array's base element
type/offset, i.e. its first element) followed by `AMPER`
(address-of), which `c1` renders as a real `lea` - confirmed via
`"lea di,*-16.(bp)"`. This is also what surfaced that `ASSIGN`'s type
argument is the lvalue's real type (`TY_INT`, or now `TY_PTR_INT =
TY_INT|010` for `"p = a;"`), not hardcoded `TY_INT` as every earlier
grammar increment happened to have (they were all plain-`int`
assignments, so the distinction was invisible until now).
Dereferencing a pointer as an assignment target (`"*p++ = 1;"`/
`"*++p = 2;"`) is a new statement form
(`c0_parser.c`'s `parse_star_assign_stmt()`, dispatched on a leading
`*` token) whose tree is the pointer sub-expression (with its own
optional postfix/prefix `++`/`--`, reusing `emit_incdec()`) followed
by `STAR`; `c1` represents `STAR`'s result as a new operand kind, an
indirect `(di)` addressing mode (`VK_IND` in the `Val`/`ValKind`
union) - confirmed via `"mov (di),*1."`. Only pointer-to-`int` and
single-dimension `int` arrays are supported; array subscripting
(`a[i]`), multi-level pointers, multi-dimensional arrays, and
explicit `&`/`*` outside array decay and this one
dereference-assignment shape are not yet (see "Next steps" below).

**All ten compound-assignment operators (`+= -= *= /= %= <<= >>= &=
|= ^=`) confirmed against `06_compasgn`.** `c0`'s wire shape reuses
`parse_assign_stmt()` for all eleven (`=` plus the ten
compound-assignment tokens) - the real `ASPLUS`/`ASMINUS`/`ASTIMES`/
`ASDIV`/`ASMOD`/`ASLSH`/`ASRSH`/`ASSAND`/`ASOR`/`ASXOR` opcode
(`v7/cc/c0.h` already has dedicated values for each) is emitted
directly, in the exact same `NAME`/rhs-expr/`<op-tag>`/`EXPR` shape as
plain `=`, never a synthesized `a = a + 5`-style tree. `c1`'s codegen
is where the interesting decisions live: `+= -= &= |= ^=` with a
constant right-hand side each compile to a *single* in-place
`"<mnem> <lvalue>,<imm>"` instruction - never routed through `DI` the
way a non-compound binary operator is, since there is no separate
`ASSIGN` node to do the memory write afterward. `*=` by a constant is
a genuine strength-reduction confirmation, not a guess: `"a *= 2;"`
compiles to `"sal *-6.(bp),*1"`, never an `imul` - 8086 `IMUL` cannot
take an immediate operand directly (the same restriction `OP_TIMES`'s
own codegen already enforces), so the real compiler substitutes a
shift for a power-of-two constant multiply; generalized to any
power-of-two up to 4 via the same repeat reasoning `LSHIFT`/`RSHIFT`
established (only `*2` is itself golden-confirmed; `*= 8` and up would
need a count of 3 or more, whose real shape for a memory operand is
unknown, so it is refused).
`/=`/`%=` need an extra step neither `+=` nor `*=` do: since `IDIV`
can't take an immediate either, the constant is loaded into `CX`
first (`"mov cx,*4."` before `"idiv cx"`), and - because `IDIV`'s
result lands in `AX`/`DX`, never directly in memory - an explicit
`"mov <lvalue>,ax"` (or `dx` for `%=`) store-back follows, the only
compound-assignment op needing one. Only a constant right-hand side
is supported for any of the ten operators (and only a power-of-two
constant for `*=`) - a variable right-hand side is not yet (see
"Next steps" below).

**This also surfaced a real, pre-existing lexer bug**, unrelated to
the grammar/opcode work above: `c0_lex.c`'s
`skip_space_and_comments()` needs to push back *two* characters when
a `/` turns out not to start a `/* ... */` comment (the `/` itself,
plus the character peeked to rule that out), but the lexer's
pushback buffer only ever had room for one - a second `lex_ungetc()`
call silently overwrote the first, dropping a character. This was
invisible for a lone `/` (only a harmless trailing space was lost)
but silently ate the `=` in `/=`, turning it into plain `/` -
`06_compasgn.c` was the first construct in this grammar scope to
actually exercise (and expose) it. Fixed with a proper 2-slot LIFO
pushback stack (`Lexer.peek[2]`/`npeek` in `c0_lex.h`/`c0_lex.c`), a
general lexer-correctness fix rather than a scope-specific
workaround.

**The `?:` conditional operator and the `,` comma operator confirmed
against `07_ternary`.** `c0`'s tree shape for `a > b ? a : b` matches
real K&R `cc` exactly: the condition (`GREAT`), then the two branches
wrapped in a `COLON` node, then `QUEST`. `c1`'s codegen is a **new
branch polarity**, not a reuse of `materialize_cond()`'s existing
bare-comparison-as-0/1-value pattern (`03_rellogic`): the *inverted*
condition branches to a false label, and the *true* branch's code
sits inline at the fallthrough - confirmed via
`"cmp *-6.(bp),di" / "ble L10000" / "mov di,*-6.(bp)" / "jmp L10001" /
"L10000:mov di,*-8.(bp)" / "L10001:"`. Each branch reloads its value
into `DI` unconditionally inside its own arm, with no cross-branch
value tracking - confirmed by the false branch re-loading `b`
(`"mov di,*-8.(bp)"`) even though the comparison's own setup had
already loaded `b` into `DI` moments earlier. The comma operator
emits **no code of its own** - `SEQNC` is a pure value-discard, since
the left operand's side effects (if any) were already emitted by
whichever opcode produced it. This surfaced that a parenthesized
comma-list can contain an embedded plain assignment (`"a = a + 1"` as
a comma-item) - not otherwise reachable from expression context,
since `assign-stmt` is a distinct, statement-level-only production;
supporting it needed `c0_parser.c` to gain one token of lookahead
(`peek2_kind()`) to tell `"IDENT = expr"` apart from a bare `IDENT`
starting a larger expression, both of which begin identically. It
also surfaced that `ASSIGN`'s own result value now has a real
consumer: an assignment used as a discarded comma-operand needs
something on `c1`'s value stack for `SEQNC` to pop, so `OP_ASSIGN`
now pushes the assigned value (previously nothing, since no prior
golden ever consumed one) - which in turn required `OP_EXPR` to
discard that now-leftover value for every *plain* top-level
assignment statement (`gen_fatal`-guarded to at most one leftover
value). The ten compound-assignment operators deliberately do
**not** push a result - `c0`'s grammar structure makes a compound-
assignment expression unreachable from anywhere but a statement's
sole top-level operator, a structural guarantee rather than an
untested convention. Separately, `"a = a + 1;"` surfaced a confirmed
`+1`-specific codegen shape: `"a + 1"` compiles to a plain `INC`,
never `"add di,*1."`; `OP_MINUS` is deliberately left unchanged
(still `"sub di,*N."` unconditionally, including by 1) since no
golden yet shows whether `"x - 1"` gets the symmetric `DEC`
treatment.

**`char`/`long` locals, casts, and `sizeof` confirmed against
`08_castsize`** - a genuine type-system extension, not just a new
operator. A `long`'s wire/memory layout matches
`docs/MUTOS_C_ABI.md` §1.6's already-documented "high word at the
lower address" convention exactly, down to the constant-encoding
level - confirmed via `"l = 70000;"` (`0x00011170`) splitting into an
`LCON` node whose two fields decode to `1` (high) then `4464` (low),
and via every long-producing/consuming opcode moving the low word
through `SI` and the high word through `DI`, storing low-then-high. A
`char` local occupies a full 2-byte stack slot despite its real 1-byte
value - confirmed by `"long l;"` (offset `-10`) immediately followed
by `"char c;"` landing at `-12`, a 2-byte gap, not 1. A
previously-undocumented opcode, `107`, sits between the already-named
`OP_SETREG=105` and `OP_ITOC=109` - confirmed absent from vanilla V7's
`c0.h` too, so this is a genuine MUTOS-1700-specific addition. Named
`OP_CTOL` (char-to-long) by the same `XTOY` convention as every other
conversion opcode here, its codegen is a textbook 8086 sign-extension
idiom: `"movb ax,<mem>"` (the char, loaded into `AX` specifically -
`CBW`/`CWD` are fixed-register instructions, `AL`/`AX` only, a
hardware necessity, not a style choice) then `CBW` then `CWD` then
moved into the `DI`(high)`:SI`(low) convention `LCON` also produces.
Casts are narrowly scoped to a bare-variable operand
(`cast-expr` above, not a general unary-expr) and compile to one of
three confirmed conversion opcodes chosen by (source-type,
target-type): `LTOI` (long→int, truncation - reads only the long's low
word, discarding the high word entirely), `ITOC` (int→char, truncation
- loads into `DX` specifically, not the usual `DI` "working register,"
since the subsequent `movb` store needs a byte-addressable register
and `DI`/`SI` have none on the 8086), and `CTOL` as above.
`sizeof(...)` never emits a wire opcode of its own at all - every
`sizeof(...)` in `08_castsize.c`, including `sizeof(i)` (a variable,
not a type name), folds directly to `CON(TY_UNSIGN, <size>)` at parse
time; for `sizeof(i)`, `i`'s own `NAME` is never even emitted,
confirming `sizeof`'s operand is genuinely never evaluated, only its
type inspected - matching real C semantics exactly. Only these three
(source, target) cast pairs, a bare-`IDENT`/bare-type-keyword `sizeof`
operand, and a memory (not register-pair) `long`/`char` operand for
every new opcode are supported.

**`long` `*`/`/`/`%` confirmed against `02_long/02_muldiv.c`.** An
integer literal with an explicit `l`/`L` suffix (`37L`) is now its own
`T_LCON` token, always `long`-typed regardless of magnitude (unlike a
bare oversized literal like `70000`, which is only promoted to `long`
by K&R/C89's usual too-big-for-`int` rule) - `parse_primary()` gained a
dedicated case for it. `ExprVal` gained a `type` field, tracked for a
`NAME` reference too (not just a still-folded constant), so
`parse_mul()`'s `*`/`/`/`%` can emit `OP_TIMES`/`OP_DIVIDE`/`OP_MOD`
with a `TY_LONG` operand type whenever either side is `long` -
confirmed against `"c = a * b;"` (`a`, `b`, `c` all `long`). `c1`'s
codegen for these three is a real runtime-helper call, not inline
8086 instructions (the 8086 has no 32x32 hardware multiply/divide):
both operands pushed flat as ordinary two-word `long`s, right-to-left
(`r_low, r_high, l_low, l_high`), `call lmul`/`ldiv`/`lrem`, `add
sp,*8.`, result read from `DX:AX` into the `DI`(high)`:SI`(low)
convention `OP_LCON`/`OP_CTOL` already use - **not** the pointer/
lvalue-and-result convention `docs/MUTOS_C_ABI.md` sect. 1.8's own
prose speculates, which this golden does not use. Only a plain memory
(`NAME`) operand is supported on either side so far. Separately,
`OP_LCON`'s own `c1` codegen turned out to have two distinct confirmed
shapes, not one: an int-range value whose high word is just its low
word's sign-extension (e.g. `37L`) uses `mov ax,<lo> / cwd / mov
di,dx / mov si,ax` (`OP_CTOL`'s idiom, starting from an immediate
instead of a `movb`), while a genuinely 32-bit value (e.g. `123456L`,
or `08_castsize`'s `70000`) keeps the prior direct `mov si,<lo> / mov
di,<hi>` split. `long` `+`/`-` were unconfirmed as of that session -
02_long/01_addsub.c` also needed `if` (`03_ctrlflow` scope) to get a
verifiable golden; both are now confirmed too (see below).

**All of `03_ctrlflow` confirmed: `if`/`else`, `while`, `do`/`while`,
`for`, nested `break`/`continue`, `switch`/`case`/`default`, and
`goto`/labels.** `mutos_c0` gained a real recursive-descent statement
dispatcher (`parse_statement()`) in place of the prior flat assign/
return-only loop; every construct reuses the existing `CBRANCH`/
`BRANCH`/`LABEL` opcodes and `p->isn` as the same function-wide label
counter `cfunc()` already seeded - only `switch` needed a genuinely new
opcode (`SWIT`). `if`/`else`'s `CBRANCH(false_lab, cond=0)` skips the
true-branch on false, allocating a second `end_lab` only once an
`else` is actually seen; its own `line` argument is confirmed to be
the line of whatever token follows the condition's `)`, not the `if`'s
own line - a direct, for-free consequence of v7/cc's one-token
lookahead there (done to check for the "simpif" shortcut below).
`while` is the textbook 2-label shape; `do`/`while` allocates 3 labels
up front in a fixed order (`contlab`, `brklab`, then the body's own
top label) but only places the top label immediately, deferring
`contlab`'s placement to right before the trailing condition test.
`for` matches `v7/cc/c02.c`'s `forstmt()` exactly, including its most
surprising piece: the increment clause is PARSED before the body (to
keep the token stream in order) but its EMITTED CODE is deferred until
after the body, keeping the increment's own `EXPR` opcode on the
source line it was originally written on (the `for`-header's own
line), not wherever body-parsing left off - since `mutos_c0` streams
wire bytes as it parses (no AST to re-emit later the way real `cc`'s
`rcexpr(st)` does), this is implemented by buffering the increment's
bytes via `open_memstream()` and flushing them verbatim after the
body. `break`/`continue` (`p->brklab`/`p->contlab`, plain ints) are
saved locally and restored by each loop/switch parser function around
its own body, so nesting falls entirely out of C's own call stack.
**The "simpif" shortcut**: an `if`'s body that is EXACTLY a bare
`goto label;`/`break;`/`continue;` compiles to a single direct
`CBRANCH(target, cond=1)` - no extra label at all - confirmed against
both `05_breakcont.c` and `07_goto.c`; without it, `if (j == 3) break;`
would emit a different, non-matching shape. `goto`/labels use a small
function-scoped label-name table (name -> label number, allocated on
first mention - whichever comes first, a `name:` definition or a
`goto name;` reference), correctly resolving both a backward
definition (`"loop:"`) and a forward reference (`"done:"`, referenced
before its own definition). `switch`/`case`/`default` matches v7/cc's
`pswitch()`: the controlling expression is `RFORCE`'d (the same
wrapper `return` uses) and emitted as its own statement, a `BRANCH`
jumps past the body to a fresh dispatch label, the body parses inline
(case/default just place a label and record a `(label, value)` pair,
or, for `default`, the label alone as `deflab`), and the dispatch site
gets `OP_SWIT(deflab, line)` followed by every collected pair and a
single lone zero-word terminator (`line` is the body's own closing
`}`, needing a new `p->prev_line` field - "the line of the token most
recently consumed" - since the nested `parse_statement()` call had
already moved past it by the time control returns). `c1`'s dispatch
codegen is a genuine jump table (`sub`/`cmp`/`bhi`/`shl`/`xchg`/
`seg cs`/indirect `jmp`) - confirmed only for a DENSE, CONTIGUOUS run
of case values; a sparse switch's compare-chain shape is unconfirmed
and explicitly not yet supported. One confirmed, unexplained quirk:
the subtract-to-normalize immediate renders in HEX (`sub ax,#/1`, per
`man/mutos_as.1`'s leading-`/` literal syntax), unlike every other
immediate in this codegen; and the table's own internal label burns
one extra label first (`L10001`, not `L10000`) for reasons this one
example can't derive.

**`long` `+`/`-`, an implicit `int`->`long` widening conversion, and a
`long`-vs-constant relational comparison, all confirmed against
`02_long/01_addsub.c`.** `OP_PLUS`/`OP_MINUS` now carry `TY_LONG` when
either operand is `long` (the same rule already confirmed for `*`/`/`/
`%`). Codegen has two confirmed shapes depending on the right operand:
a plain memory operand loads the left into `DI:SI` then `ADD`/`ADC`
(or `SUB`/`SBB`) read the right straight out of memory; an in-range
`long` CONSTANT right operand is sign-extended into `DX:AX`, pushed to
the stack to free those registers, has the left operand loaded into
`SI:DI`, then popped back into `BX`(high)`:CX`(low) before the same
`ADD`/`ADC` pair - confirmed down to a literal real-hardware asymmetry:
the second `pop` renders as `"pop cx"` with a plain space, not a tab,
unlike every other instruction here. Assigning a plain int-range
constant (no `L` suffix, e.g. `"b = 23456;"`) to a `long` lvalue now
inserts a new `OP_ITOL` opcode before `ASSIGN` - `c1`'s codegen is the
same `CWD` sign-extension idiom `OP_CTOL` already uses, just starting
from a plain immediate. A `long` relational comparison against a
constant (`"if (c > 0L)"`) is genuinely different from a 16-bit one - a
32-bit signed comparison on a 16-bit ALU compares HIGH words SIGNED
first (deciding the answer outright whenever they differ), only
falling through to an UNSIGNED low-word compare when they're equal;
only this exact shape (`OP_GREAT` against a literal `0L`, at a "branch
if false" `CBRANCH` site) is confirmed, any other operator/operand/
branch-sense combination is explicit "not yet supported". This also
surfaced that a comparison's own wire `type` argument is ALWAYS plain
`TY_INT` regardless of operand type (a comparison's RESULT is always
`int`), so `long`-ness is detected from the operands themselves - which
in turn required `OP_LCON` to become LAZILY materialized (a raw,
unmaterialized `(hi, lo)` pair pushed with no code emitted at all, since
a `long` constant used as a comparison operand never loads into
registers in the real output - it folds straight into a bare `cmp`
immediate), confirmed backward-compatible with every already-passing
golden since `ASSIGN` was already always the very next opcode after
`LCON` in every one of those cases.

**A real, pre-existing bug was also found and fixed**: `CMP`'s
immediate right-hand side does NOT universally omit the trailing `"."`
decimal-terminator - every confirmed `03_rellogic` case happened to be
value `0` specifically (the one genuine exception); `03_ctrlflow`'s
non-zero cases confirm the period is present otherwise, matching
`render_operand()`'s ordinary convention.

**Anything outside this - unary `-`/`+` on a non-constant operand
(unary `~`/`!` are covered), a non-`int`/non-pointer/non-array/non-
`char`/non-`long`
declaration, array subscripting, a compound-assignment operator's
right-hand side being anything other than a compile-time constant
(or, for `*=`, anything other than a power-of-two constant), a `?:`
branch that is itself a bare relational comparison, a compound-
assignment operator inside a comma-list, a cast combination other
than the three confirmed ones, `sizeof` on an array or a general
expression, any `long` operand outside a bare `NAME` or a directly-
confirmed constant shape, a `char`/`long` pointer or array, a sparse
(non-contiguous) `switch`, a file-scope pointer, array or initialized
variable, a `static` function, `++`/`--` or a compound assignment on a
static or file-scope variable,
a second simultaneously-live `register` variable (or one of a type
other than plain `int`), a function pointer with a non-empty
parameter signature, an immediate `IMUL`/`IDIV`
operand outside a confirmed compound-assignment shape, any opcode `c1` doesn't recognize - is a clear, explicit
"not yet supported" diagnostic and a nonzero exit status, never
silently-wrong output.** This is a deliberate design choice, not an
oversight: `tests/mutos_cc/run_goldens.sh` relies on this to keep its
pass count an honest measure of verified coverage as grammar support
grows.

The lexer (`c0_lex.c`) is comparatively complete (the full K&R token
set: all keywords, integer/float/string/char literals, every
standard operator) even though the parser only consumes a subset
today - this is real, immediately-testable code (not a placeholder),
kept complete because extending grammar coverage should not require
revisiting tokenization.

### Function parameters, calls, local `static` variables, and function pointers (confirmed via `04_funcs`)

**Parameters.** A K&R-style function definition -
`name(a, b) int a, b; { ... }` - is now supported: the parenthesized
name list is parsed first (bare identifiers only), then zero or more
`TYPE declarator-list;` statements bind each name to a type, in
*declared-parameter-list* order (not necessarily the order the type
statements themselves appear in, though every confirmed golden's
order matches both - see `c0_parser.c`'s `parse_param_decls()`). A
parameter is `hclass SC_AUTO` - the exact same representation as a
body local, just with a positive, upward-growing offset
(`MCC_STARG=4, 6, 8, ...` - `c0_sym.c`'s `symtab_declare_param()`) -
confirmed against `01_call.1.golden`'s `_a=4`/`_b=6` and
`02_manyargs.1.golden`'s six parameters up to `_f=14`. A genuinely
confirmed ordering quirk: a parameter's `ANAME` is emitted between
`SETREG` and the function body's own `BRANCH`/`LABEL` pair - *before*
any body-local's own `ANAME`, which still comes after that `LABEL` as
always (see `c0_parser.c`'s `cfunc()`).

**Calls.** `IDENT '(' args? ')'` in expression position (or a bare
`IDENT '('` lookahead in `parse_primary()`) calls that function - the
callee is `NAME(SC_EXTERN, TY_INT|FUNC=16, name)`, K&R's implicit
"extern function returning int" rule: no prior declaration is
required. Arguments are built as a left-associative chain of
`COMMA`(`TY_INT`) nodes (wire opcode `9` - the lexer *token* value K&R
reuses as a tree operator here, entirely distinct from the
already-implemented comma-*operator*'s own `SEQNC`=97) - a single
argument uses no `COMMA` at all, and zero arguments emits a lone
`NULLOP` leaf (`v7/cc/c04.c`'s `treeout(NULL)` shape). Every argument
is materialized immediately as parsed (never left foldable, unlike a
parenthesized comma-list's own last item). A top-level function
*prototype* (`int iseven();` - needed before `isodd()` calls
`iseven()`, since K&R still wants *some* prior declaration
syntactically) is parsed and entirely discarded at the wire level -
`04_mutrec.1.golden` shows no extra output for it at all; every call
site emits the same `NAME` leaf regardless.

Codegen (`c1_gen.c`'s `gen_call()`): arguments are pushed
right-to-left (`docs/MUTOS_C_ABI.md` sect. 1.1), caller-cleanup via
`add sp,N` (`N` = 2 bytes per argument *word*, not per argument - a
`long` argument occupies two, pushed low-word-first then high). An
argument is pushed AS-IS whenever 8086's `PUSH` can take it directly
(a register or any addressable memory operand - confirmed against
`07_funcptr.s.golden`'s `push *-6.(bp)`, a plain local pushed with no
preceding `mov` at all); only a genuine immediate needs `DI` first
(`PUSH` has no immediate form). The call's result is always in `AX`
(sect. 1.5); `OP_RFORCE` now skips its usual two-instruction "move
into AX" when the value is *already* `AX` (a call result, or an
`OP_TIMES`/`OP_DIVIDE` quotient - mirroring `v7/cc/c10.c`'s own
`rcexpr()`/`movreg()` "already in the target register" skip). When an
AX-resident call result instead becomes `TIMES`'s own operand
(`03_recfact`'s `n * fact(n - 1)`), the existing "load left into AX"
codegen now checks *both* operands for "already AX" and keeps
whichever one is, regardless of source left/right position - producing
a real, confirmed (not invented) `mov ax,ax` self-move.

One incidental fix this pulled in: `OP_MINUS` by exactly 1 now gets
the same `DEC` treatment `OP_PLUS` by 1 already had (`03_recfact`'s/
`04_mutrec`'s `n - 1`) - previously left unconfirmed. And `(int)`
(`OP_LTOI`) applied to a `long` *parameter* must stay a lazy memory
reference (`VK_MEM_CVT`) rather than eagerly loading into a register
the way it does as a plain assignment's rhs (`08_castsize`'s
already-confirmed shape) - confirmed against `02_long/04_params.s.
golden`'s `fd + (int) offset` -> `add di,*8.(bp)` (used directly as
`PLUS`'s operand, no separate `mov`).

**Local `static` variables.** `static int n;` inside a function body
allocates a dedicated `.bss` block instead of stack space: `BSS`,
`LABEL`(fresh), `SSPACE`(size), `PROG` opens it (`c0_parser.c`'s
`parse_static_decl()`), and `SNAME`(name, that same label) declares it
(`hclass SC_STATIC`, and - unlike an AUTO local - its "offset" *is*
the label number, not a stack displacement - `c0_sym.c`'s
`symtab_declare_static()`). Every later reference reuses that label
number; `c1_gen.c` renders it as a bare `L<n>` operand (`mov di,L4`,
`mov L4,di`) via a new `VK_STATIC` kind, confirmed against
`05_staticvar.s.golden`'s `counter()` in full. Its value genuinely
persists across calls for free, since nothing about it is
stack-relative.

**Function pointers.** A function-pointer declarator -
`int (*fp)();` (local) or `int (*f)();` (parameter) - has type
`TY_INT|FUNC|PTR = 72`, `v7/cc/c04.c`'s `incref(FUNC)` applied once
more (`incref(t) = ((t & ~TYPE) << TYLEN) | (t & TYPE) | PTR`,
`TYPE=7`/`TYLEN=2`/`PTR=8`; `incref(16) = 72` - confirmed against
every `ANAME`/`NAME`/`AMPER`/`ASSIGN` touching `f`/`fp` in
`07_funcptr.1.golden`). A bare function name used as a *value* (not
called - `fp = square;`) needs `c0` to recognize `square` as a known
function despite it not being a local - `c0_parser.c` now keeps a
whole-file registry of every function name defined or prototyped so
far (`register_func()`/`is_known_func()` - `main()`, the last function
in the file, can see `square`/`cube` because they were defined
earlier), and emits the same callee `NAME` a call would use, wrapped
in `AMPER`(72). Codegen-wise this `AMPER` is *not* the array-decay
one: a function's address is a link-time constant, so **no code is
emitted at all** - `07_funcptr.s.golden`'s `fp = square;` goes
straight to a memory-immediate `mov *-6.(bp),#_square`. An indirect
call `(*f)(x)` dereferences `f` via `STAR`(`TY_INT|FUNC`=16) -
likewise a pure type-level operation with no code of its own,
leaving `f`'s own plain memory reference untouched for `CALL` to
render as `call @*4.(bp)` (`@` = mutos_as's indirect-call marker,
confirmed against `07_funcptr.s.golden` in full). Only a plain
zero-argument-signature function pointer is supported - a pointer to
a function taking parameters, an array/struct member of function-
pointer type, and any multi-level derived type are explicit "not yet
supported".

**`long`-returning functions (confirmed via `02_long/03_retval`).** A
function definition may now carry an explicit `'int'|'char'|'long'`
return-type prefix (`long addlong(a, b) long a, b; { ... }`) - previously
only an EMPTY-parameter-list, body-less top-level declaration
(`int iseven();`) could follow a type keyword at all;
`parse_extdef()`/`parse_top_prototype()` were unified into one function so
a typed declaration with a real parameter list and body now flows into
`cfunc()` like an implicit-`int` definition, just carrying its own
`ret_type` (`04_mutrec.c`'s prototype-only path is unchanged, still
requiring an explicit type, empty parens, and a trailing `;` with no
body). Every function's return type is tracked in a new `functypes[]`
array parallel to `funcnames[]` (`register_func()`/a new
`lookup_func_type()`), driving `OP_CALL`'s type argument at each call
site (previously always hardcoded `TY_INT`) and, via a new
`p->cur_ret_type` set once per `cfunc()`, `OP_RFORCE`'s/`OP_RETRN`'s type
argument too. A genuinely separate wire-format delta: the callee's own
`NAME` leaf's type is `ret_type | 020` (the same FUNC-degree bit
`TY_FUNC_INT` already uses for `TY_INT`), not always `TY_FUNC_INT` -
confirmed against `03_retval.1.golden`'s `_addlong` callee using type 22
(`TY_LONG|020`). Codegen (`c1_gen.c`): a `long`-returning call's result
comes back in `DX:AX` and is moved into the `DI(high):SI(low)` convention
(`gen_call()`'s new `is_long_ret` parameter); `OP_RFORCE`'s new `TY_LONG`
case does the reverse move via `materialize_long()`. `OP_RETRN` itself
needed no change - its `|RTYP n` rendering was already generic. One more
gap this surfaced: `gen_call()`'s `VK_LCON` (a `long` constant call
argument) needed the SAME two-shape split `materialize_long()` already
has for an assignment target - a genuinely 32-bit value still
direct-splits (`04_params.c`'s already-confirmed `90000L` shape), but an
int-range value merely carrying an `L` suffix (`addlong`'s own `5L`
argument) instead uses the CWD sign-extension idiom.

**`06_regclass.c`'s `register` storage-class hint actually changing codegen
(confirmed).** Contrary to the source file's own comment ("a K&R compiler
is free to ignore it"), the real compiler does NOT ignore `register` - a
`register int i;` local is allocated a real physical register (`di`) for
the function's entire body, with no stack slot at all. `parse_decl()` now
accepts a leading `'register'`; for a plain (non-pointer, non-array) `int`
declarator, a new `try_claim_register()` mirrors `v7/cc/c03.c`'s
`goodreg()` exactly (`if (regvar < 3) return -1; return --regvar;`), using
a new `p->regvar` (reset to `MCC_INIT_REGVAR`=4 per function) - MUTOS has
exactly 2 claimable "working" registers (`di`/`si`) vs. v7's larger PDP-11
set, so slot 3→`di` (confirmed) and slot 2→`si` (the structurally next
slot this same algorithm hands out, not itself golden-confirmed). A
claimed variable is declared `hclass SC_REG` (`c0_sym.c`'s new
`symtab_declare_reg()` - `offset` is the register slot number, not a stack
offset) and emits `SETREG(newregvar)` *immediately before* its own new
`RNAME`(216, "BSN"-shaped like `ANAME`/`SNAME`) opcode - confirmed against
`06_regclass.1.golden`'s exact byte order. `register` on anything else
(a pointer/array/`char`/`long` declarator, or once no slots remain)
silently falls back to an ordinary `AUTO` local, exactly v7's own
`goodreg()`-fails-so-`skw=AUTO` fallback. `cfunc()` emits a RESTORE
`SETREG(MCC_INIT_REGVAR)` right before the final `LABEL`/`RETRN` if
`p->regvar` changed during the body (mirrors `v7/cc`'s `statement()`
LBRACE-block-exit restore). `c1_gen.c`'s `|NREG n` comment (previously
always silent) now renders `n = regvar - 1` on every SETREG *after* a
function's first (position, not value, is what distinguishes the two -
the end-of-function restore's raw value is numerically identical to the
silent initial one). `OP_NAME`'s new `SC_REG` case pushes
`val_reg(<physical register>)` - since `di`/`si` are already this
codebase's generic "working registers", most existing codegen worked
unchanged; genuinely new shapes: `i + 1` → plain `inc di` (the existing
`"+1"→INC` optimization applies for free); `sum = sum + i` loads the
*other* operand into `si` instead of `di` when the right operand is
already the live register variable (loading the left into `di` as usual
would clobber it); `i = i + 1`'s `ASSIGN` emits no instruction at all when
lhs and rhs resolve to the same register (the `+1` already wrote the new
value in place); and, more far-reaching, `di` becomes unavailable as the
GENERIC scratch register for the rest of the function once claimed - a new
`GenState.di_reserved` flag drives a `load_into_si()` fallback in
`OP_RFORCE`'s default path specifically (confirmed via `return sum;`
rendering through `si`, not `di`) - every other "go through DI" site is
left unchanged, since none is exercised with a live register variable by
any golden yet. See `docs/DEVLOG.md`'s Milestone 4 section for the full
byte-level derivation.

**`05_arrptr` (4 of 7 files: `01_arrbasic`, `03_ptrbasic`, `04_ptrarreq`,
`06_ptrptr`) confirmed - single-dimension array subscripting, multi-level
pointers, general `&`/`*`, and pointer/array-parameter equivalence.** The
first category needing real pointer-degree arithmetic instead of a flat
`TY_PTR_INT` constant: `c0_parser.c` gained `ty_incref_tag()`/`ty_ptr_of()`/
`ty_decref()`, transcribing v7/cc's own `incref()`/`decref()` formula
(`new_type = ((t & ~7) << 2) | (t & 7) | tag`) - confirmed via `int **pp;`
using type 40, not a naive "16" (see `docs/DEVLOG.md`). `a[i]` compiles to
`*(&a + i*sizeof(elem))`, reusing `05_incdec.c`'s `AMPER`/`ITOP`/`PLUS`/
`STAR` shapes via a new `emit_subscript()`; `*(a + i)` reaches the identical
tail through a new pointer-arithmetic case in `parse_add()`. `parse_unary()`
gained real `'&'`/`'*'` as general (not just `"*p = ...;"`-statement-
special-cased) operators, recursing for a chain of leading `'*'`s
(`**pp`). The interesting new work was almost entirely in `c1`: scaling a
*non-constant* subscript index by a constant size (previously `OP_ITOP`
only folded two compile-time constants), a dereferenced value feeding
further arithmetic needing force-materialization first (`"mov di,(di)"`,
since `ADD` can't take two memory operands), and two real surprises -
an indirect-assignment target whose right-hand side needs its own working
registers gets its address PUSHED to the hardware stack rather than left in
a register (`VK_IND_PENDING`, using one-to-few opcodes of real lookahead via
`ftell`/`fseek` on `temp1` - the first time `c1_gen.c` has ever needed
this), and `OP_AMPER`'s array-decay is deferred by default (`VK_MEM_DIRECT`)
so a compile-time-constant-index subscript can fold away entirely and a
bare array-decayed call argument doesn't get clobbered by a later sibling
argument under right-to-left pushing. Full byte-level derivation, including
the dead ends ruled out along the way, in `docs/DEVLOG.md`'s Milestone 4
section. `05_arrofptr.c`/`07_strlibc.c` (string literals) remain open -
see "Next steps" below.

**`05_arrptr/02_array2d.c` (2-D arrays) confirmed.** `int m[N][M]` locals
(`SymEntry.dim2` = M; three or more dimensions are refused). `m[i][j]` is
exactly two ordinary subscript steps, the row decayed again in between:

```
NAME(m, TY_INT) AMPER(8) <i> CON(M*2) ITOP(104) PLUS(8) STAR(0)
                AMPER(8) <j> CON(2)   ITOP(8)   PLUS(8) STAR(0)
```

The outer `ITOP`'s 104 is "pointer to array of int"
(`ty_ptr_of(ty_ary_of(TY_INT))`); it is the only node that keeps that type
because v7/cc's `disarray()` → `setype()` retypes the STAR/PLUS/AMPER/NAME
chain it walks (always the left operand) down to the element level but
never visits the `ITOP` on `PLUS`'s right - see `emit_subscript_2d()`'s
comment and `docs/DEVLOG.md`. `c1` cancels the row's `STAR`/`AMPER` pair
(v7/cc/c12.c `optim()`'s first rule, `&*x → x`), keeps both scaled indices
symbolic (`VK_SCALED`/`VK_ROWADDR`) and emits the address the way
v7/cc/c12.c's `distrib()` factors `i*R + j*E` into `(i*(R/E) + j)*E`:

```
lea di,&m / mov si,i / sal si,*1 (x log2(R/E)) / add si,j / sal si,*1 / add di,si
```

confirmed by `02_array2d.s.golden` (R/E = 4) and `10_integ/05_matmul.
s.golden` (R/E = 2), whose all-constant subscripts (`a[0][1] = 2;`) also
fold to a single bp-relative operand exactly like 1-D `v[0]`. Refused as
unconfirmed: a constant index next to a runtime one (`m[0][j]`,
`m[i][2]`), an index that is not a plain variable, a row size that is
not a power-of-two multiple (at least 2) of the element size, and a bare
or half-subscripted 2-D array (`m`, `m[i]`). The same file confirmed two
further `c1` shapes, `i * 10` → `mov ax,i / mov cx,*10. / imul cx`
(a constant multiplier goes through CX; powers of two, 0 and 1 stay
refused - V7 strength-reduces those) and an AX-resident value plus a
memory operand added in place (`add ax,mem`, never moved to DI first -
`05_matmul` shows the same with the product on the right). And the real
compiler's constant-shift threshold, from the non-optimized kernel
sources in `tests/mutos_as/kernel_nonopt/`: a register is shifted by
repeating the single-bit form for a count of 1 or 2 only; from 3 up it is
`mov cx,*N.` / `sal reg,cl` (see `MCC_SHIFT_REPEAT_MAX` in `c1_gen.c`) -
previously `c1` repeated the single-bit shift N times for any N, silently
wrong from 3 up. `<<=`/`>>=`/`*=` on a memory operand with a count above
2 is now refused, since no golden shows its shape.

**`char` elements and the MUTOS `char` conversions (confirmed via
`09_abiprobe`'s six `0N_frameNNN` files, `10_integ/04_strrev` and
`06_struct/06_union`'s `c1` half).** The MUTOS front end, unlike vanilla
v7, writes explicit char conversions - opcode 109 (`OP_ITOC`), its type
argument the RESULT type:

```
buf[0] = 1;          ... STAR(1) CON(1) ITOC(1) ASSIGN(1)
buf[0] + buf[79]     ... STAR(1) ITOC(0) ... STAR(1) ITOC(0) PLUS(0)
return buf[0];       ... STAR(1) ITOC(0) RFORCE(0)
t = *a;              NAME(t) NAME(a) STAR(1) ASSIGN(1)     (char = char: none)
```

`mutos_c0` inserts them where v7's `build()` applies conversions (see
`promote_char()`'s section in `c0_parser.c`): each char operand of a
binary arithmetic, shift, bitwise, relational or equality operator is
widened (`ITOC(0)` right after its bytes - for a left operand before the
right operand is parsed), and every `=` converts its right-hand side to
the target's type (`convert_assign()`: int → char `ITOC(1)`, char → int
`ITOC(0)`, char → long `CTOL`, int → long `ITOL`, char → char nothing);
`return` in an int function converts like an assignment (v7's `doret()`),
and `(int) c` is `ITOC(0)`. Where v7 converts nothing - a condition, an
operand of `&&`/`||`/`!`/`~`/`?:`, a call argument, the last operand of
a comma operator - a char OBJECT (a char variable, element or
dereference) is written unconverted, as v7 does; any other char value
there (a `(char)` cast's, a char assignment's) is refused - see "More
`char`" below. `&` of a
subscripted element (`&s[lo]`: the element reference plus `AMPER`) and
the `if (...) return;` shortcut (`CBRANCH` straight to the return label,
like `goto`/`break`/`continue`) came with `04_strrev`.

In `mutos_c1` a char in memory is a byte operand (`Val.bytev`) that only
the consumers written for it accept - a char `ASSIGN`, `ITOC`, `CTOL`,
`AMPER`; any other consumer refuses it, so a missing conversion can never
reach a word instruction (it used to: a plain char variable used as an
int was read as a whole word, including its slot's unused high byte).
`ITOC(0)` of a byte operand is a lazy `VK_CHARX`, loaded by its consumer
as `movb ax,<mem>` / `cbw` - into AX only, so of two, the left one moves
to DI first:

```
movb ax,*-84.(bp) / cbw / mov di,ax / movb ax,*-5.(bp) / cbw / add di,ax
```

Confirmed consumers: int `+` of two chars (above), `return` (`movb
ax,*-24.(bp)` / `cbw`, already in the return register), and an int
assignment (`mov <lhs>,ax`, from the non-optimized kernel output in
`tests/mutos_as/kernel_nonopt/amx.s`). A char with one int operand, a
char compared with an int, a char call argument and condition are in
"More `char`" below. A
byte through
a pointer variable goes through DX and BX (`mov dx,*4.(bp)` / `mov
bx,dx` / `movb dx,(bx)` - the int case uses DI), through an address
already in DI as `(di)`, and a char store through DX (`movb
*-6.(bp),dx`); a char target whose right-hand side has code is pushed and
popped into BX like an int one (`push *4.(bp)` ... `pop bx` / `movb
(bx),dx`). `ITOC(1)` of a constant folds to its sign-extended low byte,
so `buf[0] = 1;` is one `movb *-84.(bp),*1.`. Refused: a constant stored
through a char pointer, and a constant index on a char pointer (`p[2]`).
An `ITOP` scaling by 1 (a char index) leaves a plain variable index in
memory (v7 drops the multiplication), so `&s[hi]` is `mov di,*4.(bp)` /
`add di,*8.(bp)`.

**Pointer arithmetic: what is refused.** Only `pointer + int` (and
subscripting) is implemented. `mutos_c0` refuses pointer subtraction
(`p - 1` needs the `ITOP` scaling `+` gets, `q - p` v7's division by the
element size), `int + pointer` (v7 scales the left operand, already
written when the pointer is seen) and an ordered comparison with a
pointer operand (v7's unsigned `LESSP`... operators); `mutos_c1` refuses
an address (`&x`, not yet `lea`'d) as a comparison or int `+`/`-`
operand. Each of these used to compile silently wrong; `==`/`!=` of two
pointers is unaffected, and an address is loaded with `lea` wherever it
is used as a value (`p = &x;`, `f(&x)`, `return &x;`).

**File-scope variables and block scope (confirmed via `07_scope`).** A
top-level declaration without `(` is a variable (`c0_parser.c`'s
`parse_global_var()`), written the way v7's `extdef()` writes it:

```
int counter;          CSPACE "_counter" 2           .comm   _counter,2
static int hidden;    BSS NLABEL "_hidden" SSPACE 2 .bss / _hidden:.blkb 2.
extern int total;     (nothing)
```

(the size rounded up to a word, `rlength()`), at the declaration's own
position in the stream - `03_externdef`'s `int total;` sits between
`addto()` and `main()`, after an `extern int total;` that wrote nothing.
One MUTOS delta: v7 writes `SYMDEF("")` in front of a static's `BSS`, the
MUTOS front end does not (`mutos_cc.h` delta 5). Every file-scope name is
`hclass SC_EXTERN` whatever its storage class (v7 declares it through
`decl1(EXTERN, ...)`) and is referenced by its symbol - `NAME(12, type,
"_counter")`, the shape a callee's `NAME` already had - so `c0_sym`
keeps a second, file-wide table (`Parser.globals`, searched after the
function's own by `lookup_var()`), and `emit_name()` writes either shape.
A redeclaration is accepted when type and linkage agree. Supported: a
plain `int`/`char`/`long` name, a `char name[]` initialized with a string
(`10_integ/01_wordcount`, below), and - since `fltprobe/p3_global` - a
`float`/`double` variable, array or pointer (`parse_global_fvar()`: `double
ga[3];` is `CSPACE "_ga" 24`, `double *gp;` `CSPACE "_gp" 2`), a floating
variable initialized with a floating literal (`double gi = 2.5;` - `SYMDEF
"_gi"`, `DATA`, `NLABEL "_gi"`, `FCON`, `INIT(DOUBLE)` (104), `EXPR` - v7's
`cinit()`; `mutos_c1` writes `.globl _gi` / `.data` / `_gi:<TAB>.double<TAB>
2.50000000000000000e+00`, the `FCON` using up a c1 label) and a struct
variable of any kind (`CSPACE` of its size; `mutos_c1` compiles a floating
member's access and refuses an int one's). Any other file-scope pointer,
array or initializer and a `static` function are refused.

`mutos_c1` reads such a `NAME` as a memory operand named by its symbol:
the `VK_STATIC` kind a local `static` already used, with a new `Val.sym`
(the text pooled by `intern_name()`, so a `Val` stays freely copyable)
in place of the label number - so a global goes through exactly the
code paths a local static does (`mov di,_counter` / `inc di` / `mov
_counter,di`, as `mov di,L4` / `inc di` / `mov L4,di`), and `&x` is the
immediate `#_counter` (`mov di,#_proc` in the non-optimized kernel
output). `CSPACE` renders `.comm\t_counter,2` - no trailing `.`, and
decimal, as the kernel's `.comm\t_msgbuf,1024` shows - and `NLABEL` a
`_hidden:` label glued onto the `.blkb` line like an `L<n>:`. What a
local static cannot do, a global cannot either: `++`/`--` and compound
assignment on it, and a `long` one (the long handlers take a
bp-relative operand only), are refused. `x = y;` between two memory
operands - local, static or global - goes through DI (`mov di,*-8.(bp)`
/ `mov *-6.(bp),di` for `10_integ/03_linklist`'s `head = cur;`); with a
`register` variable in DI it is refused (the kernel uses DX there, in a
function with two). For two local statics `c1` once wrote `mov L4,L5`,
which `mutos_as` rejects.

A compound statement nested in a function body has a scope of its own
(`parse_nested_block()`, v7's `statement()` LBRACE case with
`blockhead()`/`blkend()`): its declarations may shadow an outer name
and vanish at the `}`; their slots continue below the enclosing
block's (`02_shadow`: outer `x` at -6, inner `x` at -8, `SETSTK 8`) and
the allocation point goes back at the `}`, so sibling blocks share
slots (v7's `sauto = autolen; ... autolen = sauto;` - the corpus has no
sibling blocks with declarations, and the kernel corpus no mid-body
declaration at all, so this part is v7's algorithm, not golden-
confirmed); a `register` local claimed inside gives its slot back with
a `SETREG` at the `}`. The function body itself shares the parameters'
scope, as in v7. Before, a nested declaration went into the function's
single flat scope: shadowing was refused ("'x' redeclared"), and each
sibling block got fresh slots (a larger frame than the real compiler's).

**A file-scope `char` array, character constants, and a char compared
with a constant (confirmed via `10_integ/01_wordcount`).** `char text[] =
"...";` is `extdef()`'s initialized-declarator case
(`parse_global_chararray()`): `SYMDEF "_text"` (none for `static`),
`DATA`, `NLABEL "_text"`, then the string's `BDATA` runs from the
label-less `putstr()` - in temp1 - and `EVEN` when the size (the string
plus its NUL) is odd. v7's lexer numbers every string token as it reads
it, so the initializer uses up a label number although nothing is
written under it - `main()`'s labels start at 2. The name is a
file-scope char array; an element is the ordinary subscript shape,
`NAME(12, 1, "_text") AMPER(9) <i> CON(1) ITOP(9) PLUS(9) STAR(1)`.
Only the flexible `[]` form with a string is supported (a sized array,
a brace list, no initializer, `extern` with one: refused). A character
constant (`'\n'`) is an int `CON` (v7's `getcc()`, a byte above 127
sign-extended). And a char variable, element or dereference compared
with a constant in 0..127 is NOT widened: the constant is typed char
instead - `STAR(1) CON(1, 10) EQUAL(0)` - which is v7 `c12.c`
`optim()`'s CHAR retyping of a relational's constant, done by the MUTOS
front end itself (`char_compare_rhs()`: the right operand is buffered,
so the left one's `ITOC` can still be written when the right one turns
out not to be such a constant).

In `mutos_c1` the data renders as `.globl _text` / `.data` /
`_text:.byte\t/74,...` (the string-literal renderer, `put_bdata_run()`,
9 values to a line) / `.even`. `text[i]` - the array's address, a
link-time constant, plus an index the `ITOP` by 1 left as the variable
itself - is a pending `VK_SYMIDX` until the `STAR`, which loads the index
into DX and on into BX and makes the element `_text(bx)`. Compared with a
char-typed constant (a `VK_COND` with `cond_is_byte`), a char in memory
is a byte compare, in the two shapes of v7's `cctab`: `cmpb
_text(bx),*10.` (also for an addressable char - `cmpb *-8.(bp),*97.` in
`kernel_nonopt/lp_AC.s`), and, for a computed element against 0, the
byte loaded and tested - `movb dx,#_text(bx)` / `orb dx,dx` / `beq`.
Loaded into a register through v7's `#1` operand the element's symbol
takes the `#` marker (`o_load()`: also `movb dx,#_amxi_bu(bx)` before a
char store in `kernel_nonopt/amx.s`); as an instruction operand or read
as an int (`movb ax,_amxscd(bx)` / `cbw`) it does not. A local char
array element with a runtime index (addressed through DI), a store into
a file-scope array element, `&text[i]`, `text + i`, a constant index,
and a char compared with a negative or larger constant, a non-constant
or a constant on the left are refused.

**More `char`: an int operand, call arguments, conditions, a store into
a global array element.** No golden has these; each shape is the real
compiler's own output in `tests/mutos_as/` (see `docs/DEVLOG.md`'s
`06_struct` section, part 1). A char with one int LEAF operand (a
constant, a variable, a `register` local) is computed in AX, the char
moved left when the operator commutes: `movb ax,<c>` / `cbw` / `add
ax,*-48.` (`c - '0'`: v7's `x - c` -> `x + -c`), `inc`/`dec` for 1,
`and`/`or`/`xor ax,<int>`, `mov ax,ax` / `mov cx,*20.` / `imul cx`,
shifts (`gen_charx_binop()`). A char AND a constant 0..127 skips the
`cbw` and uses DX (`movb dx,#_amxscd(bx)` / `and dx,*12.`), and a branch
on an AND's result reads its flags directly - `and dx,*12.` / `beq`, no
`cmp` (`Val.flagsv`); an int AND/OR/XOR of a value already in AX or DX is
done in place too. A char compared with an int leaf goes left and is
compared as an int (`movb ax,*22.(di)` / `cbw` / `cmp ax,*-6.(bp)`). A
char object as a call argument is `movb ax,...` / `cbw` / `push ax`, as a
condition a byte compare with 0 (`cmpb *1.(si),*0`, or `movb dx,#_t(bx)`
/ `orb dx,dx`). A store into a file-scope char array element pushes the
index: `push *-34.(bp)` / `mov dx,*-30.(bp)` / `pop bx` / `movb
_amxscd(bx),dx`. Refused: a computed int operand, `x - c`, a char shift
count, two chars (except their sum), a constant stored into a global
array element, and a char value that is not an object (a cast's, an
assignment's) where v7 converts nothing.

**Structs, unions, bit-fields, enums and typedefs (confirmed via
`06_struct` and `10_integ/03_linklist`).** `mutos_c0` lays a struct out
as v7's `declist()`/`align()` does (a non-char member on a word boundary,
bit-fields packed into words, union members at offset 0, the size a
whole number of words) and writes nothing for a declaration. Every struct
and union is type `TY_STRUCT` (4) on the wire, with the usual degrees;
which one travels beside the type code (`StructDef`, `c0_sym.h`). A member
reference is v7's `build()`: `a.b` = `(&a)->b`, `p->b` = `*(p + CON
offset)`, the whole NAME/AMPER/PLUS/STAR spine retyped to the member's
type (`setype()`) - `p.x` is `NAME(p, INT) AMPER(8) CON 0 PLUS(8) STAR(0)`,
and in `pts[i].x` only the `ITOP` keeps `PTR.STRUCT` (12) - so a chain is
collected and written once its last member is known ("Member and
subscript chains" in `c0_parser.c`). Two new opcodes: a bit-field's STAR
is followed by `FSEL(UNSIGN, bitoffs, flen)` (10, "BNNN"), a whole-struct
assignment is `ASSIGN(4)` + `STRASG(4, size)` (115, "BNN"). Enum
constants are int `CON`s, a typedef name stands for its type, `sizeof`
takes type names, a pointer cast of a call types the `CALL`
(`(struct node *) malloc(...)`), and arithmetic on an unsigned (a
bit-field's value, or since round 4 an `unsigned` local - see below) is
typed `UNSIGN` - `/`, `%`, `>>` and ordered comparisons on it are refused.

In `mutos_c1`, a 4-byte struct is assigned as a long (v7's `strasg()`),
a 2-byte one as an int. A bit-field is read as `mov di,<word>` / `sar`
(the constant-shift rule) / `and di,<mask>` and assigned from a constant
the way v7's `lvfield()` does: 0 as `and <word>,<~mask>` (decimal), a
value equal to the mask as `or <word>,<value>`, anything else as `and
<word>,` + the complemented mask in hex (`*/fff3`, `#/ff0f`) + `or
<word>,<value << bitoffs>` - `07_bitfield.s.golden`. Member offsets end up
as one displacement (`rp->botright.y` -> `*6.(di)`), and a store through
a pointer plus a non-zero offset computes its right-hand side first (see
`docs/DEVLOG.md`'s `06_struct` section, part 3). Refused: file-scope
struct/union/unsigned variables, a function returning a struct or union
(or a pointer to one), a struct parameter by value, `unsigned` parameters,
`register`, pointer or array ones (a plain `unsigned` local is accepted),
`long` members, char and union bit-fields, an array of arrays as a member,
a struct of more than 4 bytes assigned whole, a struct whose size is not a
power of two subscripted, a computed or out-of-range value stored into a
bit-field, `&`, `++`/`--` or a compound assignment on a bit-field, and a
char member reached through a pointer plus an offset.

**Floating point (confirmed via `08_float`).** `mutos_c0` takes `float`
and `double` locals (frame slots of 4 and 8 bytes - `float a, b, c;` at
`-8`/`-12`/`-16`), floating constants and `+ - * /`, assignment, casts
and `return`, as v7's `build()` writes them (`c0_parser.c`'s "Floating
point"): a constant is `FCON(DOUBLE, "<its source text>")` (opcode 23,
"BNF" - `c0_outcode.h`'s `F`, no `_`), never folded; every operator and
assignment is typed `DOUBLE` (3), even between floats and into a float -
only a `NAME` keeps `FLOAT` (2), and float/double convert with no node;
the conversions are `cvtab[]`'s: `ITOF` (51) and `LTOF` (57) to floating,
typed with the target's type (`d = i;` - `ITOF(DOUBLE)`; into a float,
`ITOF(FLOAT)`) or, inside `+ - * /`, the floating operand's, and `FTOI`
(52) / `FTOL` (56) from it (`(int) c`, `(long) d`, `(int) d`). `return c;`
in an int function is `FTOI RFORCE` (`doret()` returns through an
assignment). An integer left operand is converted right after its own
bytes, so the right operand is buffered until its type is known
(`arith_rhs_begin()`/`float_arith()`).

`mutos_c1` generates calls into libc.a's software floating-point runtime
(`tests/mutos1700_libc/`: `stacks.o`, `singles.o`, `doubles.o`,
`convert.o`, `lconvert.o`), which works on a stack of doubles and takes a
memory operand's ADDRESS in AX: `lea ax,<x>` / `call flds` (`fldd` for a
double) loads, `lea ax,<y>` / `call fadds` (`fmuls`, `fdivs`; `fsubs`;
`faddd`... for a double operand) combines the top with memory, `lea
ax,<z>` / `call fstsp` (`fstdp`) stores and pops; an int goes `mov
di,<i>` / `mov ax,di` / `call itof`, and `call ftoi` leaves an int in AX,
`call ftol` a long in DX:AX (then `mov di,dx` / `mov si,ax`). A constant
is written where it is read, `.data` / `L10000:<TAB>.float
3.50000000000000000e+00` / `.text` (`%.17e`, the label from c1's own
counter), and a file with floating code ends `.globl<TAB>fltused` before
its `.data` (v7's `nfloat`). Only a constant exactly representable as a
float is accepted - the real compiler keeps any other in 8 bytes, in a
form no golden shows (libc.a's `ecvt.o` has `10.0` and `1.0` in 4 bytes
but `.03` in 8) - and of those only non-zero ones below 2**24 with at
most 18 significant digits, whose `%.17e` is exact. The golden-confirmed
shapes are `flds`/`fldd`/`fstsp`/`fstdp`/`fadds`/`fmuls`/`fdivs`/`itof`/
`ftoi`/`ftol`; the rest follow the same pattern and the runtime's entry
points. `mutos_as` assembles the output (`.float`, `lea <reg>,<label>`),
and linked with the real `crt0.o`/`libc.a` both goldens return their C
sources' values under an 8086 emulator - see `STATUS.md`'s Milestone 2
section. Full derivation in `docs/DEVLOG.md`'s `08_float` section and the
`mutos_as` section after it.

**Floating point beyond the corpus (`libc.a`'s compiled C, then the
`fltprobe` goldens).** `libc.a`'s `atof.o`, `ecvt.o`, `gcvt.o` and `fltpr.o`
are C compiled by the real compiler and gave the first shapes (see
`docs/DEVLOG.md`'s "Floating shapes from libc.a's compiled C");
`tests/mutos_cc/fltprobe/`'s real-hardware goldens corrected and
completed them - round 1's nine, the four each of rounds 2, 3 and 6, round
4's three, round 5's `p17`/`p18` and round 7's `p24`..`p27` are byte-exact,
`p19` up to the real compiler's own invalid output, `p21` and `p28` with the
real compiler's own error messages (see `docs/DEVLOG.md`'s "The fltprobe
goldens" and the round-2 to round-7 sections after it). The real
compiler is
v7's `c1` with the PDP-11 floating code replaced by calls into the runtime,
so its **operand order is v7's**,
decided on the whole tree - and `mutos_c1` plans every expression with a
floating node from the pre-scanned tree (`plan_fvalue()` in "Floating
point", next to the integer "Evaluation order" planner):

- **degree** (`fdeg()`): v7's `degree()`, with a floating constant that is
  exactly a float at **1** (a 4-byte `.float`, typed FLOAT - `a < 1.5`
  loads 1.5 first) and an 8-byte `.double` one at 0 (`d < 0.1` is
  exchanged, `0.1 * a` loads the float `a` first - `p6_dblcon`); a double
  variable 0, a float one 1, a converted int, a negation or a value read
  through a pointer `max(1, operand)` (`*p + 1.5` keeps its order), a
  local's address (`lea`) 1, so an element of a local array subscripted by
  a variable 2 (`1.5 + arr[i]` loads the element first - `p10_elem`), a
  `+`/`*` chain acommute()'s, `-`/`/`/a shift optim()'s, a call 10;
- **`+`/`*` chains** in acommute()'s order (`plan_fchain()`; each term in
  the register context the term before it leaves - `p15_fltinf`), a
  **relational's exchange** by degree (mirrored by the handler), the
  **left operand always first**: a right operand in memory is combined
  from there (`fadd<s|d>`), a computed one computed after the left and
  combined on the stack (`fadd` - `d - i` is `fldd d` / `itof` / `fsub`);
- **constants**: each written constant numbered when the expression is
  planned, then each int constant converted (`fplan_constants()`); a
  constant's `.data` block printed where v7's `cexpr()` prints it, when its
  operator is matched (`SEG_FDATA` - ahead of a computed left operand); a
  negation folded into the constant, `-(-x)` dropped; the text from
  `c1_fltdec.c` - `libc.a`'s `ecvt()` of `libc.a`'s `atof()` on the 56-bit
  software double, 18 digits, exactly as the real `c1`'s `printf("%.17e")`
  writes it - a `.float` when the value is exactly a float, else an 8-byte
  `.double` (`fldd`, `faddd`...);
- **comparisons**: `call fcmp` / `sahf` / a signed branch, a zero compared
  like any constant (never tested), as a condition, under `&&`/`||`/`!`
  (`plan_cbranch()`), or as a 0/1 value;
- **int conversions** (`gen_itof()`): through DI - `mov di,i` / `mov ax,di` /
  `call itof`; any int computed there the ordinary way, then `mov ax,di`
  (`i + j`) - also after a conversion or an element (`(double) i +
  (double) j`, `arr[i] + j` - `p13_open`) - or, after a computed `*`, `/`
  or a call, straight into AX (`mov ax,c`; `c - '0'` as `mov ax,c` / `add
  ax,*-48.`, `i + 1` as `mov ax,i` / `inc ax`, `i + j` as `mov ax,i` / `add
  ax,j` - `p7_itofreg`; `i - j` as `mov ax,i` / `sub ax,j`, `i << 1` as `mov
  ax,i` / `sal ax,*1`, `j + 0` as `mov ax,j` - `p13_open`) - `SEG_FITOF`;
  DI also after `*p` (`p16_open2`), an assignment's right-hand side's
  context after it (`(e = d * 2) + i`: AX); a remainder from DX (`mov
  ax,dx`), an `unsigned` variable as a long (`mov si,u` / `sub di,di` /
  `push si` / `push di` / `call ltof` / `add sp,*4` - `p16_open2`); a
  product or a quotient is in AX already, a char loads there
  (`movb ax,c` / `cbw`), a register variable in DI goes through SI; `ltof`
  for a long (pushed through DI, or SI while DI holds a register variable,
  `add sp,*4`), `ftoi`/`ftol` back, a char through `ftoi` / `movb`;
- **compound assignments**: `+=`, `-=`, `/=` load the target first when the
  right operand is in memory, `*=` the right operand; `+=` with a computed
  right operand computes it first and adds the target from memory (`d +=
  c`: `movb ax,c` / `cbw` / `call itof` / `lea ax,d` / `call faddd` -
  `p16_open2`), `/=` loads the target first (`p2_arith`); a used value is
  `fst<s|d>` (no pop); `i *= e` into an int is
  `call ftoi` / `mov ax,ax` / `imul i` / `mov i,ax`, `i += d` / `i -= d`
  `call ftoi` / `add i,ax` / `sub i,ax` (`p8_misc`; v7's `i + (int)d`), `j
  /= e` `call ftoi` / `mov cx,ax` / `mov ax,j` / `cwd` / `idiv cx` / `mov
  j,ax` (`p13_open`); a floating zero is an ordinary constant (`d + 0.0`:
  `.float 0.0...` / `flds` / `faddd d` - only an int `+ 0` is tossed), a
  negated one written as the zero (`p16_open2`); a constant converted to an
  int is not folded (`(int) 2.5`, `i = 2.5`: `flds` / `ftoi` -
  `p16_open2`);
- **calls**: double arguments `sub sp,*8` / `mov ax,sp` / `call fstdp` (no
  decimal point), pushed right to left through the call planner; a double
  or float result from `fac` (`call fldd`), none when unused; float and
  double functions return through `fac` (`|RTYP 2`/`|RTYP 3`);
- **objects**: locals, parameters, file-scope (`.comm`, `.bss`, initialized
  `_gi:<TAB>.double<TAB>...` data), `extern`, local `static` (`L4`); an element or
  member at a constant offset folded into one operand (`SEG_FLEAF` -
  `lea ax,16.+_ga`, `8.+_gs`, `*-12.(bp)`); an element subscripted by a
  variable addressed as an int element is (`lea di,arr` / `mov si,i` /
  `mov cx,*3.` / `sal si,cl` / `add di,si`) and used through DI (`lea
  ax,(di)`), as an assignment's target after the right-hand side
  (`p8_misc`); a value through a pointer variable (`mov di,p` / `lea
  ax,(di)`, when its address is needed); `&` of each (`mov _gp,#_gd`, `lea
  di,*-20.(bp)`);
- **initializers** (`gen_finit()`, v7's `doinit()`): a floating literal, an
  int constant or either negated; a `double` variable's a `.double` of the
  value whatever it is (`= 2`: `.double<TAB>2.00000000000000000e+00`), a
  `float`'s a `.float` of the value truncated to a float (`= 0.1`:
  `9.99999940395355225e-02` - `libc.a`'s `fstsp` keeps the high half,
  `c1_fltdec.c`'s `fdec_render_single()`); each takes a `c1` label
  (`p9_init`).

Rounds 3 to 5 confirmed every shape the round before left to inference
but three: an element of a local array has degree 2 (`1.5 + arr[i]` loads
the element first - round 3), the context in a chain is the previous
term's (round 4), and a right element at offset 0 is computed first (see
"Integer shapes from the round-3 to round-5 goldens"). Round 5's
`p18_fltop3` matched at once: `d += e * 2` computes `e * 2` first and adds
`d` from memory, `f += i` into a float, `(d * e) + (i % j)` (the
remainder first, `mov ax,dx`), `(long) 3.75` (`flds` / `ftol`), a negated
zero as an initializer (`.double 0.0...`).

**`-=` with a computed right operand** (round 5, `p19_open3`): the target
is loaded FIRST, as for `/=` - `d -= c` -> `lea ax,d` / `call fldd` / `movb
ax,c` / `cbw` / `call itof` / `call fsub` / `lea ax,d` / `call fstdp`, `d
-= e * 2` the same around `flds 2.0` / `fmuld e`; no reversed subtraction
(`SEG_FLOADT`). Only `+=` computes its right operand first.

**An `unsigned` converted after a computed `*`, `/` or call is refused for
good**: the real compiler's own output there is not valid assembly
(`p19_open3.s.golden`'s `d = (d * e) + u` -> `mov ax,u` / `mov <118 bytes
of libc's _ctype_ table>,ax` / `sub ax,ax` / `push <the same>` / `push ax`
/ `call ltof` - its code table asks for the register pair starting at AX,
and the register-name table ends there). `run_goldens.sh` checks that
golden up to the line before (`../../tests/mutos_cc/invalid_goldens.txt`).
The real `as` refuses those lines too (`***ERROR*** syntax error, line
229`, real hardware 2026-10-07, `../../tests/mutos_cc/fltprobe/p19_as.log`):
no MUTOS 1700 program can contain this code.

**Floating lvalues and expression forms (round 6, `p20_fltlv`,
`p21_fltexp`).** A double struct member is an operand and a target like a
variable (`s.x`, and through a pointer: `mov di,q` / `lea ax,*8.(di)`); a
pointer to double is accepted as a parameter, a local and a function's
return type (`double *pick()`: RETRN typed PTR.DOUBLE, `*pick(a, 2)` ->
`call _pick` / ... / `mov bx,ax` / `lea ax,(bx)` / `call fldd`), subscripted,
offset and incremented (`p++` -> `add p,*8.` - a statement-level `++`/`--`
is done in place); `+=`/`-=`/`/=`/`*=` into a member or an element keep
the target's address on the machine stack (`lea ax,(di)` / `|` / `push
ax` / `call fldd` / `lea ax,c` / `call fadds` / `pop ax` / `call fstdp`;
`*=` loads its right operand first); a store through a pointer variable
after a `*` goes through AX (`mov ax,p` / `mov bx,ax` / `lea ax,(bx)`).
`d = e = 2.5` stores the inner target without a pop (`fstd e` / `fstdp
d`); `d++`/`++d` as statements add a `.float` 1.0 (`fldd d` / `fadds` /
`fstdp d`), `e = d--` keeps the old value under the new (`fdup`), `e =
++d` stores the new one with a pop and loads the variable again (`fstdp d`
/ `fldd d` - round 7's `p24_fltinf2`; v7's prefix `++` yields its lvalue),
and a float's `f++` statement is `flds f` / `fadds` / `fstsp f` (`mutos_c0`:
`CON 1, ITOF(2), INCAFT(3)`) - its value used as a double's (round 8's
`p33_fltinf3`: `e = f++` -> `flds f` / `fdup` / `fadds` / `fstsp f` /
`fstdp e`, `d = ++f` -> ... `fstsp f` / `flds f` / `fstdp d`); a char operand is widened and
converted (`movb ax,c` / `cbw` / `itof`), so is a computed int (`(double)
(i + j)`: `mov di,i` / `add di,j` / `mov ax,di` / `itof`) and a negated one
(`neg di`); a long sum is converted from DI:SI (`push si` / `push di` /
`ltof`); `(unsigned) d` is `ftol`, the low word; `?:` with double arms
loads each arm (`plan_quest()`), a comma typed DOUBLE its last operand.

**Round 7 (`p24_fltinf2`)** added: a `?:` as an operand has degree 2
(`optim()`'s default for a binary node - `r + (x ? d : e) * 2.0` computes
the `?:` first and multiplies the `.float` 2.0 in from memory); `q->x +=
d * e` pushes the target's address and loads the target FIRST (`lea
ax,(di)` / `|` / `push ax` / `fldd` / ... / `fadd` / `pop ax` / `fstdp` -
`SEG_FLOADTP`), and so does `q->x /= e`, which then loads the divisor
(`lea ax,e` / `fldd` / `fdiv`) where `-=` subtracts from memory (`fsubd`);
`a[i + 1] = 2.0` scales the index by 8 through CL (`mov cx,*3.` / `sal
si,cl`) and keeps the constant term as the displacement (`lea
ax,*8.(di)`); `d = q->k` converts the member through DI (`mov di,*16.(di)` /
`mov ax,di` / `itof`); `*pick(a, 1) = 2.5` stores through DI (`mov di,ax`
/ `lea ax,(di)` / `fstdp` - a read through a call's result goes through
BX).

**Round 8 (`p33_fltinf3`)** added: the value of a compound assignment into
a member, `e = (q->x += d)` - the right operand loaded FIRST, the target's
address pushed, the target combined from memory, stored without a pop
(`fldd d` / `mov di,q` / `lea ax,(di)` / `|` / `push ax` / `faddd` / `pop ax`
/ `fstd`; `*=` inferred the same, `-=` and `/=` refused) - which `mutos_c0`
now writes as an embedded compound assignment (`parse_comma_item()`,
`lvalue_asop_ahead()`); and a prefix `++`/`--` of a floating variable as an
operand of `+`, `*`, `-` or `/` is done FIRST, as a statement, its operator
then taking the variable as an operand like any other (`e = ++d * 2.0` ->
`fldd d` / `fadds 1.0` / `fstdp d` / `flds 2.0` / `fmuld d` - v7's
`sreorder()`; `is_fpreinc()`, `plan_fhoist()`).

**Round 9 (`p36_fltinf4`)** added: `sreorder()` takes a `+=` / `-=` on a
named target the same way (`e = (d += 1.0) * 2.0` -> `fldd d` / `fadds 1.0`
/ `fstdp d` / `flds 2.0` / `fmuld d` - a statement's pop, `SEG_FHOIST`),
and a prefix `++`/`--` or such an assignment as an operand of a floating
comparison (`x = ++d > 2.0` -> the increment, then `flds 2.0` / `fldd d` /
`fcmp` - the constant, degree 1, first, the relation mirrored); a postfix
one as an operand has degree 2 (`e = d++ * 2.0` -> `fldd d` / `fdup` /
`fadds 1.0` / `fstdp d` / `fmuls 2.0`); the value of `-=` and `/=` through
a pointer loads the target first, as a statement's `/=` does, and keeps the
result (`e = (q->y -= d)` -> `lea ax,*8.(di)` / `|` / `push ax` / `fldd` /
`fldd d` / `fsub` / `pop ax` / `fstd`); the value of `+=` into an element
loads the right operand before the element's address (`e = (a[i] += d)`).
A computed right operand of a hoisted `+=`/`-=`, a member or a constant
element as its target, and `+=` / `-=` / `*=` / `/=` through a pointer with
a computed right operand, its value used, were inferred and confirmed by
round 10 (`p40_fltinf5`), which also showed v7's `reorder()` at work on a
floating `+`: `e = (d += 2.0) + (e -= 1.0)` hoists `e -= 1.0` FIRST
(`sreorder()` of a `+` reorders its right operand before its left one) -
`plan_fsreorder()` and `plan_fchain()` now simulate `reorder()`/
`sreorder()` over the chain (`fsim_plus()`, `fsim_times()`), sorting it
with the unhoisted node's degree before each hoist and the NAME's after
(`fdeg_live`); a hoisted float compared with a `.float` constant is the
NAME it has become, so the constant goes first (`x = ++f > 2.0` -> `flds
2.0` / `flds f` / `fcmp` / `blt` - `frel_swap()`); and the value of `e =
(q->x += d * e)` computes the right operand first and adds the target
from memory, `q` addressed in the register context the right operand
left - after a `*` through AX into BX (`mov ax,q` / `mov bx,ax` / `lea
ax,(bx)` / `|` / `push ax` / `faddd` / `pop ax` / `fstd`), as `e = (q->x
*= d * e)` (`fp_lea_ctx()`). A `+=` chain reached as a returned value or
an argument, two hoisted floats compared, a member target at an offset
after a `*`, `q->y = d * e`, the value of `+=` into an element with a
computed right operand (the right operand first, then the address), a
hoist inside a hoisted `+=`'s right operand (done while that operand is
computed) were inferred and confirmed by round 11 (`p44_fltinf6`, all
fifteen), which also settled two of them there: `e = (d += ++w + ++y) *
2.0` hoists `++w`, then `++y`, then `fldd w` / `faddd y` / `faddd d` /
`fstdp d` - the hoisted statement's `rcexpr()` (called with a negative
register) runs no `reorder()` of its own, its template's `rcexpr()` of
the right operand does, and `reorder()` of a `+` as a node takes its left
operand first (`fchain_direct()`). Round 12 (`p48_fltinf7`) confirmed a
statement `d += ++w + ++y;` (its `reorder()` sreorder()s the `+` as an
operand: `++y` first), a `-=`, three terms, a `+=` operand there, a `+=`
through a pointer or into an element with a hoist in its right operand
(not hoisted itself), and settled two hoists under a `*` or a `-` there
(left to right - the node `reorder()`ed); it corrected two: a hoisted
`+=` that is itself an operand of a `+` (`e = (d += --w + --y) + 1.0`) is
reached by `reorder()`, which `sreorder()`s its right operand first - `--y`
before `--w` (`fsim_reorder()`); and `x = (d += ++w + ++y) > 2.0` stores d
WITHOUT a pop (`fstd d`), the value left on the stack and used by nothing
- the real compiler's own wrong code, which its stack model does not
report (`plan_fhoist()`'s `keepv`, `fp_store()`'s `hoistkeep`, the model
one higher - `stmt_fleft`; taken for two or more hoists in the right
operand of a comparison's operand: the mechanism - v7's efftab template
not matching, `rcexpr()` falling back to regtab - is an inference).
Round 13's `p52_fltinf8` corrected that reach: a hoisted `+=` or `-=` under
a comparison whose VALUE is taken is kept (`fstd`) whenever its right
operand is not a leaf - one hoist inside (`x = (d += ++w + 1.0) > 2.0`) or
none (`x = (d += w + y) > 2.0`, `x = (d += a[i] * e) > 2.0`) - and popped
under a condition (`if ((d += ++w + ++y) > 2.0)`): v7's `cexpr()` reaches a
comparison's value through `rcexpr(..., regtab)`, whose `reorder()` hoists
the `+=`, before it branches; a condition goes to `cbranch()` directly
(`plan_fhoist()`'s `keepv`: `frel_branch()`, `fdcalc_leaf()`; a constant, a
variable and `*p` count as leaves). And `e = -(d += ++w + ++y)` hoists out
of the negation, left to right (`is_fpreinc()` takes a NEG parent). Round
14's `p56_fltinf9` asks about their neighbours.

**The real compiler's own floating-stack errors** (round 6,
`p21_fltexp`; round 7, `p28_fltstk`): an assignment whose value is a
floating call argument (`f = half(d = 3.0)`) can be stored WITH a pop and
then pushed as the argument by a second `fstdp` - wrong code, which the
real `c1` reports from its compile-time model of the floating-point stack:
"floating point stack underflow" (a store below the bottom), "Floating
point stack underflow" (`ftoi`, `fadd`, `fmul`, `fcmp` below it), exit
status 1, the `.s` complete. `mutos_c1` reproduces the code, all fourteen
messages of the two goldens and the exit status (`fp_track()`,
`c1_error()`, `tests/mutos_cc/c1_errors.txt`). `p28` showed how the model
goes on: it is one per FILE (never reset between functions, so a balanced
statement in the next function still reports), a statement may end below
the bottom silently, and a double function's value stored into `fac` pops
without a check, like an argument push. Which store the real compiler
picks under a call is the CALL's own consumer's matter (round 8's
`p30_fltstk2`, eight more stores, all `fstd`): `fstdp` - the double pop -
when the call's value goes nowhere (`half(d = 3.0);`) or straight into the
statement's floating store (`f = half(d = 3.0)`), `fstd` - right code, no
message - under anything else: a conversion (`x = half(d = 3.0)`), an
operator (`e = half(d = 3.0) + 1.0`), a comparison, a `return` in a double
function (`argpop_end`, found by `plan_expression()`). Round 9's
`p37_fltstk3` (seventeen messages) added: a call of an int function whose
value goes nowhere pops too (`ihalf(d = 3.0);`), and so does a call that is
itself an argument of such a call (`f = half(half(d = 3.0))`), while `x =
ihalf(d = 3.0)` keeps; and the push of an argument is checked after all -
with the upper-case message - when the argument is neither an
assignment's value nor a call's result (`e = two(d = 1.0, 2.0)`: the 2.0's
push reports, the stores into d and e the lower-case message). Round 10's
`p41_fltstk4` (twenty-three messages) refined that: the push is checked
for a value LOADED for it - a variable (`half(d)`), a constant - and not
for one computed onto the stack before it - a sum (`half(d + e)`), an int
converted, a call's result, an assignment's value (`push_fp_arg()`: a
`VK_FACC` is pushed unchecked); a call nested in an int conversion keeps
its inner store, one nested in an unused call or in one whose value is
stored pops it. Round 11's `p45_fltstk5` (twenty-three messages): a float
variable's push is checked like a double's, a negation, a conversion, a
product and a call's value multiplied are not - and neither is an element
subscripted by a variable or a member through a pointer (`half(a[i])`,
`half(q->y)`): a value read through an address is v7's `*` node, not a
NAME (`push_fp_arg()`: `FM_IND` unchecked). Round 12's `p49_fltstk6`
(twenty-six messages) confirmed the rest as inferred: a file-scope double,
a constant-indexed element of a file-scope array, a local struct's member
and a local static are checked (NAMEs to v7's `optim()`), `*p`, `p[1]` and
a float element are not (`*` nodes). Round 13's `p53_fltstk7` showed the
model counting the value an `fstd` leaves (as inferred) and the stores of
the hoisted `++w` and `++y` reporting in UPPER case: a compound
assignment's or increment's store does, an assignment's in lower case
(`fp_compound`). And `p52_fltinf8`, written to report nothing, showed the
model's top: the six values its value-comparisons left pile up, and a push
beyond six reports "Floating point stack overflow; simplify expression"
(twenty-two messages - `fp_track()`'s `FP_STACK_MAX`; the depth still goes
up). Round 14's `p57_fltstk8` asks whether an unhoisted compound store
reports in upper case too, and about `fdup` and a call's value at the top.

Refused, each with its own diagnostic: an int converted after an element
or `*p` in AX context, or after a computed operand other than those above;
an int computed for AX other than a variable, `x + c`, a sum or difference
of two variables, a shift by a constant or a product/quotient/remainder -
an int member read through a pointer among them; `%`/`%=`, a floating
value tested for truth or negated with `!` (the real `c1` itself reports
"Floating point stack underflow" on one), a long computed other than a sum
converted, a double element subscripted by a computed index other than a
variable plus or minus a constant (`a[i + j]`), a pointer to a pointer to
double, an initializer other than a constant, and every operator `fdeg()`
has no degree for (`&`, `|`, `^`, ...).

**Integer shapes from the round-3 to round-5 goldens.** Round 3: a
dereferenced left operand of an int `*` whose right operand has code of
its own is loaded into its register first, as for `+`/`-` (`p10_elem`'s
`ps[i].c * qs[i].g`: `mov di,*4.(di)`, the right element's address into
SI, `mov ax,di` / `imul *12.(si)`; `load_now()`); an int `+ 0` (and `- 0`)
is dropped, v7's acommute() toss (`(j + 0)` converted is `mov ax,j`); and a
value in AX plus a constant stays in AX (`return r + (int) d - 30`: `call
ftoi` / `add ax,*-42.(bp)` / `add ax,*-30.`), `+ 1` / `- 1` as `inc ax` /
`dec ax`. Round 4 (`p14_axint`) confirmed the last stored to memory (`s = x
/ y + 3`, `f() + 1`, `x * y - 1`), `+ 0` / `- 0` in DI and `x /= f()` /
`y %= f()` (`mov cx,ax`), and showed that a right element at offset 0
(`b[j]`, not `qs[i].g` or `a[j + 1]`) is computed FIRST: for `*` its value
pushed (`... / mov di,(di)` / `push di` / the left / `mov ax,di` / `pop
cx` / `imul cx` - `order_right_first()`, as `05_matmul`'s 2-D elements),
for `&` its address (`push di` / the left, loaded / `pop bx` / `and
di,(bx)` - `is_pushaddr()`, `ORD_PUSHADDR`; `+`, `-` and `|`, which share
v7's table entry, by inference, a `+` chain's first link too). Round 5
(`p17_elem2`) settled that it is the offset, not the context: `return a[i]
* b[j]` pushes `b[j]` as `p14`'s assignment does, `s = ps[i].c * qs[i].d`
does not, as `p10`'s `return` does not. Why the offset decides is still
not understood (both kinds have the same v7 degree). It confirmed `+`, `-`,
`|` and a `+` chain's first link, and corrected two inferences: `^` pushes
the address too (`pop bx` / `xor di,(bx)` - not v7's PDP-11 rule that
`^`'s right operand be in a register), and a COMPARISON of two elements
at offset 0 pushes the LEFT one's address, loads the right one and
compares in place - `if (a[i] > b[j])` -> `lea di,a` / ... / `push di` /
`lea di,b` / ... / `mov di,(di)` / `pop bx` / `cmp (bx),di` / `ble`
(`is_pushleft()`, `ORD_PUSHLEFT`, `SEG_KEEPIND`/`SEG_DEFPOPL`; cctab's
`%nw*,nw*` after `%n,ew*`, which `02_bubsort`'s `a[j] > a[j + 1]`
matches). `p19_open3` added `x - b[j]` with a variable on the left (`b[j]`'s
address pushed, `mov di,x` / `pop bx` / `sub di,(bx)`) and `a[i] * b[j -
2]` with DI and SI taken (the index through DX, added to the array's
address in SI: `lea si,b` / `mov dx,j` / `sal dx,*1` / `add si,dx` / `mov
ax,di` / `imul *-4.(si)` - the constant term the displacement, not folded
into the `lea`). Round 6's `p23_elem3` settled the mixed cases: a LEFT
element with an offset compared with one at offset 0 keeps its offset as
the displacement off BX (`pop bx` / `cmp *4.(bx),di`), the reverse is
`%n,ew*` (`cmp di,*4.(si)`); `x * b[j]` loads the element into AX (`mov
ax,(di)` / `imul x`); a constant, a variable or a file-scope variable minus
an element pushes the element's address (`mov di,*5.` / `mov di,_g` / `pop
bx` / `sub di,(bx)`), `x - *p` the pointer (`push p`); `b[j + 1]` is `mov
di,*2.(di)`, `a[i] + b[j + 1]` takes its index through DX; two comparisons
summed compute the right term first and push it (`is_relsum()`); and an
element compared with 0 is loaded and tested (`mov di,(di)` / `or di,di` -
`cond_ortest`). Round 7's `p26_elem4` corrected one inference and settled
the rest: an element read through a computed address and tested for truth
is loaded and tested too (`if (b[i])`, `if (ps[i].c)`: `mov di,(di)` / `or
di,di` - `ortest`; `mutos_c1` had inferred `cmp (di),*0`, the shape through
a pointer variable); `b[i] * b[j] + b[j - 1]` moves the product into DI
before the right element's address is computed (`mov di,ax` / `lea si,b`
/ ... / `add di,*-2.(si)` - `ORD_LEFTDI`); `(b[i] + 1) * b[j]`, `b[j] /
b[i]` and `b[j] % b[i]` compute the element at offset 0 first and push it
(`pop cx` / `imul cx`, `cwd` / `pop cx` / `idiv cx`); `r + (b[i] < b[j]) +
(b[i] > b[j]) * 2 + (b[i] == b[j]) * 4` is v7's `distrib()` (`(A*2 + B)*2`,
computed first, then C, each comparison's 0/1 pushed in both arms -
`is_distrib()`, `plan_distrib()`, see `docs/DEVLOG.md`'s round-7 section);
`b[i] += b[j]` / `b[j] -= x` push the target's address (`pop bx` / `add
(bx),di` - `is_aspush()`), `*ip += 2` is `add (di),*2.`, `ps[i].c += x`
computes the right-hand side first (`add *4.(si),di` - `is_asdisp()`),
`b[0] *= b[i]` multiplies in AX (`imul *-28.(bp)` / `mov *-28.(bp),ax`);
and a constant-index element is a leaf (degree 0), added in order after a
computed member. Round 8's `p32_elem5` corrected one inference and settled
the refusals: in a `distrib()` sum a comparison of a variable with a
constant (degree 1) is computed after the left term, into SI (`%n,e` -
`mov si,*0.` / ... / `add di,si`), where two elements compared (degree 3)
are pushed; `*ip += x` pushes the pointer variable (`push ip` / `mov di,x` /
`pop bx` / `add (bx),di` - `is_asptr()`); `b[3] ^= b[i]` loads the element
and combines in place (`xor *-50.(bp),di`); `x + f(1) + g(2)` sums the calls
first, then adds the variable to AX (`acommute()` by degree -
`is_callacom()`); and `b[j] / b[i] + b[j] % b[i]` computes the remainder
first and pushes DX (`is_callsum()` widened to quotients and remainders).
Round 9's `p35_elem6` corrected two inferences and settled the refusals:
`c / d + f(2)` computes the quotient first and pushes it, the call after
it (`acommute()` puts the call, degree 10, on the left - `is_callsumx()`,
`ORD_SPILLX`), and so does `f(1) + g(2) * 3` with the call pushed and the
product computed second; `(y > 2) + (x < 3)` and `(y > 2) + (y < 9) * 2 +
y` compute the left comparison into DI and an easy right one (a variable
compared with a constant, degree 1) into SI, shifted there (`%n,e` -
`is_rele()`, `ORD_RELE`, `SEG_MATDI`); `c % d + c / d` computes the
remainder second and adds in DX (`pop bx` / `add dx,bx` / `mov x,dx`); `c *
d + f(1)` calls first, moves the value into DI and multiplies in AX (`mov
di,ax` / `mov ax,c` / `imul d` / `add di,ax` - `ORD_LEFTDIX`); `f(1) -
g(2)` and `f(1) * g(2)` push the right call's value (`pop bx` / `sub
ax,bx`; `mov ax,ax` / `pop cx` / `imul cx` - `is_hardsub()`,
`is_calltimes()`); `b[i] + f(1)` pushes the element's address first and
adds it to the call's value (`pop bx` / `add ax,(bx)` - `ORD_PUSHADDRX`).
Round 10's `p39_elem7` confirmed all seventeen inferences (the other
written orders, two remainders, a call minus a quotient or a remainder
and the reverse, a comparison scaled by 4, a constant on the left of an
easy comparison) except one: `f(2) * f(3) * 2` shifts the product where it
is (`imul cx` / `sal ax,*1` - a value in AX is shifted there, not moved
into DI). It settled the rest: `x - f(1)` pushes the call and loads `x`
into DI after it (`pop bx` / `sub di,bx` - `is_hardsub()` widened to a
variable, a constant, a sum of calls or a product on the left), `f(1) - x`
subtracts in AX (`sub ax,x`); `(y > 2) + (x < y)` compares the two
variables through SI, where the 0/1 goes (`mov si,y` / `cmp x,si` -
`cmp_reg`), and `(y > 2) - (x < 3)` is `is_rele()`'s `sub di,si`; `f(1) &
b[i]` pushes the element's address (`and ax,(bx)`); `c * d + c * y` and
`f(1) + c * 3` move the left value into DI and compute the right product
in AX (`is_leftdi()`, `is_easyprod()`); `c * d - f(1)`, `f(1) + g(2) -
f(3)` and `f(1) | g(2)` push the right call (`sub ax,bx`, `or ax,bx`).
Round 11's `p43_elem8` confirmed those inferences (a call, a quotient or a
product shifted in AX, by CL too, a remainder in DX, a quotient or
remainder on the right of a variable, `b[i] - f(1)`, `f(1) - b[i]` - `sub
ax,(bx)` -, `|`, `^`, `&` of a call and an element or two calls, products
by a constant summed, differences of comparisons) except one: `c % d - x`
subtracts in DX, the remainder's working register (`sub dx,x` / `mov
x,dx` - not moved into DI first). It settled the refusals: `y % (c / d -
s)` computes the divisor where it ends (AX) and pushes it, then loads the
dividend into DI (`sub ax,s` / `push ax` / `mov di,y` / `mov ax,di` /
`cwd` / `pop cx` / `idiv cx` - `is_harddiv()`, `ORD_SPILL`: a divisor that
makes a division or a call, v7's degree beyond the registers left; such a
divisor is exempt from `divisor_next()`); `f(1) * 2 + c / d` pushes the
quotient first (`is_callsum()` with a call's product on the left), `f(1)
* 3 - f(2)` the right call (`is_hardsub()` likewise), and `c * d + f(1) *
3` computes the call's product first and moves it into DI
(`is_leftdix()`). Round 12's `p47_elem9` confirmed a quotient, a
remainder or a call as the divisor, an element as the dividend, a call's
product next to a remainder, a quotient, a variable or another call, and
corrected seven inferences by four rules: a remainder plus a variable or a
constant is added in DX, F's register for it (`add dx,x`, `add dx,*3.` -
as a divisor `add dx,s` / `push dx`); EVERY computed divisor is pushed
first (`y % (c - s)` -> `mov di,c` / `sub di,s` / `push di` / `mov di,y` /
`mov ax,di` / `cwd` / `pop cx` / `idiv cx`; `y % (c * d - s)` from AX - no
golden has `idiv di`: `is_harddiv()` with `is_computed_int()`); `x - f(1)
* 3` pushes the product (`is_hardsub()` with a call's product on the
right); `f(1) - (x < y)` moves the call's value into DI first and
computes the comparison into SI, `sub di,si` (v7's `%n,e` - `is_sube()`,
`SEG_LEFTDI`; the int `+`/`-` tail now computes into SI while DI holds
such a left operand). It settled the refusals: `y % (c * d)` pushed from
AX, `c % d - b[i]` with the element's address pushed (`sub dx,(bx)` -
`is_pushaddr()` with a quotient or remainder on the left), `c * 3 + f(1)
* 5` with the call's product moved into DI (`is_leftdix()` with
`is_easyprod()`), `f(1) * 2 + f(2) * 3` with the LEFT product pushed first
(`is_callsumx()`: its degree, 10 - a power of two as the multiplier -
below the right one's 11, so `acommute()` exchanges them;
`callprod_degree()`). Round 13's `p51_elem10` confirmed twenty-five of
the thirty shapes inferred from those and corrected five, by four rules:
`%n,e` reaches a product of two variables on the right (`x - c * d` ->
`mov di,x` / `mov ax,c` / `imul d` / `sub di,ax`), a difference of two
leaves times a power of two (`a - (c - d) * 2` -> ... `sal si,*1` / `sub
di,si`) and a quotient or remainder of two leaves on the left (`c % d - (a
- s)` -> [c % d] / `mov di,dx` / [a - s into SI] / `sub di,si` -
`is_sube()`, `plan_leftdi()`; a product on the left inferred); a call's
value is divided by a pushed divisor (`f(1) / (c - d)` -> `push di` /
[f(1)] / `mov ax,ax` / `cwd` / `pop cx` / `idiv cx` - `is_harddiv()`; a
product, quotient or remainder dividend inferred); `f(1) * 4 + f(2) * 8`
is v7's `distrib()`'s `(f(2) * 2 + f(1)) * 4` ([f(1)] / `push ax` / [f(2)]
/ `sal ax,*1` / `pop bx` / `add ax,bx` / `sal ax,*1` twice -
`is_distrib_call()`, the DTree simulation with calls). It settled the
refusals: `c % d & b[i]` pushes the element's address (`and dx,(bx)` -
`is_pushaddr()`), `c % d | f(1)` and `c % d + f(1) * 2` push the remainder
first (`is_callbitx()`, `is_callsumx()`). Round 14's `p55_elem11` asks
about their neighbours. Still refused: a remainder times a constant plus a
call (`c % d * 4 + f(1)`), a call chain mixing comparisons.

**`unsigned` locals, `long` mixing and octal constants (round 4).** A
plain `unsigned` local is accepted (`p16_open2`: NAME typed `UNSIGN`, `u =
5` an `ASSIGN(7)`), with the operators a bit-field's unsigned value already
had. Testing it showed `mutos_c0` silently miscompiling a `long` combined
with an int-class VARIABLE (`l + i` added the two words at `i`'s address,
`l > i`, `l & m`, an int assigned a long - `x = l` stored the high word):
v7's `build()` converts with `ITOL`/`LTOI`, which `mutos_c0` did not write.
Now an int or unsigned target of a long gets `LTOI` (as `(int) l` has). And
an octal or hex constant from 0100000 to 0177777 is an int, not a long
(v7's `getnum()`; the `11_kernel` goldens' `CON -32768` for `0100000`).

**`long` mixed with int-class values (round 5, `p19_open3`).**
`mutos_c0` now writes v7's conversions (`mix_rhs_end()`, `emit_itol()`):
an int-class operand of a binary operator whose other operand is a long
gets `ITOL` right after its own bytes, a variable and a constant alike
(`l + i`: NAME l, NAME i, ITOL, PLUS(LONG); `i + l`: NAME i, ITOL, NAME l;
`m & 255`: NAME m, CON 255, ITOL, AND(LONG)), an unsigned one too (`u >
39999`: NAME u, ITOL, LCON, GREAT); `l += i` converts the right-hand side
(ITOL, ASPLUS(LONG)); `l = u` is ITOL of the unsigned; `u = 40000` is
LCON, LTOI(UNSIGN); `(int) (l - 100000)` is the tree, LTOI(INT). A char
with a long is refused (CTOL or ITOC then ITOL - no golden). `mutos_c1`
(`OP_ITOL`, `gen_long_constop()`, `gen_long_relop()`, `gen_long_cmp()`):

- `l + i`, `i + l`: the int widened first (`mov ax,i` / `cwd` / `mov
  di,dx` / `mov si,ax`), the variable added from memory (`add si,l+2` /
  `adc di,l`) - acommute() puts the ITOL (degree 2) ahead of the NAME;
  `l += i` the same, added into memory (`add l+2,si` / `adc l,di`);
- an unsigned widened: `mov si,u` / `sub di,di`;
- a long variable `&`, `|` (and by inference `^`, `+`, `-`) a widened int
  constant: `c + 1L`'s shape - `mov ax,#255.` / `cwd` / `push ax` / `push
  dx` / `mov si,m+2` / `mov di,m` / `pop bx` / `pop cx` / `and si,cx` / `and
  di,bx`;
- `r + (int) (l - 100000)`: the low words only (v7's `unoptim()`
  distributes the LTOI - `VK_LOWADD`): `mov di,l+2` / `add di,r` / `add
  di,#31072.`;
- comparisons (v7's `longrel()`, `lrtab[0]`): `l > i` and `l == i` swapped
  to the widened int on the left, in DX:AX (`mov ax,i` / `cwd` / `cmp dx,l`
  / `bgt` / `blt` / `cmp ax,l+2` / `bhis` - the low words unsigned); `u >
  39999` as `39999 < u` with the widened unsigned pushed and popped into
  CX:BX after the constant is loaded (`push si` / `push di` / `mov
  si,#-25537.` / `mov di,*0.` / `pop cx` / `pop bx` / `cmp di,cx` / `blt` /
  `bgt` / `cmp si,bx` / `blo`), as a value too.

**Round 6 (`p22_long2`)** settled every shape left: `l - i` (the widened
int pushed, v7's `%nl,nl` - `pop bx` / `pop cx` / `sub si,cx` / `sbb
di,bx`), `i - l` (`sub si,l+2` / `sbb di,l`), a widened constant `+`/`^`
(`c + 1L`'s shape) and a negative one folded (`l - 2` -> `add si,*-2.` /
`adc di,*-1.`), `l -= i` into memory, `l *= i` (`lea di,l` / `push di` /
`call almul`), `l / i` and `l % i` (`ldiv`/`lrem`, the widened int pushed
from DX:AX), a long constant into an int (`mov x,#-25536.`), every
relation with `0L` and with a widened int constant (`lrtab[0]` - `cmp
l,*0` / ... / `cmp l+2,*2.` / `blos`, no `tst`), `i < l`, a comparison as a
value, `if (l)`/`!l` (both words, the high one first), `u < l`, two long
variables compared (`mov si,l+2` / `mov di,l` / `cmp di,m` ...), `~l`
(`not di` / `not si`), `l << 2`/`l >> 1` (`sal si,*1` / `rcl di,*1` per
bit; `sar di,*1` / `rcr si,*1`), `x ? l : m` (each arm into DI:SI), `x =
(int) (l - 5)` (the low word in DI), `(long) u` and `l & m`.

**Round 7 (`p25_long3`)** settled the rest of what round 6 refused or
inferred: `l += 1`, `l++` and `++l` in place (`add l+2,*1.` / `adc l,*0`;
`l--` `sub` / `sbb ...,*0`); `-l` (`neg di` / `neg si` / `sbb di,*0`); a
shift by 3 or more through CX (`mov cx,*3.` / `sal si,*1` / `rcl di,*1` /
`loop .-4`); a long under `&&`/`||` and as the condition of `?:` (both
words tested, the high one first); `r + (int) (l - 9990)` the other term
first (`mov di,r` / `add di,l+2` / `add di,#-9990.`), `r + 10 - (int) (l -
4995)` through SI (`mov si,l+2` / `add si,#-4995.` / `sub di,si`); `l * 3`
(`mov ax,*3.` / `cwd` / pushed / `call lmul`); `l + i * j` (`imul j` /
`cwd`, no `mov ax,ax`); `m = l = 5` (`mov ax,*5.` / `cwd` / both stores).
`l | m`, `l ^ m`, `x = (int) (l + 5)`, `l = c` and `l / 7` matched as
inferred.

**Round 8 (`p31_long4`)** corrected `m * i` - the long variable pushed
first, the int widened and pushed after it (`acommute()`: the `ITOL`,
degree 2, ahead; `Val`'s `itolw`) - and settled every refusal: `x = l++`
increments the LOW WORD as an int (`mov di,l+2` / `inc l+2` / `mov x,di` -
the `LTOI` distributed into the `++`, the carry lost: the real compiler's
own); `l += 70000` loads the constant into DI:SI and adds it (`mov
si,#4464.` / `mov di,*1.` / `add l+2,si` / `adc l,di`); `l << i` shifts
through CX, skipping a count of 0 (`mov cx,i` / `or cx,cx` / `jz .+8` /
`sal si,*1` / `rcl di,*1` / `loop .-4`); `x = !l` is both words through
`cbranch()`, then 0/1; `10 - (int) (l - 35)` loads the 10 first (`mov
di,*10.` / `mov si,l+2` / `add si,*-35.` / `sub di,si`).

**Round 9 (`p34_long5`)** corrected three inferences: `x = ++l` (and `--l`)
increments the WHOLE long in place first and then reads the low word
(`add l+2,*1.` / `adc l,*0` / `mov di,l+2` - v7's `sreorder()`; only the
postfix form distributes the `LTOI`); `l += -5` (and `l -= -3`) loads the
constant's two words, `mov si,*-5.` / `mov di,*-1.` - an int constant
widened that is negative is an LCON (v7's optim()), loaded by the
assignment-operator template as two numbers, never through `cwd`; `l =
-i` negates in AX (`mov ax,i` / `neg ax` / `cwd`). It settled every
refusal: `l <<= 3` / `l >>= 2` (`mutos_c0` widens the count, as for a
binary shift; DI:SI, `mov cx,*N.` and the pair looped - for 2 too - then
stored back HIGH word first); `l /= m` (`mov di,m+2` / `push di` / `mov
di,m` / `push di` / `lea di,l` / `push di` / `call aldiv` / `add sp,*6.`);
`(int) (l * 2)` (the `lmul` result's low word stored straight from AX);
`l - m - 1` (`sub`/`sbb` from memory, then `add si,*-1.` / `adc di,*-1.`);
`100000 - l` (`mov si,#-31072.` / `mov di,*1.` / `sub si,l+2` / `sbb
di,l`).

**Round 10 (`p38_long6`)** corrected three inferences: `l <<= 1` shifts the
variable IN PLACE and then loads it (`sal l+2,*1` / `rcl l,*1` / `mov
di,l` / `mov si,l+2`); `l <<= i` and `l >>= i` skip with `jz .+16`, not
`.+8` - the template's own displacement, as if each store back took four
bytes (a file-scope long's do; a local's take three, so with a count of 0
the real code jumps two bytes into the next instruction - written as the
real compiler writes it); `l += 5L` is in place (`add l+2,*5.` / `adc
l,*0`) - v7's `c1` reads a long constant that fits an int as the `ITOL` of
a CON (getree()), so `5L` is `5` widened (`OP_LCON`'s `fromu`). It settled
every refusal: `l <<= 0` is no code at all; `5 - l` (and `5L - l`) goes
through `cwd` (`mov ax,*5.` / `cwd` / `mov di,dx` / `mov si,ax` / `sub
si,l+2` / `sbb di,l`), where `-5L - l` loads the LCON's two words; `(l -
m) + 3` pushes the constant first (`mov ax,*3.` / `cwd` / `push ax` /
`push dx` / [l - m in DI:SI] / `pop bx` / `pop cx` / `add si,cx` / `adc
di,bx` - v7's `%n,n`), and `l * m * 2` the same, the first product's DX:AX
pushed straight after it as the left argument (`push ax` / `push dx` /
`call lmul` - `is_lspill()`, `ORD_LSPILL`, `SEG_LSPILL`). Matched as
inferred: `l -= 70000`, `l /= i`, `l %= m`, `l %= 7`, `l *= m`, `(int) (l /
m)`, `(int) (l % 7)`, `(l + m) - 3`, `(l - m) + 70000`, `l * m - 1`, `l /
m + l`, `-5L - l`, `m + -i`, `m - -i`.

**Round 11 (`p42_long7`)** confirmed every inference (`l >>= 1` in place -
`sar l,*1` / `rcr l+2,*1` -, `l >>= 0`, `l += 300L` / `l -= 300` in place,
`300 - l` through `cwd`, a sum, product or quotient plus a constant, a
product or quotient times, by or modulo a constant) but one: `l = -300`
loads the constant's two words (`mov si,#-300.` / `mov di,*-1.`), where `l
= 300` goes through `cwd` - v7's `getree()` reads an LCON that fits an int
as the ITOL of a CON, and `optim()` turns an ITOL of a NEGATIVE CON back
into an LCON (`OP_ITOL`, `materialize_long()`; a negative long constant
argument is inferred to be pushed as two words the same way). It settled
the refusals: `(int) ((l - m) + 3)` is computed in one word, `mov di,l+2`
/ `sub di,m+2` / `add di,*3.` - `unoptim()` distributes an LTOI over a
long `+`/`-`, takes a long NAME's low word, an ITOL's int and an LCON's
low word (`lowword_region()`: the region's nodes are compiled as the int
expression, retyped in the pre-scan too); `(l + m) * 2` pushes the 2
first and the sum from DI:SI (`push si` / `push di` / `call lmul` -
`is_lspill()` with a sum on the left, `gen_long_stacked_call()`); `(int)
(l * m * 2)` is pushed as `l * m * 2` is, an LTOI not being distributed
into a `*` (`mov x,ax`).

**Round 12 (`p46_long8`)** confirmed `l = -5L`, `l = -1`, a negative long
constant passed and returned, an LTOI over sums and differences of long
variables and int values, a sum or difference times, by or modulo a
constant, a quotient or product under an LTOI - all but two: `(int) (i +
(l - m))` takes the difference first (`mov di,l+2` / `sub di,m+2` / `add
di,i` - the int `+` `unoptim()` makes is `acommute()`d: OP_PLUS's
exchange, `lowword_then_assign()`), and `(int) (l - (m - l))` loads the
left word first and the difference into SI (`mov di,l+2` / `mov si,m+2` /
`sub si,l+2` / `sub di,si` - v7's `%n,e`, `is_sube()`). It settled the
refusals: an LTOI over `&` and `|` is distributed too (`(int) (l & m)` ->
`mov di,l+2` / `and di,m+2`; `lowword_region()`, `^` inferred); `(l + m)
* m` pushes m first, through DI (`mov di,m+2` / `push di` / `mov di,m` /
`push di` / [the sum] / `push si` / `push di` / `call lmul` -
`is_lspill()` with a long variable on the right, `plan_lspill()`); `(l <<
2) + 3` pushes the 3 first (`is_lspill()` with a shift on the left).
Round 13's `p50_long9` confirmed thirteen of the fourteen shapes inferred
from those and corrected one: in `(int) (l - (m + i))` the sum adds m's
low word to i (`mov si,i` / `add si,m`) - `unoptim()` `optim()`s the long
tree before it distributes the LTOI, and `acommute()` puts the ITOL
(degree 2) ahead of m (0); the int `optim()` after keeps that order
(`lowword_swap()` - `&`, `|`, `^` inferred). It settled the refusals:
`(int) (-l + m)` in one word (`neg di` - `lowword_region()` takes a NEG or
COMPL), `l = lsub(l, m) * n` with n pushed first and the call's DX:AX
after it (`is_lspill()`, `finish_call()` keeping DX:AX), `l = n * (l + m)`
exchanged into that shape (`is_lspillx()`, `ORD_LSPILLX`), `l = (l + m) <<
2` shifted in DI:SI (`gen_long_shift()`). And it showed a `long` variable
passed as an argument in two words through DI - `mutos_c1` had pushed its
high word alone, silently wrong code (`push_call_arg()`, Val's `longv`).
Round 14's `p54_long10` asks about the neighbours.

Still refused: a long shifted in place by a char or long count, a constant
on the left of a long `*` (`3 * m`, `3 * (l + m)`), two long calls summed,
a long value computed into DI:SI as a call argument, a char with a long in
an operator. Silent miscompiles
found while testing this, each a refusal or fixed now: two long variables
compared (`l > m` compared the high words only), a long tested for truth
(the high word only), `~l`, `l << 2` and `x ? l : m` (one word each), and
a negated long constant (`l = -65536;` stored 0 - `mutos_c0` truncated
`-` of a long constant to an int; now LCON, NEG(LONG), which `mutos_c1`
folds, as v7's `unoptim()` does).

## The port of the real `c0`

`c0port/` holds it, in v7's file split (`../../docs/C0_PORT_PLAN.md`;
`tests/realbin/c0map.txt` lists all 73 functions with their text offsets
in the original and what changed against v7):

- `c0.h` - the memory image and its accessors (a word as `WI()`/`WU()`,
  a byte as `B()`, a structure field at its byte offset: `OP(p)`,
  `HCLASS(p)`, ...), every global at its address in the original, v7's
  `c0.h` constants with the MUTOS values (`HSHSIZ` 500, the five-bit
  conversion codes, the char conversions 107..109 and the pseudo
  conversions 122/123).
- `c00.c` - `main`, the lexer (`lookup`, `findkw`, `symbol`, `getnum`,
  `subseq`, `putstr`, `getcc`, `mapch`) and the expression parser
  (`tree`, `xprtype`, `copnum`).
- `c01.c` - `build` (the MUTOS char rules), `convert`, `setype`,
  `chkfun`, `disarray`, `chkw`, `lintyp` (five classes), `error`,
  `werror`, the block allocators, `chklval`, `fold` (the 8086's shifts
  and divisions) and `conexp`.
- `c02.c` - `extdef`, `cfunc`, the initializers (`cinit`, `strinit`,
  `setinit`), the statements (`statement`, `forstmt`, `pexpr`,
  `pswitch`), `funchead`, `blockhead`, `blkend`, `prste`, `errflush`.
- `c03.c` - the declarations: `declist`, `getkeywords`, `strdec`,
  `declare`, `decl1`, `pushdecl`, `cpysymb`, `getype`, `typov`,
  `align`, `decsyn`, `redec`, `goodreg`.
- `c04.c` - `decref`, `incref`, `cbranch`, `rcexpr`, `treeout`,
  `branch`, `label`, the length functions, `simplegoto`, `nextchar`,
  `spnextchar`, `chconbrk`, `dogoto`, `doret` and `outcode` (the
  `temp1`/`temp2` writer).
- `c0data.c` - generated from the binary by `tests/realbin/c0tables.py
  --c` and checked in (`make check-realbin` checks it is current): the
  original's initialized data segment, 2,878 bytes - the keyword table,
  `opdope[]`, `cvtab[]`, `cvntab[]`, `ctab[]`, the message texts.
- `c0lib.c` - the machine around the program: the image's start
  (`crt0`'s segment words), the input, `sbrk()`, the stack area for the
  locals whose address the original passes on, `libc.a`'s `printf` for
  the messages, `exit()`, and the refusals (`c0trap()`).

**Refusals.** Where the original traps, reads past the end of its input
for ever, or reads memory whose contents depend on state the port does
not model (`libc.a`'s stdio buffers and FILE structures), `mutos_c0`
stops with "c0: ...; refused" and exit status 2 after writing
everything before that point - see `../../docs/C0_PORT_PLAN.md`,
section 4. Everything else the original does with its own memory, bugs
included, it reproduces; the bugs are listed in
`../../docs/MUTOS_ORIGINAL_BUGS.md` (C0-01 .. C0-12).

**How it is checked.** `tests/realbin/c0cmp.py` compares its `.1`, `.2`,
messages and exit status with the emulated original's on every `.i`
golden, and `tests/realbin/c0fuzz.py` does the same on random and
mutated programs (see `../../tests/realbin/README.md`); `make
check-realbin` runs both. It is invoked as v7's `cc` invokes `c0`:
`mutos_c0 source.i temp1 temp2 [profileflag]`.

## The port of the real `c1`

`c1port/` holds it, in v7's file split (`../../docs/C1_PORT_PLAN.md`;
`tests/realbin/c1map.txt` lists all 67 functions with their text offsets
in the original and what changed against v7):

- `c1.h` - v7's `c1.h` with the MUTOS ops (89 `*(x+c)`, the char/long
  and unsigned ops, 121..123), the 8086 registers (`ax dx si di cx bx bp
  sp`, numbered 0..7 and handed out downwards, `reg1 = reg-1`), and one
  host `struct tnode` whose union keeps the original's field overlays at
  their byte offsets; the 8086's 16-bit arithmetic as explicit helpers
  (unmasked shift counts, a divide fault refused).
- `c10.c` - `main` (options after the three file names: `a b c d r`
  traces into the `.s`, `s<n>` the `chkstk` frame threshold), the SIGFPE
  handler, `match`, `rcexpr`, `cexpr` (v7's macro interpreter with the
  MUTOS additions - the floating-point stack model `U`/`Q`/`R` and its
  messages, push/pop counting, `R*` through BX, `F!`, `Fd`), `reorder`,
  `sreorder`, `delay`, `sdelay`, `ncopy`, `chkleaf`, `comarg`,
  `strfunc`, `doinit`, `movreg`.
- `c11.c` - `degree`, `pname`, `pbase`, the `%c%d.` number printer,
  `dcalc`/`xdcalc`/`notcompat` with the base-register test, `prins`,
  `collcon`, `pswitch` (direct and hashed switches), `cbranch`,
  `branch`, `longrel`/`xlongrel`, `label`, `popstk`, `error`/`werror`,
  `getree` (the `temp1`/`temp2` reader), `strasg`, the tree printer of the
  traces.
- `c12.c` - the tree optimizer: `optim`, `unoptim`, `lvfield`,
  `acommute`, `distrib`, `squash`, `const`, `lconst`, `insert`, `tnode`,
  `getblk`, `fconchk` (the FCON→SFCON test in the MUTOS floating format),
  `hardlongs`, `isregptr`.
- `c1tab.c` - generated from the binary by `tests/realbin/c1tables.py
  --c` and checked in (`make check-realbin` checks it is current): the
  code tables `regtab`, `efftab`, `cctab`, `sptab` - 496 rows, 292
  template strings byte for byte, 0200 bits included -, `opdope[]`,
  `opntab[]`, `instab`, `branchtab`, `maprel`, `notrel`, `lrtab`,
  `regname`.
- `c1lib.c` - the library routines the output passes through, as
  `libc.a` has them, not as C would compute: `atof` (through
  `../mutos_as/fltconst.c`), `printf`'s `%.17e` (through
  `c1_fltdec.c`'s `ecvt()`) and its `%c` of 0 (nothing), `itod` (its
  store loop leaves the low mantissa byte in bytes 1 and 3), `ltof`,
  `dtoi`, `ftol`, `fneg`, and `aldiv`/`alrem` for `lconst()`'s long `/`
  and `%` (sixteen shift-and-subtract steps, not a full division).

**Refusals.** Where the original traps, jumps into the middle of an
instruction or reads memory whose contents depend on state the port does
not model, `mutos_c1` stops with "...; refused" and exit status 2 after
writing everything before that point (`c1trap()`): the register name
outside `regname[]` (`p19_open3`: -1, the register after AX), a long constant shifted by a count whose
tested word is 0, `xlongrel()` reading past `lrtab[]`, `ltof` of 0, the
overflows of `dtoi`/`ftol` and `dtoi` from 2\*\*15. A second floating
overflow in one file ends it as SIGFPE ends the original - its `.s` cut
to whole 512-byte buffers. Error messages (`Floating point stack
overflow; simplify expression`, `Divide check`, ...) and the exit status
are the original's. The original's bugs - the refused cases and the wrong
code it is faithful to - are listed in `../../docs/MUTOS_ORIGINAL_BUGS.md`.

**How it is checked.** `tests/realbin/c1trees.py` compares it with the
emulated original at four depths - the trees as read, the trees after
`optim()`, the `.s` with all of the original's traces on, and the `.s`
with messages and exit status - and `tests/realbin/c1fuzz.py` does the
same on random programs (see `../../tests/realbin/README.md`); `make
check-realbin` runs both. The tree dump for depths 1 and 2 goes to the
file the environment variable `C1PORT_TREES` names.

## Next steps (roughly in dependency order)

1. **`mutos_cc` driver**: chains `mutos_cpp | mutos_c0 | mutos_c1 |
   mutos_as | mutos_ld` the way `v7/cc/cc.c` does - not yet written;
   today's pipeline is exercised by invoking each tool directly (see
   `tests/mutos_cc/run_goldens.sh`). The `-P`/`-S` interaction
   CLAUDE.md flagged as "undecided... revisit once `mutos_cc`'s
   driver is actually being written" is still open.
2. **`mutos_c2`** (Milestone 5): the same way again - v7's `c20.c`/`c21.c`,
   the real `c2` in `mutos_bin/`, `tests/mutos_as/kernel_opt/` as `cc -O`
   output.

On the assembler side, `mutos_as` accepts every `%.17e` constant
`mutos_c1` writes from `e-37` up (`e-38` when the real `atof` drops the
18th digit): the real conversion is settled, rounding and multiplication
included (`tests/mutos_as/float_coverage/fltmode.s`, `fltmul.s`). Below
that the conversion's 5\*\*55 overflows: the real assembler aborts
there ("floating point over/under flow- assembly aborted",
`tests/mutos_as/float_open/fltsig.s`), and `mutos_as` refuses the
constant with the reason - `mutos_c1` itself still writes it, as the
real compiler does, so a program with such a constant fails at the
assembler step either way.

## Source layout

- `c0port/` - `mutos_c0`, the port of the real `c0` (see "The port of
  the real `c0`"); invoked as v7's `cc` invokes `c0`: `mutos_c0
  source.i temp1 temp2 [profileflag]`.
- `c1port/` - `mutos_c1`, the port of the real `c1` (see "The port of
  the real `c1`"); invoked as v7's `cc` invokes `c1`: `mutos_c1 temp1
  temp2 output.s [options]`.
- `c1_fltdec.h`/`c1_fltdec.c` - `libc.a`'s `ecvt()` on the MUTOS double,
  for `mutos_c1`'s `%.17e` (and `fltdec_test`, below).

## Testing

`tests/mutos_cc/run_goldens.sh` runs the real `mutos_cpp → mutos_c0 →
mutos_c1` pipeline over every corpus file that has goldens and diffs
every stage (`.i`/`.1`/`.2`/`.s`) byte-for-byte, reporting "not yet
supported" separately from a genuine mismatch (see "Current scope"
above) so the exit status stays a meaningful signal as coverage
grows. A file whose goldens lack the `.i` is checked from `mutos_c0` on
and listed apart (its category 7).

A file `mutos_c0` refuses would still be checked at `mutos_c1`, from
its `.1`/`.2` goldens (category 10; none at present). `tests/realbin/`
(`make check-realbin`, needs `unicorn`) checks `mutos_c1` against the
emulated original `c1` at four depths, and `mutos_c0` against the
emulated original `c0`, on every golden and on random programs.

`tests/mutos_cc/fuzz/` (`make fuzz`) complements that with random
programs: `fuzz_c.py` computes what each one must do under C semantics,
runs it through the real pipeline, executes the output with `x86sim.py`,
comparing every variable at every statement boundary, and - by default -
requires `mutos_c0`'s and `mutos_c1`'s output to be the emulated
original `c0`'s and `c1`'s byte for byte; with `--baseline` it also
classifies every difference against an earlier build. It found the
constant-left-operand bug and two wrong-code bugs of `c1_gen.c` (all
three fixed) - see its README. With `--scope` it also makes some
variables file-scope ones and wraps statements in blocks that shadow
them. The generator produces neither chars nor calls nor floating code;
`x86sim.py` itself executes all three (byte moves, calls of functions in
the same file, `chkstk`, and the floating-point runtime's entry points
against a model of it) and fixed-address variables, so hand-written
programs of that kind can be checked the same way;
`tests/realbin/c1fuzz.py`'s generator covers every type against the
original.

## Tooling

`dump_temp.py` is a standalone, human-readable decoder for any
`temp1`/`temp2` (`.1`/`.2`) file - one `mutos_c0` just generated, or a
real-hardware `*.1.golden`/`*.2.golden` reference. It is not a mode of
`mutos_c0` itself (which could never read a golden file it didn't
produce), so it works equally on both:

```
src/mutos_cc/dump_temp.py tests/mutos_cc/03_ctrlflow/06_switch.1.golden
src/mutos_cc/dump_temp.py tests/mutos_cc/00_smoke/*.1.golden
```

For each `B`-tagged opcode it prints the byte offset, the opcode's
name, and every `N`/`S` argument that follows it, resolving `TY_*`/
`SC_*` constants to their names (e.g. `type=TY_LONG(6)`,
`hclass=SC_AUTO(11)`) rather than leaving them as bare numbers - a
derived type as its full chain, outermost degree first, e.g.
`type=PTR.ARRAY.TY_INT(104)` ("pointer to array of int") or
`type=PTR.FUNC.TY_INT(72)` - and
handling `OP_SWIT`'s variable-length case table and `OP_NAME`'s
`SC_EXTERN`-vs-otherwise conditional shape (a symbol name vs. a
numeric offset - see `v7/cc/c04.c`'s `treeout()`) correctly. It has a
shape for every opcode the original `c0` can write (the port's
`outcode()` call sites); an opcode it has no shape for stops the dump
cleanly with a clear message rather than guessing and silently
desyncing the rest of the file - the same "never silently wrong" rule
this project applies everywhere else.

`dump_temp.py`'s own `OPCODES` table is a hand-transcribed copy of
every `outcode()` call site's argument shape, not something derived
automatically from the wire format at read time - see `CLAUDE.md`'s
Workflow Guideline 8 for the mandatory rule that keeps it in sync
whenever an opcode's shape (or the `TY_*`/`SC_*` constants) changes.

`fltdec_test` (built with `mutos_c1`) reads C floating literals, one per
line (a leading `-` negates), and prints the text and size `mutos_c1`
writes for each - `0.1 double 1.00000000000000000e-01` - from
`c1_fltdec.c`, the model of the real compiler's `printf("%.17e")` (its
`libc.a`'s `ecvt()` of `libc.a`'s `atof()`). `make check-libcatof` runs
it against `libc.a`'s own `ecvt()` under an 8086 emulator
(`tests/mutos_as/float_coverage/libcatof.py ecvt`).
