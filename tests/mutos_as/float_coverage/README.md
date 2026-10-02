# `tests/mutos_as/float_coverage/` — closing the open floating-point gaps

Eight hand-written, **new** assembler sources (not reconstructed from a
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
| `fltmul.s` | five constants whose bytes depend on which product `dmul` rounds, zeros with decimal exponent 0 (one digit, a `.double`, negated), a value `atof` gives up on (LOGHUGE), a negative nonzero `.float` | succeeds (2026-09-29) | byte-identical (9 of its 10 constants were refused until 2026-09-29; written in `../float_open/`) |
| `fltovf.s` | results outside the format's exponent range (above the largest value, below the smallest, either sign) while every step before `ldexp` stays in range, and two controls just inside the range | succeeds (2026-09-29) | byte-identical (7 of its 9 constants were refused until 2026-09-29; written in `../float_open/`) |

All eight run in the top-level `make test` (`../run_goldens.sh` in this
directory), and so does `fltmodel.py` (see "The conversion model" below),
which reads every golden here.

## Why these eight, specifically

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
path; it moved here on 2026-09-29. `fltmul.s`, the fourth, asked which
product the real multiplication rounds and moved here the same day.
`fltovf.s`, the fifth, asked what the real assembler writes when only
the result leaves the format's exponent range; it moved here the same
day too. Its partner `../float_open/fltsig.s`, an overflow inside the
conversion, has no golden and stays there: the real assembler aborts
on it (see "Results, fifth round" below).

## Running this

Two steps, split across two machines — same shape as every other golden
corpus in this project (see `tests/mutos_cc/README.md`):

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltaddr.o`, `fltmulti.o`, `fltzero.o`, `fltdbl.o`, `fltopen.o`,
   `fltmode.o`, `fltmul.o`, `fltovf.o`.
2. `make goldens` — **on the modern (Linux) side**, after copying the
   `*.o` back into this directory. Produces `*.o.golden` (a
   verbatim copy) and `*.o.golden_base64.txt` (base64 text) for each —
   the same naming `../kernel_opt/`, `../kernel_nonopt/` and
   `../libc_recon/` already use (`../mk_goldenbase64.sh`).

**Real goldens exist as of 2026-09-28** — all five sources then here
assembled successfully on real MUTOS 1700 hardware and were pushed (each
`<name>.o.golden` plus its `*.o.golden_base64.txt` companion);
`fltmode.o.golden`, `fltmul.o.golden` and `fltovf.o.golden` followed on
2026-09-29. The open questions they were written for are resolved — see
"Results" below — and `mutos_as` reproduces all eight objects byte for
byte.

## Results (2026-09-28)

- **`fltaddr.o.golden` / `fltmulti.o.golden` — byte-identical to current
  `mutos_as`'s own output for the same sources.** This is a clean
  regression confirmation, not just "assembles without error": the
  `lea <reg>,<label>` + `.float` combination this pair was written to
  test matches exactly.
- **`fltzero.o.golden`'s data segment is `bc a2 31 00`** — **exactly**
  the bytes `atof.o`/`ecvt.o` were already known to hold for their zero
  constants (see "Why these eight, specifically" above), now confirmed
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
8086 emulator (now `libcatof.py`, see below; `docs/DEVLOG.md` has the
derivation):

- It gives every **nonzero** constant in this directory byte for byte,
  so the real assembler converts like `libc.a`'s `atof`. It gives a
  clean zero for every zero, though: `libc.a`'s `dmul`/`ddiv` clear the
  whole accumulator `fac` for a zero operand. Changed to clear only
  `fac`'s exponent byte, the runtime reproduces all seven real zeros
  with k >= 1 (`fac` still holds `flexp`, the previous result) — but not
  `Z0`, where no multiplication built `flexp` and `fac` holds something
  the digit loop left there.
- `libc.a`'s `dmul` does not form the exact product: its partial-product
  routine takes one term, a0\*b2, from the wrong word (b1). No constant
  here needed a product where that matters, so whether the real
  assembler's `dmul` does the same was left to the next round.

## Results, fourth round: `fltmul.o.golden` (2026-09-29)

| Label | Constant | Real bytes | Reading |
|---|---|---|---|
| `P1` | `.float 1.18059162071741130e+21` | `ff ff 7f c6` | `libc.a`'s product (exact product: `00 00 00 c7`) |
| `P2` | `.float 1.26765060022822940e+30` | `ff ff 7f e4` | `libc.a`'s product (exact product: `00 00 00 e5`) |
| `P3` | `.double 1.23456789012345678e+25` | `4d ea 27 82 c9 64 23 d4` | `libc.a`'s product (exact product: `a6 ...`) |
| `P4` | `.double 1.23456789012345678e-33` | `d5 c8 7d e1 b5 20 4d 13` | `libc.a`'s product (exact product: `cb ...`) |
| `Z54` | `.double 0.00000000000000000e-37` | `45 4e a6 40 3c 0c 27 00` | 5\*\*54 as `libc.a`'s product builds it |
| `K1` | `.float 0e0` | `ff ff ff 00` | like `Z0` with one digit instead of 18 |
| `K2` | `.double 0.00000000000000000e+17` | `00 00 00 00 ff ff ff 00` | `Z0`'s high half, low half 0 |
| `K3` | `.float -0.00000000000000000e+17` | `ff ff 7f 00` | `fneg` flips bit 7 of byte 6 |
| `LH` | `.double 1e-41` | `00 00 00 00 00 00 00 00` | LOGHUGE: `fl` = 1.0 left in `fac`, exponent byte 0 |
| `NG` | `.float -1.50000000000000000e+00` | `00 00 c0 81` | as predicted |

All five constants the product decides have `libc.a`'s: the real
assembler multiplies like `libc.a`'s `dmath.o`. The zeros with decimal
exponent 0 have the same bytes whatever the digit count (1 or 18) and
whatever constant came before, so the digit loop's leftover in `fac` is
a fixed `00 00 00 00 ff ff ff` for an all-zero text - not what
`libc.a`'s runtime leaves there (-2\*\*56), so one of the real runtime's
operations on zeros differs in a way these bytes do not show. `LH` is
the `fac` rule once more: after LOGHUGE (`fl = 0`, exponent 0) the
multiplication by `flexp` = 1.0 finds a zero and keeps `fac`, which the
last `fadd` of the digit loop left holding `fl` = 1.0 - whose stored
mantissa bits are all 0.

## Results, fifth round: `fltovf.o.golden` and `fltsig.s` (2026-09-29)

| Label | Constant | Real bytes | Reading |
|---|---|---|---|
| `E1` | `.double 1.70141183460469232e+38` | `c9 ff ff ff ff ff 7f ff` | control: 2\*\*127 in the compiler's form, in range |
| `E2` | `.double 3.0e-39` | `14 11 ff 27 1e ab 02 01` | control: just above the smallest value |
| `O1` | `.float 2.00000000000000000e+38` | `99 76 16 00` | exponent byte 256 → 0 |
| `O2` | `.float -2.00000000000000000e+38` | `99 76 96 00` | the same, negative |
| `O3` | `.double 1.0e+39` | `eb 50 e2 a4 3f 14 3c 02` | 258 → 2 |
| `O4` | `.double 1.0e+50` | `ce 24 f3 2b 76 d8 08 27` | 295 → 39 |
| `U0` | `.double 2.5e-39` | `21 c7 53 ed dc c7 59 00` | exponent byte 0 |
| `U1` | `.double 1.0e-39` | `1b 6c a9 8a 7d 39 2e ff` | -1 → 255 |
| `U2` | `.float 5.0e-40` | `7d 39 2e fe` | -2 → 254 |

Every byte is what `libcatof.py` predicted before the run. `atof`'s
last step is `ldexp(fl, exponent)` (10\*\*k = 5\*\*k × 2\*\*k), and
`libc.a`'s `ldexp` adds the exponent to `fac`'s exponent byte as a
16-bit sum, checks only for a signed 16-bit overflow and stores the low
byte - so the exponent byte wraps, and the real assembler writes the
result without a diagnostic. The mantissa is always the value's own.

`fltsig.s` (still in `../float_open/`) asked the other question: an
overflow **inside** the conversion, `flexp` = 5\*\*k from k = 55 on
(the compiler's `%.17e` text from `e-38` down, a value that may fit).
The real assembler refuses it: `***ERROR*** floating point over/under
flow- assembly aborted`, twice for line 41 (its first constant), exit
status 4, no object. `libc.a`'s runtime raises `SIGFPE` there (`__ovfl`
in `dmul`, then `__div0` in the division by the zero left behind - two
signals, two messages); the real `as` evidently catches the signal and
gives up. The full output is in `../float_open/README.md`.

## The conversion model (implemented 2026-09-28, settled 2026-09-29)

`src/mutos_as/fltconst.c` re-enacts the real assembler's conversion —
v7 `atof()` on the 56-bit double, `.float` = the double's high four
bytes — rather than encoding the exact decimal value (full description:
`src/mutos_as/fltconst.h`): `dadd` nearest-even, `ddiv` nearest (either
tie rule, which never matters), `dmul` nearest-even of `libc.a`'s
product; a zero gives what `fac` held - `flexp`'s mantissa for a nonzero
decimal exponent, the digit loop's leftover for exponent 0 - with
exponent byte 0. Every constant it covers is determined:

- **Accepted**: every text whose arithmetic stays inside the format's
  exponent range - every `%.17e` constant `mutos_c1` writes, from
  `e-37` (`e-38` when `atof` drops the 18th digit) up to the format's
  largest value, every zero whose `flexp` stays in range, and a result
  that only `ldexp` takes out of the range, with the exponent byte
  wrapped as the real one does (`fltovf.o.golden`: `.float
  2.00000000000000000e+38` → `99 76 16 00`).
- **Refused, as the real assembler refuses it** (`FLT_RANGE`): a step of
  the arithmetic that overflows - `flexp` = 5\*\*k for k >= 55 (e.g.
  `.float 1.00000000000000000e-38`, whose value would fit), or the
  product `fl` × `flexp` (e.g. `.double 9.9e+54`). The real assembler
  aborts there (`fltsig.s`); `mutos_as` reports the constant and writes
  no object either (exit status 1, the real one 4).
- **Refused, unknown** (`FLT_UNKNOWN`): a text `atof` gives up on
  (LOGHUGE) after dropping its last digit, where `fac` holds a
  difference no real constant shows, and a text of more than 100,000
  digits. `FLT_ROUNDING` remains only for a tie in the division, which
  cannot occur.

`fltmodel.py` is an independent Python version of the same model.
`fltmodel.py survivors` reads every `<name>.s` here that has a golden,
takes each constant's real bytes from the golden's data segment, and
reports which combinations reproduce all of them — today 2 of 80 (the
64 rounding-mode combinations plus `libc.a`'s product for `dmul`) — and
fails if that set differs from the one `fltconst.c` uses, or if a golden
holds a constant the model does not cover. `fltmodel.py check
<fltconst_test>` compares `fltconst.c` (via `src/mutos_as/fltconst_test`)
with the model on fixed edge cases and seeded random texts. Both run in
`make test`. A new golden dropped in here narrows the set automatically;
`survivors` then says so, and `fltconst.c`'s `MODES_MUL`/`MODES_ADD`/
`MODES_DIV` (and `fltmodel.py`'s `EXPECTED`) get updated to match.

## `libcatof.py` — `libc.a`'s own `atof`, run

A second oracle, made of the real toolchain's machine code instead of a
model: `libc.a`'s `atof.o` and the software floating-point runtime it
calls (`stacks.o`, `stkmath.o`, `doubles.o`, `dmath.o`, `convert.o`, ...),
decoded from `tests/mutos1700_libc/`, linked with the real `crt0.o` by
`mutos_ld`, and `_atof(text)` called under Unicorn's 8086 mode. By
default `dmath.o`'s `zero` routine is patched to clear only `fac`'s
exponent byte, as the real assembler's runtime does ("assembler
zeros"); `--libc` runs it unmodified. It reproduces every real constant
here except the all-zero texts with decimal exponent 0, which it reports
separately ("not emulated": `libc.a`'s digit loop leaves -2\*\*56 in
`fac`, the real one `ff ff ff`).

Needs the Python module `unicorn` (`pip install unicorn`) and a built
`mutos_as`/`mutos_ld`; not part of `make test` - run `make
check-libcatof` from the repository root, or directly:

```
python3 libcatof.py atof 0.1 1.26765060022822940e+30   # the bytes, double and float
python3 libcatof.py goldens [DIR ...]                  # every golden constant
python3 libcatof.py check ../../../src/mutos_as/fltconst_test 3000
python3 libcatof.py ops 2000                           # dmul/ddiv/dadd vs fltmodel.py
python3 libcatof.py ecvt ../../../src/mutos_cc/fltdec_test 3000
python3 libcatof.py fecvt ../../../src/mutos_cc/fltdec_test 3000
```

`ecvt` checks the compiler side: the real `c1` writes a floating constant
with `printf("%.17e")` - `fltpr.o`'s `_pscien()` over `libc.a`'s own
`ecvt.o` (v7's `cvt()`, 18 digits) - of `libc.a`'s `atof()`, so the image
also links `ecvt.o` and `modf.o`, and the subcommand runs `_ecvt` on
`_atof`'s result (the unmodified runtime, as the compiler is an ordinary
program) for C literals of every shape, comparing text and size (`.float`
when the double's low four bytes are zero) with `mutos_c1`'s model,
`src/mutos_cc/c1_fltdec.c`, through `src/mutos_cc/fltdec_test`. That model
reproduces `tests/mutos_cc/fltprobe/p4_const.s.golden`'s texts (`0.1` ->
`1.00000000000000000e-01`, `1e30` -> `1.00000000000000005e+30`) and, at
the last run, 28,188 random literals with no difference.

`fecvt` does the same for a `float` variable's initializer: v7's `c1`
writes it with `doinit()`'s `sfval = fval`, the double stored as a float
(`fldd` / `fstsp` on `libc.a`'s runtime) and printed - so the subcommand
runs `fstsp`, `flds` and `fstdp` on `_atof`'s result, then `_ecvt`, and
compares with `fltdec_test -f` (`c1_fltdec.c`'s `fdec_render_single()`).
`fstsp` truncates - it stores the double's high four bytes, which the
subcommand also checks for every literal: `fltprobe/p9_init.s.golden`'s
`float gy = 0.1;` -> `.float 9.99999940395355225e-02` (2026-10-02: 5,649
literals compared, no difference).

`check` sends edge cases, seeded random texts and targeted ones
(`%.17e` of exact floats and doubles over the whole range, zeros,
LOGHUGE texts, results past both ends of the range) through
`fltconst_test` and compares every constant it accepts with the
emulation. `__ovfl` and `__div0`, where the runtime would send itself
`SIGFPE`, stop the emulation: `atof` then prints `SIGFPE` instead of
bytes, and `check` requires that every text `fltconst_test` refuses as
`RANGE` raises it and that no accepted one does. A build with the exact
product instead of `libc.a`'s fails `check` (749 of 7,588 compared texts
differ), and so does one that refuses the wrapped results instead of
writing them (57 `RANGE` texts without `SIGFPE`). The linked
image is checked before use: `dmath.o`'s `zero` routine, its entry
points and `pmuld`'s misplaced load must be where the tool expects them.
