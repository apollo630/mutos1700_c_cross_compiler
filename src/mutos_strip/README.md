# mutos_strip - MUTOS 1700 Cross Symbol Stripper

`mutos_strip` is a port of the original MUTOS 1700 `strip`
(`mutos/mutos_bin/strip.golden.base64.txt`): Unix V7's `strip.c` for the 8086.
Its three functions, `main()`, `strip()` and `copy()`, were read instruction
by instruction beside `v7/strip/strip.c` - the binary is the specification,
v7's source the skeleton - and the port does what the binary does, its
quirks included (`docs/MUTOS_ORIGINAL_BUGS.md`, ST-01 ..). Added on
2026-11-02 (`docs/TOOLCHAIN_PLAN.md`, step 8).

`tests/realbin/stripmap.txt` maps the three functions and the libc.a
functions linked in to their text offsets in the stripped binary and
describes the data; `tests/realbin/stripdis.py` disassembles one by name.
The comments in `strip.c` carry the offsets.

## What it does

    strip file ...

For every file named it reads the 16 bytes of an a.out header. If the magic
number is 0407, 0410, 0411 or 0405 and the file still has symbols (or its
"relocation stripped" bit is not set) it writes the header with `a_syms = 0`
and the bit set, followed by the text and the data, to a temporary file
(`/tmp/sXXXXX`, one for the whole run), and copies that back over the file
- `creat()` truncates it first. Messages go to standard output: `cannot open
x`, `x not in a.out format`, `x already stripped`, `x cannot recreate`, `x
unexpected eof` (the file is shorter than its header says), `x unexpected
write eof`. The exit status is 0, 1 if any file went wrong and 2 for a write
that failed (the run ends there, with the file cut) or a temporary file that
could not be made (`cannot create temp file`).

## The result of the reading

None of the three functions differs from v7's in what it does. What differs
is the representation and the libc.a underneath: the header `head` is a
global of 16 bytes, the sizes are longs (high word first), `mktemp()` takes
the 16-bit process number, standard output is libc.a's 512-byte block and
`main()` ends in `exit(status)`.

## Where the original is odd, and the port with it

- the header `head` is global: a short read (a file of less than 16 bytes,
  an empty one) leaves the bytes of the file before it. After a file that was
  stripped these are a_syms 0 with the flag set: a file that begins with a
  magic number and nothing more "is already stripped"; the first file finds
  zeros;
- hangup, interrupt and quit are ignored from the start;
- the temporary file's name is the process number's five digits (16 bits, as
  on the 8086) behind `s`, the first digit `a`, `b`, .. as long as the name
  exists, `/` at `z` - then `creat()` and `open()` fail;
- `/tmp/sXXXXX` is the MUTOS root's `tmp/` (`src/h/mutos_root.h`); the names
  on the command line are host paths;
- the temporary file is made once and removed at the end of the run: a
  run that ends by status 2 leaves the file it was copying back cut.

## What the port refuses

A situation the port does not model ends with exit status 125 and a last
message line ending in `refused`: a file name that is a directory (the
original reads its entries).

## How it is checked

- `tests/realbin/utilcmp.py cases strip` (in `make check-realbin`): every
  object and archive of the tree, headers with every magic number and flag,
  files that are cut short, several files, an unwritable temporary directory,
  a full disk (`/dev/full`), directories - run on the original under the
  8086 emulator and on the port: the messages, the exit status and the
  resulting files (bytes and modes) the same.
- `tests/realbin/utilfuzz.py strip`: differential fuzzing with random and
  damaged headers.
- `tests/mutos_binutils/check_tools.py` (in `make test`, no emulator needed).

## Source layout

- `strip.c` - `main()`, `strip()`, `copy()`, `mktemp()`, the refusals
- `Makefile` - `make` builds `mutos_strip`
