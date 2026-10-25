# mutos_cc / mutos_c0 / mutos_c1 / mutos_c2 - MUTOS 1700 Cross C Compiler (Milestones 4 and 5)

`mutos_cc`, the driver that runs the passes, is since 2026-10-24 a
**port of the original MUTOS 1700 `cc`** - v7's `cc` with the MUTOS
deltas - read from its binary (`ccport/`, `../../docs/TOOLCHAIN_PLAN.md`,
step 3): v7's functions with the binary's deltas, run on an image of the
original's 64K segment, its text and data generated from the binary. It
finds its passes through the MUTOS root and is checked against the
original itself, run under an 8086 emulator with its passes traced - see
"The port of the real `cc`" below.

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

`mutos_c2`, the optimizer `cc -O` runs between `c1` and `as`, is since
2026-10-25 a **port of the original MUTOS 1700 `c2`** - v7's `c2`,
rewritten for the 8086 and the MUTOS assembler, with passes of its own -
read from its binary (`c2port/`, `../../docs/TOOLCHAIN_PLAN.md`, step 4):
v7's functions with the binary's deltas and the new ones, run on an image
of the original's data segment, its initialized data generated from the
binary. It is checked against the original itself, run under an 8086
emulator, on every `.s` golden and on random input, and gives
`tests/mutos_as/kernel_opt/`'s optimized kernel sources byte for byte -
see "The port of the real `c2`" below. Until then `mutos_cc -O` said
`Can't find /lib/c2` and assembled `c1`'s output unoptimized, as the
original does without `/lib/c2`.

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

## The port of the real `cc`

`mutos_cc` is the original `cc` (`mutos_bin/cc.golden.base64.txt`, 0407,
text 8,026 bytes, stripped): v7's `cc.c`, its ten functions in source
order, with the MUTOS deltas. Every function was read instruction by
instruction from the binary beside `v7/cc/cc.c`
(`../../tests/realbin/ccmap.txt` maps them, `ccdis.py` disassembles one
by name); the port does what the binary does, its bugs included
(`../../docs/MUTOS_ORIGINAL_BUGS.md`, CC-01 to CC-06).

### Behaviour (the original's)

