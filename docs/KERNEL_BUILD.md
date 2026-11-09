# Building the MUTOS 1700 kernel with the cross toolchain

The MUTOS 1700 kernel builds on Linux with the cross toolchain and its
original, unchanged `conf/Makefile`, and the result is the original kernel
byte for byte. On 2026-11-07 the project owner built the kernel of the
7100 configuration (`TYP = M7100`, `DRIVER = V30IDE V24 IFSS`) this way:
`mutos_make` ran 62 `cc` calls, `em` and `ld -i`; all 61 objects are the
real-hardware objects of `tests/mutos_as/kernel_opt/` (`scripts/kcmp.py`:
`same: 61`), and the kernel `mutos` has the MD5 sum of the kernel the
original build made on the hardware (`5f60bb499b26df4d4adb6c82dd928b2e`).

This document is the step-by-step guide; `STATUS.md` ("The MUTOS kernel,
built with the cross toolchain") has the verified state, and
`docs/devlog/originals_and_ports.md` ("The MUTOS kernel, cross-built") how
it was found.

## What it needs

- **Host**: a Linux with a C compiler, GNU make, git and Python 3 (on Ubuntu:
  `sudo apt install build-essential git python3`). Python 3 is for `make
  test` and `scripts/kcmp.py`; the build itself needs none. Checked on Ubuntu
  24.04, whose `/bin/sh` is dash.
- **The kernel tree** as it is on MUTOS under `/usr/sys`: `conf/` with
  `Makefile`, `c.c`, `symtab.c` and `arith.a`, and beside it `h/`, `io/`,
  `mdep/` (with `fakeawk`), `sys/` and `cfg/`. The makefile names everything
  relative to `conf/` (`../h`, `../io`, ...). Below it is `~/sys`. The tree is
  not in this repository; its makefile is (`tests/mutos_ed/kernel/conf.Makefile`,
  without the dependency lines that `make depend` appends).
- **`conf/arith.a`**, an archive of the original kernel tree that no source
  makes: `locking.o` (`_locking`, `_locked`, `_unlckf`, `_deadloc`,
  `_lockadd`, `_lockall`, `_lockfre`) and the long-arithmetic routines
  `aldiv.o`, `almul.o`, `alrem.o`, `ldiv.o`, `lmul.o`, `lrem.o`, without a
  table of contents (`__.SYMDEF`). The kernel's objects call `lmul`, `ldiv`,
  `lrem`, `almul`, `aldiv`, `_locking` (`sysx.o`), `_locked` (`sys2.o`) and
  `_unlckf` (`fio.o`), which none of them defines. The owner's `arith.a` is in
  the repository (`tests/mutos1700_arith/`, since 2026-11-09): its six
  long-arithmetic members are `libc.a`'s byte for byte.
- **The repository** under `~/mutos1700_c_cross_compiler`. Its tools have the
  absolute path of its MUTOS root `mutos_root/` compiled in; after moving the
  repository, rebuild it or set `MUTOS_ROOT` to the new `mutos_root/`.

The makefile stands in the 7100 configuration; `make` without a target
builds that kernel, and nothing in the makefile has to change.

## Step 1: build and test the toolchain

```sh
cd ~/mutos1700_c_cross_compiler
make all                 # every port and the MUTOS root mutos_root/
make test                # exit status 0; last line: check_editors: 212 checks, 0 failed
ls mutos_root/bin        # ar as cc ed em ld lorder make nm ranlib size strip tsort
```

This is the host's GNU make, in the repository - right there, and nowhere
else in this guide.

## Step 2: check the kernel tree

```sh
ls ~/sys                  # cfg conf h io mdep sys
ls ~/sys/conf/Makefile ~/sys/conf/arith.a ~/sys/conf/c.c ~/sys/conf/symtab.c ~/sys/mdep/fakeawk
grep -rl $'\r' --include='*.[ch]' --include=Makefile --include=fakeawk ~/sys
```

The `grep` must print nothing: a file it names has DOS line ends, which the
makefile, `cpp` and `em` would trip over.

## Step 3: a working copy without old build products

Build in a copy from which the old objects and the old kernel are removed.
`make` rebuilds only what is older than its sources; objects of an earlier
build on the hardware would count as up to date, and the cross compiler
would not run at all.

```sh
cp -a ~/sys ~/sys_cross
cd ~/sys_cross
find . -name '*.o' | wc -l      # objects of an earlier build
find . -name '*.o' -delete
rm -f conf/mutos
```

- `cp -a`, not `mv`: the original tree stays as it was, the reference for
  step 6.
- `cp -a` keeps the times. The dependency rules at the end of the makefile
  (`touch $@` when a header is newer than a source) then act as they did on
  the hardware.
- `arith.a` stays; it is not an `.o`.

## Step 4: check `arith.a`

```sh
M=~/mutos1700_c_cross_compiler/mutos_root/bin
$M/ar tv ~/sys_cross/conf/arith.a
$M/nm -g ~/sys_cross/conf/arith.a | grep ' T '
```

`T` lines are expected for `aldiv`, `almul`, `alrem`, `ldiv`, `lmul`,
`lrem` and `_locking`, `_locked`, `_unlckf` (with `_deadloc`, `_lockadd`,
`_lockall`, `_lockfre`). `$M` holds for this terminal only; repeat its line in
a new one. Whether it is the owner's archive of the repository:

```sh
base64 -d ~/mutos1700_c_cross_compiler/tests/mutos1700_arith/arith.a.base64.txt |
    cmp - ~/sys_cross/conf/arith.a && echo "arith.a the same"
```

## Step 5: build the kernel

```sh
cd ~/sys_cross/conf
$M/make 2>&1 | tee ../build.log; echo "Exit: ${PIPESTATUS[0]}"
```

- `$M/make` is `mutos_make`, the port of the original `make`. It runs each
  recipe through `/bin/sh` with `mutos_root/bin` first in the `PATH`, so `cc`,
  `em`, `ld` and `size` in the makefile are the MUTOS tools; `mv`, `rm`,
  `touch` and `sync` are the host's.
- **Never a plain `make` in `conf/`**: that is GNU make, and its recipes run
  the host's `gcc` and `ld`.
- The lines starting with `+` come from `set -x` in the makefile's `SETOPT`.
- `touch ../io/intr.c` and the like are the makefile's dependency rules at
  work (headers newer than the sources).

The run takes about a second. It ends:

```
size mutos;sync
50496+8288+44600 = 103384b = 0311730b
Exit: 0
```

Those are the numbers of the owner's tree. Another `arith.a` changes the
text size; an `arith.a` with a table of contents older than the archive adds
`ld:arith.a(__.SYMDEF): out of date (warning)`, which the original `ld` prints
as well.

## Step 6: check the result

```sh
grep -c '^+ cc' ../build.log     # 62
od -An -to2 -N16 mutos           # the first word 000411: separate I and D (ld -i)
python3 ~/mutos1700_c_cross_compiler/scripts/kcmp.py ~/sys_cross/conf \
    ~/mutos1700_c_cross_compiler/tests/mutos_as/kernel_opt
```

`scripts/kcmp.py` compares each of the 61 objects with its real-hardware
object in `tests/mutos_as/kernel_opt/` and prints `same` or the first
difference. The same means: the header but the symbol table's size, the text
and data, and the relocation, an external one by its symbol's name. The
symbol tables are compared apart, because the goldens were assembled without
`-C` and carry the local `L` symbols that `cc`'s `as -gC` leaves out. Exit
status 0: all the same. Expected: `same: 61`.

A difference such as `text/data differ (first at text 0x01a2)` means another
source than the golden's, or a fault of the toolchain - then the file,
`build.log` and the source are what to look at.

Against an original build, when the tree held one (step 3 found objects):

```sh
python3 ~/mutos1700_c_cross_compiler/scripts/kcmp.py -t ~/sys_cross/conf ~/sys/conf
cmp ~/sys_cross/conf/mutos ~/sys/conf/mutos && echo "kernel the same"
```

`-t` compares the same files in two trees, the kernel `mutos` included. The
same bytes can only be expected from a kernel built from these sources and
this `arith.a` in the 7100 configuration; a kernel taken from the A7100's
disk is compared with `cmp` the same way.

## Step 7 (since 2026-11-08): the corrected mode and the rule log

The corrections of `docs/TOOLCHAIN_PLAN.md`, section 7, are chosen by
`MUTOS_MODE=corrected` - the kernel's makefile passes no switch to its `c.c`,
`symtab.c` and `mch` rules or to `ld` - and the rule log
(`docs/RULES.md`) says which of the hooked bugs the kernel's code meets. In a
fresh copy (step 3):

```sh
cd ~/sys_cross/conf
rm -f /tmp/rules.log
MUTOS_MODE=corrected MUTOS_RULELOG=/tmp/rules.log $M/make 2>&1 | tee ../build.log
python3 ~/mutos1700_c_cross_compiler/scripts/rulelog_summary.py /tmp/rules.log
cmp mutos ~/mutossys/sys/conf/mutos && echo "kernel the same"
```

While no rule was active (step F0) the kernel was the original's in either
mode. Since step F1 the corrected mode corrects C1-03, C1-06, C1-02 and C1-01
(`docs/RULES.md`). In the repository's copy of `09_amx.c` the two
`(int)temp++` meet C1-03: `inc *-16.(bp)` becomes `add *-16.(bp),*1.` /
`adc *-18.(bp),*0`, so `amx.o` - and with it the kernel - is no longer the
original's, and `cmp` says so; `kcmp.py` (step 6) names the objects that
differ. The summary lists each rule met (the original's path) or applied
(corrected), with the source and line; the kernel's other sources are not
in the repository, and the summary shows what they meet. Without
`MUTOS_MODE` the kernel is the original's, as in step 6. The plan's step F5
accounts for each difference.

The kernel's long arithmetic (`l / m`, `l % m`, `l /= m` ...) is `arith.a`'s
`aldiv` ... `lrem`, which are `libc.a`'s routines with their bugs (L-04, L-08
of `docs/MUTOS_ORIGINAL_BUGS.md`). The makefile names `arith.a` by its path,
so the corrected mode does not replace it - `ld`'s rule LIBC-F concerns `-lX`
only. For a kernel with the corrected long helpers (step F3b), put the
corrected copy that `make` builds in the repository into the working copy in
place of the tree's:

```sh
cp ~/mutos1700_c_cross_compiler/src/libc_fixed/arith.a ~/sys_cross/conf/arith.a
```

It is made from the owner's `arith.a` (`tests/mutos1700_arith/`): the same
members in the same order with the same dates, `aldiv.o`, `ldiv.o`, `alrem.o`
and `lrem.o` corrected. For another tree's `arith.a`:
`python3 ~/mutos1700_c_cross_compiler/src/libc_fixed/fix_archive.py -o arith.a
~/sys/conf/arith.a` (it refuses if those members are not the routines it
corrects). The copy changes the linked kernel `mutos` (the four members
are longer), not the objects; the original's `arith.a` stays where the
original kernel is to be built.

## When it goes wrong

| Message | Cause | Remedy |
|---|---|---|
| `Make:  Don't know how to make arith.a.  Stop.` | `arith.a` is missing in `conf/` | take it from the original tree (the makefile's target `save` archives `conf/arith.a`) |
| `ld:arith.a: Premature EOF`, `*** Error code 4` | `arith.a` is empty: the targets `7100`, `7150`, `1834` only `touch` it | as above |
| `Undefined:` and names, `*** Error code 1` | `arith.a` or an object lacks them | see what defines the names in the original tree |
| `` `mutos' is up to date. `` at the first run | old objects or an old `mutos` left in the copy | step 3 |
| `cc: error: unrecognized command-line option '-K1024'` | the host's `gcc` ran: a plain `make`, or `MUTOS_ROOT` names a directory without `bin/` | `$M/make`; check or unset `MUTOS_ROOT` |
| `cc: cannot create temp` | the MUTOS root is missing, or has no `tmp/` | `make all` in the repository |
| a message ending in "refused" (exit status 2 or 125) | a port met something it does not model | the line, `build.log` and the source concerned |

Not needed, and best left alone:

- `make 7100`, `make 7150`, `make 1834`, `make depend` and the option
  targets (`AMX`, `noV24`, ...): they rewrite the makefile with `ed` (the old one
  kept as Makefile.bak). They work with the ports (`tests/mutos_ed/check_editors.py`),
  but the makefile is to stay as it is, and `make` alone builds the kernel.
- `make save`: writes the tree with `tar` to a floppy drive.
- `ranlib arith.a`: changes `arith.a` for nothing.
- Building in the original tree itself.

## What the makefile runs

| Recipe | Tools |
|---|---|
| 58 sources: `cc -c -O -K1024 -DM7100 -DASK -DIFSS -DV24 -DV30IDE x.c` | `cc` (`cpp`, `c0`, `c1` with `s1024`, `c2`, `as -gC`), then `mv` for the renamed objects (`intr_AC.o`, `cons_M7100.o`, ...) |
| `c.c`, `symtab.c`: `cc -c` with the same `-D` flags, without `-O` | `cc` |
| `mch.c`: `cc -P`, `em mch.i <fakeawk`, `mv mch.i mch.s`, `cc -c mch.s` | `cpp`, `em`, `as` |
| the kernel: `ld -i -o mutos symtab_M7100.o mch_M7100.o ... arith.a` | `ld` (0411, text and data apart) |
| `-size mutos;sync` | `size`; the host's `sync` |

The `-D` flags come from `SETOPT` and `CONFOPT`: the shell sets a variable
for each word of `TYP`, `HTYP`, `STYP`, `CONFIG` and `DRIVER`, `IFSS` and
`V24` set `ASP` and `ASK`, and `${X+-DX}` turns each one set into a flag.
`-K1024` makes `c1` call `chkstk` only for frames from 1024 bytes up.

## How this was verified

- On 2026-11-07, before the owner's run, in a kernel tree assembled from this
  repository (the makefile, `tests/mutos_cpp/h/` as `h/`, `mdep/mch.c`,
  `fakeawk`, the thirteen kernel sources of `tests/mutos_cc/11_kernel/` and
  `tests/mutos_cpp/c/`, small stand-ins for the other 48): `mutos_make` with
  the unchanged makefile ran 62 `cc` calls, `em`, `ld -i` and `size`; the
  thirteen real sources gave the objects of `kernel_opt/` (which settles that
  those goldens were compiled with exactly this configuration); the 61
  `kernel_opt/` objects linked through the makefile's `mutos` rule; a second
  `make` did nothing, a newer header rebuilt the sources depending on it.
- On 2026-11-07 on the owner's tree: the run of step 5 (62 `cc` calls, no
  message, exit status 0), `kcmp.py`: `same: 61`, and the kernel's MD5 sum
  equal to that of the original build's `conf/mutos`.
- Not checked: other configurations (7150, 1834, other drivers), whose
  objects have no goldens. A kernel with the same bytes as the original boots
  as the original does; this one was not started on its own.
