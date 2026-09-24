# mutos_c0 / mutos_c1 - MUTOS 1700 Cross C Compiler (Milestone 4)

`mutos_c0` (front end: lex/parse/typecheck, emits the `temp1`/`temp2`
intermediate-code stream) and `mutos_c1` (back end: reads
`temp1`/`temp2`, emits `mutos_as`-syntax assembly text) are a
from-scratch, modern-C11 reimplementation - not a literal port of
`v7/cc`'s pointer-arithmetic K&R source - matching this project's
established approach for `mutos_as`/`mutos_cpp`/`mutos_ld`: validated
byte-for-byte against real-hardware golden output, with `/v7/cc/`
used strictly as an algorithmic/structural reference (CLAUDE.md
Workflow Guideline 3), not copied wholesale.

**Status: verified byte-exact, end-to-end (`.c` → real `mutos_cpp` →
`mutos_c0` → `mutos_c1` → `.s`), for 38/62 of the full corpus:**
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
`09_abiprobe/01_argvmain.c` and `10_integ/05_matmul.c`. See
STATUS.md
for the currently-verified details and `tests/mutos_cc/run_goldens.sh` for a
full-corpus run (which reports every uncovered file as a clear,
expected "not yet supported" diagnostic - see "Current scope" below -
never a silent wrong-output mismatch).

## The `temp1`/`temp2` wire format

