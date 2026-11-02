# mutos_make - MUTOS 1700 Cross Make

`mutos_make` is a port of the original MUTOS 1700 `make`
(`mutos/mutos_bin/make.golden.base64.txt`): Unix V7's `make` for the 8086.
Every function of the binary was read instruction by instruction beside
`v7/make/` - the binary is the specification, v7's source the skeleton - and
the port does what the binary does, its quirks included
(`docs/MUTOS_ORIGINAL_BUGS.md`, MK-01 ..). Added on 2026-11-02
(`docs/TOOLCHAIN_PLAN.md`, step 7).

`tests/realbin/makemap.txt` maps every function to its text offset in the
stripped binary and gives the data of the program; `tests/realbin/makedis.py`
disassembles one function by name, `tests/realbin/maketables.py` prints and
checks the data the port is generated from. The comments in the sources
carry the offsets.

## The result of the reading

49 of the 50 functions of `make.o` are v7's: compiled with this project's own
`cc -O -Dunix` they give the original's bytes, with three differences that are
no difference of source but of the headers the original was built with -
`METERFILE` is not defined (no `meter()`), `struct nlist` has `char n_type;
char n_other;` where v7's has an `int n_type` (the same 12 bytes), and
`NLEFTS`, the number of names in one rule, is 300 where v7's `defs` says 40.
The fiftieth, `yyparse()`, is yacc's parser (the skeleton of v7's `yaccpar`,
without `yydebug`) on the eight tables of `gram.y`'s grammar; the port runs
the same parser on the same tables, which `tests/realbin/maketables.py --c`
writes from the binary as `makedata.c` together with `builtin[]`, the
built-in rules and macros - string for string v7's `files.c`. What differs
from a build for the host is the representation and the libc.a underneath:

- `int` is 16 bits: sums, the hash of a name, the status word `wait()` gives
  (`status >> 8` of `0xC800` is -56 - MK-10), `errstat`; `char` is signed
  (the port is built with `-fsigned-char`); times are 32-bit longs;
- the structures are the originals' in what they hold (`nameblock` 12
  bytes, `lineblock` 6, `depblock` 4, `shblock` 4, `varblock` 8, `pattern`
  4, `opendir` 6, `chain` 4); the heap they live in is the host's but it is
  counted in the original's sizes, with the frames of the recursive
  functions, against the 64K segment (`mk_account()`, `mk_enter()`);
- stdout is libc.a's stdio: 512-byte blocks written when the 513th
  character comes, `fflush()` and `exit()` write what is there (a forked
  child shares what the parent has not written), stderr is not buffered; the
  order of what make and the commands it runs write to one file is the
  original's;
