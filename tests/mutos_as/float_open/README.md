# `tests/mutos_as/float_open/` — floating-point probes `mutos_as` still refuses

This directory holds hand-written probes for the real MUTOS 1700 `as`
whose constants the current `mutos_as` refuses on principle. Every
constant in them is plain `atof` syntax, so the real `as` assembles
them. Because `mutos_as` does not, `../run_goldens.sh` would put them in
category 3 ("errors calling mutos_as") permanently, so this directory
is deliberately **not** part of the top-level `make test`. Once
`mutos_as` reproduces a probe's golden, the probe and its golden move to
`../float_coverage/` and become regression goldens. That is what
happened to all three probes written here so far: `fltopen.s`
(2026-09-28), `fltmode.s` and `fltmul.s` (2026-09-29) - see
`../float_coverage/README.md`.

## Current probes: `fltovf.s` and `fltsig.s` (not yet run on real hardware)

`fltmul.s` settled the conversion model: every constant it covers is
determined. What `mutos_as` still refuses is a conversion that leaves
the format's exponent range at some step (`FLT_RANGE`) - the real
assembler's behaviour there has never been observed. There are two
kinds, in two files, because the second may kill the real `as` and
would then take the first one's data with it:

- **`fltovf.s` - the result is out of range, no step before it is.**
  The last step of `atof` is `ldexp(fl, exponent)`. `libc.a`'s `ldexp.o`
  (`../libc_recon/ldexp.s`) adds the exponent to `fac`'s exponent byte as
  a 16-bit sum, checks only for a signed 16-bit overflow (`jo`) and
  stores the low byte, so an exponent byte past 255 or below 1 wraps.
  Run on `libc.a`'s own runtime (`../float_coverage/libcatof.py`), none
  of these texts reaches `__ovfl`: each gives the value's mantissa with
  a wrapped exponent byte. That is the prediction if the real `as` uses
  the same `ldexp`.
- **`fltsig.s` - a step inside `atof` overflows**: `flexp` = 5\*\*k in the
  repeated squaring, from k = 55 on - the compiler's `%.17e` text from
  `e-38` down (`e-39` when `atof` drops the 18th digit), although such a
  value may fit. In `libc.a`'s runtime the overflowing `dmul` calls
  `__ovfl` (`fperr.o`): `errno` = `ERANGE`, then `kill(getpid(), 8)` -
  `SIGFPE`. Unless the real `as` catches that signal, it dies at `S1`
  and writes no object; the message it dies with is the finding.

### `fltovf.s` - predicted bytes (`libc.a`'s `ldexp`, run under the emulator)

