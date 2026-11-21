# Building the MUTOS 1700 kernel with `-1`, for the NEC V30

This guide builds the MUTOS 1700 kernel with the cross toolchain and its
original, unchanged `conf/Makefile`, as [`KERNEL_BUILD.md`](KERNEL_BUILD.md)
does, but with its C sources compiled with `-1`: the 80186/V30 code of the
three rules of `docs/RULES.md` ("The V30 rules") - a push of a constant
(V30-PUSH), a shift of a register by a constant from 3 to 15 (V30-SHIFT), a
product by a constant (V30-IMUL). It is the open part of step V4 of
`docs/TOOLCHAIN_PLAN.md`: the kernel built with `-1` and booted on the
owner's A7100 with its NEC V30.

**Such a kernel runs on the NEC V30 or an 80186 only.** On an 8086 the
first 80186/V30 instruction is a different instruction. Boot it from a test
medium and keep the original kernel bootable (step 7).

What it rests on: each form the rules write ran on the owner's V30 on
2026-11-21 - the probes and programs of `tests/rules/v30kit.py`, every
value and every defined flag as predicted (`STATUS.md`, "Step V4"). Step 5
below checks that the kernel's code has no other form.

## What changes, and what does not

- **The 58 sources the makefile compiles with `$(CFLAGS)`** get `-1`:
  their objects have 80186/V30 instructions where a rule applies, and are
  otherwise the original compiler's code.
- **`c.c`, `symtab.c` and `mch`** (`mch.c` through `cpp`, `em` and `as`)
  are compiled without `$(CFLAGS)` and stay 8086 code; their objects are the
  original's. So does **`arith.a`**, whose members are hand-written 8086
  code.
- **One change at a time**: the build runs in the original mode
  (`MUTOS_MODE` unset) with the tree's own `arith.a`, so the kernel differs
  from the original only by `-1`. The corrected copy of `arith.a` is a
  second boot of its own, once the first one works (step 8, optional); the
  corrected mode is the plan's step F5 (`KERNEL_BUILD.md`, step 7).

## What it needs

- Everything [`KERNEL_BUILD.md`](KERNEL_BUILD.md) needs: the host, the
  repository under `~/mutos1700_c_cross_compiler`, the kernel tree under
  `~/sys`.
- **The reference build** of `KERNEL_BUILD.md`, steps 1 to 6, in
  `~/sys_cross`: the original kernel byte for byte (`kcmp.py`: `same: 61`).
  This build is compared with it.
- **A test medium**: a second boot medium of MUTOS - a copy of the one the
  machine boots from - on which the new kernel replaces `/mutos`. The
  medium with the original kernel stays as it is.

## Step 1: the toolchain, up to date

```sh
cd ~/mutos1700_c_cross_compiler
make all
make test                # exit status 0
unset MUTOS_MODE MUTOS_RULELOG
M=~/mutos1700_c_cross_compiler/mutos_root/bin
```

`make test` also builds four of the kernel's sources with `-1` through
its makefile and checks `scripts/v30report.py` on the log
(`tests/rules/check_v30report.py`). `unset` leaves the original mode, no
rule log; `$M` holds for this terminal only.

## Step 2: a working copy

As in `KERNEL_BUILD.md`, step 3, beside the reference build:

```sh
cp -a ~/sys ~/sys_v30
cd ~/sys_v30
find . -name '*.o' -delete
rm -f conf/mutos
base64 -d ~/mutos1700_c_cross_compiler/tests/mutos1700_arith/arith.a.base64.txt |
    cmp - conf/arith.a && echo "arith.a the same"
```

The `cmp` shows that `conf/arith.a` is the tree's original archive (the
owner's, in the repository since 2026-11-09), not the corrected copy.

## Step 3: build with `-1`

```sh
cd ~/sys_v30/conf
$M/make CFLAGS="-c -O -K1024 -1" 2>&1 | tee ../build.log; echo "Exit: ${PIPESTATUS[0]}"
```

