# The original binaries and the ports of `c1` and `c0`

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). From reading the original MUTOS 1700 binaries (2026-10-12) to the ports of `c1` (2026-10-13) and `c0` (2026-10-14, 2026-10-18), and what the other originals showed (2026-10-20).

---

## Plan after round 13: read the original `c1` (2026-10-10)

Asked whether `c1_gen.c` converges or circles, the honest answer was: both.
For the shapes probed, the output is byte-exact (111 files, 0 mismatches,
a 28,000-program differential fuzz without wrong code). But the shape
*space* does not close: the corrections per round were 7, 7, 3, 11 and 12,
round 13 refuted twelve of some sixty inferences, and each new rule widens
the neighbourhood the next round has to ask about. Round 13 also exposed a
silent wrong-code bug (a `long` variable passed as an argument pushed as its
high word alone) that neither the goldens nor the fuzzer had shown. And
`c1_gen.c` (~15k lines, one file) is a growing list of `is_*` predicates
(`is_sube`, `is_lspillx`, `is_callbitx`, ...), each describing a shape that
was seen, not the mechanism of v7's `optim()`/`acommute()`/`degree()`/
`rcexpr()` and its code tables; the rule "a handful or fewer corrections
ends the series" would not have been met.

What closes the space is the real compiler's own tables. v7's `c1` is small
C code plus tables (`efftab`, `regtab`, `cctab`, `sptab`, `instab`, whose
templates are plain text); the obstacle named earlier for a faithful port -
"the MUTOS tables are unknown" - disappears if the original `c1` binary is
read: the tables are data structures that can be extracted without
disassembling anything, and the few key functions can be compared with
`v7/cc/c10.c`..`c13.c`. Decisions taken:

1. **Round 14 is not run** (`p54`..`p57` stay in the tree, prepared). A
   hardware run later is a check of what the tables predict, not a data
   source.
2. **First the binary**: bring `c1` (and `c0`, `as`, `ld`) from the hardware;
   header and symbols; extract the tables; compare the key functions;
   test the tables against the existing goldens. Only then decide whether
   `c1_gen.c` becomes table-driven (step by step) or the tables serve as
   a reference to correct it by. Risks, to be seen only with the file: a
   stripped binary, another memory model or overlays, MUTOS-specific
   deviations from v7 (registers, `long`, floating point).
3. **Then `11_kernel`** (the project's purpose, and the best measure of
   which shapes matter), with int division by a constant. Measuring how far
   the nine kernel files get today needs no hardware and no binary.
4. **Independent of the outcome**: split `c1_gen.c` into modules and add
   tests for call arguments and `long` arguments.

## The original binaries (2026-10-12)

The binaries came from the hardware in commit `e8c7516` -
`mutos_bin/<name>.golden.base64.txt` for `as`, `c0`, `c1`, `c2`, `cc`,
`cpp` and `ld` (headers in `mutos_bin/README.md`): all stripped of
relocation, all but `cpp` stripped of symbols, all 0411 (separate I&D)
but `cc` (0407). This section is what reading them showed; the tools are
in `tests/realbin/` (`make check-realbin`), the consequence is
`docs/C1_PORT_PLAN.md`.

### They run: an emulated oracle

`tests/realbin/mutosemu.py` loads a MUTOS `a.out` into Unicorn - text in
one 64K segment, data, BSS and stack in another for 0411 - lays out
`crt0`'s argument stack and serves the two far-call trampolines `crt0`
sets up: `Iscadr` is CS:0002, the system call (number in AX, arguments
in BX, CX, SI, DI as `sys123.o` and `lseek.o` load them, results in AX
and BX, carry set on error), `Isgadr` CS:0004, `chkstk`'s stack growth
(see `docs/MUTOS_C_ABI.md` 2.2 - `Iscadr`, data 0006, holds offset 2,
`Isgadr`, data 0002, offset 4; `crt0` fills in `cs` as their segment). Fourteen system calls are enough for
the compiler passes and the assembler; `cc` itself needs `fork`/`exec`
and does not run.

Against the project's goldens (`tests/realbin/check_real.py goldens`):

- the real `cpp`: 137/137 `.i` goldens (`tests/mutos_cc`'s 132 and
  `tests/mutos_cpp/c`'s five with `run_goldens.sh`'s defines);
