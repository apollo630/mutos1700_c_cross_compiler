# mutos_ranlib - MUTOS 1700 Cross Archive Indexer

`mutos_ranlib` is a port of the original MUTOS 1700 `ranlib`
(`mutos/mutos_bin/ranlib.golden.base64.txt`): Unix V7's `ranlib.c` for the
8086 - which is `nm`'s code with a different `main()`. Every function of
the binary was read instruction by instruction beside `v7/ranlib/ranlib.c`
- the binary is the specification, v7's source the skeleton - and the port
does what the binary does, its quirks included
(`docs/MUTOS_ORIGINAL_BUGS.md`, RL-01 ..). Added on 2026-10-26
(`docs/TOOLCHAIN_PLAN.md`, step 6).

`tests/realbin/ranlibmap.txt` maps the five functions of `ranlib.o` and the
libc.a functions linked in to their text offsets in the stripped binary and
describes the data; `tests/realbin/ranlibdis.py` disassembles one by name.
The comments in `ranlib.c` carry the offsets.

## What it does

    ranlib archive ...

For every archive named it reads the symbol tables of the members that are
`a.out` files (the magic numbers 0407, 0410, 0411 and 0405), collects an
entry for every external symbol they define - eight bytes of name and the
offset of the member's header (a long, high word first) -, writes them as
`__.SYMDEF` in the current directory, and has `ar` put that file in front
of the archive (`ar rlb first archive __.SYMDEF`, or `ar rl archive
__.SYMDEF` if the archive has a table already) before it sets the date of
the archive's first member - the table - five seconds into the future, so
that `ld` finds it newer than the archive. `mutos_ld` reads that table
(and complains "out of date" if the archive is newer); `libc.a` carries
one.

`mutos_ranlib` runs `$MUTOS_ROOT/bin/ar` - the MUTOS root's link to
`mutos_ar` - where the original runs `ar` through the shell's search
path, in the current directory, with the same words as arguments.

## The result of the reading

None of the five functions of `ranlib.o` differs from v7's in what it
does. What differs is the representation and the libc.a underneath:

- the headers are kept as bytes: `struct ar_hdr` (26 bytes), `struct exec`
  (16), `struct nlist` (12), `struct tab` (8 bytes of name and a long); the
  longs have their high word first; a symbol's type is a word (`n_type` and
  the spare byte behind it), and the test for an external is its bit 040;
- the messages begin with `nm: ` (v7's `ranlib` is `nm`'s code);
- the table has 700 entries - `symbol table overflow` and exit status 1 -,
  the command line is built in `char buf[256]`;
- stdio is libc.a's: twenty `FILE` slots, three of them the standard
  streams.

## Where the original is odd, and the port with it

- `arp` (the archive header) and `exp` (the a.out header) lie one behind
  the other in the BSS: `nm: %s-- no name list` prints `arp.ar_name` - 14
  bytes with no NUL if the name has 14 characters - on through the date,
  the owner and the size, and the a.out header behind it, up to the first 0
  byte. The port keeps the two in one 42-byte array and prints the same;
- `arp`, `exp` and the symbol `sym` are not cleared between the members nor
  between the archives: a read that comes back short leaves the old bytes
  (an empty file named after an archive keeps the archive's magic number),
  and so does the last symbol read;
- a file that is not an archive is not closed (`not archive: x`): the
  eighteenth such argument finds no free `FILE` slot and reports
  `nm: cannot open`;
- `fixdate()` patches the date of the archive's *first member*, whether or
  not it is `__.SYMDEF`;
- a negative member size can send `nextel()` round the same members for
  ever (the port loops as well; the comparator stops the original after a
  fixed number of instructions and the port with a time limit, and compares
  what both had said), or seek to a negative offset (refused, below).

## What the port refuses

A situation the port does not model ends with exit status 125 and a last
message line ending in `refused`: an argument that is a directory (the
original reads its entries); a seek to a negative offset; a symbol table
that ends inside an entry before any entry was read in full (the original
uses what its stack held); an `ar_name` with no 0 in the 42 bytes; a
command line of 256 characters or more (the original's `sprintf()` runs
past `buf[256]` over the return address); a name that the shell would
take apart or expand, or an empty one (the original goes through
`/bin/sh -c`); no executable `bin/ar` in the MUTOS root, or an `ar` that
refused.

## How it is checked

- `tests/realbin/ranlibcmp.py cases` (in `make check-realbin`):
  hand-written cases - every message, every magic number, symbols of every
  type, empty and cut tables, members that are not objects, archives with a
  table already, names of 14 characters, more than seventeen non-archives,
  the limits - run on the original `ranlib` and `ar` under the 8086
  emulator and on the ports, in two copies of one directory: the messages,
  the exit status and both trees (bytes and modes) after every step the
  same, the date `ranlib` gives the table aside (it is within the clock's
  window on both).
- `tests/realbin/ranlibcmp.py libc`: libc.a rebuilt from its 167 objects.
- `tests/realbin/ranlibfuzz.py`: differential fuzzing - random objects (every
  symbol type, the four magic numbers and some that are none, a table
  cut short, bytes behind it), random names, `ar` commands between the
  `ranlib` runs; `--mutate` damages the archive.
- `tests/mutos_ar/check_libc.py` (in `make test`, no emulator needed): the
  rebuild of libc.a by `mutos_ar` and `mutos_ranlib`.

## Source layout

- `ranlib.c` - the five functions in v7's order, the stdio slot model, the
  refusals, the call of `ar`
- `Makefile` - `make` builds `mutos_ranlib`
