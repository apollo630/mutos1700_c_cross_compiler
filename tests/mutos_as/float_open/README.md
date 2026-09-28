# `tests/mutos_as/float_open/` — floating-point probes `mutos_as` still refuses

This directory holds hand-written probes for the real MUTOS 1700 `as`
whose constants the current `mutos_as` refuses on principle. Every
constant in them is plain `atof` syntax, so the real `as` assembles
them. Because `mutos_as` does not, `../run_goldens.sh` would put them in
category 3 ("errors calling mutos_as") permanently, so this directory
is deliberately **not** part of the top-level `make test`. Once
`mutos_as` reproduces a probe's golden, the probe and its golden move to
`../float_coverage/` and become regression goldens. That is what
happened to this directory's first probe, `fltopen.s`, on 2026-09-28:
see `../float_coverage/README.md`.

## Current probe: `fltmode.s` (not yet run on real hardware)

`mutos_as` re-enacts the real assembler's conversion: v7 `atof()` on
the 56-bit double, with `.float` stored as the double's high half (see
`src/mutos_as/fltconst.h`). The one unknown left is how the real double
arithmetic rounds. That covers `dmul` (M), `dadd` (A) and `ddiv` (D),
each of which may truncate, round to nearest (ties to even or away) or
round away from zero. Of those 64 combinations, 48 still reproduce every
golden in `../float_coverage/`; only a truncating `ddiv` is ruled out
(`fltdbl.o.golden`). `mutos_as` refuses any constant whose bytes the 48
disagree on. `fltmode.s` is built to pin the rounding down, and to test
the model where it has not been tested yet:

| Label | Data offset | Text | Purpose |
|---|---|---|---|
| `M1` | `data+0` | `.double 1.47397409833160963e-08` | rounding modes |
| `M2` | `data+8` | `.double 2.97967517326469533e-09` | rounding modes |
| `M3` | `data+16` | `.double 1.45615926012396812e-02` | rounding modes |
| `M4` | `data+24` | `.double 1007211910940938336` | rounding modes (and `atof`'s digit dropping beyond 2\*\*56) |
| `CF` | `data+32` | `.float 2.93572534179687500e+03` | an exact float the compiler can write, refused now |
| `D1` | `data+36` | `.double 0.10000000000000000e+00` | accepted now on the model's prediction alone |
| `NZ` | `data+44` | `.double -0.00000000000000000e+00` | accepted now on the model's prediction alone |
| `Z0` | `data+52` | `.float 0.00000000000000000e+17` | a zero on `atof`'s multiplication path (exponent 0) |
| `Z4` | `data+56` | `.float 0.0e+05` | a zero on the multiplication path (`fl *= 5**4`) |
| `Z25` | `data+60` | `.double 0.0000000000000000000000000` | a zero with 25 fraction digits: `5**25` is rounded |

`M1`…`M4` were chosen by searching about 7,500 compiler-style texts for
the ones that split the 48 combinations best. Together they separate
them into 32 classes; the only thing they leave open is `ddiv`'s tie
rule, and a division by `5**k` never ties.

### Predicted bytes

Each row below is a possible outcome under the 48 combinations. If the
real bytes match none of a constant's rows, the conversion model
itself is wrong, which is also a finding.

- `M1`: `00 00 00 00 11 3a 7d 66` (D away) · `ff ff ff ff 10 3a 7d 66`
  (D nearest) · `fe ff ff ff 10 3a 7d 66` (M not trunc, A trunc, D away)
  · `fd ff ff ff 10 3a 7d 66` (M not trunc, A trunc, D nearest) ·
  `02 00 00 00 11 3a 7d 66` (M trunc, A not trunc, D away) ·
  `01 00 00 00 11 3a 7d 66` (M trunc, A not trunc, D nearest)
- `M2`: `02 00 00 00 00 c3 4c 64` · `01 00 00 00 00 c3 4c 64` ·
  `00 00 00 00 00 c3 4c 64` · `ff ff ff ff ff c2 4c 64`
- `M3`: `02 00 00 00 bf 93 6e 7a` · `00 00 00 00 bf 93 6e 7a` ·
  `fe ff ff ff be 93 6e 7a`
- `M4`: `07 d5 52 58 5e a5 5f bc` · `05 d5 52 58 5e a5 5f bc` ·
  `06 d5 52 58 5e a5 5f bc`
- `CF`: `9b 7b 37 8c` (the exact value) or `9a 7b 37 8c` (one unit
  less: `dmul` truncates and `ddiv` rounds to nearest)
- `D1`: `cd cc cc cc cc cc 4c 7d` (the only prediction)
- `NZ`: `00 00 c5 2e bc a2 b1 00` (the only prediction)
- `Z0`, `Z4`, `Z25`: no prediction. The model covers a zero only on
  `atof`'s division path with an exact `5**k`. For `Z25` a guess would
  be the rounded `5**25`'s mantissa with exponent byte 0.

## Running this

Same two-machine shape as `../float_coverage/`:

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltmode.o`. If the real `as` refuses an operand instead, note which
   one and its exact error text; that is the finding.
2. `make goldens` — **on the modern (Linux) side**, after copying
   `fltmode.o` back here. Produces its golden and base64 companion
   (`<name>.o.golden`, `<name>.o.golden_base64.txt` - `../mk_goldenbase64.sh`'s
   naming).

## Reading the result

From `../float_coverage/`:

```
python3 fltmodel.py survivors . ../float_open
```

This reads every constant of `fltmode.s` out of its golden
alongside the existing goldens and prints the rounding-mode
combinations that fit all of them. It lists `Z0`/`Z4`/`Z25` as "NOT
MODELLED" together with their real bytes, and reports "MODEL
CONTRADICTED" if a modelled constant fits no combination at all. It
exits 1 in any case until `fltconst.c` catches up. Then:

- narrow `fltconst.c`'s `MODES_MUL_ADD`/`MODES_DIV` and `fltmodel.py`'s
  `EXPECTED` to the surviving set. With M and A each down to one mode,
  every constant the model covers becomes determined: `CF` and every
  other refused `%.17e` float included;
- extend the zero rule in both from `Z0`/`Z4`/`Z25`'s bytes;
- move `fltmode.s` and its golden to `../float_coverage/` (add it to
  that directory's `Makefile`/`Makefile.mutos`), and update
  `STATUS.md`'s open item 7, `CLAUDE.md`'s "Next up" and
  `docs/DEVLOG.md` in the same change (Workflow Guideline 6).