- the real `c0`: 132/132 `.1`/`.2` goldens;
- the real `c1`: 132/132 `.s` goldens, with exactly the messages of
  `c1_errors.txt` and of `round14.log` (`p56_fltinf9`, `p57_fltstk8`:
  eighteen each) and exit status 1 where it printed any - `p19_open3`'s
  invalid lines included (bytes read past the end of a table, which the
  data segment, loaded from the file as the kernel loads it, reproduces);
- the real `as`: 75/76 `.o` goldens, the 76th a finding (below).

So any `.c`, `.i`, `.1`/`.2` or `.s` gets the real toolchain's answer in
a fraction of a second, without the hardware - including the whole
`cpp`, `c0`, `c1`, `as` pipeline on `11_kernel/09_amx.c`, byte-exact at
each stage.

### `c1` is v7's `c1`, ported to the 8086

Mapping the stripped text by the strings each function prints, the
calls it makes and its position (`tests/realbin/c1map.txt`;
`libc.a` matched against the text by its objects' code with relocated
words as wildcards - `tests/realbin/c1dis.py`): `crt0`, then 67
functions in exactly v7's source order (`c10.c`, `c11.c`, `c12.c`), then
`libc.a` from text 5b46. v7's `oddreg` (PDP-11 register pairs) and
`psoct` (octal numbers) are gone; new are a SIGFPE handler, the `%c%d.`
number printer and its `*`/`#` size test, a tree printer, the FCON→SFCON
test for the MUTOS floating format, a "register usable as a base"
(SI/DI/BP) test and `werror`.

The data structures are v7's: `struct tnode` (op, type, degree, tr1,
tr2), `opdope[]` and `opntab[]` (122 names; new ops 89 `*(x+c)`, 107/108
char<->long, 117..120 unsigned `/`, `/=`, `%`, `%=`, 121 char->int, and
122/123, a floating `*(x+c)` and indirection that `cexpr()` uses only
to look up a table), `instab`, `branchtab`, `maprel`/`notrel`, `isn` =
10000, `nreg` = 3. The register names are `ax dx si di cx bx bp sp`,
numbered 0..7 and handed out downwards (`reg1 = reg-1`): a long is in
DI:SI or DX:AX, a char forced into DX or AX (`reg` at most 1), an auto
variable becomes OFFS on register 6, BP.

Compared with v7 so far: `reorder()`, `sreorder()` - the hoist order
rounds 10..13 probed -, `sdelay()`, `ncopy()`, `max()`, `islong()`,
`isconstant()`, `hardlongs()`, `notcompat()` and `error()` do what v7's
do. With small, local changes: `match()` (op 89 counts as an indirection
for a table's `*`), `degree()` (`&x` is -2 only for an extern or static
`x`; an auto's address has degree 1 - round 3's "its `lea` counts as
computed"), `dcalc()` (constants 0, 1, -128..127, others: 4, 5, 7, 8;
`*p` addressable only through an SI/DI/BP register variable, else 24;
`&extern` like a constant; a strict `<` against the free registers),
`delay()` (nothing is delayed in a floating expression), `acommute()`
(no PDP-11 rule for `^`; `& -1` dropped like `* 1`; an `&` with a
constant up to 0377 makes its char operands op 121) and `optim()` (the
unsigned ops, ITOL dropped from a shift count). Rewritten for the 8086
and the floating-point runtime: `rcexpr()`, `cexpr()`, `pname()`,
`comarg()`, `doinit()`, `cbranch()`, `pswitch()`, `strasg()`. The rest
(`unoptim()`, `distrib()`, `lvfield()`, `const()`, `lconst()`,
`insert()`, `getree()`, ...) is placed but not compared yet.

### The code tables: v7's, in v7's encoding

`rcexpr()` compares its table argument with `cctab` and `efftab` and
hands `regtab` to RFORCE, `comarg()` pushes through `sptab`: data offsets
3e66, 3c9e, 334a and 40ca. Each is v7's `struct table` list (op, rows),
each row v7's `struct optab` (degree and type of each operand, template
pointer), the classes and type flags exactly `v7/cc/cvopt.c`'s numbers
(z 4, 1 5, c 8, r 9, a 16, e 20, n 63, `*` +0100; w 1, b 3, f 4, d 5,
l 8, u 9). The templates are `cvopt` output too: no leading tab, no
newline after `F`/`S`/`H`/`T`/`X`, 0200 for an inner tab, the macro
letters v7's (`A`/`B` addresses, `C`/`D` type suffixes, `G`/`K`/`H`
subtrees with `'A'`+flags, `I`/`J` registers, `M` instructions, `#`/`"`
offsets, `T`, `V`, `X`, `Z`, `L`, `P`). `tests/realbin/c1tables.py`
inverts that and gives back a `table.s` in v7's notation -
`tests/realbin/c1_tables.txt`: 496 rows, 292 templates, line for line
v7's table with Intel operand order, `(R*)` for a base register and calls
into the floating-point runtime. Two examples, both rules the probe
rounds needed a correction to find:

