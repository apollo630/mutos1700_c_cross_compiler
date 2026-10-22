# `tests/mutos_ld/` - the linker's own tests

`mutos_ld` (`src/mutos_ld/`) is a port of the original MUTOS 1700 `ld`
(2026-10-22, `docs/TOOLCHAIN_PLAN.md` step 2). Its oracle is the original
itself, run under the 8086 emulator: `tests/realbin/ldcmp.py` (every
compiled golden linked with a matrix of options, and hand-written cases)
and `tests/realbin/ldfuzz.py` (differential fuzzing) compare the two -
see `tests/realbin/README.md`.

What is here:

- `check_rflag.py` - the port's extension, `-r` and `-d`, which the
  original refuses and so cannot check: every program of the construct
  corpus (`tests/mutos_cc`, categories 00 to 10), linked through a
  relocatable object (`ld -r x.o`, `ld -r crt0.o x.o -lc`, `ld -r -d ...`
  then `ld` or `ld -i`), must run under the emulator as the same program
  linked at once - exit status and output; a `-r` output's header is
  checked (0407, relocation present), and that the original refuses both
  options. In `make check-realbin` (needs the Python module `unicorn`).

Not here: the real-hardware linked binaries `myhello` (0407) and `idhello`
(`-i`, 0411) that the earlier `mutos_ld` was once checked against are
missing from the tree; they would still be welcome as real-hardware
evidence (`STATUS.md`, open item 1).
