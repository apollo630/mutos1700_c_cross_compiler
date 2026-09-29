# `tests/mutos_as/float_open/` — floating-point probes `mutos_as` still refuses

This directory holds hand-written probes for the real MUTOS 1700 `as`
whose constants the current `mutos_as` refuses on principle. Every
constant in them is plain `atof` syntax, so the real `as` is expected to
assemble them. Because `mutos_as` does not, `../run_goldens.sh` would
put them in category 3 ("errors calling mutos_as") permanently, so this
directory is deliberately **not** part of the top-level `make test`.
Once `mutos_as` reproduces a probe's golden, the probe and its golden
move to `../float_coverage/` and become regression goldens. That is what
happened to four of the five probes written here so far: `fltopen.s`
(2026-09-28), `fltmode.s`, `fltmul.s` and `fltovf.s` (2026-09-29) - see
`../float_coverage/README.md`.

**There is no open probe at the moment.** The fifth, `fltsig.s`, is
kept here as evidence: the real `as` refuses it, so it has no golden and
never will (below).

## `fltsig.s` - resolved without a golden: the real `as` aborts (2026-09-29)

`fltovf.s` and `fltsig.s` were written together, for the two ways the
conversion can leave the format's exponent range (`FLT_RANGE` in
`src/mutos_as/fltconst.h`):

- **`fltovf.s` - only the result is out of range.** The last step of
  `atof` is `ldexp(fl, exponent)`; `libc.a`'s `ldexp.o`
  (`../libc_recon/ldexp.s`) adds the exponent to `fac`'s exponent byte as
  a 16-bit sum, checks only for a signed 16-bit overflow (`jo`) and
  stores the low byte. The real `as` wrote all nine predicted constants
  byte for byte - wrapped exponent bytes, no diagnostic. `mutos_as`
  writes them the same way now, and `fltovf.s` has moved to
  `../float_coverage/`.
- **`fltsig.s` - a step inside `atof` overflows**: `flexp` = 5\*\*k in
  the repeated squaring, from k = 55 on - the compiler's `%.17e` text
  from `e-38` down (`e-39` when `atof` drops the 18th digit), although
  such a value may fit. In `libc.a`'s runtime the overflowing `dmul`
  calls `__ovfl` (`fperr.o`): `errno` = `ERANGE`, then
  `kill(getpid(), 8)` - `SIGFPE`.

| Label | Line | Text | Step that overflows in `libc.a`'s runtime |
|---|---|---|---|
| `S1` | 41 | `.float 1.00000000000000000e-38` | k = 55: `flexp` 5\*\*23 × `exp5` 5\*\*32 - the value fits; the case the compiler can write |
| `S2` | 42 | `.double 2.93873587705571877e-39` | k = 56: 5\*\*24 × 5\*\*32 - the value is 2\*\*-128, the format's smallest |
| `S3` | 43 | `.float 1.0e+65` | k = 64: the squaring 5\*\*32 × 5\*\*32 itself (the value is above the range anyway) |

**The real result**, `as -o fltsig.o fltsig.s` on MUTOS 1700 hardware
(via this directory's `Makefile.mutos`), verbatim:

```
as -o fltsig.o fltsig.s
***ERROR*** floating point over/under flow- assembly aborted
W
?h), line 41
***ERROR*** floating point over/under flow- assembly aborted
W
?h), line 41
*** Error code 4

Stop.
```

Exit status 4, no object. Line 41 is `S1`: the first constant already
stops the assembly, so `S2` and `S3` are never reached. The `W` / `?h)`
lines stand where a file name might be expected; they are recorded as
they appeared, not interpreted.

**Reading.** The real `as` catches `SIGFPE` rather than dying of it
(there was no "Floating exception" and no core): as far as these lines
show, its handler prints the message and returns, and the assembler
gives up at the end of the constant. That the message appears twice
fits `libc.a`'s runtime: run under the emulator with a handler that
returns (the harness `../float_coverage/libcatof.py` is built on),
`atof("1.00000000000000000e-38")` raises `SIGFPE` twice - `__ovfl` in
the overflowing `dmul`, then `__div0` when `atof` divides by the zero
`flexp` that is left - and so does `S2`, while `S3` raises only `__ovfl`.
Two signals at line 41, two messages.

**Consequence for `mutos_as`.** Its refusal of these texts (`FLT_RANGE`:
"a step of the real assembler's conversion overflows ...") is the
faithful behaviour: neither assembler writes an object. `mutos_as`
exits with status 1 on an error, the real `as` with 4 here; that
difference is known and left as it is. `libcatof.py check` confirms,
over thousands of random and compiler-shaped texts, that every text
`fltmodel.py` and `fltconst.c` class as `RANGE` makes `libc.a`'s runtime
raise `SIGFPE`, and that none of the texts they convert does (zeros
aside, which the emulator does not model).

`fltsig.s` stays unchanged here so that line 41 keeps meaning `S1`.
`make -f Makefile.mutos` in this directory is therefore **expected** to
stop with `*** Error code 4`.

## Also still open

- **LOGHUGE after a dropped digit**: a text with more than 17 or so
  digits and a decimal exponent below -39 minus the digit count -
  `atof` leaves `fcmp`'s difference in `fac`, never observed.
  `mutos_as` refuses it (`FLT_UNKNOWN`). The value is below the format's
  range anyway, and the compiler never writes such a text.
- Not understood, but pinned by real bytes: where the `ff ff ff` that
  the digit loop leaves in `fac` for an all-zero text comes from
  (`libc.a`'s runtime leaves -2\*\*56 there) - see
  `src/mutos_as/fltconst.h`.

## Adding a probe

1. Write `<name>.s` here (see `../float_coverage/fltmul.s` for the
   shape: each constant loaded the way `mutos_c1` emits one, a header
   saying what each constant asks), list it in `Makefile`'s `SRCS` and
   give it a rule in `Makefile.mutos` - before `fltsig.o` in `all`, since
   that one stops `make` - and write down the predicted bytes here:
   `../float_coverage/fltmodel.py` and `libcatof.py` compute them. A
   text for which `libcatof.py atof` prints `SIGFPE` aborts the real
   `as`: keep it out of a probe that is meant to produce a golden.
2. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. If the real `as`
   refuses an operand instead, note which one, its exact error text and
   the exit status; that is the finding. An object left behind by an
   `as` that failed is not a golden: `rm -f <name>.o core`.
3. `make goldens` — **on the modern (Linux) side**, after copying
   `<name>.o` back here. Produces its golden and base64 companion
   (`<name>.o.golden`, `<name>.o.golden_base64.txt` - `../mk_goldenbase64.sh`'s
   naming); a missing or empty object is skipped.
4. From `../float_coverage/`: `python3 fltmodel.py survivors . ../float_open`
   reads every constant of the probe out of its golden alongside the
   existing goldens, prints the combinations that fit all of them, lists
   constants outside the model as "NOT MODELLED" with their real bytes,
   and reports "MODEL CONTRADICTED" if a modelled constant fits no
   combination. Then update `fltconst.c` and `fltmodel.py`, move the
   probe and its golden to `../float_coverage/` (its `Makefile`/
   `Makefile.mutos`), and update `STATUS.md`'s open item 7, `CLAUDE.md`'s
   "Next up" and `docs/DEVLOG.md` in the same change (Workflow
   Guideline 6).