```
/ op 43 / (and % - the same list)       / op 40 + (and - - the same list)
%nw,nw                                  %nw,ew
	SS                                      F
	F                                       S1
	mov	ax,R                            I	R,R1
	T
	pop	cx
	I	cx
```

- every computed divisor pushed first (round 12), and `%n,e`: the left
operand into DI, the right one into SI (round 12).
`check_real.py coverage` hooks `match()`'s return over every golden
run: 12,926 matches use 160 of the 496 rows (`regtab` 99 of 345,
`efftab` 29 of 65, `cctab` 28 of 79, `sptab` 4 of 7). The other 336 rows
are code shapes no golden has shown, fixed all the same.

`cexpr()`'s interpreter (a hashed switch on the character, `c % 7`) has
v7's letters but `-` and `)` (PDP-11 auto-decrement and increment), and
these additions: `<U>` pushes the compile-time model of the
floating-point stack (counter at data 431a, "Floating point stack
overflow; simplify expression" past 7), `<Q>` pops it ("Floating point
stack underflow"), `<R>` pops it ("floating point stack underflow") and
prints `p` only while a nesting counter `rcexpr()` keeps (data 000a) is
0 - so a compound assignment's store, which uses `<Q>`, reports in upper
case and an assignment's, `<R>`, in lower case, as round 13 found -; a
literal `push`/`pop` in a template counts `nstack`; `R*` moves the
register to BX unless it is SI or DI; `F!` forces the register and `Fd`
puts a long result into DX:AX. Before matching, a floating STAR or op
89 is looked up as op 123 or 122.

### Options and traces

`main()` reads options after the three file names: `s<n>` sets the frame
size from which a function calls `chkstk` (data 04a6, default 100 - the
`(98,100]` threshold round 8 settled), and `a`, `b`, `c`, `d`, `r`
switch on debug traces written into the `.s`: `b` each expression tree
as read, `a` the same tree after `optim()`, `r` each `rcexpr()` call
with its tree, `c` each `cexpr()` call, the op and type after every line
it emits and the register it returns, `d` each `delay()` call. Run under
the emulator they show the real compiler's intermediate states - the
basis of the port's verification (`docs/C1_PORT_PLAN.md`, section 2).

### The real `as` and `insw`/`outsw`

`tests/mutos_as/kernel_opt/mch_insw_outsw.s` is `mch.s` with its two
`.byte /6d` and `.byte /6f` replaced by `insw` and `outsw`; its
`.o.golden` is the real `mch.o`, assembled from the `.byte`s. The real
`as` does not know those 80186 mnemonics: it assembles each, without a
message, as an undefined external word - two more text bytes and two
more symbols (`insw`, `outsw`) than the golden, exactly what `mutos_as`
did before its dispatch entries were added. So the encodings 0x6D/0x6F
are confirmed (by the real kernel's bytes and Intel's tables), but not
that the real assembler accepts the mnemonics; `mutos_as` accepting
them is an extension, like its other 80186 opcodes. `check_real.py`
lists the file apart.

### Round 14, through the binary

The round-14 goldens (commit `148efa3`) and `round14.log` (`71f0a07`)
came back while the binaries were being read. The emulated `c1` gives
all four `.s` and both message lists (`p56_fltinf9`, `p57_fltstk8`,
eighteen each) exactly. `mutos_c1` does not yet (`make test`: `p54`,
`p55`, `p57` refused, `p56` mismatched); they are left to the port,
which has to produce them like every other golden.

## The port of `c1` (2026-10-13)

`docs/C1_PORT_PLAN.md`'s five steps, in one session; `mutos_c1` is now
`src/mutos_cc/c1port/` and `c1_gen.c` is gone. The plan's section 8 is
the summary; this is how the steps went and what the binary held.

### Steps A to C: from the disassembly, checked by the traces

Every function was decompiled from `tests/realbin/c1dis.py`'s listing
with v7's source beside it, the binary winning where they differ (each
such place commented with its text offset). One host `struct tnode`
keeps the original's overlays - degree, then at byte offsets 4..15 the
value or class and register number, offset, name, an 8-byte floating
value, a bit-field mask, a long's two words - so that the code reads
like the original's while the tree pointers are host pointers. The
tables come from `c1tables.py --c`, never typed. The switch statements
of the binary are hashed (`op % N` into a jump table) or jump tables;
their cases were read off the tables, not guessed.