- `printf("%s", 0)` prints `(null)`, as libc.a's does (`-p` shows `@ =
  (null)` between two commands);
- a directory read by `srchdir()` is what a MUTOS program sees: the entries
  `.` and `..`, then the names in byte order, those of more than 14
  characters left out; it is read again at each pattern;
- an empty path name is the current directory, as in V7's `namei()` (and in
  the emulator);
- the archive `a(b)` / `a((b))` is read from its bytes: the header
  (`struct ar_hdr`, 26 bytes, the longs high word first), the a.out header of
  an object (16 bytes) and its symbols (12 bytes each, `N_EXT` 040).

## Command line and behaviour (the original's)

    make [-f makefile] [-p] [-i] [-k] [-s] [-r] [-n] [-t] [-q] [-d] [-S]
         [macro=value ...] [name ...]

The description file is `-f file` (`-f -` is the standard input; several
`-f` are read in turn), else `makefile`, else `Makefile`. The built-in rules
and macros are read first (`-r` leaves them out). Without a name the first
target that does not begin with a dot is made. A name on the command line
that has an `=` defines a macro, which the description file cannot change.
`-n` prints the commands and runs none, `-t` touches the targets, `-q`
makes nothing and exits 0 if the target is up to date, 255 (-1) if not,
`-i`, `-k`, `-S` and `-s` as in V7, `-p` prints the macros, the rules and
the built-in ones, `-d` the steps (`doname(name,level)`, `TIME(name)=...`).
Special targets: `.SUFFIXES`, `.DEFAULT`, `.SILENT`, `.IGNORE`, `.PRECIOUS`.
Macros in `$(X)`, `${X}` and `$X` forms, the automatic `$@`, `$?`, `$<`,
`$*`. Wildcards `*`, `?`, `[..]` in dependencies; archive members `lib(mem)`
and entry points `lib((_sym))`. The messages and the exit status are the
original's.

## Where the original is odd, and the port with it

`docs/MUTOS_ORIGINAL_BUGS.md` has MK-01 to MK-13: `$?` of a command with
nothing newer (MK-01), a `$` at the end of a string (MK-02), the 301st
name of a rule (MK-03), `-d` with a wildcard (MK-04), `$*` read after
the frame it points into has gone (MK-05), buffers of the stack without
checks (MK-06), a 14-character member name (MK-07), a negative archive
position (MK-08), an archive of one byte (MK-09), `status >> 8` on a signed
16-bit int (MK-10), a command that is blank after its prefixes (MK-11), a
path component of more than 14 characters (MK-12) and the "out of memory"
the hardware reported for a 19 KB makefile (MK-13). The port reproduces
MK-03, MK-10 and MK-11 and the cases of MK-01 whose value is defined; it
refuses the others where the original's output depends on state it cannot
know.

## What the port refuses

`mk_trap()` ends the program with exit status 125 and a last message line
"Make: ... - the original depends on state the port does not model here;
refused" (what was written before is the original's, in order):

- `$?` read when it holds the address `mkqlist()` returned for an empty
  chain (MK-01); a `$` as the last character of a string (MK-02); `$*`
  after the `doname()` that set it has returned (MK-05); the first name of
  the `$?` list longer than the list's buffer (it writes before `qbuf`);
- a name expand() replaced by its matches, read where the original reads
  `depname->namep` through 0 (`-d`, the `.PRECIOUS` test, `appendq()`:
  MK-04);
- more than 300 names in a rule while the heap is at or above 32K (MK-03);
- a command, a name, a macro name, a message or a line that would run over a
  buffer of the original's stack (MK-06): `string[2500]`, `sourcename[100]`,
  `prefix[100]`, `temp[100]`, `concsuff[20]`, `vname[100]`, `buf[100]`,
  `buf[50]`; a command of more than 199 words (`argv[200]`);
- a byte of 0x80 or more where the original indexes `funny[]` or the ctype
  table with a negative `char` (the bytes before them are the libc's data;
  the byte 0xff, `EOF`, is the one valid negative index of the ctype
  table); a negative hash;
- a path component of more than 14 characters (MK-12), a pattern whose
  directory part is a file, a directory as the description file or the
  archive's name opened as a file;
- an archive of one byte (MK-09), a member size that takes the position
  below zero (MK-08), the 14 characters of a member name with no end
  printed (MK-07);
- more doname() calls than the original's stack takes, the heap and the
  stack together at or above 0xF000 of the 64K segment (an assumption:
  MK-13), a directory or a message that cannot be allocated, and a `fork()`
  that fails.

## MUTOS paths

`make` names `/bin/sh` for the commands that have a shell character, and
`execvp()` searches `$PATH` for the others (libc's, with `:/bin:/usr/bin`
when `PATH` is not set). On the host the shell is the host's `/bin/sh`
(`sh -c`, or `sh -ce` without `-i` or a leading `-`), and the PATH of the
commands has `$MUTOS_ROOT/bin` first (`mk_setpath()`), so that `cc`, `as`,
`ld`, `ar`, `ranlib` and `make` in a recipe are the MUTOS tools of the
root. **An absolute path in a recipe (`/lib/c0`, `/lib/cpp`) is run by the
host and cannot be redirected by any `make`**: such a makefile takes the
path as a macro, `make CC=$MUTOS_ROOT/bin/cc CPP=$MUTOS_ROOT/lib/cpp
C0=$MUTOS_ROOT/lib/c0` (`docs/TOOLCHAIN_PLAN.md`, decision 10).

## How it is checked

Against the original under the emulator (`tests/realbin/makecmp.py`; its
`fork`, `exece` and `wait` are faked and the commands it runs are the
host's, the same for both; the roots hold fake `cc`, `as`, `ld`, `yacc`,
`lex`, `cpp` and `c0` that say what they were asked and make the file they
were asked for): every step's standard output and error, the exit status and
both directory trees, byte for byte and mode for mode, in two copies of one
directory:

- `makecmp.py cases`: 307 hand-written sequences (457 runs) - the
  description file, `-f`, every flag, rules and dependencies, `::`, macros
  and their forms, the lines (comments, continuation, tabs, CR LF, long
  lines, NUL and high bytes), syntax errors, the commands' prefixes, exit
  statuses and signals, `.DEFAULT` and the other special targets, the
  built-in rules for every suffix, wildcards and archives of every kind
  (damaged ones, entry points, a directory in the name);
- `makecmp.py corpus`: the thirteen `tests/mutos_cc/*/Makefile.mutos` under
  three timestamp conditions and eight command lines each (`-n`, a run, a
  second run, `-q`, `-pn`, `-t`, `clean`, `-n clean`);
- `makefuzz.py`: random makefiles (macros, suffix rules, wildcards,
  archive members, flags) and command lines with changes of the files
  between the runs, also with damaged makefiles (`--mutate`).

All three without a difference: the runs the port refuses are counted
apart, and the lines it had written before are lines of the original's, in
order. `tests/mutos_make/check_corpus.py` (in `make test`, no emulator)
runs the thirteen makefiles of the corpus through `mutos_make` and the
ported tools of the MUTOS root: every `.s`, `.i`, `.1` and `.2` is the
real-hardware golden, a second run does nothing, `clean` cleans.

Not compared with the original, read only: the signal handlers (`intrupt()`,
`enbint()`: make ignores SIGINT and SIGQUIT while it waits for a command,
and the emulator cannot signal it from outside), and the effect of a
`fork()` that fails.

## Open points

- The limit of the segment (MK-13) is a model, not a measurement: the
  hardware ran out of memory for a makefile the emulator and the port take.
- A byte of 0x80 or more indexes `funny[]` below its start. The first fifty
  or so of those 128 bytes are the tail of libc's ctype table - static data,
  which the port could model -, the rest variables of libc.a that change in
  a run; the port refuses all of them for now, which includes a German
  umlaut in a name or in a command that has no shell character.
- The order of the directory is sorted, not the disk's; the 14-character
  policy and the empty path name are V7's, not measured on the hardware.

## Source layout

| File | Content |
|---|---|
| `make.h` | the structures, constants, prototypes, the host representation |
| `main.c` | `main`, `intrupt`, `isprecious`, `enbint`, `rddescf`, `rdd1`, `printdesc` and the global variables |
| `doname.c` | `doname`, `docom`, `docom1`, `expand` (with the serial of the live `doname()` frames) |
| `misc.c` | names, strings, macros, messages: `hashloc` .. `mkqlist` |
| `files.c` | `exists`, `prestime`, `srchdir`, `amatch`, `umatch`, the archive reader |
| `dosys.c` | `dosys`, `metas`, `doshell`, `await`, `doclose`, `doexec`, `touch` |
| `gram.c` | `yylex`, `retsh`, `nextlin`, the actions of `gram.y` and `yyparse` |
| `makedata.c` | generated: yacc's tables and `builtin[]` (`tests/realbin/maketables.py --c`) |
| `mklib.c` | the libc/segment model: stdio, the counted heap and stack, directories, `PATH` |
