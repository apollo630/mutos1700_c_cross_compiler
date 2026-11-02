# mutos_size - MUTOS 1700 Cross Segment Sizes

`mutos_size` is a port of the original MUTOS 1700 `size`
(`mutos/mutos_bin/size.golden.base64.txt`): Unix V7's `size.c` for the 8086.
Its one function, `main()`, was read instruction by instruction beside
`v7/size/size.c` - the binary is the specification, v7's source the skeleton -
and the port does what the binary does, its quirks included
(`docs/MUTOS_ORIGINAL_BUGS.md`, SZ-01 ..). Added on 2026-11-02
(`docs/TOOLCHAIN_PLAN.md`, step 8).

`tests/realbin/sizemap.txt` maps `main()` and the libc.a functions linked in
to their text offsets in the stripped binary and describes the data;
`tests/realbin/sizedis.py` disassembles one by name. The comments in
`size.c` carry the offsets.

## What it does

    size [file ...]

For every file named (`a.out` if there is none) it reads the 16 bytes of an
a.out header and, if the magic number is 0407, 0410, 0411 or 0405, prints

    [name: ]text+data+bss = <sum>b = 0<sum in octal>b

with the three sizes as 16-bit unsigned numbers and their sum a long. The
name is printed when more than one file is named (v7 counts `a.out` in, when
it supplies it). A file that cannot be opened (`size: x not found`) or has
another magic number (`size: x not an object file`) is reported on standard
output - `size` has no standard error - and the run goes on.

## The result of the reading

`main()` does what v7's does. What differs is the representation and the
libc.a underneath: the header is a 16-byte buffer of bytes, the words are 16
bits wide, `printf` is libc.a's (`%ld` and `%lo` take the sum as a long, high
word first), and `main()` has no `exit()`.

## Where the original is odd, and the port with it

- the header buffer is a local of `main()`: a short read (a file of less than
  16 bytes, an empty one) leaves the bytes of the file before it - the first
  file finds zeros, so an empty first file is "not an object file" and an
  empty file after an object has that object's sizes again;
- `main()` has no `exit()`: the value left in AX is the exit status. After a
  file that was read it is `fclose()`'s, 0; after a file that was not found
  it is `printf()`'s, 0 or -1 (status 255) once standard output has failed
  to take a block (`src/h/mutos_stdio.h`);
- standard output is a 512-byte block, written when the next character
  arrives or the program ends; a terminal gets a line at a time.

## What the port refuses

A situation the port does not model ends with exit status 125 and a last
message line ending in `refused`: a file name that is a directory (the
original reads its entries).

## How it is checked

- `tests/realbin/utilcmp.py cases size` (in `make check-realbin`): every
  object and archive of the tree (the compiled goldens, `crt0.o`, `libc.a`
  and its objects), headers with every magic number, truncations, several
  files, directories, a full standard output - run on the original under the
  8086 emulator and on the port: the messages and the exit status the same.
- `tests/realbin/utilfuzz.py size`: differential fuzzing with random and
  damaged headers.
- `tests/mutos_binutils/check_tools.py` (in `make test`, no emulator needed).

## Source layout

- `size.c` - `main()`, the refusals
- `../h/mutos_stdio.h` - libc.a's stdio as the originals show it, shared with
  `nm`, `strip` and `tsort`
- `Makefile` - `make` builds `mutos_size`