The four depths did what section 2 of the plan said: the `b` trees
checked the reader (depth 1, 132/132), the `a` trees the optimizer
(depth 2, 132/132), the `r`/`c`/`d` traces put each code-generation
difference at the function that made it (depth 3), and the `.s` with its
messages and exit status closed it (depth 4: 131/132, `p19_open3`
refused where it must be). What the binary does that v7 does not, and
the port keeps:

- `itod`'s store loop writes AX, which still holds BL, into the two
  words below the double's top: bytes 1 and 3 are the low mantissa
  byte, not 0 - so an int constant with more than eight significant
  bits converted to floating point is a double, not a float, constant.
- `longrel()`, when the table fails and it tests the register pair word
  by word, stores its new label in the variable holding the saved
  `xzero` (text 2f83): `xzero` comes back as a label number, which a
  later `xlongrel()` would use as an index into `lrtab[]` - refused when
  it would.
- `lconst()` shifts a long `>>` by the count's high word and `<<` by its
  low word, and with that word 0 both jump into the middle of an
  instruction - refused (the fuzzer found the original crashing or
  looping there).
- `cexpr()`'s `T` for an ITOC tests the type of the node at address 0,
  which is `crt0`'s data word 4 - never INT, so never a `cbw`.
- `printf`'s `%c` of 0 writes nothing (settled with a program built by
  the real toolchain under the emulator); a zero is printed unsigned.
- A STRASG's degree is never set by `getree()`: the original's trace
  shows whatever word the block held before. The only place
  uninitialized memory shows (plan question 5.3), and only in a trace.
- `fpeintr()` is installed once; v7's kernel resets a caught signal, so
  a second floating overflow kills `c1` and leaves its `.s` cut to whole
  512-byte buffers. `mutosemu.py` now delivers signals (`kill()` of the
  process itself, as `libc.a` raises SIGFPE), which settled it.

### Step D: breadth

`tests/realbin/c1fuzz.py` runs random programs through `mutos_cpp`, the
emulated real `c0` and then both `c1`s, at depth 4 by default. Its
findings, all fixed in the port: the `itod` bytes above; the `lconst()`
shifts and `xlongrel()`'s stale index (refusals); and last `libc.a`'s
`aldiv`, through which `lconst()` folds a long `/`. `0x7fffffffL /
0177777` folds to -65535 in the original: `aldiv`'s short path is
taken when the divisor's high word equals the low word's sign extension
- with the sign word -1 negated to 1 first, so a divisor of 0x18000 to
0x1ffff is divided by its low word alone and a negative one-word divisor
goes the long way -, and the long way does sixteen shift-and-subtract
steps (enough when the divisor has a high word) and clears the
quotient's high word. `alrem` has the same long way. Both are
re-enacted on 16-bit words in `c1lib.c`; `c1fuzz.py --gen lconst`
(long constants at the edges of those paths) found the old port
different on 232 of 300 programs, the new one on none.

The port's refusals are checked too: what it wrote before refusing must
be what the original wrote, and where the original does not finish
(jumped into data, looping), the port must have refused - every such
case so far was a `lconst()` shift. One run showed that a jump into an
instruction can also change what the original still holds in its stdio
buffer (its first byte became a NUL, and the run ended in "Arg count"),
so after that refusal only what the original had written out is
compared.

The corpus at the end (2026-10-14, the final build, depth 4): 6,000
programs of `c1fuzz.py`'s own generator (seeds 1 to 6), 1,500 of
`fuzz_c.py`'s, 1,000 of long constants, 500 more through an ASan/UBSan
build - no difference; 694 + 59 refusals, each checked against what the
original wrote, and 16 programs on which the original does not finish
(all refused by the port). The ASan/UBSan build is clean on every golden
at every depth as well.

### The originals' bugs, collected