| Label | Data offset | Text | Prediction | Reading |
|---|---|---|---|---|
| `E1` | `data+0` | `.double 1.70141183460469232e+38` | `c9 ff ff ff ff ff 7f ff` | control: in range (2\*\*127 in the compiler's form); `mutos_as` writes this now |
| `E2` | `data+8` | `.double 3.0e-39` | `14 11 ff 27 1e ab 02 01` | control: in range, just above the smallest value; `mutos_as` writes this now |
| `O1` | `data+16` | `.float 2.00000000000000000e+38` | `99 76 16 00` | above the largest value: exponent byte 256 → 0 |
| `O2` | `data+20` | `.float -2.00000000000000000e+38` | `99 76 96 00` | the same, negative |
| `O3` | `data+24` | `.double 1.0e+39` | `eb 50 e2 a4 3f 14 3c 02` | 258 → 2 |
| `O4` | `data+32` | `.double 1.0e+50` | `ce 24 f3 2b 76 d8 08 27` | 295 → 39 |
| `U0` | `data+40` | `.double 2.5e-39` | `21 c7 53 ed dc c7 59 00` | just below the smallest value: exponent byte 0 |
| `U1` | `data+48` | `.double 1.0e-39` | `1b 6c a9 8a 7d 39 2e ff` | -1 → 255 |
| `U2` | `data+56` | `.float 5.0e-40` | `7d 39 2e fe` | -2 → 254 |

If the real `ldexp` checks the range, `O1`...`U2` show how instead: a
saturated value (`libc.a`'s `ldexp` has `huge` = `ff ff ff ff ff ff 7f ff`
for its 16-bit overflow case), zero, or an error message from `as`. If
`E1` or `E2` differ from the prediction, the conversion model itself is
wrong near the range limits, which is also a finding.

### `fltsig.s` - what each constant exercises

| Label | Data offset | Text | Step that overflows in `libc.a`'s runtime |
|---|---|---|---|
| `S1` | `data+0` | `.float 1.00000000000000000e-38` | k = 55: `flexp` 5\*\*23 × `exp5` 5\*\*32 - the value fits; the case the compiler can write |
| `S2` | `data+4` | `.double 2.93873587705571877e-39` | k = 56: 5\*\*24 × 5\*\*32 - the value is 2\*\*-128, the format's smallest |
| `S3` | `data+12` | `.float 1.0e+65` | k = 64: the squaring 5\*\*32 × 5\*\*32 itself (the value is above the range anyway) |

No byte predictions: if the real `as` survives the `SIGFPE` (catches or
ignores it), `libc.a`'s runtime would go on with a "zero" `flexp` and
divide by it (`__div0`, `SIGFPE` again), so whatever the real one writes
is new information.

## Running this

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. It assembles
   `fltovf.s` first, then `fltsig.s`.
   - If `as` fails on either, `make` stops there: note the exact message
     (and the exit status, `echo $?` after a manual `as -o fltsig.o
     fltsig.s`), and `rm -f fltsig.o core` - an object left behind by an
     `as` that died is not a golden. If `fltovf.s` fails too, note its
     message the same way.
   - If `as` succeeds on both, both objects are goldens.
2. `make goldens` — **on the modern (Linux) side**, after copying back
   only the objects `as` wrote successfully. Produces `<name>.o.golden`
   and `<name>.o.golden_base64.txt` for each (`../mk_goldenbase64.sh`'s
   naming); a missing or empty object is skipped.

## Reading the result

From `../float_coverage/`:

```
python3 fltmodel.py survivors . ../float_open
python3 libcatof.py atof 2.00000000000000000e+38 1.0e-39    # the predictions, recomputed
```

`survivors` lists every constant `mutos_as` refuses (`O1`...`U2`, and
`S1`...`S3` if `fltsig.s` got a golden) as "NOT MODELLED" with its real
bytes, and still checks `E1`/`E2` and every earlier golden. Compare the
real bytes with the tables above. Then, in `fltconst.c` and
`fltmodel.py`: implement the observed range behaviour (e.g. the wrapped
exponent byte, if confirmed), move the probe that `mutos_as` then
reproduces to `../float_coverage/`, and update `STATUS.md`'s open item 7,
`CLAUDE.md`'s "Next up" and `docs/DEVLOG.md` in the same change. If `as`
died on `fltsig.s`, `mutos_as`'s refusal of those texts is the faithful
behaviour: record the message in `docs/DEVLOG.md` and `STATUS.md`, and
the probe stays here as the evidence.

## Also still open

- **LOGHUGE after a dropped digit**: a text with more than 17 or so
  digits and a decimal exponent far below -39 - `atof` leaves `fcmp`'s
  difference in `fac`, never observed. The value is below the format's
  range anyway (`FLT_RANGE`).
- Not understood, but pinned by real bytes: where the `ff ff ff` that
  the digit loop leaves in `fac` for an all-zero text comes from
  (`libc.a`'s runtime leaves -2\*\*56 there) - see
  `src/mutos_as/fltconst.h`.

## Adding a probe

1. Write `<name>.s` here (see `../float_coverage/fltmul.s` for the
   shape: each constant loaded the way `mutos_c1` emits one, a header
   saying what each constant asks), list it in `Makefile`'s `SRCS` and
   give it a rule in `Makefile.mutos`, and write down the predicted
   bytes here - `../float_coverage/fltmodel.py` and `libcatof.py` compute
   them.
2. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. If the real `as`
   refuses an operand instead, note which one and its exact error text;
   that is the finding.
3. `make goldens` — **on the modern (Linux) side**, after copying
   `<name>.o` back here. Produces its golden and base64 companion
   (`<name>.o.golden`, `<name>.o.golden_base64.txt` - `../mk_goldenbase64.sh`'s
   naming).
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
