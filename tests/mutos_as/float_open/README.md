# `tests/mutos_as/float_open/` — floating-point probes `mutos_as` still refuses

This directory holds hand-written probes for the real MUTOS 1700 `as`
whose constants the current `mutos_as` refuses on principle. Every
constant in them is plain `atof` syntax, so the real `as` assembles
them. Because `mutos_as` does not, `../run_goldens.sh` would put them in
category 3 ("errors calling mutos_as") permanently, so this directory
is deliberately **not** part of the top-level `make test`. Once
`mutos_as` reproduces a probe's golden, the probe and its golden move to
`../float_coverage/` and become regression goldens. That is what
happened to this directory's first two probes, `fltopen.s` (2026-09-28)
and `fltmode.s` (2026-09-29): see `../float_coverage/README.md`.

## Current probe: `fltmul.s` (not yet run on real hardware)

`fltmode.s` settled the rounding: `dmul`, `dadd` and `ddiv` all round to
nearest, ties to even (`ddiv`'s tie rule never matters). What is left is
**which product `dmul` rounds**. `libc.a`'s own `dmath.o` does not form
the exact one: its partial-product routine loads the mantissa word b1
where b2 was meant, so it computes a\*b + (a0\*b1 - a0\*b2) \* 2\*\*32
(a = the operand `fmuld` addresses, b = the one on the floating-point
stack, words of 16 bits from the bottom). `libc.a`'s `atof` run on that
runtime gives every nonzero real constant so far, because none of them
needed a product where the two differ. They do differ in `atof`'s
`fl *= flexp` for a positive decimal exponent of 4 or more (`%.17e` text
from `e+20` up) and inside `flexp` for k = 50..54 (`e-33` down);
`mutos_as` refuses such constants (`FLT_ROUNDING`) where the stored bytes
differ. `fltmul.s` asks five times, and also asks the zero questions
`fltmode.s`'s `Z0` (`.float 0.00000000000000000e+17` → `ff ff ff 00`)
raised:

| Label | Data offset | Text | Purpose |
|---|---|---|---|
| `P1` | `data+0` | `.float 1.18059162071741130e+21` | 2\*\*70 in the compiler's form: which product |
| `P2` | `data+4` | `.float 1.26765060022822940e+30` | 2\*\*100: which product |
| `P3` | `data+8` | `.double 1.23456789012345678e+25` | which product (`fl *= 5**8`) |
| `P4` | `data+16` | `.double 1.23456789012345678e-33` | which product (inside `flexp` = 5\*\*50) |
| `Z54` | `data+24` | `.double 0.00000000000000000e-37` | a zero: its bytes are `flexp` = 5\*\*54 itself |
| `K1` | `data+32` | `.float 0e0` | decimal exponent 0 with one digit instead of 18 |
| `K2` | `data+36` | `.double 0.00000000000000000e+17` | `Z0`'s text as a `.double`: its low half |
| `K3` | `data+44` | `.float -0.00000000000000000e+17` | `Z0` negated: `fneg` on a set bit 7 |
| `LH` | `data+48` | `.double 1e-41` | `atof` gives up (nd - k < -39): what the digit loop left in `fac` |
| `NG` | `data+56` | `.float -1.50000000000000000e+00` | a negative nonzero constant, none seen yet |

### Predicted bytes

- **Exact product** (`M=ne`): `P1` `00 00 00 c7` · `P2` `00 00 00 e5` ·
  `P3` `a6 ea 27 82 c9 64 23 d4` · `P4` `cb c8 7d e1 b5 20 4d 13` ·
  `Z54` `6d 4e a6 40 3c 0c 27 00`
- **`libc.a`'s product** (`M=libc`): `P1` `ff ff 7f c6` · `P2`
  `ff ff 7f e4` · `P3` `4d ea 27 82 c9 64 23 d4` · `P4`
  `d5 c8 7d e1 b5 20 4d 13` · `Z54` `45 4e a6 40 3c 0c 27 00`
- `NG`: `00 00 c0 81` under both.
- `K1`, `K2`, `K3`, `LH`: no prediction. If `fac`'s leftover does not
  depend on the digit count, `K1` is `ff ff ff 00` like `Z0`; `K2`'s
  high half is `Z0`'s `ff ff ff 00` if the leftover does not depend on
  the constant before it either; `K3` is `ff ff 7f 00` if `fneg` flips
  bit 7 (as `libc.a`'s `stkmath.o` does) and `ff ff ff 00` if it sets it.

If the real bytes of `P1`…`Z54` match neither row, the product model is
wrong, which is also a finding. A simulated golden built from either row
decodes back to exactly that `dmul` candidate with
`fltmodel.py survivors . ../float_open`.

## Running this

Same two-machine shape as `../float_coverage/`:

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltmul.o`. If the real `as` refuses an operand instead, note which
   one and its exact error text; that is the finding.
2. `make goldens` — **on the modern (Linux) side**, after copying
   `fltmul.o` back here. Produces its golden and base64 companion
   (`<name>.o.golden`, `<name>.o.golden_base64.txt` - `../mk_goldenbase64.sh`'s
   naming).

## Reading the result

From `../float_coverage/`:

```
python3 fltmodel.py survivors . ../float_open
```

This reads every constant of `fltmul.s` out of its golden alongside the
existing goldens and prints the combinations that fit all of them
(`M` should drop to one of `ne`, `libc`). It lists `K1`/`K2`/`K3`/`LH`
as "NOT MODELLED" together with their real bytes, and reports "MODEL
CONTRADICTED" if a modelled constant fits no combination at all. It
exits 1 in any case until `fltconst.c` catches up. Then:

- narrow `fltconst.c`'s `MODES_MUL` and `fltmodel.py`'s `EXPECTED` to the
  surviving set - every constant the model covers becomes determined;
- extend the zero rule for decimal exponent 0 (and, if `LH` shows a
  rule, the LOGHUGE path) in both from `K1`/`K2`/`K3`/`LH`'s bytes;
- move `fltmul.s` and its golden to `../float_coverage/` (add it to
  that directory's `Makefile`/`Makefile.mutos`), and update
  `STATUS.md`'s open item 7, `CLAUDE.md`'s "Next up" and
  `docs/DEVLOG.md` in the same change (Workflow Guideline 6).
