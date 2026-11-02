# mutos_ed - MUTOS 1700 Cross Text Editor

`mutos_ed` is a port of the original MUTOS 1700 `ed`
(`mutos/mutos_bin/ed.golden.base64.txt`): Unix V7's `ed.c` with two changes.
Added on 2026-11-02 (`docs/TOOLCHAIN_PLAN.md`, step 9).

The binary was read beside `v7/ed/ed.c` - the binary is the specification,
v7's source the skeleton - and turned out to be that source with two changes:
v7's `ed.c` with them, compiled by this project's own `cc -O -s` (`mutos_cc`
with the ported passes) and linked with the real `libc.a`, is the original
binary byte for byte (11522 bytes of text, 234 of data, 4886 of bss). The
port is that source, function for function, with the 8086's representation
kept where a caller can tell (below). `tests/realbin/edmap.txt` maps the
functions to their text offsets - the symbol table of the rebuild - and
`tests/realbin/eddis.py` disassembles one by name; the comments in `ed.c`
carry the offsets.

The MUTOS kernel's makefile edits itself with it (`ed - Makefile <eddep`):
`tests/mutos_ed/check_editors.py` runs those targets through `mutos_make`.

## What it does

    ed [-] [-q] [-x] [file]

v7's line editor (`mutos/mutos_man/ed.1.d`): the commands `a c d e E f g i
j k l m p P q Q r s t u v w W x = ! newline`, addresses `n . $ +n -n ^n
/re/ ?re? 'x a,b a;b`, regular expressions with `. * [..] [^..] ^ $ \( \)
\n`; the buffer in the temporary file `/tmp/eXXXXX`; "?" for an error,
"?name" for a file that cannot be read or written, "?TMP" when the temporary
file is full (128K), the number of characters after `e`, `r` and `w`. `-`
suppresses the counts and the "?" for a `q` or `e` with changes not written
(as `ed -` in the kernel's makefile), `-q` lets the quit signal end it.

## The two MUTOS changes

- `j` with one address joins that line and the next; with none `.` and
  `.+1` (v7: `.,.+1` only without an address, a single address joins
  nothing);
- `m` to the line before the first one moved, or to the first one itself,
  does nothing (v7 moves the lines onto themselves - the same text, and the
  current line set).

## The 8086's representation, kept

- `int` is 16 bits: a number of an address is computed in 16 bits and the
  address in the 64K segment, both wrapping - `1+65536p` and `65537p` print
  line 1, `1+32768p` too -, before it is checked against the buffer; the
  words of the line table (block `<< 8` | offset `>> 1`, the low bit `g`'s
  mark) are 16 bits; the counts are longs;
- the line table is `malloc()`ed at 0x4104 (measured) and grows by 512 lines
  where it is (`free()` and `realloc()`); the difference of two of its
  pointers is the 8086's (`sbb`, `idiv`: exact);
- the arrays `savedfile`, `file`, `linebuf`, `rhsbuf` and `expbuf` lie one
  behind the other in the bss (0x2df2 .. 0x31f6), as in the original: a file
  name of more than 63 characters runs on into the next arrays
  (`docs/MUTOS_ORIGINAL_BUGS.md`, ED-01);
- `char` is signed;
- the standard input is read a byte at a time, the input masked to 7 bits;
  after an error the standard input is sought to its end (v7's
  `lseek(0, 0L, 2)`), so that a script from a file ends at its first error -
  a pipe cannot be sought, and the script goes on;
- the output is written a line at a time (at a newline or at 64 bytes);
- the temporary file's name is `libc.a`'s `mktemp()` on the 16-bit process
  number, under the MUTOS root (`src/h/mutos_root.h`); the names of the
  command line and of the commands are host paths.

`!` runs the host's `/bin/sh` with the rest of the line - the original runs
`sh -t`, which reads that line from the standard input a byte at a time and
ends; the port's child reads the line and runs `sh -c` - with the MUTOS
root's `bin/` first in the `PATH` (as `mutos_make` runs its commands), and
prints "!".

## What the port refuses

`mu_trap()` of `src/h/mutos_stdio.h`: exit status 125, the last message
ending in "refused", where the original depends on state the port does not
model:

- a write or a read past `expbuf`'s end through the run-on names (the
  original overwrites `circfl` and the buffer's pointers, ED-01, ED-02), or
  through a pattern with a NUL byte that matches past a line's end (ED-06);
- a line table that would end above 0xF000 in the segment (where the kernel
  refuses the break - "MEM?" - is not measured; `mutos_make` draws the same
  line);
- a pattern on which the original loops for ever: an empty `\(\)` repeated by
  `\1*` (ED-05);
- the encryption: `-x` (the original's error before its `setjmp()` jumps
  through an unset `jmp_buf`, ED-03) and an `e` after `x` (the temporary
  file's key is made from the time); `x` itself fails as the original's
  does without `/usr/lib/makekey` ("?crypt: cannot generate key", ED-04)
  - a `makekey` in the MUTOS root, a MUTOS program, is refused.

## Checked

- the rebuild: v7's `ed.c` with the two changes, `mutos_cc -O -s`, is the
  original binary byte for byte;
- `tests/realbin/edcmp.py` (in `make check-realbin`): the port against the
  original under the emulator on hand-written cases - every command, the
  error paths, the 16-bit addresses, long lines and names, NUL and 8-bit
  bytes, a full temporary file, 4000 lines - the scripts from a file and
  through a pipe, and the eddep scripts of the kernel's makefile on it;
  `edfuzz.py`: random files and scripts; no difference;
- `tests/mutos_ed/check_editors.py` (in `make test`, no emulator): the
  kernel's makefile through `mutos_make` - the machines, the options,
  `depend` - and `!`.

The emulator cannot answer `fork()` (`!`) or a terminal (`x`); those are
checked by reading and, for `!`, by `check_editors.py`.
