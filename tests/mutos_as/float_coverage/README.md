# `tests/mutos_as/float_coverage/` — closing the open floating-point gaps

Six hand-written, **new** assembler sources (not reconstructed from a
real object, unlike `../libc_recon/`) targeting the specific floating-point
gaps this project's own docs already flag as open, rather than general
"more float coverage":

| File | Tests | Real `as` (2026-09-28/29) | Current `mutos_as` |
|---|---|---|---|
| `fltaddr.s` | `lea <reg>,<label>` + `call` addressing a real `.float` constant, in one whole object | succeeds | byte-identical (regression golden) |
| `fltmulti.s` | two `.float` constants, `.text`/`.data` switch + `EVEN`-padding interaction, chained runtime calls | succeeds | byte-identical (regression golden) |
| `fltzero.s` | `.float 0.00000000000000000e+00` | succeeds | byte-identical (was refused, `FLT_ZERO`, until 2026-09-28) |
| `fltdbl.s` | `.double` (8-byte), a value needing >24 mantissa bits | succeeds | byte-identical (was refused, `.double` not implemented, until 2026-09-28) |
| `fltopen.s` | `.float 0.0`, `.float -0.00000000000000000e+00`, `.double 0.00000000000000000e+00`, `.float 0.10000000000000000e+00` | succeeds | byte-identical (was refused until 2026-09-28; written in `../float_open/`) |
| `fltmode.s` | four compiler-style `.double` constants splitting the rounding modes, an 18-digit exact `.float`, `.double 0.1`, a negative zero `.double`, zeros with decimal exponent 0, +4 and -25 | succeeds (2026-09-29) | byte-identical (8 of its 10 constants were refused until 2026-09-29; written in `../float_open/`) |

All six run in the top-level `make test` (`../run_goldens.sh` in this
directory), and so does `fltmodel.py` (see "The conversion model" below),
which reads every golden here.

## Why these six, specifically

