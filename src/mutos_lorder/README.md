# mutos_lorder - MUTOS 1700 Cross Library Orderer

`mutos_lorder` is the original MUTOS 1700 `lorder`
(`mutos/mutos_bin/lorder.golden.base64.txt`), which is a shell script - the
one of Unix V7 - and so is not ported but run: the text of the script is the
specification and the implementation. Added on 2026-11-02
(`docs/TOOLCHAIN_PLAN.md`, step 8, which settles section 5's question
whether `lorder` needs a port).

## What it does

    lorder file ...

For the object files and archives named it prints pairs `a b` of object
names, one to a line: `a` uses a symbol that `b` defines. `lorder *.o | tsort`
is the order of the members of a library, each before the members that
define what it uses - the order `ld` needs to find them in one pass.

It runs `nm -g` on the files and reads its list: a line `x.o:` names an
object and is printed as the pair `x.o x.o` (so that `tsort` knows an object
that uses nothing and defines nothing), a symbol of type `T` or `D` is a
definition, every other symbol a use; the two lists are sorted and joined on
the symbol name. A single object file is given to `nm` twice (`lorder x.o`
becomes `lorder x.o x.o`, so that `nm` names it), and the pair is printed
twice.

## What differs from the original's text

Three things and nothing else:

- `nm` is `$MUTOS_ROOT/bin/nm`, the root's link to `mutos_nm`, not the first
  `nm` of the `PATH` (on a host that is the host's own `nm`); the script
  fails when there is none. The `PATH` puts `$MUTOS_ROOT/bin` first for the
  rest;
- `sed`, `sort` and `join` are the host's, run in the C locale (`LC_ALL=C`):
  byte order, as the original's `sort`;
- `nm`'s exit status is passed through a file, because a pipeline gives only
  the last command's: 125 (`mutos_nm` refused something it does not model,
  and said so) ends `lorder` with 125 before it prints anything. The
  original's `nm` ends with 0 for anything but an invalid option (1) and no
  memory (2), and `lorder` goes on regardless.

The script is `mutos_lorder.in`; the `Makefile` writes `mutos_lorder` with the
MUTOS root's compiled-in default (`src/h/mutos_root.h`) filled in. `$MUTOS_ROOT`
in the environment wins, as for every tool.

## How it is checked

- `tests/realbin/utilcmp.py cases lorder` (in `make check-realbin`): chains
  of objects, cycles, archives and `libc.a`, a single file, none, a missing
  file - run with the original `lorder` and `nm` under the 8086 emulator and
  with this script and `mutos_nm`: the output the same.
- `tests/mutos_binutils/check_tools.py` (in `make test`, no emulator needed):
  `lorder libc.a | tsort` gives every member of `libc.a` once.

## Source layout

- `mutos_lorder.in` - the script
- `Makefile` - `make` builds `mutos_lorder`