- `CFLAGS` on the command line takes precedence over the makefile's
  `CFLAGS = -c -O -K1024`; the makefile stays as it is. Never a plain `make`
  in `conf/` (that is the host's GNU make).
- Each `cc` with `$(CFLAGS)` is followed by the report of `as`, for
  example `as: -1: 49 80186/V30 instructions: push 36, shift 5, imul 8`, or
  `as: -1: no 80186/V30 instructions`.
- It ends with the `size` line, the text smaller than the original's
  50,496 bytes (by the plan's estimate up to 3.4%; the repository's 13
  kernel sources came out 3.7% smaller), data and bss the same (8288,
  44600), and `Exit: 0`.

## Step 4: check the build

```sh
grep -c '^+ cc' ../build.log                       # 62
grep -c '^as: -1:' ../build.log                    # 58
od -An -to2 -N16 mutos                             # the first word 000411 (ld -i)
python3 ~/mutos1700_c_cross_compiler/scripts/v30report.py ../build.log
python3 ~/mutos1700_c_cross_compiler/scripts/kcmp.py -t ~/sys_v30/conf ~/sys_cross/conf
```

- `v30report.py` sums the reports: `58 assemblies with -1 (of 62 cc
  commands in the log)` and the 80186/V30 instructions by kind (`--files`:
  by source). The original `c2`'s output of the kernel has 478 loads of a
  constant before a push, 208 shifts by CL of a constant from 3 to 15 and
  215 products by a constant - about what to expect, the pushes fewer (a
  register read again is not replaced).
- `kcmp.py -t` against the reference build: `symtab_M7100.o`, `c_M7100.o`
  and `mch_M7100.o` the same, the objects with 80186/V30 instructions
  `text/data differ`, those without the same, and the kernel `mutos`
  different. An object that differs although its report has no
  80186/V30 instruction is not expected: bring back its line.

## Step 5: every form the kernel has ran on the V30

```sh
python3 ~/mutos1700_c_cross_compiler/scripts/v30report.py ../build.log --forms
```

It compiles each of the 58 sources again with the command of the log, `-S`
for `-c`, in a scratch directory (the tree gets no file), and checks the
assembler text: as many 80186/V30 instructions by kind as the build's
report of that source counted (`as` counts them from the bytes), and each
one a form the kit ran on the V30 - `push` of `*n.` or `#...`, `sal`, `sar`,
`shr` of AX, BX, CX, DX, SI or DI by 3 to 15, `imul ax,` with `*n.` or
`#n`. Expected, with the total of step 4:

```
v30report: 58 of 58 sources with -1 compiled again with -S: N 80186/V30 instructions (push ..., shift ..., imul ...), each a probed form and as many as the build's report
```

Exit status 0. Any other line names the source and the instruction: then
the kernel does not go on the machine - it has a form the hardware has not
run; bring back the line.

## Step 6: onto the test medium

1. Make the test medium: a copy of the boot medium MUTOS runs from.
2. Copy `~/sys_v30/conf/mutos` onto it as `/mutos`, the way the kits
   came onto the machine (on Linux onto the v7-formatted CF card); keep the
   test medium's original kernel there as `/mutos.orig`.
3. Read `/mutos` back from the medium on Linux and compare it:
   `cmp` with `~/sys_v30/conf/mutos` must print nothing.

The new kernel is `/mutos` on the test medium, not a second name beside the
old one: programs that read the kernel's symbols from `/mutos` (`ps` and
the like) must find this kernel's.

## Step 7: boot and try it

