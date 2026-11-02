# mutos_em - MUTOS 1700 Cross Extended Text Editor

`mutos_em` is a port of the original MUTOS 1700 `em`
(`mutos/mutos_bin/em.golden.base64.txt`), its line mode. Added on 2026-11-02
(`docs/TOOLCHAIN_PLAN.md`, step 9).

`em` has no source. Its binary (0411: 27136 bytes of text, 2106 of data,
10966 of bss) is the original `ed` (`src/mutos_ed/`: v7's `ed.c` with the
two MUTOS changes) with a new `main()` and `commands()`, an input reader of
its own, a session log, a few commands more and a screen mode: 33 of ed's
functions are in it instruction for instruction, ten more with small changes,
found by matching the original `ed`'s - rebuilt byte for byte - against the
text. `tests/realbin/emmap.txt` maps every function to its text offset with
what it is (ed's, ed's changed, em's own, the screen mode) and the data;
`tests/realbin/emdis.py` disassembles one by name. The port is
`src/mutos_ed/ed.c` with em's changes and functions, the comments carrying
the offsets.

The MUTOS kernel's makefile runs it on the preprocessed `mch.c` (`em mch.i
<fakeawk >/dev/null`): with the real `mch.i` (`tests/mutos_cpp/c/mch.i.golden`)
the port gives the real `mch.s` (`tests/mutos_as/kernel_opt/mch.s`) byte for
byte, as the emulated original does (`tests/mutos_ed/check_editors.py`).

## What it does

    em [-] [-s] [-e] [-l] [-q] [-r log] [-x] [file]

`ed`'s commands and addresses, and (`mutos/mutos_man/ed.1.d`, appendix 1, and
the binary):

- "Editor" at the start and ">" before every command read from the standard
  input (written at once); `-` and `-s` neither, nor the counts (ed's `-`);
  `<` turns the prompt and the counts on, `>` off - both start the command
  loop over, so the rest of the line is read as the next command (the
  newline: print the next line);
- `-e`: only the file of the command line is edited ("no file" and exit
  status 255 without one); `e`, `f`, `w` and `W` are errors, a name that is
  not given is taken once only, `q` writes the file and ends; no counts;
- `-l`: no session log - nor one when the standard input is not a terminal or
  with `-e`. The log, `<file>.elog` (the file's last component cut to eight
  characters; "elog" without a file; a digit appended while the name
  exists), holds every byte read, after "r <file>" and the number of the
  current line; `w` without a name and `e` start a new one, the end of the
  session removes it, `q name` links it to name first; `-r log` replays a log
  (echoed through stdio) before the standard input is read;
- the standard input is read a byte at a time, or 128 bytes at a time from a
  terminal; bytes of 0200 and above are taken as they are (8-bit text),
  `l` shows them as `\ooo`;
- `q` and `e` with changes not written ask "sure? " - the answer `y` or `n`,
  read with `read(0, buf, 128)` - where ed refuses once; at the end of a
  standard input that is not a terminal em ends without asking, at the end
  of a terminal it goes on;
- `s/re/text/n` prints the number of substitutions (`gn` both); `z` is `s`
  asking at every match - '.' substitutes, a newline does not -, the match
  shown between '"' (or marked by '^' under it on a fast terminal), the
  answer read from the standard error; a pattern ended by the newline is the
  last one (`/` alone searches again); a search or a substitution that finds
  nothing is "??";
- `"` prints the lines after the current one (`.+1,.+lines-2`), `&` those
  before it (`.-lines+3,.`), `%` those around it (`lines/2-3` on either side)
  with a rule of '_' above and below the current line, which stays - lines
  and columns are the terminal's, 20 and 80 without one;
- `(.,.)|cmd` writes the lines into a pipe to the shell's cmd; `!cmd` runs it
  with `%` replaced by the file's name, `!!` the last command and the rest of
  the line behind it (both echoed); both the host's `/bin/sh` with the MUTOS
  root's `bin/` first in the `PATH`;
- `h` lists `/usr/lib/emhelp/commands`, `h<name>` `/usr/lib/emhelp/<name>`
  of the MUTOS root, to the standard error ("<name> not found" to the standard
  output);
- `b+`, `b-` (and a file whose name ends in `.n`) set a "line break" flag that
  nothing in the original reads.

## The 8086's representation, kept

As in `mutos_ed`: 16-bit ints and addresses, the line table at 0x3312 in the
data segment (measured), signed chars, error()'s seek to the end of the
standard input, the output a line at a time, `/tmp/eXXXXX` under the MUTOS
root. The bss arrays without a bound lie one behind the other as in the
original: the last shell command (0x0840, 100 bytes), `savedfile` (0x08a4),
the log's name (0x08e4), `file` (0x0924), `linebuf`, `rhsbuf`, `expbuf` -
`circfl` at 0x0ce8.

## What the port refuses

`mu_trap()`: exit status 125, the last message ending in "refused":

- `o`, the screen mode (an ANSI terminal and its function keys);
- a write past `circfl` or `genbuf` through the arrays without a bound - a
  long file name, `!!` again and again, a long `h` name
  (`docs/MUTOS_ORIGINAL_BUGS.md`, EM-02, EM-04) -, a `|` command of more than
  79 characters (the original overruns its stack, EM-03), a read past them
  (a NUL byte in a pattern);
- a line table above 0xF000 in the segment;
- where the original loops for ever: a `z` answer, or a "sure?" answer, at
  the end of its input (EM-05, EM-06), an empty `\(\)` repeated by `\1*`, a
  help file that cannot be read; and a "sure?" whose first read gives
  nothing (the answer is the stack's garbage);
- the encryption (`-x`, `x`), as in `mutos_ed`.

## Checked

- `tests/realbin/edcmp.py` (in `make check-realbin`): the port against the
  original under the emulator on hand-written cases - ed's (the same
  scripts) and em's own: the options, "sure?", `z`, `"`, `&`, `%`, `<`, `>`,
  `b`, `h`, `n`, 8-bit text, a byte 0377, a replayed log, the kernel's `mch.i`
  with `fakeawk` -, the scripts from a file and through a pipe;
  `edfuzz.py`: random files and scripts; no difference;
- `tests/mutos_ed/check_editors.py` (in `make test`, no emulator): the
  kernel's `mch` rule through `mutos_make` (cc -P, em, cc -c: the real
  object), and `!`, `%`, `!!` and `|`.

The emulator cannot answer `fork()` and `pipe()` (`!`, `|`) or a terminal
(the prompt's line reads, the log, `-x`): those are checked by reading and,
for `!` and `|`, by `check_editors.py`.
