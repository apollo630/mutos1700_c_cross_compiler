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

## No current probe

`fltmul.s` settled the last open part of the conversion model: the real
`dmul` rounds the product `libc.a`'s own `dmath.o` forms, one partial
product taken from the wrong word, not the exact one - and every
constant the model covers is now determined (`fltconst.c` refuses none
for rounding). What `mutos_as` still refuses, and why:

- **Out of range at some step** (`FLT_RANGE`): a value, or `atof`'s
  `flexp` = 5\*\*k, outside the format's exponent range - k >= 55, i.e.
  `%.17e` text from `e-38` down (from `e-39` when `atof` drops the 18th
  digit), e.g. `.float 1.00000000000000000e-38`, although the value fits. `libc.a`'s runtime answers an overflow with `__ovfl`,
  which sends the process `SIGFPE`; what the real assembler does then
  (dies, or writes something) is not observed.
- **LOGHUGE after a dropped digit**: a text with more than 17 or so
  digits and a decimal exponent far below -39 - `atof` leaves `fcmp`'s
  difference in `fac`, never observed. The value is below the format's
  range anyway.

A probe for the first would be the natural next one, if the compiler's
smallest floats matter. Not understood, but pinned by real bytes: where
the `ff ff ff` that the digit loop leaves in `fac` for an all-zero text
comes from (`libc.a`'s runtime leaves -2\*\*56 there) - see
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
