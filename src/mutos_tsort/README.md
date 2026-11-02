# mutos_tsort - MUTOS 1700 Cross Topological Sort

`mutos_tsort` is a port of the original MUTOS 1700 `tsort`
(`mutos/mutos_bin/tsort.golden.base64.txt`): Unix V7's `tsort.c` for the
8086. Its eight functions and libc.a's `fscanf` under them were read
instruction by instruction beside `v7/tsort/tsort.c` - the binary is the
specification, v7's source the skeleton - and the port does what the binary
does, its quirks included (`docs/MUTOS_ORIGINAL_BUGS.md`, TS-01 ..). Added on
2026-11-02 (`docs/TOOLCHAIN_PLAN.md`, step 8).

`tests/realbin/tsortmap.txt` maps the eight functions of `tsort.o` and the
libc.a functions linked in to their text offsets in the stripped binary and
describes the data; `tests/realbin/tsortdis.py` disassembles one by name. The
comments in `tsort.c` carry the offsets.

## What it does

    tsort [file]

Reads pairs of items (strings with no blank in them) from the file, or
standard input, and writes the items in an order that obeys the pairs (`a
b`: a before b); a pair of the same item twice only says that the item
exists. With a cycle among the pairs it writes `tsort: cycle in reverse
order` and the items of one cycle to standard error, and breaks the cycle at
the last of them. `tsort: odd data` (an odd number of items) and `tsort:
cannot open x` end with status 1.

`lorder`'s output (`src/mutos_lorder/`) is what it is meant for.

## The result of the reading

None of the eight functions differs from v7's in what it does. What differs
is the representation and the libc.a underneath: words of 16 bits, structures
of 8 (`struct nodelist`) and 4 bytes (`struct predlist`), `fscanf` and
`printf` of libc.a, V7's `malloc()` in the 64K of the process, and `main()`
has no `exit()`.

## Where the original is odd, and the port with it

- the two items are read by `fscanf(input, "%s%s", precedes, follows)` into
  `char precedes[50], follows[50]` in `main()`'s frame; the separators are
  the three characters of libc.a's `__sctab` that carry the bit 1 - blank,
  tab and newline, not CR, VT or FF - and an item of 50 characters or more
  runs on: `follows`' tail into `precedes`, `precedes`' tail into `x` and `j`.
  The port keeps the frame as bytes, in the original's order, with the same
  stack bytes staying from one pair to the next;
- the exit status is what `main()` leaves in AX: 255 when standard input is
  empty, otherwise what the last `printf()` returned - 0, or -1 (255) once
  standard output has failed to take a block (`src/h/mutos_stdio.h`);
- the items are looked up in the order they were first seen, by a linear
  search of a list, and the nodes come out in that order;
- `malloc()` fails when the 64K of the segment are used up: `tsort: too many
  items`. A failing `malloc()` of a predecessor list is not an error but a
  write to address 0.

## What the port refuses

A situation the port does not model ends with exit status 125 and a last
message line ending in `refused`: a file name that is a directory (the
original reads its entries); an item of 54 characters or more in the first
place, or 104 or more in the second (the original's `input` pointer is
overwritten); a byte of 0200 or more in the input (`__sctab` has 128
entries); more graph than the port knows the original's heap to hold
(`TS_HEAP`); `error 1` and `error 2` of `findloop()`, which cannot happen.

## How it is checked

- `tests/realbin/utilcmp.py cases tsort` (in `make check-realbin`): graphs
  of every shape, long items, items with every separator, random DAGs and
  cycles, self-loops, duplicates, empty input, a missing file, a directory -
  run on the original under the 8086 emulator and on the port: standard
  output, standard error and the exit status the same.
- `tests/realbin/utilfuzz.py tsort`: differential fuzzing with random
  graphs and damaged input.
- `tests/mutos_binutils/check_tools.py` (in `make test`, no emulator needed):
  `lorder | tsort` on `libc.a`.

## Source layout

- `tsort.c` - the eight functions, the frame model, `fscanf`, the heap, the
  refusals
- `../h/mutos_stdio.h` - libc.a's stdio as the originals show it
- `Makefile` - `make` builds `mutos_tsort`
