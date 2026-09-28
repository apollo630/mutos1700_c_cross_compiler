# `tests/mutos_as/float_open/` — the floating-point spellings still open after 2026-09-28

One hand-written, **new** assembler source — `fltopen.s` — probing the
four specific spellings `STATUS.md`'s open item 7 still lists as
unresolved once `../float_coverage/` closed the zero-`.float` and
`.double` gaps (2026-09-28, see `src/mutos_as/fltconst.h` and
`CLAUDE.md`'s "Next up"). Like `../float_coverage/fltzero.s`/`fltdbl.s`
were before that session, this is a **probe for open questions, not a
regression golden**: it exists to gather real-hardware bytes for cases
the current `mutos_as` refuses on principle rather than guesses at.

## The four cases

| Label | Text | What it probes |
|---|---|---|
| `Z1` | `.float 0.0` | Is the short zero spelling's real encoding the same as the confirmed `%.17e` one (`bc a2 31 00`), or different? The confirmed bytes look like a leftover of scaling 17 fraction digits by `5**17` (v7 `atof()`'s `flexp`) — a text with a different fraction-digit count may convert to something else entirely. |
| `Z2` | `.float -0.00000000000000000e+00` | The one confirmed zero spelling, negated. Does the sign bit (byte 2's top bit) get set on an otherwise-zero exponent byte, or is zero special-cased to ignore the sign? |
| `Z3` | `.double 0.00000000000000000e+00` | Zero in the 8-byte format. No real `.double` zero exists anywhere in `tests/mutos1700_libc`'s objects to compare against — `.double` itself was only confirmed for exactly representable non-zero values (`fltdbl.o.golden`, `1 + 2**-32`). |
| `IN` | `.float 0.10000000000000000e+00` | 0.1 is not exactly representable in a float's 24-bit mantissa (a repeating binary fraction). Does the real assembler round it, truncate it, or refuse it? `ecvt.o`'s one known inexact `.double` sample (`.03`) is correctly *rounded*, but its assembler-input spelling is lost, so it doesn't answer this for `.float`. |

All four are plain decimal texts in the syntax
`MUTOS1700_Assembler_as.pdf` sect. 3.2 documents ("Zeichen ..., die
`atof` als Floating-Point-Zahl akzeptiert") — nothing in the manual
restricts that to exactly representable values, so **the real `as` is
expected to accept all four without error**. This probe is about what
*bytes* it writes, not about whether it errors.

The **current `mutos_as` refuses all four**, each with its own
diagnosis (verified against the source in this directory):

```
error: bad .float operand '0.0' at line 63: this zero is not supported
  (the real assembler's bytes are confirmed only for
  0.00000000000000000e+00: 17 fraction digits, exponent 0, no minus sign)
error: bad .float operand '-0.00000000000000000e+00' at line 64: this
  zero is not supported (...)
error: bad .double operand '0.00000000000000000e+00' at line 65: zero
  is not supported in .double (the real assembler's bytes for it are
  unconfirmed)
error: bad .float operand '0.10000000000000000e+00' at line 66: not
  exactly representable as a float (the real assembler's rounding is
  unconfirmed)
```

That is by design (see `fltopen.s`'s own header and
`src/mutos_as/fltconst.h`'s comment), not a bug to work around —
**this directory does not wire into the top-level `make test`**, unlike
`../float_coverage/`: since `mutos_as` refuses every constant in
`fltopen.s`, `../run_goldens.sh` would always put it in category 3
("errors calling mutos_as") with no golden to diff against, which would
make `make test` fail permanently until the real answers land. Run it
manually instead (see below).

Each constant is loaded the way `mutos_c1` actually emits a floating
operand (`lea <reg>,<label>` / `call flds`|`fldd` —
`docs/DEVLOG.md`'s `08_float` section), so `fltopen.s` is a normal
whole-object source, not a data-only file like
`../libc_recon/floatdat.s`.

## Running this

Same two-machine shape as `../float_coverage/`:

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltopen.o`. Expected to succeed (see above); if the real `as`
   refuses any operand instead, that is itself the finding — note
   which one and its exact error text.
2. `make goldens` — **on the modern (Linux) side**, after copying
   `fltopen.o` back into this directory. Produces `fltopen.o.golden`
   (a verbatim copy) and `fltopen.o.golden_base64.txt` (base64 text),
   the same naming `../kernel_opt/`, `../kernel_nonopt/`,
   `../libc_recon/` and `../float_coverage/` already use
   (`../mk_goldenbase64.sh`).

## Reading the result

`fltopen.o`'s `.data` segment holds the four constants back to back,
in declaration order, with no padding between them (no relocation, no
alignment — same as every other `.float`/`.double` constant this
project has seen):

| Bytes | Offset | Constant |
|---|---|---|
| 4 | `data+0` | `Z1` (`.float 0.0`) |
| 4 | `data+4` | `Z2` (`.float -0.00000000000000000e+00`) |
| 8 | `data+8` | `Z3` (`.double 0.00000000000000000e+00`) |
| 4 | `data+16` | `IN` (`.float 0.10000000000000000e+00`) |

(`data` itself starts right after the `a.out` header's 16 bytes plus
`a_text` bytes — `../libc_recon/check_floatdat.sh`'s `data_bytes()`
shows the exact offset arithmetic, including reading `a_text`
byte-by-byte so host endianness doesn't matter.)

## Once the golden exists

Each of the four answers feeds back into
`src/mutos_as/fltconst.c`/`.h` independently — they don't have to all
resolve at once:

- If `Z1`'s bytes equal `Z2`'s with bit 7 of byte 2 clear regardless of
  the source's minus sign, or if `Z1` differs from the confirmed
  `bc a2 31 00`, that tells `flt_encode()`'s zero branch exactly which
  spellings besides the current one to accept, and what bytes to write
  for them (`FORMATS[].zero_confirmed` in `fltconst.c` and the
  `ZERO_FRAC_DIGITS`-gated check would need to grow into a small table
  of confirmed spelling→bytes pairs, or a real conversion rule, once
  more than one shape is known).
- `Z3`'s bytes pin down whether `.double`'s zero follows the same
  leftover-mantissa pattern as `.float`'s (scaled by `5**17` again, or
  by a different power for the extra 32 mantissa bits) or is a clean
  8-byte zero — either answer lets `FORMATS[FP_DOUBLE].zero_confirmed`
  flip to `true` with its own confirmed bytes.
- `IN`'s bytes settle whether `FLT_INEXACT` should become "round to
  nearest" (most likely, matching `ecvt.o`'s `.03`) or something else;
  implementing that touches the bit-length check in `flt_encode()`
  (`if (bits > fmt->sig_bits) return FLT_INEXACT;`), which would need
  to round the mantissa to `sig_bits` instead of refusing once rounding
  is confirmed on more than the one existing `ecvt.o` sample.

Update `CLAUDE.md`'s "Next up" and `STATUS.md`'s open item 7 in the
same change per Workflow Guideline 6, and add `../run_goldens.sh`
support once `mutos_as` actually accepts `fltopen.s` (at which point
this directory's `Makefile`/reasoning above should fold back into
`../float_coverage/` and this README should say so).
