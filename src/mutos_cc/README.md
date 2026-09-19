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
`mutos_c0` → `mutos_c1` → `.s`), for 12/62 of the full corpus:**
`tests/mutos_cc/00_smoke/`'s three files, plus all of `01_expr`:
`01_intarith.c`, `02_bitwise.c`,
`03_rellogic.c`, `04_shift.c`, `05_incdec.c`, `06_compasgn.c`,
`07_ternary.c` and `08_castsize.c`, plus `02_long/02_muldiv.c`. See STATUS.md
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
  example using plain `sub sp,N`: `N=76`). The symmetric `> 256`
  case (`"mov ax,N / call chkstk"`) is implemented from that same
  document but not yet confirmed against a golden of its own; the
  unconfirmed `(76,256]` gap remains an explicit "not yet supported"
  rather than a guess - see `tests/mutos_cc/09_abiprobe/`.
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
decl              := ('int' declarator (',' declarator)*
                      | ('char'|'long') IDENT (',' IDENT)*) ';'
declarator        := '*' IDENT | IDENT ('[' ICON ']')?
stmt              := assign-stmt | star-assign-stmt | return-stmt
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
PRIMARY           := ICON | IDENT | cast-expr | sizeof-expr
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
single-bit-shift form N times, confirmed against `04_shift.s.golden`'s
`"r >> 2"` (two `sar\tdi,*1` lines) and `"a << 1"` (one `sal\tdi,*1`
line). This is still the minimum
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
power-of-two ≥ 2 via the same N-times-repeat reasoning `LSHIFT`/
`RSHIFT` already established (only `*2` is itself golden-confirmed).
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
di,<hi>` split. `long` `+`/`-` remain unconfirmed - `02_long/01_addsub.c`
also needs `if` (`03_ctrlflow` scope), so there is no golden to verify
their `c1` codegen shape against yet.

**Anything outside this - unary `-`/`+` on a non-constant operand
(unary `~`/`!` are covered), a non-`int`/non-pointer/non-array/non-
`char`/non-`long`
declaration, array subscripting, a compound-assignment operator's
right-hand side being anything other than a compile-time constant
(or, for `*=`, anything other than a power-of-two constant), a `?:`
branch that is itself a bare relational comparison, a compound-
assignment operator inside a comma-list, a cast combination other
than the three confirmed ones, `sizeof` on an array or a general
expression, `long` `+`/`-` or any `long` operand outside a bare
`NAME`, a `char`/`long` pointer or array,
function parameters, a third kind of statement, memory-to-memory
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

## `SETSTK` / local-frame handling

`c1_gen.c`'s `SETSTK` handler computes `extra = value - 4` (4 = the
bytes the `SAVE` prologue's own `push di`/`push si` already reserve).
`extra == 0` (no real locals, every `00_smoke` golden) needs no
additional instruction - confirmed via `tests/mutos_cc/00_smoke/
*.s.golden`'s `L1:jmp L2` with nothing in between. `extra > 0` up to
76 bytes emits a plain `"sub sp,*N."` - confirmed against
`01_intarith.s.golden`'s `extra = 6` case, and consistent with
`docs/MUTOS_C_ABI.md` sect. 1.9's real-hardware bound (largest
`libc.a` example using plain `sub sp,N`: `N=76`). `extra > 256` emits
`"mov ax,*N. / call chkstk"` per that same document, though this
branch is not yet confirmed against a golden of its own. The
unconfirmed `(76,256]` gap in between is an explicit "not yet
supported" rather than a guess - `tests/mutos_cc/09_abiprobe/
frame080.c` … `frame300.c` exist specifically to pin this down once
analyzed (see `docs/DEVLOG.md`'s Milestone 4 "Open item").

## Next steps (roughly in dependency order)

1. **All of `01_expr` is now done.**
   `03_rellogic` (relational/logical `< <= > >= == != && || !`),
   `04_shift` (`<< >>`, both variable-count-via-`CL` and
   constant-count-via-repeated-single-bit-shift), `05_incdec`
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
   above.
2. **`02_long` arithmetic (remainder), `05_arrptr`/`06_struct` groundwork**:
   `long` `*`/`/`/`%` (via the `lmul`/`ldiv`/`lrem` runtime helper calls -
   see `docs/MUTOS_C_ABI.md` sect. 1.8, though the real confirmed calling
   shape turned out simpler than that section's own prose - see "Current
   scope" above) is now done, confirmed against `02_muldiv.c` (`long`
   *locals*, casts, and constants already existed, from `08_castsize` -
   see above). Still open in `02_long` itself: `long` `+`/`-`
   (`01_addsub.c` - blocked on `if`, i.e. `03_ctrlflow`, not on anything
   `long`-specific) and a `long`-returning function's `DX:AX` return-value
   convention plus `long` parameters (`03_retval.c`/`04_params.c` -
   blocked on `04_funcs`). Array subscripting and
   multi-level pointers/multi-dimensional arrays (single-degree
   pointers and single-dimension arrays already exist, from
   `05_incdec` - see above), structs/
   unions/enums - each adds real type-system work (sizes beyond a
   flat "2 bytes", degree-of-reference, member layout) the current
   `SymEntry`/`ExprVal` model doesn't fully have yet.
3. **`03_ctrlflow`**: `if`/`while`/`for`/`do`/`switch`/`goto` - each
   needs its own confirmed label-allocation/branch shape, the same
   way `00_smoke`'s `sloc`/`sloc+1`/`retlab` scheme was derived here.
4. **Function parameters and calls** (`04_funcs`): parameter offsets
   (`bp+4, bp+6, ...` per `docs/MUTOS_C_ABI.md` sect. 1.3), the
   right-to-left-push/caller-cleanup call sequence, and (for
   `07_funcptr`) function-pointer types.
5. **`09_abiprobe/frame*`**: pins down the real `chkstk` threshold
   once those goldens exist, unblocking `SETSTK`'s unconfirmed
   `(76,256]` gap.
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
  above), including its `Val`/value-stack operand-kind tracking.
- `c1_main.c` - CLI entry point (`mutos_c1 temp1 temp2 output.s`).

## Testing

`tests/mutos_cc/run_goldens.sh` runs the real `mutos_cpp → mutos_c0 →
mutos_c1` pipeline over every corpus file that has goldens and diffs
every stage (`.i`/`.1`/`.2`/`.s`) byte-for-byte, reporting "not yet
supported" separately from a genuine mismatch (see "Current scope"
above) so the exit status stays a meaningful signal as coverage
grows.
