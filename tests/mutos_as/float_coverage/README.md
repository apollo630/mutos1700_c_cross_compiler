# `tests/mutos_as/float_coverage/` — closing the open floating-point gaps

Five hand-written, **new** assembler sources (not reconstructed from a
real object, unlike `../libc_recon/`) targeting the specific floating-point
gaps this project's own docs already flag as open, rather than general
"more float coverage":

| File | Tests | Real `as` (2026-09-28) | Current `mutos_as` |
|---|---|---|---|
| `fltaddr.s` | `lea <reg>,<label>` + `call` addressing a real `.float` constant, in one whole object | succeeds | byte-identical (regression golden) |
| `fltmulti.s` | two `.float` constants, `.text`/`.data` switch + `EVEN`-padding interaction, chained runtime calls | succeeds | byte-identical (regression golden) |
| `fltzero.s` | `.float 0.00000000000000000e+00` | succeeds | byte-identical (was refused, `FLT_ZERO`, until 2026-09-28) |
| `fltdbl.s` | `.double` (8-byte), a value needing >24 mantissa bits | succeeds | byte-identical (was refused, `.double` not implemented, until 2026-09-28) |
| `fltopen.s` | `.float 0.0`, `.float -0.00000000000000000e+00`, `.double 0.00000000000000000e+00`, `.float 0.10000000000000000e+00` | succeeds | byte-identical (was refused until 2026-09-28; written in `../float_open/`) |

All five run in the top-level `make test` (`../run_goldens.sh` in this
directory), and so does `fltmodel.py` (see "The conversion model" below),
which reads every golden here.

## Why these five, specifically

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
`mutos_as` reproduced it.

## Running this

Two steps, split across two machines — same shape as every other golden
corpus in this project (see `tests/mutos_cc/README.md`):

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltaddr.o`, `fltmulti.o`, `fltzero.o`, `fltdbl.o`, `fltopen.o`.
2. `make goldens` — **on the modern (Linux) side**, after copying the
   `*.o` back into this directory. Produces `*.o.golden` (a
   verbatim copy) and `*.o.golden_base64.txt` (base64 text) for each —
   the same naming `../kernel_opt/`, `../kernel_nonopt/` and
   `../libc_recon/` already use (`../mk_goldenbase64.sh`).

**Real goldens exist as of 2026-09-28** — all five sources assembled
successfully on real MUTOS 1700 hardware and were pushed (each
`<name>.o.golden` plus its `*.o.golden_base64.txt` companion). The open
questions they were written for are resolved — see "Results" below —
and `mutos_as` reproduces all five objects byte for byte.

## Results (2026-09-28)

- **`fltaddr.o.golden` / `fltmulti.o.golden` — byte-identical to current
  `mutos_as`'s own output for the same sources.** This is a clean
  regression confirmation, not just "assembles without error": the
  `lea <reg>,<label>` + `.float` combination this pair was written to
  test matches exactly.
- **`fltzero.o.golden`'s data segment is `bc a2 31 00`** — **exactly**
  the bytes `atof.o`/`ecvt.o` were already known to hold for their zero
  constants (see "Why these four, specifically" above), now confirmed
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

## The conversion model (implemented 2026-09-28)

`src/mutos_as/fltconst.c` now re-enacts the real assembler's conversion
— v7 `atof()` on the 56-bit double, `.float` = the double's high four
bytes — rather than encoding the exact decimal value (full description:
`src/mutos_as/fltconst.h`). The one unknown left is how the real double
arithmetic rounds (`dmul`, `dadd`, `ddiv`): `mutos_as` runs the
conversion under every rounding-mode combination that still reproduces
all the goldens here, and accepts a constant only if they all agree.

That has two consequences beyond these four constants:

- **More is accepted**: inexact values whose stored bytes the unknown
  rounding cannot reach (`.float 0.1` → `cc cc 4c 7d`, `.double 0.1` →
  `cd cc cc cc cc cc 4c 7d`), and a zero of either size and sign with 1
  to 24 more fraction digits than its exponent.
- **Some exact floats are now refused** (`FLT_ROUNDING`), including
  about 6% of the `%.17e` constants `mutos_c1` can write, e.g.
  `2.93572534179687500e+03`: their 18th digit makes `atof`'s `10*fl`
  inexact, and under a truncating `dmul` the real assembler would store
  one unit less in the last place. The previous exact-value encoder
  wrote the exact value for them without knowing that. No `.s` in this
  repository uses such a constant (every one assembles to the same
  object as before).

`fltmodel.py` is an independent Python version of the same model.
`fltmodel.py survivors` reads every `<name>.s` here that has a golden,
takes each constant's real bytes from the golden's data segment, and
reports which rounding-mode combinations reproduce all of them — today
48 of 64 (`dmul`, `dadd` anything; `ddiv` anything but truncation,
which `fltdbl.o.golden` rules out) — and fails if that set differs from
the one `fltconst.c` uses. `fltmodel.py check <fltconst_test>` compares
`fltconst.c` (via `src/mutos_as/fltconst_test`) with the model on fixed
edge cases and seeded random texts. Both run in `make test`. A new
golden dropped in here narrows the set automatically; `survivors` then
says so, and `fltconst.c`'s `MODES_MUL_ADD`/`MODES_DIV` (and
`fltmodel.py`'s `EXPECTED`) get updated to match. The next probe for
exactly that is in `../float_open/`.