Everything above that is a bug of the original - in `c1`, in the
`libc.a` routines it and `as` link, and the earlier findings about `as`
and `c0` - is listed in `docs/MUTOS_ORIGINAL_BUGS.md` (2026-10-14, a rule
since: `CLAUDE.md`'s Workflow Guideline 9), each with a minimal trigger
where one is known. Writing the triggers made two of them sharper than the
fuzzer had: `lconst()`'s right shift takes the count's high word, so the
original `c1` cannot fold `L >> n` for any constant count (`0x12345678L
>> 4` stops it at a wild address, `100000L >> 1` ends in "Missing temp
file"), and `itod`'s stray bytes change values, not only constant sizes
(`double d = 1001;` is `.double 1.00100001525902189e+03`).

### Step E: the switch

`mutos_c1` is built from `c1port/` by `src/mutos_cc/Makefile`;
`c1_gen.c`, `c1_gen.h`, `c1_main.c`, `c1_stream.c`, `c1_stream.h` and
`c1port/Makefile` are deleted. `invalid_goldens.txt` gives `p19_open3`
228 lines (the port writes the statement's valid start - the product,
the unsigned loaded into AX - and refuses at the register name, where
`c1_gen.c` refused at the statement); `c1_errors.txt` has `round14.log`'s
messages for `p56_fltinf9` and `p57_fltstk8`. `run_goldens.sh` got a
category 10 - `mutos_c1` on the `.1`/`.2` goldens of a file `mutos_c0`
refuses: all eight remaining kernel files byte-exact.

`fuzz_c.py`'s default oracle is the emulated original: `mutos_c0`'s and
`mutos_c1`'s output must be the real `c0`'s and `c1`'s byte for byte,
and a program wrong or unexecutable exactly as the real compiler's
output is counted apart, not as a failure. Its first runs showed
`x86sim.py` stopping on 7 of 40 programs: the port branches on the flags
of `add`, `sar`, `test` (the real compiler's code), where `c1_gen.c`
always compared first. `x86sim.py` now models ZF, SF, OF and CF as the
8086 sets them (OF unknown after a shift by more than one, all flags
after `imul`/`idiv`/a call), with the same result as before on every
golden; the next 200 programs were all correct.

The larger runs then showed what the oracle is for. In 14 of 4,000
programs `mutos_c0`'s `.1` differed from the real `c0`'s: it folded `!`,
`&&` and `||` of constants, which v7's `fold()` (c01.c) does not - the
real `c0` writes `CON 1 / EXCLA` for `!(12 >> 3)` and `CON 4 / CON 0 /
LOGOR` for `4 || 0`, leaving them to `c1`'s `optim()`. Semantically the
same, so the semantic check alone had never noticed. Fixed; the next
5,000 programs had no `c0` difference.

And about 0.9% of the programs come out wrong - identically - from the
real compiler and from `mutos_c1`: the original's code tables keep
PDP-11 assumptions. On the PDP-11 `com` and `mov` set the condition
codes; on the 8086 `not` and `mov` set no flags. So `s = (~a[k][j] ||
n++);` is `... add di,si / mov di,(di) / not di / bne L`, the branch
taken on the flags of the `add` (`n++` lost), and `y = (!y >= 0);`
branches after `mov di,*1.` on the flags of the `cmp _y,*0` before it;
after an `imul` (flags undefined) the checker cannot even tell. Also
`(u = 4, x) * 0` drops the assignment (`acommute()` returns the 0 of
`e * 0` or `e & 0` without looking for side effects - v7's own code,
c12.c). `fuzz_c.py` counts such a program apart ("as the
real compiler"), since `mutos_c1` must reproduce it; `x86sim.py` finds
it because `not` and `mov` leave its flags alone, as on the 8086.

## The port of `c0` (2026-10-14, 2026-10-18)

`STATUS.md`'s "Next up" item 2 - `mutos_c0` refused eight of the nine
`11_kernel` files - by the method of the `c1` port (`docs/C1_PORT_PLAN.md`,
section 7). The port was written in one session (2026-10-14) and left
uncommitted, its plan already saying "done"; the next session (2026-10-18)
built it in, checked it, corrected one premise and switched to it.
`docs/C0_PORT_PLAN.md` is the summary; this is how it went and what the
binary held.

### `c0` is v7's `c0`

The stripped binary (`mutos_bin/c0.golden.base64.txt`, 0411, 23,168 bytes
of text) is `crt0`, then 73 functions in v7's source order - `c00.c` to
`c04.c`, v7's 72 and `werror()`, which nothing calls - then `libc.a` from
text 4a1e (`tests/realbin/c0map.txt`, `c0dis.py`). 60 functions do what
v7's do; the deltas are local: `build()`'s char rules (a relational with a
small constant against a char makes the constant a char; `+ - * / %`
convert a char operand with ITOC first; no `t==CHAR -> INT`), the
five-bit conversion codes of a 5x5 `cvtab[]` and two pseudo conversions
(char to floating and back, resolved by `convert()`), `lintyp()`'s five
classes, the function header (`PROG EVEN RLABEL`, `SETREG` first,
`regvar` 4, `STAUTO` -4, `RETRN` with the function's type), `goodreg()`
writing a `SETREG` per register and giving none to a char, and
`getype()`'s rule that a parameter list belongs only right after the
declarator's name. The data are v7's with `HSHSIZ` 500 and `opdope[]`'s
EXCLA taking LWORD.

### A memory image, not host structures

The `c1` port keeps host structures with the original's field offsets.
`c0` could not be ported that way: it lays its structures over one
another (a `struct hshtab` built for a cast is a tree node with op
ETYPE, the tree node `funcblk` is initialized as a symbol, a parameter
list is linked through the symbols' `hoffset`), and in its error paths it
reads fields of a null tree. So the port runs on an image of the
original's 64K data segment: the binary's initialized data at address 0
(`c0data.c`, generated by `c0tables.py --c`), every global at its
address, the heap from the original's break in the original's block
sizes, 16-bit pointers, and the locals whose address the original passes
on in a stack area at the top. What the image does not model -
`libc.a`'s stdio state - is refused when touched.

### Checked by plain comparison

`c0` has no traces and needs none: its output is two files, its messages
and its exit status. `c0cmp.py` compares them with the emulated
original's on every `.i` golden (137, all identical, also with an
ASan/UBSan build), `c0fuzz.py` on random programs - its own
declaration-heavy generator, `c1fuzz.py`'s and `fuzz_c.py`'s, each
optionally mutated at the token level for the error paths. A refusal
must have written a prefix of the original's output, and where the
original does not finish, the port must have refused. `mutosemu.py` got
an `eof_limit` for the second case: the original reads past the end of
its input for ever in three places (C0-02), one system call per read.

### The emulator was an 80386

The first port refused a constant shifted by a count of 32 or more, with
the reason "the 8086 and the V30 differ". They do not. The V20/V30
manual's page for every shift and rotate by CL says "temp <- CL, while
temp != 0": the V30 shifts by all of CL, as the 8086 does (the AP-186 note: the
80186 ANDs the count with 1FH, "the 8086 does not do this"). It was the
emulator that differed: Unicorn executes x86 code as an 80386, which
masks. So the original `c0` folded `1 << 40` to 256 under the emulator
and folds it to 0 on the hardware, and the port's refusal rested on the
oracle's error, not the hardware's behaviour.

`mutosemu.py` now does the 8086's shift: every D2/D3 opcode byte in the
text gets a code hook (one inside another instruction never fires),
except a shift right after `mov cx,<n>`/`mov cl,<n>` with n < 32 that no
relative jump or call targets and no word of the text or data equals
(a jump table) - which drops the hooks that fired some 20,000 times per
kernel file to about 3,300 calls. The hook does nothing below CL = 32;
from there it does the shift bit by bit for a register operand (and
stops the run, not modelled, for a memory operand). Checked against
closed forms for all seven operations, both widths and 3,000 random
cases, and by a run in which the instruction must not execute twice
(`rol` by 33); `check_real.py goldens` gives the same results as
before, and so do `c1trees.py --depth 4` and a 300-program `c1fuzz.py`
run. The port now folds as the 8086 does (`shl16()`, `shr16()`,
`sar16()` in `c01.c`, as `c1port/c1.h` already had), and the 56 programs
of the first 2,000 that it had refused are identical.

The same reading found a second difference. The 8086 (AP-186: "whenever
the absolute value of the quotient is greater than 7FFFH") and the V30
(its manual: the minimum negative quotient is -32,767) raise a divide
error for a quotient of -32768; the 80186 and the 80386 do not. So
`fold()` of `-32768 / 1` (and `% 1`) faults on the hardware and gave
-32768 under the emulator. `mutosemu.py` hooks the `idiv`s too (all but
`cwd` / `mov cx,<n>` / `idiv cx` with |n| >= 2, the hash's) and stops
the run with a divide error for that quotient; both ports refuse there
(C0-01, `c1port/c1.h`'s `div16()`/`rem16()`).

### The originals' bugs

Every refusal and every quirk the port keeps on purpose is a bug of the
original, now in `docs/MUTOS_ORIGINAL_BUGS.md` with a minimal trigger
each (C0-02 .. C0-12; C0-01 updated). Two were sharper than expected:
63 `char`/`int` pairs and a `char` in one structure (127 members, 63
alignment holes, which `decl1()` adds to the member list without counting
them) overrun `strdec()`'s `mems[]` and its frame - the original prints
"Too many structure members", then " redeclared", "Illegal storage
class" and `main()`'s "Arg count", and exits with status 1; and whether a
redefined enum tag is reported depends on the storage class of its first
declaration, because the enum's size is whatever the register holding
`elsize` kept from the caller.

### The switch

`src/mutos_cc/Makefile` builds `mutos_c0` from `c0port/`; `c0_lex.c`,
`c0_diag.c`, `c0_outcode.c`, `c0_sym.c`, `c0_parser.c`, `c0_main.c`, their
headers and `mutos_cc.h` - 8,189 lines - are deleted (the port: 4,954,
the generated `c0data.c` included). `run_goldens.sh` reports a
`mutos_c0` refusal (exit status 2) as category 3 and any other failure as
a mismatch, since the real `c0` wrote every golden; categories 3 and 10
are empty, `make test` has 121 files byte-exact at all four stages, all
nine kernel files among them. `make check-realbin` adds `c0data.c`'s
check, `c0cmp.py goldens` and two `c0fuzz.py` runs. `fuzz_c.py` accepts
the port's refusals and checks that it rejects only what the real `c0`
rejects. `dump_temp.py` already had a shape for every opcode the original
can write (all 264 golden files decode).

The final build against the final emulator: 5,800 `c0fuzz.py` programs
(all five generator settings) with 5,723 identical, 22 refusals and 55
runs of the original that do not finish - every refusal checked, no
difference -, 1,000 through an ASan/UBSan build, and 400 in `make
check-realbin`; `make fuzz` 500 programs, none wrong or bad. Before the
division hook the same campaign had run on 11,800 programs, also without
a difference; `c1fuzz.py --gen lconst` afterwards met the new divide
error once (its real `c0` folding `-32768 / 1`).

## The other originals as oracles (2026-10-20)

Planning the `mutos_cc` driver - where `crt0.o` and `libc.a` come from -
meant asking the original `cc` and `ld` first, and the answers changed
the plan (`docs/TOOLCHAIN_PLAN.md`). All of it was run under
`tests/realbin/mutosemu.py`; `cc` with a scratch harness (not yet in the
tree, step 0 of the plan) that fakes `fork`/`exece`/`wait`: at `fork`
the emulator snapshots the process and returns 0 (the child), at the
child's `exece` it records the path and `argv` and resumes the parent as
if the child had exited 0. Every pass `cc` would run is so listed with
its arguments; none actually runs.

### The original `cc` (0407, text 8,026 bytes)

It is v7's `cc` changed, not v7's `cc` (`v7/cc/cc.c`):

- **The link step** is always `/bin/ld -X <startup> [-o out] <files and
  -l options in command-line order> [-lf] -lc`. `cc` names no library
  by path - `-lc` is resolved by `ld`. Startup: `/lib/crt0.o`; `-p`
  `/lib/mcrt0.o`; `-2` `/lib/crt2.o`; `-20` `/lib/crt20.o` and `-l2`
  in place of `-lc`.
- **`-f`** keeps `crt0.o`, runs `/lib/c0f`, `/lib/c1f` and `/bin/asf`
  instead of `c0`, `c1`, `as`, and adds `-lf` before `-lc` (v7:
  `fcrt0.o`, `-lfpsim`). None of these files is in `mutos_bin/`, nor are
  `mcrt0.o`, `crt2.o`, `crt20.o`, `libf.a`, `lib2.a`.
- **`as` is called as `as -gC -o <name>.o <temp>`** (v7: `as -u`).
- `cpp <file> <temp>` without `-P` for a compile (so the real `cpp` writes
  `# N "file"` lines, see below); `cc -P` runs `cpp <file> <name>.i -P`
  and stops, also with `-S` in either order - the `-P`/`-S` question
  `CLAUDE.md` kept open has the original's answer. `cc -E` runs `cpp
  <file> - -E`. `-O` runs `/lib/c2 <temp5> <temp3>` between `c1` and
  `as`. Temporary files: `/tmp/ctm<n><pid>` from `mktemp`.
- Unknown options (`-i`, `-n`, `-s`, `-x`, `-d`, `-v`, ...) go to `ld` in
  their place, as in v7.
- **`-o` with a `.c` or `.o` name** is not refused (v7: "Would overwrite",
  exit status 8): the name is also taken as an input file, and the output
  is the name without suffix and directory - `cc -o hello.c x.o` compiles
  `hello.c` and links `ld -X /lib/crt0.o -o hello hello.o x.o -lc`; `cc
  -o dir/p.o x.o` links `-o p dir/p.o x.o`. Whether that is meant is open
  (`docs/MUTOS_ORIGINAL_BUGS.md`, "Suspect").
- Its data has messages v7's has not ("Executing cpp...", "Assembler %s
  ...", "Loader...", "Can't find %s", "Try again", "Fatal error in %s");
  what switches them on is to be read in the binary.

### The original `ld`

- **`-lc` is `/lib/libc.a`, then `/usr/lib/libc.a`** (v7's
  `/usr/lib/libxxx` name with `filname+4` tried first), never the
  current directory - checked with a real `libc.a` at all three places.
  `mutos_ld` looks in `./` first and never in `/lib`.
- **`-r` and `-d` are refused**: `ld: -r and -d flags not supported`,
  exit status 4. `mutos_ld` implements both (kept as an extension, see
  the plan).
- `libc.a`'s `__.SYMDEF` carries 1989-01-31 20:41:17 UTC (602282477). A
  `libc.a` decoded from its base64 text has a new modification time, so
  `mutos_ld` warns `out of date (warning)` on every link and scans the
  archive linearly; on 14 compiled goldens linked both ways the output
  was the same, only the warning differs. The planned `mutos_root/` sets
  the decoded file's time to the table's.

### The original `as`

- **Options**: it reads them letter by letter (`-gCe` is g, C, e). It
  knows `-o`, `-e` (only external symbols in the symbol table), `-g`,
  `-C` and `-l` (no visible effect on the probes); every other letter
  gives `Unknown option X ignored.` - the manual's `-L` (known since
  2026-09-28), and also its `-W`, `-D` and `-T`. `mutos_as` rejects
  `-gC` with its usage message, which makes the driver's first `as`
  call fail.
- **Its mnemonic table** (read from the data segment) has the 8086 set in
  this project's spelling and the whole 8087 set (`fadds`, `fsqrt`,
  `f16add`, ...), but not `esc`/`escb`, `lods`/`stos` (it has
  `lodb`/`lodw`/`stob`/`stow`), `scas`/`scasb` (`scab`/`scaw`), `movsw`,
  `inb`/`outb` (`in`/`inw`, `out`/`outw`) or `bloss`, and no 80186
  mnemonic. An identifier it does not know at the start of a statement
  is an expression: `pusha`, `enter`, `insw` each assemble, without a
  message, to a word referring to an undefined external of that name
  (AS-02, now for any such word).
- It has "insert" files (`cannot open insert file %s`) and a yacc parser
  (`yacc stack overflow`).

### The original `c2`

It runs under the emulator (`mutosemu.py c2 in.s out.s`), and on the
nine `tests/mutos_cc/11_kernel` `.s` goldens - the real `c1`'s output for
those kernel files - it gives `tests/mutos_as/kernel_opt/`'s `delay.s`,
`prim.s`, `mem.s`, `pipe.s`, `nami.s`, `fio.s`, `v24.s`, `tty.s` and
`amx.s` byte for byte: the kernel's optimized sources are its output,
which makes nine real-hardware goldens for a port. Its counters ("%d
mov/push converted to push", "%d ors -> tests", "%d long branches to
short", ...) are 8086 transformations; there is nothing for the 80186 or
the V30.

### The original `cpp`

`@(#)cpp.c 1.6 12/12/86` with `yylex.c 1.3`, Reiser's `cpp` (the
functions of `v7/cpp/cpp.c`, `yylex.c` and `cpy.y`, one more:
`savestr`), the only original with its symbol table (267 symbols).
Without `-P` it writes `# 1 "e.c"` / `# 1 "./inc.h"` / `# 2 "e.c"`, and
the real `c0` uses them: for a file with an `#include` before an error,
the original pipeline says `e.c:3: y undefined; func. main`, `mutos_cpp`
and `mutos_c0` say `4: y undefined; func. main` - a wrong line and no
file name. Its default include directory is `/usr/include`.