`c0`/`c1` communicate through the exact `temp1`/`temp2` tagged byte
stream V7's own `cc` pipeline comment documents (`v7/cc/cc.c`:
`c0 source temp1 temp2` / `c1 temp1 temp2 assembly.s`) - see
`docs/DEVLOG.md`'s Milestone 4 "`c0`/`c1` process split" section for
why this project deliberately keeps that boundary. The format itself
(transcribed from `v7/cc/c04.c`'s `outcode()`, reimplemented
type-safely with `stdarg.h` in `c0_outcode.c`/`c1_stream.c` instead of
the original's raw varargs-pointer-walk):

| Tag | Meaning | Bytes written |
|---|---|---|
| `B` | one opcode/operator tag | `(value & 0xFF, 0xFE)` - `0xFE` (octal `0376`) marks "this is a tag", never a data word |
| `N` | one 16-bit word | little-endian `(low, high)` |
| `S` | one symbol name | `'_'` (iff non-empty) + up to `MCC_NCPS` (8) significant chars, 7-bit-masked + a terminating `NUL` |
| `1` / `0` | the literal word 1 / 0 | shorthand constants |

`temp1` carries everything but string-literal data, which goes to `temp2`
(v7's `putstr()` switches `outcode()`'s destination with `strflg`), one
block per literal: `LABEL <n>`, then `BDATA`, `(1, byte)` per byte and
`(1, 0)` for the NUL, ended by a lone `0` - a new `BDATA` run starting
before every 15th byte. `mutos_c1` reads `temp2` after `temp1`'s `EOFC`,
behind the `.data` it always emits, rendering each run as `.byte` lines of
at most 9 hex values (`L4:.byte\t/6f,/6e,/65,/0`) - confirmed against
`05_arrptr/05_arrofptr` and `07_strlibc`, and against the line layout of
all 208 literals in `tests/mutos_as/kernel_nonopt|kernel_opt/*.s`; see
`docs/DEVLOG.md`'s Milestone 4 string-literal section.

All opcode/type/storage-class values are transcribed verbatim from
`v7/cc/c0.h` into `mutos_cc.h` - they are load-bearing wire-format
tag values, not "mixed-in V7 logic" in the sense CLAUDE.md's Workflow
Guideline 3 warns against.

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
  have since confirmed both shapes and narrowed the `chkstk` threshold
  to `(80,128]` - see the `SETSTK` paragraph further below.
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
implements this for every immediate it renders; memory-operand
displacements keep using `*` unconditionally, matching `man/
mutos_as.1`'s note that the marker "has no effect" there and every
confirmed golden's uniform `*offset.(bp)` usage regardless of
magnitude. The `-128` lower bound is the natural symmetric
completion of "fits in a sign-extended byte" but, unlike the `127`
upper bound, is not yet independently confirmed by a golden with a
negative large-magnitude constant.

## Current scope

`mutos_c0`'s grammar coverage (`c0_parser.c`) is deliberately narrow -
exactly `tests/mutos_cc/00_smoke`'s three programs plus all of
`tests/mutos_cc/01_expr`: `01_intarith.c`, `02_bitwise.c`,
`03_rellogic.c`, `04_shift.c`, `05_incdec.c`, `06_compasgn.c`,
`07_ternary.c` and `08_castsize.c`:

```
translation-unit  := extdef*
extdef            := IDENT '(' ')' compound-stmt
compound-stmt     := '{' decl* stmt* '}'
decl              := ('int'|'char'|'long') declarator (',' declarator)* ';'
declarator        := '*'* IDENT ('[' ICON ']' ('[' ICON ']')?)?
stmt              := assign-stmt | star-assign-stmt | call-stmt | return-stmt
call-stmt         := IDENT '(' ... ')' ... ';'
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
PRIMARY           := ICON | IDENT | STRING | cast-expr | sizeof-expr
                    | '(' comma-item (',' comma-item)* ')'
cast-expr         := '(' ('int'|'char'|'long') ')' IDENT
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
sibling as a genuine `CON` leaf at that point. Folding uses 16-bit
truncation (`trunc16()`), matching the target's 16-bit `int` - except
an integer literal too large for a plain `int` (K&R/C89 promotion
rule: `ExprVal` gains an `is_long` flag, carrying the full untruncated
value; see the char/long/cast/sizeof derivation below). A
postfix/prefix `++`/`--` operand and a bare array name used as an
rvalue are never treated as compile-time constants (both always
emit real code - see the increment/decrement/pointer/array
derivation below). A `?:` with a compile-time-constant condition
folds to whichever branch is selected (matching real K&R `cc`'s own
`build()`-time folding again), though not itself exercised by any
golden (`07_ternary`'s own condition is always a real comparison).
A parenthesized comma-list's embedded `IDENT '=' expr` items are
never constants (an assignment always has a side effect, so it is
always emitted for real, exactly like `assign-stmt`'s own handling).
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
`RETRN`, `SETSTK`, `EOFC`. Its
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
(non-contiguous) `switch`, block-scoped declarations,
a second simultaneously-live `register` variable (or one of a type
other than plain `int`), a function pointer with a non-empty
parameter signature, memory-to-memory
assignment, an immediate `IMUL`/`IDIV`
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

## `SETSTK` / local-frame handling

`c1_gen.c`'s `SETSTK` handler computes `extra = value - 4` (4 = the
bytes the `SAVE` prologue's own `push di`/`push si` already reserve).
`extra == 0` (no real locals, every `00_smoke` golden) needs no
additional instruction - confirmed via `tests/mutos_cc/00_smoke/
*.s.golden`'s `L1:jmp L2` with nothing in between. `extra > 0` up to
76 bytes emits a plain `"sub sp,*N."` - confirmed against
`01_intarith.s.golden`'s `extra = 6` case, and consistent with
`docs/MUTOS_C_ABI.md` sect. 1.9's real-hardware bound (largest
`libc.a` example using plain `sub sp,N`: `N=76`). The `09_abiprobe`
goldens pin both shapes down: up to `extra = 80` emits a plain `"sub
sp,N"` (`02_frame080.s.golden`: `"sub\tsp,*80."`), and from `extra =
128` up `"mov ax,N / call chkstk"` (`03_frame128` … `07_frame300`, e.g.
`"mov\tax,#300."`) - `N` rendered as an ordinary immediate either way,
so the `chkstk` form always takes the `#` marker. The real threshold is
therefore in `(80,128]`; `extra` of 81..127 bytes is an explicit "not yet
supported" rather than a guess (see `docs/DEVLOG.md`'s Milestone 4 "Open
item").

**Register-occupancy guard.** `c1` has no register allocator: each
operator loads into its golden-confirmed working register (`DI`, `AX`,
`CX`, `DX`, ...). Every emitted instruction's register effects are looked
up in `c1_gen.c`'s `INSN_FX` table, and a still-pending value (on the
value stack, or popped by a handler but not yet used) whose register an
instruction would overwrite makes `c1` stop with an explicit "not yet
supported" diagnostic - so shapes like `f(a) + a * b`, `v[a + 1]` or
`if (a + b < c)`, which need a spill or a different evaluation order, are
refused rather than silently miscompiled. The same guard keeps a
`register` local's register (`DI`/`SI`) from being used as scratch, except
while computing that variable's own new value. See `docs/DEVLOG.md`'s
Milestone 4 "`c1_gen.c` review" section.

