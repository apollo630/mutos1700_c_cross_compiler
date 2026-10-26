# mutos_ar - MUTOS 1700 Cross Archiver

`mutos_ar` is a port of the original MUTOS 1700 archiver
(`mutos/mutos_bin/ar.golden.base64.txt`): Unix V7's `ar` for the 8086. Every
function of the binary was read instruction by instruction beside
`v7/ar/ar.c` - the binary is the specification, v7's source the skeleton -
and the port does what the binary does, its quirks included
(`docs/MUTOS_ORIGINAL_BUGS.md`, AR-01 ..). Added on 2026-10-26
(`docs/TOOLCHAIN_PLAN.md`, step 6).

`tests/realbin/armap.txt` maps every function to its text offset in the
stripped binary and gives the data of the program; `tests/realbin/ardis.py`
disassembles one function by name. The comments in `ar.c` carry the
offsets.

## The result of the reading

None of the 33 functions of `ar.o` differs from v7's `ar.c` in what it
does. What differs is the representation and the libc.a underneath:

- the archive header (`struct ar_hdr`, 26 bytes: `name[14]`, `long date`,
  `char uid, gid`, `int mode`, `long size`) is kept as 26 bytes, the longs
  as the 8086 compiler stores them - the high word first, each word
  little-endian (`ar_getlong()`/`ar_putlong()`); the magic number is
  `0177545`, the bytes `65 ff`;
- `char` is signed: `ar t` shows an owner above 127 as a negative number,
  and the option counters (`flg[]`) wrap after 128 repetitions of a key;
- the 32-bit `struct stat` is the 30-byte MUTOS one; only the size, the
  time, the mode and the owner of a file are used;
- the temporary files are `/tmp/vXXXXX`, `/tmp/v1XXXXX` and `/tmp/v2XXXXX`
  (`mktemp()`: the process number's digits replace the X's, then the
  first digit becomes `a`, `b`, .. while the name exists), or, with the
  key `l`, `vXXXXX` etc. in the current directory;
- `perror()` is libc.a's, with its 41 messages (`ardata.c`, generated from
  the binary by `tests/realbin/artables.py --c`); a system call that
  fails with an error number above 34 (the host's own) is reported as
  EINVAL;
- `ctime()` is libc.a's `localtime()`/`gmtime()`/`asctime()`; the port takes
  the host's calendar (`localtime_r()`), so `ar tv` shows the host's
  time zone where the original, whose `ftime()` says UTC unless told,
  shows UTC - the comparisons run with `TZ=UTC`.

## Command line and behaviour (the original's)

    ar [mrxtdpq][uvnbailc] [posname] archive [file ...]

- `r` replaces or appends, `q` appends without a check, `d` deletes, `m`
  moves, `x` extracts, `p` prints, `t` lists (`tv`: mode, owner/group, size,
  time and name); `u` alone is `r`, `u` replaces only a file newer than the
  member, `c` leaves out the message "ar: creating x.a" when `r` or `q`
  makes the archive, `n` is accepted and has no effect, `v` is verbose, `l`
  puts the temporary files into the current directory, `a`, `b` and `i`
  (= `b`) take the position name from the next argument;
- a member is named by the last component of the file name (trailing
  slashes dropped), cut at 14 characters; the comparison for `match` uses
  the whole name given on the command line;
- `usage:` goes to the standard output, the messages (`ar: bad option`,
  `ar: cannot open`, `ar: x not found`, ..) to the standard error; the
  exit status is 0, 1 after an error that ends the run, else the number
  of names given that were not found (`done(notfound())`).

## Where the original is odd, and the port with it

- a name longer than 14 characters never matches a member: `ar r` adds it
  again each time, `ar d` does not find it ("not found");
- `ar r` with names copies the old member unchanged when the named file
  cannot be opened, saying "cannot open" only when names were given;
- a file named twice on the command line may be put in twice;
- `ar x` makes the file with the member's mode and no time (the file is
  dated now); extracting the archive from within itself, or `ar q x.a x.a`,
  feeds the archive to itself - the port keeps the same sequence of reads
  and writes, so the same bytes result (a runaway one is stopped by the
  harness's file-size limit, not by `ar`);
- a read that returns less than asked in `copyfil()` is a "phase error",
  the block written all the same with the old bytes of the buffer's end; the
  short reads are counted in a 16-bit int, so 65536 of them (a member 32 MB
  past the archive's end) are none (AR-05);
- a write that comes back short (the file-size limit; `ar p` of a member that
  is longer than the file) is "ar write error" with the error number the last
  failed system call left: on a file or a pipe, the `ENOTTY` of the
  `isatty()` that libc.a's stdio asks when the first character goes to stdout
  (`ar_out()` does the same), else a stale `ENOENT` of an earlier `open()`,
  or "Error 0";
- a 14-character name is not terminated in the header (`ar t` and the
  messages read it into a 16-byte buffer, which has the NUL).

## What the port refuses

A situation the port does not model ends with exit status 125 and a last
message line ending in `refused` (the temporary files are removed first):
an opened directory (the original reads its entries as bytes; the
emulator's `open()` of a directory answers ENOENT); a file name longer than
the host's limits; a time the host's calendar cannot give. The comparators
treat these as a class of their own.

## MUTOS paths

`ar` has no absolute path compiled in but the temporary directory `/tmp`;
it is looked up under `$MUTOS_ROOT` (`src/h/mutos_root.h`) - by default this
tree's `mutos_root/tmp/`. The names on the command line are host paths.

## How it is checked

- `tests/realbin/arcmp.py cases` (in `make check-realbin`): hand-written
  command sequences - every command and key, positions, `u`, `l`, names of
  every length, paths, damaged and foreign archives, odd sizes, owners
  above 127, times past 2038, files that change between commands - run on
  the original under the 8086 emulator and on the port in two copies of one
  directory: the messages, the exit status and both trees (names, bytes
  and modes) after every step must be the same.
- `tests/realbin/arfuzz.py`: differential fuzzing - random pools of files,
  random commands with random keys, files that change between the steps;
  `--mutate` damages the archive (cut short, bytes changed) for the error
  paths.
- `tests/mutos_ar/check_libc.py` (in `make test`, no emulator needed):
  libc.a rebuilt from its 167 objects by `mutos_ar rc` and `mutos_ranlib`
  is the same archive byte for byte but for `__.SYMDEF`'s time, group and
  mode; `t`, `x`, `p`, `d` and `rb` on it.
- `tests/realbin/artables.py --check-c src/mutos_ar/ardata.c`: the generated
  error texts are the binary's.

## Source layout

- `ar.h` - the header's layout, the codec of the longs, the prototypes
- `ar.c` - the 33 functions, in v7's order, with the original's text
  offsets
- `arlib.c` - what ar gets from libc.a: `mktemp()`, `perror()`, `ctime()`,
  the MUTOS path, the refusal
- `ardata.c` - libc.a's `sys_errlist[]`, generated from the binary
- `Makefile` - `make` builds `mutos_ar` (the top-level Makefile passes the
  MUTOS root's default path)
