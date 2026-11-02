# mutos_nm - MUTOS 1700 Cross Name List

`mutos_nm` is a port of the original MUTOS 1700 `nm`
(`mutos/mutos_bin/nm.golden.base64.txt`): Unix V7's `nm.c` for the 8086. Its
three functions, `main()`, `compare()` and `nextel()`, and libc.a's `qsort()`
under them were read instruction by instruction beside `v7/nm/nm.c` - the
binary is the specification, v7's source the skeleton - and the port does
what the binary does, its quirks included (`docs/MUTOS_ORIGINAL_BUGS.md`,
NM-01 ..). Added on 2026-11-02 (`docs/TOOLCHAIN_PLAN.md`, step 8).

`tests/realbin/nmmap.txt` maps the three functions of `nm.o` and the libc.a
functions linked in to their text offsets in the stripped binary and
describes the data; `tests/realbin/nmdis.py` disassembles one by name. The
comments in `nm.c` carry the offsets.

## What it does

    nm [-gnopru] [file ...]

For every file named (`a.out` if there is none) it prints the symbol table of
the file, or of every member of an archive that is an a.out file:

    0010 T _main
         U _printf

- value (four hex digits where v7's format is `%06o`; blank for an
  undefined symbol), type letter (`a` absolute, `b` bss, `c` common, `d` data,
  `f` file name, `r` register, `t` text, `u` undefined; upper case for an
  external symbol) and the name, at most eight characters;
- the options are read from the first argument only: `-g` external symbols
  only, `-n` sort by value (and then by name), `-o` prepend the file's (and
  the archive member's) name to every line, `-p` do not sort, `-r` sort in
  reverse order, `-u` undefined symbols only (the name only);
- an archive is named (`\nlibc.a:\n`) when more than one file is given, and
  every member of an archive - every file, when there are several - is named
  before its list (`\nname:\n`) unless `-o` is given;
- `nm: x-- bad format`, `nm: x-- no name list`, `nm: cannot open x`, `nm:
  invalid argument -c` and `nm: out of memory on x` go to standard error.

## The result of the reading

None of the three functions differs from v7's in what it does. What differs
is the representation and the libc.a underneath:

- the headers are kept as bytes: `struct ar_hdr` (26 bytes), `struct exec`
  (16), `struct nlist` (12); the longs have their high word first; a symbol's
  type is a word (`n_type` and the spare byte behind it);
- `FORMAT` is `%04x`;
- the sort is libc.a's `qsort()`, V7's.

## Where the original is odd, and the port with it

- `arp` (the archive header) and `exp` (the a.out header) lie one behind the
  other: `nm: %s-- no name list` and the member names in the list print
  `arp.ar_name` - 14 bytes with no NUL if the name has 14 characters - on
  through the date, the owner and the size, and on into the a.out header, up
  to the first 0 byte. The port keeps the two in one array;
- `arch_flg` is never reset: the files that follow an archive on the command
  line are read as archives, too - an a.out file is "an archive of one
  member", read from offset 2 on, and its name is not printed;
- `arp`, `exp` and the symbol `sym` are not cleared between members nor
  between files: a short read leaves the old bytes (an empty file keeps the
  magic number of the one before; the first file finds zeros), and so does
  `sym`;
- `n_type` is a word: `-g` tests the bit 040 of the whole word; the letter is
  the type masked with 037, 24 being a register and 037 a file name, every
  other number one that is not named an absolute symbol;
- names are compared as signed characters (a name with a byte of 0200 or more
  sorts before the others), values as unsigned numbers; elements that compare
  equal come out in the order of `qsort()`'s swaps - the port has the same
  algorithm in the original's 16-bit pointer arithmetic;
- an undefined symbol is printed as six blanks, every other as four digits
  and a blank: the columns do not line up;
- a file that is neither an archive nor an a.out file is not closed (`bad
  format`): the library has twenty `FILE` slots, three of them taken;
- the exit status is 0 whatever happened to the files (1 for an invalid
  option, 2 for no memory).

## What the port refuses

A situation the port does not model ends with exit status 125 and a last
message line ending in `refused`: a file name that is a directory (the
original reads its entries); the eighteenth file open at the same time (the
original uses a `FILE` beyond its table); a seek to a negative offset (a
negative member size: the original's `lseek()` fails); an `ar_name` with no 0
in the 42 bytes; more than `NM_MAXSYM` (2000) symbols in one a.out file (the
original's heap and stack share the 64K of the process).

## How it is checked

- `tests/realbin/utilcmp.py cases nm` (in `make check-realbin`): every object
  and archive of the tree (the compiled goldens, `crt0.o`, `libc.a` and its
  objects) with every option and combinations, random symbol tables with
  equal values and names, every type, names of every length, header
  variants, truncations, archives of odd sizes and cuts, more than seventeen
  files that stay open, a full standard output, directories - run on the
  original under the 8086 emulator and on the port: standard output,
  standard error and the exit status the same.
- `tests/realbin/utilfuzz.py nm`: differential fuzzing with random and
  damaged objects and archives.
- `tests/mutos_binutils/check_tools.py` (in `make test`, no emulator needed).

## Source layout

- `nm.c` - `main()`, `compare()`, `nextel()`, `qsort()`, the stdio slot
  model, the refusals
- `../h/mutos_stdio.h` - libc.a's stdio as the originals show it
- `Makefile` - `make` builds `mutos_nm`