**Evaluation order.** `c1` reads temp1 in postfix (left-operand-first)
order, but the real compiler sometimes evaluates the right operand first -
`v7/cc/table.s`'s `*` template `%n,n` computes it onto the stack before
the left one. At the start of every expression `plan_expression()`
pre-scans it (with `scan_op_args()`, the arity table `scan_consumer()`
uses too); when an operator must go right operand first it builds a plan
that replays temp1's subtrees in that order through the ordinary opcode
handlers, with a spill (`push`, value kind `VK_STACKED`) and a swap in
between. Only the golden-confirmed decision is taken: int `*` of two
complete 2-D element reads with plain-variable subscripts
(`10_integ/05_matmul.s.golden`'s `... / push di` ... `mov ax,di` / `pop
cx` / `imul cx`). Every other expression gets no plan and streams exactly
as before; shapes that would need a different order and have no golden -
`a[i] * b[j]`, `f(a) + a * b`, ... - are still refused by the guard above.
Full derivation in `docs/DEVLOG.md`'s "Evaluation order - implemented"
section.

## Next steps (roughly in dependency order)

1. **All of `01_expr` and `03_ctrlflow` are now done.**
   `03_rellogic` (relational/logical `< <= > >= == != && || !`),
   `04_shift` (`<< >>`, both variable-count-via-`CL` and
   constant-count-via-repeated-single-bit-shift - for a count of up to 2;
   from 3 up also via `CL`), `05_incdec`
   (`++`/`--`, pointer-scaled where applicable, plus the pointer/array
   groundwork it pulled in), `06_compasgn` (all ten
   compound-assignment operators, with a genuine `*=`-by-power-of-two
   strength reduction to a shift), `07_ternary` (`?:` with a new
   inverted-condition-branches-to-false-label codegen shape, and the
   `,` comma operator, which emits no code of its own), and
   `08_castsize` (`char`/`long` locals, casts between `int`/`char`/
   `long` via three confirmed conversion opcodes including the
   MUTOS-specific `CTOL`, and `sizeof` folded entirely at parse time)
   are now done -
   see the `VK_COND`
   design, the `LSHIFT`/`RSHIFT` handling, the increment/decrement/
   pointer/array derivation, the compound-assignment derivation, the
   ternary/comma-operator derivation, and the char/long/cast/sizeof
   derivation
   above. `03_ctrlflow`'s `if`/`else`, `while`, `do`/`while`, `for`,
   nested `break`/`continue`, `switch`/`case`/`default` (a genuine
   jump table), and `goto`/labels are done too - a real recursive-
   descent statement dispatcher replaced the prior flat assign/
   return-only loop; see "Current scope" above for the full
   derivation.
2. **`02_long` arithmetic, function parameters/calls (`04_funcs`),
   local `static` variables, function pointers, `long`-returning
   functions, and real `register`-variable allocation are now ALL done -
   `02_long` (4/4) and `04_funcs` (7/7) are both fully covered.**
   `long` `*`/`/`/`%` (via the `lmul`/`ldiv`/`lrem` runtime helper calls -
   see `docs/MUTOS_C_ABI.md` sect. 1.8, though the real confirmed calling
   shape turned out simpler than that section's own prose - see "Current
   scope" above), `long` `+`/`-`, an implicit `int`->`long` widening
   conversion, and a `long`-vs-constant relational comparison were done
   in an earlier session (`long` *locals*, casts, and constants already
   existed, from `08_castsize` - see above); K&R-style parameters,
   direct/indirect calls, local `static` variables, and function
   pointers (`04_funcs`) were done in a later session; `02_long/
   03_retval.c` (a `long`-returning function's `DX:AX` return-value
   convention) and `04_funcs/06_regclass.c` (real `register`-variable
   allocation) are done this session - see "Current scope" above for
   the full derivation of all of it, and `docs/DEVLOG.md`'s Milestone 4
   section for the byte-level detail behind the last two.
3. **`05_arrptr`/`06_struct`**: array subscripting (one or two dimensions),
   multi-level pointers, general `&`/`*`, and pointer/array-parameter
   equivalence are now done (5 of 7 files - `01_arrbasic`/`02_array2d`/
   `03_ptrbasic`/`04_ptrarreq`/`06_ptrptr` - see "Current scope" above and
   `docs/DEVLOG.md`'s Milestone 4 section for the full derivation).
   String literals (`05_arrofptr.c`/`07_strlibc.c`) are done too, with
   `char`/`long` arrays and pointers, pointer-returning prototypes and call
   statements (see the `temp2` paragraph above and `docs/DEVLOG.md`) - so
   all 7 of `05_arrptr` pass. Then `06_struct` (structs/unions/enums -
   member layout, sizes beyond a flat "2 bytes") - real type-system work
   the current `SymEntry`/`ExprVal` model doesn't fully have yet, untouched
   so far.
4. **Evaluation order**: done for `10_integ/05_matmul` (see "Evaluation
   order" above). `02_bubsort`'s swapped comparison (v7's `optim()`
   swapping a relational's operands by `degree()`, through `maprel[]`) and
   `03_linklist`'s right-hand-side-first store are the same mechanism with
   other decisions - each a new case in `order_right_first()`, once
   `mutos_c0` can parse those files. Before that: `mutos_c0` emits a
   constant LEFT operand of a binary operator on the right (`7 - x`
   compiles as `x - 7`) - silent wrong code no golden exercises, found by
   the `05_matmul` semantic check; see `STATUS.md`'s open items.
5. **`char` element access and `09_abiprobe/frame*`**: their goldens have
   narrowed the real `chkstk` threshold to `(80,128]` (implemented - see
   above); the files themselves now declare fine but need `char` element
   reads/writes - opcode 109 as a char-to-int conversion, `movb`/`cbw` -
   which `mutos_c0` refuses until implemented. The 81..127-byte gap stays
   "not yet supported" until a golden lands in it.
6. **`mutos_cc` driver**: chains `mutos_cpp | mutos_c0 | mutos_c1 |
   mutos_as | mutos_ld` the way `v7/cc/cc.c` does - not yet written;
   today's pipeline is exercised by invoking each tool directly (see
   `tests/mutos_cc/run_goldens.sh`). The `-P`/`-S` interaction
   CLAUDE.md flagged as "undecided... revisit once `mutos_cc`'s
   driver is actually being written" is still open.

## Source layout

- `mutos_cc.h` - opcode/type/storage-class constants shared by both
  passes (transcribed from `v7/cc/c0.h`), plus the confirmed MUTOS
  deltas documented above.
- `c0_lex.h`/`c0_lex.c` - tokenizer.
- `c0_diag.h`/`c0_diag.c` - error/warning reporting.
- `c0_outcode.h`/`c0_outcode.c` - the `temp1`/`temp2` stream writer.
- `c0_sym.h`/`c0_sym.c` - local (`AUTO`) symbol table: name → {storage
  class, type, `bp`-relative offset}, with `MCC_NCPS`-truncated name
  comparison and `v7/cc/c03.c`-matching offset assignment.
- `c0_parser.h`/`c0_parser.c` - front-end driver (current grammar
  scope above).
- `c0_main.c` - CLI entry point (`mutos_c0 source temp1 temp2 [-P]`,
  matching `v7/cc/cc.c`'s own invocation shape).
- `c1_stream.h`/`c1_stream.c` - the `temp1`/`temp2` stream reader
  (byte-level inverse of `c0_outcode.c`).
- `c1_gen.h`/`c1_gen.c` - code generator (current opcode scope
  above), including its `Val`/value-stack operand-kind tracking, the
  emission layer every line of assembly text goes through (typed `Opnd`
  operands, `ins0()`/`ins1()`/`ins2()`, `put_label()`/`put_line()`, the
  `RELOPS`/`ALUOPS` tables and `SEQ_*` fixed idioms), the
  register-occupancy guard, and the evaluation-order planner (both see
  above).
- `c1_main.c` - CLI entry point (`mutos_c1 temp1 temp2 output.s`).

## Testing

`tests/mutos_cc/run_goldens.sh` runs the real `mutos_cpp → mutos_c0 →
mutos_c1` pipeline over every corpus file that has goldens and diffs
every stage (`.i`/`.1`/`.2`/`.s`) byte-for-byte, reporting "not yet
supported" separately from a genuine mismatch (see "Current scope"
above) so the exit status stays a meaningful signal as coverage
grows.

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
numeric offset - see `v7/cc/c04.c`'s `treeout()`) correctly. An
opcode it has no confirmed argument shape for (a construct outside
`mutos_c0`'s current grammar/opcode scope, e.g. a struct or a function
call) stops the dump cleanly with a clear message rather than
guessing and silently desyncing the rest of the file - the same
"explicit not yet supported, never silently wrong" rule this project
applies everywhere else.

`dump_temp.py`'s own `OPCODES` table is a hand-transcribed copy of
every `outcode()` call site's argument shape, not something derived
automatically from the wire format at read time - see `CLAUDE.md`'s
Workflow Guideline 8 for the mandatory rule that keeps it in sync
whenever an opcode's shape (or the `TY_*`/`SC_*` constants) changes.