`tests/mutos_as/libc_recon/floatdat.s` covers `.float` constants but has
no code around them (its own header: "not a whole object — no real
object is made of `.float` constants alone"), and only checks isolated
4-byte ranges via `check_floatdat.sh`, never a whole `.o.golden` diff.
`ldexp.s` covers `lea <reg>,<label>` + code but never combines it with a
`.float` constant (its one float-ish value, `huge`, is emitted as raw
`.word`s). `fltaddr.s` and `fltmulti.s` close that combination gap with
values `mutos_as` could already encode exactly (at the time,
`src/mutos_as/fltconst.h` only accepted a value exactly representable in
24 mantissa bits — `2.5`, `3.140625`, `2.71875` are all exact terminating binary fractions,
picked for that reason, not because they're numerically interesting).

`fltzero.s` and `fltdbl.s` are different in kind: they were written as
**probes for open questions**, not regression goldens. When they were
written, `mutos_as` refused both outright —

- a zero `.float`, because `fltconst.h` said plainly: *"a zero's real
  bytes are not a plain zero"* — known only from the two real compiled-C
  objects that happen to hold one (`atof.o`/`ecvt.o`, both `bc a2 31 00`),
  whose real assembler-input spelling is unknown (see
  `../libc_recon/README.md`'s "Why only these"), so `mutos_as` refuses to
  guess.
- `.double` at all — `src/mutos_as/assemble.c` errored out before even
  parsing the operand, because the one known real 8-byte constant
  (`ecvt.o`'s `.03`) can't by itself pin down how a value needing more
  than a float's 24 bits is converted.

Both gaps share the same root cause: no real object with a **known
hand-written source** exercises them, only compiled-C objects whose
source is lost. `fltzero.s` and `fltdbl.s` are exactly that missing
known source — real hardware assembling them for the first time
produces ground truth that `atof.o`/`ecvt.o` alone cannot.

`fltopen.s` is the second round, written in `../float_open/` (the
directory for probes `mutos_as` still refuses) for the four spellings
the first round left open, and moved here with its golden once
`mutos_as` reproduced it. `fltmode.s` is the third, written there the
same way for the one unknown the second round left - how the real
double arithmetic rounds - and for zeros on `atof`'s multiplication
path; it moved here on 2026-09-29.

## Running this

Two steps, split across two machines — same shape as every other golden
corpus in this project (see `tests/mutos_cc/README.md`):

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltaddr.o`, `fltmulti.o`, `fltzero.o`, `fltdbl.o`, `fltopen.o`,
   `fltmode.o`.
2. `make goldens` — **on the modern (Linux) side**, after copying the
   `*.o` back into this directory. Produces `*.o.golden` (a
   verbatim copy) and `*.o.golden_base64.txt` (base64 text) for each —
   the same naming `../kernel_opt/`, `../kernel_nonopt/` and
   `../libc_recon/` already use (`../mk_goldenbase64.sh`).

**Real goldens exist as of 2026-09-28** — all five sources then here
assembled successfully on real MUTOS 1700 hardware and were pushed (each
`<name>.o.golden` plus its `*.o.golden_base64.txt` companion);
`fltmode.o.golden` followed on 2026-09-29. The open questions they were
written for are resolved — see "Results" below — and `mutos_as`
reproduces all six objects byte for byte.

## Results (2026-09-28)

- **`fltaddr.o.golden` / `fltmulti.o.golden` — byte-identical to current
  `mutos_as`'s own output for the same sources.** This is a clean
  regression confirmation, not just "assembles without error": the
  `lea <reg>,<label>` + `.float` combination this pair was written to
  test matches exactly.
- **`fltzero.o.golden`'s data segment is `bc a2 31 00`** — **exactly**
  the bytes `atof.o`/`ecvt.o` were already known to hold for their zero
  constants (see "Why these six, specifically" above), now confirmed
  from a known, hand-written `.float 0.0` source rather than an
  unreproducible compiled-C object. `mutos_as`'s `FLT_ZERO` refusal in
  `src/mutos_as/fltconst.c` can be replaced with this confirmed encoding.
- **`fltdbl.o.golden`'s data segment is `00 00 80 00 00 00 00 81`** —
  this matches the documented double format exactly at the bit position
  predicted for this source's chosen value (`1 + 2**-32`): exponent byte
  `0x81` (excess-128 → `2**1`, correct for a value just above 1.0), sign
  bit 0, and a single mantissa bit set 32 bits in. This confirms
  `.double` follows the same `0.1mmm * 2**(e-128)` scheme as `.float`,
  just with 55 mantissa bits instead of 23, and gives a real worked
  example to implement `.double` support from in
  `src/mutos_as/fltconst.c`/`assemble.c`.

## Implemented (2026-09-28)

`src/mutos_as/fltconst.c`/`assemble.c` now encode both, from these two
goldens; `../run_goldens.sh` here: 4/4 byte-identical, part of the
top-level `make test`. What is accepted, and why no more:

- **Zero `.float`**: `bc a2 31 00`, but only for the spelling
  `fltzero.s` uses — the `%.17e` text the real compiler writes (no minus
  sign, exactly 17 digits after the `.`, exponent 0). The mantissa bits
  are those of 5\*\*17, which looks like v7 `atof()` scaling the text's 17
  fraction digits by `flexp` = 5\*\*17; under that reading `.float 0.0`
  (scaled by 5) would come out differently, so every other zero
  spelling stays an explicit error. `../libc_recon/check_floatdat.sh` now
  also compares this zero with all six zero constants compiled into
  `atof.o`/`ecvt.o`: identical.
- **`.double`**: 8 bytes, 56 significant bits, exactly representable
  values only (as for `.float`: the real assembler's rounding of an
  inexact value is still unconfirmed — `ecvt.o`'s `0.03` is one rounded
  sample with a lost source spelling). Bare numbers as in `fltdbl.s`, or
  the manual's `0d` prefix with a `d`/`D` exponent. A zero `.double` is
  refused: no real zero `.double` bytes exist anywhere (`libc.a` has
  none).

Full derivation and verification in `docs/DEVLOG.md` ("`mutos_as`: zero
`.float` and `.double` implemented").

(Both restrictions above were superseded the same day by `fltopen.s`'s
results — see the next two sections.)

## Results, second round: `fltopen.o.golden` (2026-09-28)

| Constant | Real bytes | Reading |
|---|---|---|
| `.float 0.0` | `00 00 20 00` | exponent byte 0, mantissa of 5\*\*1 |
| `.float -0.00000000000000000e+00` | `bc a2 b1 00` | the confirmed zero with the sign bit |
| `.double 0.00000000000000000e+00` | `00 00 c5 2e bc a2 31 00` | mantissa of 5\*\*17 in 56 bits; its high half is `fltzero.o`'s `bc a2 31 00` |
| `.float 0.10000000000000000e+00` | `cc cc 4c 7d` | 0.1 **truncated** to 24 bits — correct rounding gives `cd` |

The first and third are exactly the bytes `docs/DEVLOG.md` had predicted
before the run from v7 `atof()`'s algorithm, where a zero dividend keeps
the divisor `flexp` = 5\*\*k's mantissa (k = the number of fraction
digits here). The fourth shows that a `.float` is the high half of the
double conversion, truncated.

## Results, third round: `fltmode.o.golden` (2026-09-29)

| Label | Constant | Real bytes | Reading |
|---|---|---|---|
| `M1` | `.double 1.47397409833160963e-08` | `ff ff ff ff 10 3a 7d 66` | these four leave 2 of the 64 rounding-mode combinations |
| `M2` | `.double 2.97967517326469533e-09` | `ff ff ff ff ff c2 4c 64` | |
| `M3` | `.double 1.45615926012396812e-02` | `fe ff ff ff be 93 6e 7a` | |
| `M4` | `.double 1007211910940938336` | `05 d5 52 58 5e a5 5f bc` | |
| `CF` | `.float 2.93572534179687500e+03` | `9b 7b 37 8c` | the exact value: `dmul` does not truncate |
| `D1` | `.double 0.10000000000000000e+00` | `cd cc cc cc cc cc 4c 7d` | as predicted |
| `NZ` | `.double -0.00000000000000000e+00` | `00 00 c5 2e bc a2 b1 00` | as predicted |
| `Z0` | `.float 0.00000000000000000e+17` | `ff ff ff 00` | decimal exponent 0: **not** the mantissa of `flexp` = 1.0 |
| `Z4` | `.float 0.0e+05` | `00 40 1c 00` | mantissa of 5\*\*4, exponent byte 0 (multiplication path) |
| `Z25` | `.double 0.0000000000000000000000000` | `85 14 40 61 51 59 04 00` | mantissa of the rounded 5\*\*25 |

`dmul` and `dadd` round to nearest, ties to even; `ddiv` to nearest (its
tie rule never matters). Two further findings came from running
`libc.a`'s own `atof.o` on `libc.a`'s own floating-point runtime under an
8086 emulator (a scratch harness, not part of `make test`; see
`docs/DEVLOG.md`):

- It gives every **nonzero** constant in this directory byte for byte,
  so the real assembler converts like `libc.a`'s `atof`. It gives a
  clean zero for every zero, though: `libc.a`'s `dmul`/`ddiv` clear the
  whole accumulator `fac` for a zero operand. Changed to clear only
  `fac`'s exponent byte, the runtime reproduces all seven real zeros
  with k >= 1 (`fac` still holds `flexp`, the previous result) — but not
  `Z0`, where no multiplication built `flexp` and `fac` holds something
  the digit loop left there.
- `libc.a`'s `dmul` does not form the exact product: its partial-product
  routine takes one term, a0\*b2, from the wrong word (b1). No real
  constant so far has needed a product where that matters, so it is an
  open question whether the real assembler's `dmul` does the same.

## The conversion model (implemented 2026-09-28, narrowed 2026-09-29)

`src/mutos_as/fltconst.c` re-enacts the real assembler's conversion —
v7 `atof()` on the 56-bit double, `.float` = the double's high four
bytes — rather than encoding the exact decimal value (full description:
`src/mutos_as/fltconst.h`). It runs the conversion under every
combination of operation behaviours that still reproduces all the
goldens here and accepts a constant only if they all agree:
`dadd` nearest-even; `ddiv` nearest-even or nearest-away; `dmul`
nearest-even of the exact product or of `libc.a`'s.

- **Accepted since 2026-09-29**: every constant whose conversion needs
  no product the two `dmul` candidates disagree on — among them every
  `%.17e` constant `mutos_c1` writes with an exponent from `e-32` to
  `e+19` (the 6% of exact floats refused before, e.g.
  `2.93572534179687500e+03`, included), and every zero with a nonzero
  decimal exponent (`0.0`, `0e5`, 25 or more fraction digits), except
  where `atof` gives up (a decimal exponent below -39 minus the digit
  count, e.g. `0e-41`).
- **Refused** (`FLT_ROUNDING`): constants whose bytes the `dmul`
  question decides — possible only for a positive decimal exponent of 4
  or more, counting digits `atof` drops past 2\*\*56 (`%.17e` text from
  `e+20` up, e.g. `.float
  1.26765060022822940e+30` = 2\*\*100: `00 00 00 e5` from the exact
  product, `ff ff 7f e4` from `libc.a`'s) and for a decimal exponent
  of -50 to -54 (`%.17e` text from `e-33` down), where `flexp` itself
  differs. Zeros with decimal exponent 0 are accepted only in `Z0`'s
  shape (`FLT_ZERO` otherwise).

`fltmodel.py` is an independent Python version of the same model.
`fltmodel.py survivors` reads every `<name>.s` here that has a golden,
takes each constant's real bytes from the golden's data segment, and
reports which combinations reproduce all of them — today 4 of 80 (the
64 rounding-mode combinations plus `libc.a`'s product for `dmul`) — and
fails if that set differs from the one `fltconst.c` uses, or if a golden
holds a constant the model does not cover. `fltmodel.py check
<fltconst_test>` compares `fltconst.c` (via `src/mutos_as/fltconst_test`)
with the model on fixed edge cases and seeded random texts. Both run in
`make test`. A new golden dropped in here narrows the set automatically;
`survivors` then says so, and `fltconst.c`'s `MODES_MUL`/`MODES_ADD`/
`MODES_DIV` (and `fltmodel.py`'s `EXPECTED`) get updated to match.