`mutos_cc [option ...] file ...`, an option's letter right after the
`-`, its value the rest of the argument (`-o`'s the next one):

- **The passes.** For a source `x.c` (with `-E` any name): `cpp x.c
  /tmp/ctm4...` (without `-P`: the `# N "file"` lines), `c0 tmp4 tmp1
  tmp2 [-P]`, `c1 tmp1 tmp2 tmp3 [s<n>]`, with `-O` `c2 tmp5 tmp3`, then
  `as -gC -o x.o tmp3`; for `x.s` only `as`. The passes are `/lib/cpp`,
  `/lib/c0`, `/lib/c1`, `/lib/c2`, `/bin/as`, `/bin/ld`; with `-f`
  `/lib/c0f`, `/lib/c1f`, `/bin/asf` and `-lf` before `-lc`.
- **The link.** `ld -X /lib/crt0.o [-o out] file ... -lc` - the objects,
  archives, other names and unknown options (`-i`, `-s`, `-x`, `-lm`,
  ...) in their order -, unless there was an error, `-c`, `-S`, `-P` or
  `-E`. One source alone: its `.o` is removed after a successful link.
- **The options.** `-c` no link; `-S` stops after `c1` with `x.s`; `-P`
  runs `cpp ... -P` into `x.i` and nothing else, with or without `-S`;
  `-E` runs `cpp x.c - -E`; `-O` adds `c2` (a `c2` that fails is passed
  over: `as` gets `c1`'s output); `-p` gives `c0` `-P` and links
  `/lib/mcrt0.o`; `-2` links `/lib/crt2.o`, `-20` `/lib/crt20.o` and
  `-l2` for `-lc`; `-D`, `-I`, `-U`, `-C` go to `cpp` (at most 100: "Too
  many DIUC options"); `-t[012p]` and `-B[prefix]` replace passes by the
  prefix and their name (`-t` alone: all four; the prefix by default
  `/usr/src/cmd/c/`, `-B` alone `/usr/src/cmd/c/o`); `-a` writes
  `Executing cpp...`, `Executing c0...`, `Executing c1...`, `Executing
  Optimizer...`, `Assembler as ...`, `Loader...` before each pass; `-K<n>`
  gives `c1` the option `s<n>` (the frame size from which a function
  calls `chkstk`). `-o name` names the output; a name that is a `.c` or
  `.o` name, or starts with `-`, is an input as well, and the output is
  then the next object's name without suffix and directory (`cc -o
  hello.c` compiles `hello.c` and writes `hello`).
- **Messages and exit status.** `x.c:` on stdout before each source's
  passes when there are several; `Can't find /lib/c0f` for a pass that is
  not there (an error); `Fatal error in /lib/c0` for a pass killed by a
  signal other than SIGINT (exit status 8, `cc` ends); the passes' own
  messages. The exit status counts the errors, `ld`'s status or'ed in.
- **Temporary files.** `/tmp/ctm0` and six digits of the process number,
  created with mode 0400 and kept for the run; `ctm1` .. `ctm5` beside it
  for the passes. All are removed at the end, and on SIGINT and SIGTERM
  (exit status 100).

### The memory image

`cc` keeps its file names in fixed arrays and copies into them without a
check: a `-K` argument runs over `kopt[]` and the passes' paths (CC-03), a
`-B` prefix over the next path (CC-04), a long source path past `ts[]`
into `av[]` or past its `malloc()` block (CC-01), more than 50 arguments
for `ld` past `av[]` into `clist[]` and `llist[]`, more than 100 `-P`
past `ptemp[]` into `main()`'s `pv` (CC-02) - and it reads through a null
pointer with `-P` (CC-06) and through -1 after an unresolved `-o`
(CC-05). So the port keeps the original's 64K segment as an image
(`ccport/cc.h`): the binary is 0407 - one segment -, so its text is at
address 0 (read only), its data at 1f5a (`ccport/ccdata.c`, generated by
`../../tests/realbin/cctables.py --c`), the BSS at 22d8, the heap from
2db0 (libc.a's `malloc()` re-enacted on the image, its words at the
binary's addresses), `main()`'s frame and the argument words and strings
at the top, laid out as crt0 and `tests/realbin/mutosemu.py` lay them
out. Words are little-endian at any address, chars signed.

What the image does not model is refused - exit status 2 and a message
on stderr ending in "refused" (`ccport/cclib.c`, `cctrap()`):

- libc.a's data and BSS (the FILE structures, the stdio buffers, the
  signal table) - reached by about 150 `-P` (CC-02);
- memory past the break - reached when a `malloc()` block's overrun
  (CC-01) breaks the list of blocks;
- a heap that reaches `main()`'s frame (MUTOS refuses such a break, the
  emulator gives it), arguments longer than about 32K;
- a printf conversion the port's printf does not have (only from formats
  a `-K` argument has written over).

### Paths and the passes

What the original names by an absolute path of the MUTOS file system - its
passes (`/lib/c0`, `-t`'s `/usr/src/cmd/c/c0`, ...), the startup files
(`/lib/crt0.o`, ...), its temporary files (`/tmp/ctm...`) - is looked up
under the MUTOS root (`$MUTOS_ROOT`, by default the build tree's
`mutos_root/`; `../h/mutos_root.h`): where `cc` uses it itself (`execv()`,
`creat()`, `access()`, `unlink()`) and where it passes it to a pass,
which gets the host path (`$MUTOS_ROOT/tmp/ctm4...`,
`$MUTOS_ROOT/lib/crt0.o`). A name from the command line - a source, an
object, `-o`'s name, a `-B` prefix - is the host's
(`../../docs/TOOLCHAIN_PLAN.md`, decision 4). The image marks every byte
that came from the command line and carries the mark along where `cc`
copies a string: an absolute name is a MUTOS path when its `/` is not so
marked. The passes run as real host processes (`fork()`, `execv()`,
`wait()`) with `MUTOS_ROOT` set to `cc`'s root in their environment. In
the build tree's root `lib/cpp`, `lib/c0`, `lib/c1`, `bin/as`, `bin/ld`
are symbolic links to `mutos_cpp`, `mutos_c0`, `mutos_c1`, `mutos_as`,
`mutos_ld`, `lib/c2` to `mutos_c2` (since 2026-10-25; until then it
dangled).

The process number in the temporary names is the host's, folded to 15
bits as MUTOS's are - two runs at the same time in one root whose numbers
agree in those 15 bits would meet at `/tmp/ctm0...` (the second says `cc:
cannot create temp`, or, run as root, shares the files).

### How it is checked

- `../../tests/realbin/cccmp.py` runs the port and the original (under
  the emulator, its `fork`/`exece`/`wait` faked) on the same command line
  in copies of one directory, each with a MUTOS root of its own, and
  compares the passes run with their paths and arguments, what was
  written to stdout and stderr before, between and after them, the exit
  status and the files left (in the directory and the root's `tmp/`).
  For the port every pass is `tests/realbin/ccstub.c`, which logs its
  arguments and leaves the files the pass would write. `cases`: 177
  command lines with the passes traced - every option, `-o` in its forms,
  the limits and the overruns, passes that fail or are killed; `run`:
  the construct corpus with the emulated original passes run in both;
  `programs`: the port with the ported passes against the original chain
  (the same `a.out`); `goldens`: the ported chain against the
  real-hardware goldens (`cc -S`, `cc -P`, `cc -O -S` - with the original
  `c2` until 2026-10-24, since then with `mutos_c2`).
- `../../tests/realbin/ccfuzz.py`: differential fuzzing - random command
  lines of every option, names of every kind, the limits, failing
  passes.
- `../../tests/mutos_cc/check_driver.py` (in `make test`, no emulator):
  the port with the ported passes against the goldens and linking the
  construct corpus.

## The port of the real `c2`

`mutos_c2` is the original `c2` (`mutos_bin/c2.golden.base64.txt`, 0411,
text 17,792 bytes, stripped): crt0, then `c20.o` - v7's `c20.c` in source
order, `reducelit()` gone -, then `c21.o` - v7's `c21.c` rewritten for the
8086, twelve functions new -, then libc.a. Every function was read
instruction by instruction from the binary beside `v7/cc/c20.c` and
`c21.c` (`../../tests/realbin/c2map.txt` maps them, `c2dis.py`
disassembles one by name); the port does what the binary does, its bugs
included (`../../docs/MUTOS_ORIGINAL_BUGS.md`, C2-01 to C2-07).

### Behaviour (the original's)

- **The command line.** `mutos_c2 [+] [-] [input [output]]` - `cc -O`
  runs `c2 tmp5 tmp3`. The input replaces stdin (`C2: can't find x`,
  exit status 1), the output stdout (`C2: can't create x`); `-` writes
  the statistics to stderr at the end (`%d iterations` .. `%d long
  branches to short`, `%dK core`), `+` sets a flag nothing reads. A line
  of more than 510 characters: `C2: Sorry, input line too long`, exit
  status 1; no memory: `C Optimizer: out of space`.
- **The work, by chunks.** The input is read up to each `.globl` (a
  chunk, usually a function) and optimized and written before the next
  is read; the memory for the instructions is used again for each. v7's
  passes run on it - jumps to jumps, code after jumps, jumps to the next
  line, unreferenced labels, cross-jumps, code motion, loop inversion,
  redundant moves on what the registers are known to hold, constant
  comparisons - in the MUTOS assembler's syntax: `op dst,src`, constants
  `*n.`, the 8086's registers (ax dx si di bx cx; bp the frame). "`jmp`"
  and the short "`j`", "`je`" .. "`jae`" are read as v7's `br` and
  conditional branches.
- **The new passes.** After `rmove()`: `pushopt()` - registers renamed
  backwards and forwards to remove a `mov` between them, `mov r,x` /
  `push r` to `push x`, `push x` .. `pop r` to `mov r,x` -, the register
  variables kept out as the chunk's least `|NREG n` says; then `indexopt()`
  (`add si,bx` before `(si)` becomes `(si)(bx)`), `simplify()` (`mov
  r,*0.` to `sub r,r`, `cmp r,*0.` to `test r,r`, `and ax,#255.` to `subb
  ah,ah`) and `shortbr()`, which writes a branch whose label is near
  enough by the instructions' lengths as the short `j`/`je`/...
- **What it is checked by**: `tests/mutos_as/kernel_opt/`'s nine
  optimized kernel sources are its output for `11_kernel`'s `.s` goldens -
  real-hardware goldens - and the emulated original for anything else.

### The memory image

`c2` keeps the instructions as 16-byte nodes in memory it takes with
`sbrk()`, the operands it looks at in buffers of 32 bytes (`regs[]`), and
copies into those and others without a check (C2-03): a long operand runs
over `regs[]` into the opcode hash table, a long first token past
`tmpop[]`, `repladdr()`'s copies past `rt1[]` and `rt2[]` into libc.a's
data and the heap; a jump to an undefined label makes it write through a
null pointer (C2-06), and an empty line after a label through another
(C2-07). So the port keeps the original's data segment as an image
(`c2port/c2.h`): the binary's initialized data at address 0
(`c2port/c2data.c`, generated by `../../tests/realbin/c2tables.py --c`),
every global at its address, the BSS and from 1120 the heap, crt0's words
at 0 .. 9 and the argument words and strings at the top, laid out as
`tests/realbin/mutosemu.py` lays them out. libc.a's stdio runs on the image
too - the FILE structures, `_sibuf` and `_sobuf` where the original has
them -, so that an overrun into them does what it does in the original.
Words are little-endian at any address, chars signed.

What the image does not model is refused - exit status 2 and a message on
stderr ending in "refused" (`c2port/c2lib.c`, `c2trap()`), after what the
original had written by then:

- memory past the break (the emulator gives zeros, MUTOS gives what is
  there) - reached through the opcode hash table a long operand has
  written over (C2-03), or nodes read through such pointers;
- a system call after crt0's far pointer (words 6 and 8, which a node
  pointer that is null reaches) was written over;
- a heap that would reach the stack (MUTOS's own break limit is not
  known; the emulator grants any break);
- the original's endless loops where the port sees them: `dest()` with a
  destination that stays equal to a register's entry (C2-01),
  `iterate()` deleting the code after a branch in a list that has become
  a circle (C2-07);
- a directory as the input, a printf conversion the port's printf does
  not have.

### How it is checked

- `../../tests/realbin/c2cmp.py` runs the port and the original (under
  the emulator) on the same command line in copies of one directory and
  compares the output, the messages and the exit status; a refusal must
  have written a prefix of the original's output. `goldens`: every `.s`
  golden of `tests/mutos_cc` three ways - `c2 in out`, `c2 - in out` (the
  statistics), `c2 <in >out` - and the nine `11_kernel` outputs against
  `kernel_opt/`; `cases`: hand-written cases - the command lines, the
  input's form, the line buffer's end, long tokens and operands over the
  fixed buffers, many chunks, a large function, the heap's end, label
  numbers, every bug of C2-01 to C2-07.
- `../../tests/realbin/c2fuzz.py`: differential fuzzing - the real
  compiler's output for random programs (`c1fuzz.py`'s and `fuzz_c.py`'s,
  through the ported chain), assembly made to reach every pass, and both
  damaged at random (`--mutate`).
- `../../tests/mutos_cc/check_driver.py` (in `make test`, no emulator):
  `cc -O -S` on `11_kernel` against `kernel_opt/`, `cc -O -o` on the
  construct corpus.

## Next steps

The order is `../../docs/TOOLCHAIN_PLAN.md`'s (decided 2026-10-20); the
driver (step 3) and `mutos_c2` (step 4) are done - every pass `cc` runs is
a port of the original now. What comes next is the assembler's
differential fuzzer (step 5); 80186/V30 code generation in `mutos_c1` and
`mutos_c2` waits until the 8086 toolchain works completely (decision 8).

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

- `ccport/` - `mutos_cc`, the port of the real `cc` (see "The port of
  the real `cc`"): `cc.h` (the image, the binary's addresses), `cc.c`
  (`cc.o`'s ten functions), `cclib.c` (the machine: the image's initial
  state, libc.a's printf, `strcpy()`, `strcat()`, `malloc()`,
  `mktemp()`, the system calls on the host, the paths, the refusals),
  `ccdata.c` (generated: the binary's text and data), `main.c`.
- `c2port/` - `mutos_c2`, the port of the real `c2` (see "The port of
  the real `c2`"): `c2.h` (the image, the binary's addresses, the node),
  `c20.c` (`c20.o`'s functions, `main()` to `backjmp()`), `c21.c`
  (`c21.o`'s, `rmove()` to `renfwd()`), `c2lib.c` (the machine: the
  image's initial state, crt0's words, `sbrk()`, libc.a's stdio on the
  image, the system calls on the host, the refusals), `c2data.c`
  (generated: the binary's data), `main.c`; invoked as `cc` invokes it:
  `mutos_c2 [+] [-] [input [output]]`.
- `c0port/` - `mutos_c0`, the port of the real `c0` (see "The port of
  the real `c0`"); invoked as v7's `cc` invokes `c0`: `mutos_c0
  source.i temp1 temp2 [profileflag]`.
- `c1port/` - `mutos_c1`, the port of the real `c1` (see "The port of
  the real `c1`"); invoked as v7's `cc` invokes `c1`: `mutos_c1 temp1
  temp2 output.s [options]`.
- `c1_fltdec.h`/`c1_fltdec.c` - `libc.a`'s `ecvt()` on the MUTOS double,
  for `mutos_c1`'s `%.17e` (and `fltdec_test`, below).

## Testing

`tests/mutos_cc/check_driver.py` (in `make test`) runs `mutos_cc` with the
ported passes through the build tree's MUTOS root: `cc -S` and `cc -P` on
every golden's source (the `.s` with `c1`'s messages and exit status, the
`.i`), `cc -o` on the construct corpus. `tests/realbin/cccmp.py` and
`ccfuzz.py` (`make check-realbin`) compare it with the original `cc` - see
"The port of the real `cc`". `check_driver.py` also runs `cc -O -S` on the
nine `11_kernel` sources against `tests/mutos_as/kernel_opt/` and `cc -O
-o` on the construct corpus - `mutos_c2` in the chain; `tests/realbin/c2cmp.py`
and `c2fuzz.py` compare `mutos_c2` with the original `c2` - see "The port
of the real `c2`".

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
