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
"Current scope" (now `../../docs/history/cc_current_scope.md`) come from
that time.

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
the git history: commit `a870e55`, unchanged until its deletion in
`ff1e589`); the code-generation explanations in "Current scope" (now
`../../docs/history/cc_current_scope.md`) come from that time.

**Status: verified byte-exact, end-to-end (`.c` → `mutos_cpp` → `mutos_c0` →
`mutos_c1` → `.s`), on every golden of `tests/mutos_cc/`** - the 62-file
construct corpus (`00_smoke` to `10_integ`, 62/62), the nine real kernel
driver files of `11_kernel/` (9/9) and the 61 probes of `fltprobe/`'s
fourteen rounds: 50 byte-exact, ten more together with the real compiler's
own `c1` error messages and exit status (`tests/mutos_cc/c1_errors.txt`),
and `p19_open3` up to the real compiler's own invalid output, which
`mutos_c1` refuses on purpose (`tests/mutos_cc/invalid_goldens.txt`). See
`../../STATUS.md` for the currently-verified details and
`tests/mutos_cc/run_goldens.sh` for a full-corpus run (which reports a
refusal of either port - where the original traps or reads state the port
does not model - apart from a mismatch, never as a silent wrong output).

The driver `mutos_cc` and the optimizer `mutos_c2` are not written yet;
both are to be ports of the original binaries
(`../../docs/TOOLCHAIN_PLAN.md`), see "Next steps".

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

## Current scope

Until 2026-10-20 this section described, construct by construct, what
the hand-written `mutos_c0` and `mutos_c1` covered and the code the
real compiler gives each construct (the floating point among it). It is
kept, unchanged, in
[`docs/history/cc_current_scope.md`](../../docs/history/cc_current_scope.md):
its findings about the real compiler's output still hold, its
statements about the two replaced programs are history. The ports
compile everything the originals compile; they refuse only where an
original traps or reads state the port does not model (see the next
two sections).

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

## Next steps

The order is `../../docs/TOOLCHAIN_PLAN.md`'s (decided 2026-10-20):

1. **`mutos_cc` driver** - a port of the original `cc` binary, which differs
   from `v7/cc/cc.c` (`as -gC`, `-f`, `-o name.o`, its messages - the plan's
   section 5). It runs the passes as the original runs `/lib/cpp`,
   `/lib/c0`, ... - under `$MUTOS_ROOT`, where the build links them - and
   passes `$MUTOS_ROOT/lib/crt0.o` and `-lc` to `ld`. It comes after the
   ports of `cpp` (whose `# N "file"` lines the original `c0`'s messages
   need) and `ld` (whose `-l` it relies on). Until then the pipeline is
   exercised by invoking each tool directly (`tests/mutos_cc/run_goldens.sh`).
   The `-P`/`-S` question is settled by the original: `-P` stops after
   `cpp`, with or without `-S`.
2. **`mutos_c2`** (Milestone 5): a port of the original `c2`, 8086 only - the
   emulated original reproduces `tests/mutos_as/kernel_opt/`'s optimized
   kernel sources from `11_kernel`'s `.s` goldens.

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
every stage (`.i`/`.1`/`.2`/`.s`) byte-for-byte, reporting a refusal of
either port (where the original traps or reads state the port does not
model) separately from a genuine mismatch, so the exit status stays a
meaningful signal. A file whose goldens lack the `.i` is checked from `mutos_c0` on
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
constant-left-operand bug of the old front end and two wrong-code bugs
of `c1_gen.c` (all three fixed before the ports) - see its README. With `--scope` it also makes some
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