Boot the A7100 from the test medium. (How the machine chooses its boot
medium is the owner's setup; this repository does not record it.) Then:

1. **The start**: the kernel's messages as with the original kernel, the
   login, `ls -l /`, `date`. A `panic:` line or a hang is the result to
   bring back, with the last message seen.
2. **The drivers compiled with `-1`**: the IDE interface (`v30ide`: the CF
   card - read and write files on it), the console, the V24 line and the
   IFSS interface if used, the floppy drive.
3. **Processes and files**: run the kit of step V4 again (`sh v30check.sh
   >log 2>&1` in its directory, `expect/` with its files): every value as
   before - it forks, executes, reads and writes for some minutes;
   compile a few programs with the machine's own `cc`.
4. **Optional, the time**: the same command under both kernels, e.g.
   `time ls -lR / >/dev/null`, twice each; the `sys` time is the kernel's.
5. **Back**: `sync`, shut down as usual, boot the original medium and
   check the test medium's file system with the usual check.

Nothing on the original medium changes; to go back, boot it.

## Step 8 (optional): the same kernel with the corrected `arith.a`

Only once the kernel of step 7 boots and works. The corrected copy of the
kernel's `arith.a` (step F3b, `src/libc_fixed/arith.a`; `make` builds it
from the owner's archive in `tests/mutos1700_arith/`) has the long helpers
`aldiv.o`, `ldiv.o`, `alrem.o` and `lrem.o` of the corrected `libc.a`, byte
for byte - the routines that ran on the owner's V30 on 2026-11-14 in the
trigger programs (`tests/libc_runtime/README.md`, "The check on the
hardware"); `almul.o`, `lmul.o` and `locking.o` are the original's.

**What it changes in this kernel: no result.** The original routines are
wrong only for a divisor from 98304 to 131071, a negative one or -2\*\*31
(L-04), and `alrem` for a divisor outside -32768 to 32767 (L-08;
`docs/MUTOS_ORIGINAL_BUGS.md`). The kernel's code - the compiler output of
all its objects, `tests/mutos_as/kernel_opt/*.s` - calls `ldiv` 14 times,
`lrem` 14 times, `aldiv` twice and `alrem` never, and divides only by the
constants 2, 7, 16, 60, 512, 1024, 2048 and 86400, by the number base 8 or
10 (`printn`, `prf`) and by unsigned words of the K5170 driver (`k5170`, 0
to 65535): none of them meets a bug, unless a K5170 word of 32768 met a
dividend of -2\*\*31 in `lrem` (`-2**31 % 32768`, the one remainder L-04
gets wrong below 2\*\*16). So this boot shows that the corrected
routines work in the kernel; the kernel computes what it computed before.
`c1`'s folding of long constants is the original's here (the original
mode); in the corrected mode it follows the copy - that kernel is the plan's
step F5.

`arith.a` is a prerequisite of `mutos` in the makefile, so the newer copy
makes `make` link again and compile nothing:

```sh
cd ~/sys_v30/conf
cp mutos mutos.v30                                 # the kernel of steps 3 to 7
cp ~/mutos1700_c_cross_compiler/src/libc_fixed/arith.a arith.a
$M/make CFLAGS="-c -O -K1024 -1" 2>&1 | tee ../build_fixarith.log; echo "Exit: ${PIPESTATUS[0]}"
grep -c '^+ cc' ../build_fixarith.log              # 0
cmp mutos mutos.v30                                # differs
```

- The log has the `ld` command and the `size` line, no `cc`.
- The `size` line: data and bss as in step 3 (8288, 44600); the text
  as in step 3 or 64 bytes more. The kernel links `aldiv.o`, `ldiv.o` and
  `lrem.o` (38 bytes longer together; `alrem.o` is not linked, nothing calls
  it), and `ld -i` rounds the text up to a multiple of 64 bytes - the
  original 8086 kernel's text goes from 50,496 to 50,560 bytes with the copy.
- No `ld:arith.a(__.SYMDEF): out of date` warning: the copy has no table
  of contents, as the original has none.

Then steps 6 and 7 with this `mutos`: onto the test medium as `/mutos`
(keep the kernel of step 7 there as `/mutos.v30`), boot, try. What uses the
long helpers: every number the kernel prints (`printf`, through `printn`),
the memory message at the start (`main`), the IDE driver (`v30ide`), the
K5170 driver and its drives (`k5170`), reading `/dev/mem` and `/dev/kmem`
(`mem`, e.g. `ps`).

Back to the tree's own `arith.a` - and to the kernel of step 7 - with:

```sh
base64 -d ~/mutos1700_c_cross_compiler/tests/mutos1700_arith/arith.a.base64.txt >arith.a
$M/make CFLAGS="-c -O -K1024 -1"
cmp mutos mutos.v30 && echo "the kernel of step 7"
```

## What to bring back

- the two lines of `v30report.py`, the line of `v30report.py --forms`, the
  `size` line and the summary line of `kcmp.py -t`;
- whether the kernel boots, what was tried in step 7 and how it went (the
  log of `v30check.sh`), any message of the kernel;
- the times of step 7.4, if taken;
- with step 8: its `size` line, and whether that kernel boots and works.

## How this was verified

- `tests/rules/check_v30report.py` (in `make test`, since 2026-11-21): in a
  scratch kernel tree with the makefile and four real kernel sources
  (`main.c`, `sys.c`, `tty.c`, `v30ide.c`), `mutos_make CFLAGS="-c -O
  -K1024 -1"` - every `cc` with `-1` and followed by its report;
  `v30report.py`'s sum; `--forms`: each source as many instructions as its
  report, each a probed form, the tree unchanged; it fails on a changed
  report and a missing source.
- On 2026-11-15 seven of the kernel sources of the tree through the
  kernel's makefile (`v24`, `amx`, `v30ide`, `prim`, `pipe`, `nami`, `fio`):
  without `-1` the objects of `kernel_opt/`, with it 443 80186/V30
  instructions (`STATUS.md`, "Step V4").
- `v30kit.py --check`: every 80186/V30 instruction of the 13 kernel
  sources of the repository with `cc -O -S -1` is a probed form.
- Step 8 (2026-11-21), in a scratch kernel tree (`check_editors.py`'s)
  with the 61 objects of `tests/mutos_as/kernel_opt/` in their places and
  the owner's `arith.a`: `mutos_make CFLAGS="-c -O -K1024 -1"` linked
  without a `cc` (`50496+8288+44600`); with the copy in place of `arith.a`
  the commands of step 8 linked again without a `cc` and without a warning,
  `50560+8288+44600`, the kernel different; with the original back the
  kernel was the first one byte for byte. The divisors: every `call` of
  `ldiv`, `lrem`, `aldiv`, `alrem` in `kernel_opt/*.s` with the long pushed
  first before it (the divisor).
- **Not yet**: the build on the owner's whole tree, and the boot - that is
  this guide.
