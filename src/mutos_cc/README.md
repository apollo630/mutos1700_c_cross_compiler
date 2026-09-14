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
`mutos_c0` → `mutos_c1` → `.s`), for 5/62 of the full corpus:**
`tests/mutos_cc/00_smoke/`'s three files, plus
`tests/mutos_cc/01_expr/01_intarith.c` and `02_bitwise.c`. See STATUS.md
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
exactly `tests/mutos_cc/00_smoke`'s three programs plus
`tests/mutos_cc/01_expr/01_intarith.c` and `02_bitwise.c`:

```
translation-unit  := extdef*
extdef            := IDENT '(' ')' compound-stmt
compound-stmt     := '{' decl* stmt* '}'
decl              := 'int' IDENT (',' IDENT)* ';'
stmt              := assign-stmt | return-stmt
assign-stmt       := IDENT '=' expr ';'
return-stmt       := 'return' expr? ';'
expr              := BITOR
BITOR             := BITXOR ('|' BITXOR)*
BITXOR            := BITAND ('^' BITAND)*
BITAND            := ADD ('&' ADD)*
ADD               := MUL (('+'|'-') MUL)*
MUL               := UNARY (('*'|'/'|'%') UNARY)*
UNARY             := ('-'|'+'|'~') UNARY | PRIMARY
PRIMARY           := ICON | IDENT | '(' expr ')'
```

Every function is implicitly `int`-returning (K&R default) and takes
no parameters. A block may declare any number of plain `int` locals
(no initializers) before its statements, matching K&R's
declarations-before-statements rule; each statement is either a
single-variable assignment or a `return`. Expression evaluation
(`c0_parser.c`'s `parse_expr()`/`parse_bitor()`/.../`parse_add()`
family) uses the `ExprVal` fold-or-emit representation: a
subexpression stays an unmaterialized compile-time constant for as
long as every operand feeding it is also constant (matching real K&R
`cc`'s own per-operation `build()`-time folding - the golden for
`return 6 * 7;` still contains a single folded `CON(42)` node, never
a `TIMES` opcode), and is emitted as a real `NAME`/operator tree the
moment a variable enters the picture, materializing any constant
sibling as a genuine `CON` leaf at that point. Folding uses 16-bit
truncation (`trunc16()`), matching the target's 16-bit `int`.

`mutos_c1`'s opcode coverage (`c1_gen.c`) matches exactly what the
above grammar can produce: `SYMDEF`, `PROG`, `EVEN`, `RLABEL`, `SAVE`,
`SETREG`, `BRANCH`, `LABEL`, `ANAME`, `NAME`, `CON`, `PLUS`, `MINUS`,
`TIMES`, `DIVIDE`, `MOD`, `AND`, `OR`, `EXOR`, `COMPL`, `ASSIGN`,
`RFORCE`, `EXPR`, `RETRN`, `SETSTK`, `EOFC`. Its own value stack
(`Val`/`push_val`/`pop_val` in `c1_gen.c`) tracks, for each pending
intermediate value, whether it's an immediate, a `bp`-relative memory
location, or a value already in a specific register - the minimum
needed to pick a legal 8086 instruction shape (e.g. `IMUL`/`IDIV`
cannot take an immediate operand directly, and an immediate's `*`/`#`
size-marker choice depends on whether it fits a signed byte) without
a general table-driven register allocator (see "Next steps" below).

**Anything outside this - unary `-`/`+` on a non-constant operand
(unary `~` is covered), a non-`int` declaration,
relational/logical/shift/compound-assignment operators, function
parameters, a second kind of statement, memory-to-memory assignment,
an immediate `IMUL`/`IDIV` operand, any opcode `c1` doesn't recognize
- is a clear, explicit "not yet supported" diagnostic and a nonzero
exit status, never silently-wrong output.** This is a deliberate
design choice, not an oversight: `tests/mutos_cc/run_goldens.sh`
relies on this to keep its pass count an honest measure of verified
coverage as grammar support grows.

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

1. **The rest of `01_expr`** (`03_rellogic` … `08_castsize`):
   relational/logical `< <= > >= == != && || !` (the first construct
   needing real conditional branching - a 0/1 result on the 8086
   means `cmp` + a conditional jump, since there's no `setcc` before
   the 386), shift `<< >>` (a variable shift count must load into
   `CL`), `++`/`--`, compound assignment (`+= -=` etc.), `?:`, the
   comma operator, casts, and `sizeof` - each needs its own confirmed
   opcode/instruction shape, the same way `+ - * / % & | ^ ~` were
   derived here. `05_incdec` also needs pointer-scaled arithmetic
   (`p++` on an `int *` must step by `SZINT=2`, not 1) and thus
   arrays/pointers, pulling in some of item 2 below early.
2. **`02_long`/`05_arrptr`/`06_struct` groundwork**: `long` (the
   `almul`/`aldiv`/`alrem` extended-ABI runtime calls - see
   `docs/MUTOS_C_ABI.md` sect. 1.8), arrays, pointers, structs/
   unions/enums - each adds real type-system work (sizes beyond a
   flat "2 bytes", degree-of-reference, member layout) the current
   `TY_INT`-only `SymEntry`/`ExprVal` model doesn't have yet.
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
