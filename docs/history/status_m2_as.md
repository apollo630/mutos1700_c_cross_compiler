# `STATUS.md` history — Milestone 2 (`mutos_as`), 2026-09-27 to 2026-09-29

Sections moved here from `STATUS.md` on 2026-10-20, unchanged: the records of the sessions that added `.float`/`.double` and settled the real conversion. Each was true when written ("this session" means that session); the current state is in [`STATUS.md`](../../STATUS.md), the narrative in [`docs/devlog/m2_as_float.md`](../devlog/m2_as_float.md).

---

## Earlier fixes (prior session records; both still covered by the regression above)

1. **INSW/OUTSW mis-assembly** — `insb`/`insw`/`outsb`/`outsw` had no dispatch entry
   in `encode_instruction()` at all. An unrecognized zero-operand mnemonic fell into
   `assemble.c`'s bare-identifier fallback (meant for real symbol/pointer-table
   references, e.g. `tty.s`'s jump tables) and was silently mis-assembled as a 2-byte
   external-symbol placeholder + a spurious relocation entry instead of the correct
   1-byte opcode — desyncing the location counter for everything downstream, with no
   diagnostic. Fixed by adding real `0x6C`/`0x6D`/`0x6E`/`0x6F` dispatch entries;
   INSW/OUTSW confirmed byte-exact via the new `mch_insw_outsw.s` test (`mch.s` with
   its two `.byte /6d` / `.byte /6f` raw-byte workarounds replaced by plain
   `insw`/`outsw`).
2. **Missing "unknown opcode" safety net** — the same bare-identifier fallback would
   silently swallow *any* mnemonic-shaped token that failed to encode, not just
   genuinely unknown ones. Added `encode_is_known_mnemonic()`, which checks a token
   against every mnemonic name this assembler actually dispatches on (built by
   walking the same `GROUP1`/`GROUP1B`/`GROUP2`/`GROUP2B`/`REAL_JCC`/`LOOP_OPS`/
   `PSEUDO_BRANCHES`/`NOARG_TABLE` arrays `encode_instruction()` itself uses, plus a
   small explicit list for irregularly-dispatched mnemonics, so a new table entry is
   automatically covered with no second edit). `assemble.c` now only takes the
   bare-symbol fallback path when the token is *not* a known mnemonic; a recognized
   mnemonic used with an unsupported operand count/shape is now a hard compile error
   with a line number instead of silent mis-assembly. (A first version of this fix
   omitted the entire Group3 family — `mul`/`div`/`idiv`/`imul`/`neg`/`not` and their
   byte variants — from the known-mnemonic list; caught and fixed later the same
   session by regenerating the list programmatically from the source instead of by
   hand a second time.)

## `.float`, `lea <reg>,<label>`, no padding at a segment switch, byte-register `mov` (verified this session)

`mutos_c1`'s floating output (Milestone 4's `08_float`) now assembles, links with
the real `crt0.o`/`libc.a`, and runs. Full derivation in `docs/DEVLOG.md`'s section
"`mutos_as`: `.float`, `lea <reg>,<label>` - `08_float` assembles, links and runs".

- **`.float`** (`fltconst.c`): one 4-byte MUTOS float per operand (`L10000:<TAB>.float
  3.50000000000000000e+00`; the manual's `0f` prefix accepted too), converted with
  exact integer arithmetic - no host floating point. Only a value exactly
  representable in 24 significant bits is accepted; an inexact value is an
  explicit error (the real assembler's rounding is not known). At the time, zero
  (`bc a2 31 00` in `atof.o`/`ecvt.o`, the mantissa of 5\*\*17 with a zero
  exponent) and `.double` were explicit errors too - both are implemented now,
  from real-hardware goldens: see "Zero `.float`, `.double`" below.
- **`lea <reg>,<label>`**: `8D /r`, mod=00 rm=110 + a relocated disp16 (R_DATA /
  R_TEXT / R_BSS / R_EXT like any direct operand) - confirmed by `libc.a`'s machine
  code (`atof.o`/`ecvt.o` `8d 06 <disp16>` R_DATA; `ldexp.o`, `modf.o`,
  `doubles.o`, `singles.o`, `stkmath.o` with R_EXT `fac`). `lea` now also refuses a
  byte-register destination.
- **No padding at a `.text`/`.data` switch - a real bug, found by this work.**
  `mutos_as` padded the segment it left to an even size at every switch; the real
  assembler rounds each segment once, at the end. `mutos_c1` writes each floating
  constant's `.data` / `L10000: .float ...` / `.text` block between two
  instructions, so an odd location there put a `0x00` byte INTO the code (`add
  [bx+si],al` at run time). `atof.o`/`ecvt.o` show the lea directly after the
  previous instruction at odd offsets (text 71, 407; 201, 345). The earlier evidence
  (`malloc.o`, `mch.o`) was each file's last switch, where both rules give the same
  bytes - all 136 other `.s` inputs assemble to identical objects under the new rule.
- **`mov` with a byte register and a memory or register operand** (`mov al,x`,
  `mov x,al`, `mov al,bl`) got the WORD opcode (A1/A3/8B/89) unless written `movb` -
  now byte-sized, like the reg,imm form (`mov al,*0` in `mch.s`). Found reproducing
  `ldexp.o` (A0, A2, `8A 46 0A`); no corpus file writes one.
- **Regression tests**: `tests/mutos_as/libc_recon/` (`ldexp.s` reproduces the real
  `ldexp.o` byte for byte - `lea` with external and local labels at even and odd
  offsets, the byte `mov`s; `floatdat.s` + `check_floatdat.sh` for `.float`) and
  `tests/mutos_as/assemble_cc_goldens.sh`, all in the top-level `make test`. See
  that directory's `README.md` for why no compiled-C `libc.a` object can be
  reproduced whole yet: no `L` labels in their symbol tables (why the kernel
  goldens have them is open, see open item 6), and for `ecvt.o` also an inexact
  8-byte constant (`.03`). (A zero constant was a third reason until the zero
  `.float` below.)
- **End to end**: both `08_float` goldens, assembled and linked by `mutos_ld` with
  the real `crt0.o` and `libc.a`, return 7 and 3 - their C sources' values - when
  their `_main` runs under an 8086 emulator (Unicorn, a scratch harness, not part of
  `make test`), the real runtime's `flds`/`fadds`/`fmuls`/`fstsp`/`itof`/`fdivs`/
  `ftol`/`ftoi` reading the assembled constants. Ten more floating programs through
  `mutos_cpp`, `mutos_c0`, `mutos_c1`, `mutos_as`, `mutos_ld` and the same harness
  all return what the host C compiler's build returns.
- **`fltconst.c` checked**: 59,000 randomized exact and inexact texts (random 24-bit
  mantissas, exponents over the whole range, several spellings each) against an
  independent exact-fraction encoder in Python and `x86sim.py`'s `mbf_encode()`: 0
  differences; ASan/UBSan clean.

## Zero `.float`, `.double` (verified this session)

Both implemented from `tests/mutos_as/float_coverage/`'s real-hardware goldens
(former open item 7). Full derivation in `docs/DEVLOG.md`'s section
"`mutos_as`: zero `.float` and `.double` implemented". The restrictions below
(one zero spelling, exact values only) were superseded the same day - see "The
real conversion, re-enacted" next.

- **Zero `.float`**: `bc a2 31 00` - `fltzero.o.golden`'s bytes, identical to all
  six zero constants in `atof.o`/`ecvt.o` - but only for the spelling it is
  confirmed for, `0.00000000000000000e+00` (the `%.17e` text the real compiler
  writes: no minus sign, exactly 17 fraction digits, exponent 0). The mantissa is
  5\*\*17's, which fits a conversion that scales the digits by 5\*\*(fraction
  digits), so another spelling (`0.0`) may have other real bytes: every other
  zero is an explicit error naming the accepted spelling.
- **`.double`** (`fltconst.c`'s `flt_encode(FP_DOUBLE, ...)`, `assemble.c`'s
  `.float` branch): one 8-byte constant per operand, the float layout with 56
  significant bits (exponent byte 7, sign bit 7 of byte 6) - `fltdbl.o.golden`,
  `1 + 2**-32` → `00 00 80 00 00 00 00 81`, reproduced byte for byte. Exactly
  representable values only, as for `.float` (`ecvt.o`'s inexact `.03` is one
  rounded sample, not a rule); bare atof syntax or the manual's `0d` prefix with a
  `d`/`D` exponent; every zero `.double` refused (no real bytes for one exist).
  Previously `handle_directive()` refused every `.double` in Pass 1.
- **Checked**: `fltconst.c` against an independent exact-fraction encoder in
  Python on 136,818 random texts over both formats - 0 differences; the 69,296
  `.float` texts among them give the previous encoder's answers except for the
  seven accepted zero spellings; ASan/UBSan clean. End to end, five programs
  with `.double` constants (mantissa bits down to the lowest, a negative value,
  the `0d` spelling) run through the real runtime's
  `fldd`/`fsubd`/`fmuld`/`ftoi` - assembled by `mutos_as`, linked by `mutos_ld`
  with the real `crt0.o`/`libc.a`, under an 8086 emulator (scratch harness, not
  part of `make test`) - return the expected 1, 12345, -7, 23130, 1499.

## The real conversion, re-enacted (verified this session)

From `tests/mutos_as/float_coverage/fltopen.o.golden` (real hardware, 2026-09-28):
`.float 0.0` → `00 00 20 00`, `.float -0.00000000000000000e+00` → `bc a2 b1 00`,
`.double 0.00000000000000000e+00` → `00 00 c5 2e bc a2 31 00`, `.float
0.10000000000000000e+00` → `cc cc 4c 7d`. The first and third are the bytes
`docs/DEVLOG.md` had predicted before the run from v7 `atof()` (a zero dividend
keeps the divisor `flexp` = 5\*\*k's mantissa); the last is 0.1 **truncated** -
a `.float` is the high half of the double conversion. Full derivation in
`docs/DEVLOG.md`'s section "`mutos_as`: the real conversion, re-enacted".
Its rounding set (48 combinations) and the refusals and zero limits below
were narrowed the next day - see "Rounding pinned, zeros on both paths".

- **`fltconst.c` rewritten** to re-enact that conversion - v7 `atof()` on the
  56-bit double (digits accumulated while below 2\*\*56, `flexp` = 5\*\*k by
  repeated squaring, one divide or multiply, `ldexp`), `.float` = the double's
  high four bytes - instead of encoding the exact decimal value. How the real
  `dmul`/`dadd`/`ddiv` round is unknown except that `ddiv` does not truncate
  (`fltdbl.o.golden`), so the conversion runs under all 48 remaining
  combinations and a constant is accepted only if all give the same bytes
  (otherwise `FLT_ROUNDING`, which replaces `FLT_INEXACT`).
- **Newly accepted**: inexact values the unknown rounding cannot reach
  (`.float 0.1` → `cc cc 4c 7d`, `.double 0.1` → `cd cc cc cc cc cc 4c 7d`,
  `.double .03` → `ecvt.o`'s `c3 f5 28 5c 8f c2 75 7b`), and zeros of either
  size and sign with 1 to 24 more fraction digits than exponent (the mantissa of
  5 to that power, exponent byte 0).
- **Newly refused - a latent mismatch closed**: exact floats whose conversion
  rounds, e.g. the compiler's 18-digit `2.93572534179687500e+03` (its 18th digit
  makes `10*fl` inexact; under a truncating `dmul` the real bytes are one unit
  lower, `9a 7b 37 8c`). The previous encoder wrote the exact value for these
  without knowing it was right. About 6% of the `%.17e` constants `mutos_c1` can
  write fall here; no `.s` in the repository has one.
- **Tests**: `float_coverage/fltopen.s` (moved from `float_open/`, 5/5 there);
  `float_coverage/fltmodel.py`, an independent Python model, in `make test`
  (`survivors`: re-derives the 48 combinations from every golden in that
  directory and fails if `fltconst.c`'s set differs; `check`: `fltconst.c` via
  the new `src/mutos_as/fltconst_test` against the model); `floatdat.s` gains
  `ecvt.o`'s `.03`. One-off: 170,778 texts (random and targeted - compiler
  `%.17e` form, the 2\*\*56 accumulation boundary, zeros, prefixes) through
  `fltconst_test` and the model - 0 differences.
- **Next probe** (then): `fltmode.s` - four compiler-style `.double` constants
  chosen to split the 48 combinations into 32 classes, the refused
  `2.93572534179687500e+03` itself, and zeros on `atof`'s multiplication path.
  Run on real hardware; see "Rounding pinned, zeros on both paths" next.

## Rounding pinned, zeros on both paths (verified this session)

From `tests/mutos_as/float_coverage/fltmode.o.golden` (real hardware, committed
2026-09-29; moved from `float_open/` with its source). Its open question - which
product `dmul` rounds - and the k = 0 zero limits were settled the same day:
see "The real product, zeros at exponent 0" next. Full
derivation in `docs/DEVLOG.md`'s section "`mutos_as`: rounding pinned, zeros on
both paths, and `libc.a`'s own `atof` as a second oracle".

- **Rounding**: only 2 of the 64 rounding-mode combinations fit its ten
  constants and the nine before: `dmul` and `dadd` round to nearest, ties to
  even; `ddiv` to nearest (its tie rule never matters for `atof`). The
  compiler's `2.93572534179687500e+03` is the exact value, `9b 7b 37 8c`;
  `.double 0.1` and a negative zero `.double` are the predicted bytes.
- **`libc.a`'s own `atof`**, run on `libc.a`'s own floating-point runtime
  under an 8086 emulator (scratch harness), gives all 11 nonzero real
  constants byte for byte - the real assembler converts like it. Its zeros
  differ: `libc.a`'s `dmul`/`ddiv` clear the accumulator `fac` for a zero
  operand. Changed to clear only `fac`'s exponent byte, it reproduces all
  seven real zeros with a nonzero decimal exponent k.
- **Zero rule, now on both paths**: a zero gives the mantissa of `flexp` =
  5\*\*k with exponent byte 0 - the last multiplication's result, still in
  `fac` - for any k >= 1 on either path (`.float 0.0e+05` → `00 40 1c 00`,
  `.double 0.0000000000000000000000000` → the rounded 5\*\*25's mantissa),
  both directives and signs, unless `atof` gives up (a decimal exponent below
  -39 minus the digit count). For k = 0 `fac` holds what the digit loop left:
  only `Z0`'s shape is accepted (an unsigned `.float` of 18 digits,
  `ff ff ff 00`); other k = 0 zeros stay `FLT_ZERO`.
- **New open question - which product `dmul` rounds.** `libc.a`'s `dmul`
  is not exact: its partial-product routine takes the a0\*b2 term from b1's
  word (checked against the emulated routine on 20,000 random products: 0
  differences). No real constant so far needed a product where that
  matters, so `fltconst.c` keeps both candidates (`MODES_MUL`:
  `RM_NEAR_EVEN`, `RM_LIBC_MUL`; `MODES_ADD`: `RM_NEAR_EVEN`; `MODES_DIV`:
  `RM_NEAR_EVEN`, `RM_NEAR_AWAY` - replacing `MODES_MUL_ADD`) and refuses
  (`FLT_ROUNDING`) a constant whose bytes they decide: possible only for a
  positive decimal exponent of 4 or more (`%.17e` text from `e+20` up) and
  for k = 50..54 (`e-33` down). Every `%.17e` text from `e-32` to `e+19` is
  accepted - the 6% of exact floats refused before, `CF` among them,
  included. Over the whole exponent range 19% of random exact floats and 29%
  of exact doubles are refused (e.g. `.float 1.26765060022822940e+30`, 2\*\*100:
  `00 00 00 e5` or `ff ff 7f e4`), against 49%/93% under the previous 48
  combinations; 224 of 20,000 floats the previous model accepted are refused
  now - latent mismatches if the real `dmul` is `libc.a`'s.
- **Next probe** (then): `fltmul.s` - five constants whose bytes the two
  products decide, three zeros with k = 0 (digit count, `.double` low half,
  sign), the LOGHUGE path and the first negative nonzero constant.
- **Tests**: `float_coverage/fltmode.s` in `make test` (6/6 there);
  `fltmodel.py` extended the same way (`libc` as a fifth `dmul` candidate,
  the zero rule, 25 new edge cases).

## The real product, zeros at exponent 0 (verified this session)

From `tests/mutos_as/float_coverage/fltmul.o.golden` (real hardware, committed
2026-09-29; moved from `float_open/` with its source this session). Full
derivation in `docs/DEVLOG.md`'s section "`mutos_as`: the real product, zeros at
exponent 0, and `libcatof.py`".

- **The multiplication rounds `libc.a`'s product.** All five constants whose
  bytes depend on it have `libc.a`'s, none the exact product's - e.g. `.float
  1.26765060022822940e+30` (2\*\*100) is `ff ff 7f e4`, one unit below the
  value. `survivors`: 2 of 80 combinations left (`M=libc A=ne D=ne|na`);
  `fltconst.c`'s `MODES_MUL` is `RM_LIBC_MUL` alone. Every constant the model
  covers is now determined: `FLT_ROUNDING` cannot occur (only a division tie
  could raise it, and `atof` has none), and every `%.17e` constant `mutos_c1`
  writes from `e-37` up (`e-38` when `atof` drops the 18th digit) is
  accepted.
- **Zeros with decimal exponent 0**: `ff ff ff 00` for one digit (`K1`) as for
  18 (`Z0`), `00 00 00 00 ff ff ff 00` as a `.double` (`K2`), `ff ff 7f 00`
  negated (`K3`: `fneg` flips bit 7 of byte 6) - independent of the digit
  count and of the constant before. `mutos_as` accepts every such zero now.
  Why the digit loop leaves `ff ff ff` in `fac` is not known (`libc.a`'s
  runtime leaves -2\*\*56 there).
- **LOGHUGE** (nd - k < -39): `.double 1e-41` → eight 0 bytes, the `fac` rule
  again - `atof` sets `fl = 0`, exponent 0, and the multiplication by
  `flexp` = 1.0 keeps what `fac` held, `fl` = 1.0 from the digit loop's last
  `fadd`. Accepted when the text's last digit was accumulated (`fl`'s
  mantissa, exponent byte 0) or all digits are 0 (the zero above); after a
  dropped digit (`fac` holds `fcmp`'s difference, never observed) still
  `FLT_RANGE`. `FLT_ZERO` is gone - no zero is refused any more except where
  `flexp` overflows.
- **Still refused**: out of range at some step (`FLT_RANGE`), e.g. `.float
  1.00000000000000000e-38`, whose 5\*\*55 overflows although the value fits;
  `libc.a`'s runtime answers an overflow with `SIGFPE`, what the real
  assembler does is not observed. (Settled in the next section: `ldexp`
  wraps, an overflow inside the arithmetic aborts the real assembler.)
- **`libcatof.py`** (new, `tests/mutos_as/float_coverage/`, `make
  check-libcatof`): the emulator harness of the previous section as a tool -
  links `libc.a`'s `atof` and floating-point runtime from the base64 objects,
  runs it under Unicorn with the real assembler's zero handling (or
  unmodified, `--libc`), and checks the goldens, `fltconst_test`'s accepted
  output and the runtime's `dmul`/`ddiv`/`dadd` against `fltmodel.py`; it
  checks the linked image's code where it patches or relies on it. A build
  with the exact product instead of `libc.a`'s fails its `check`.
- **Tests**: `float_coverage/fltmul.s` in `make test` (7/7 there). Next
  probes: `float_open/fltovf.s` and `fltsig.s` (see open item 7) - run, see
  the next section.

## Range: `ldexp` wraps, an overflow aborts (verified this session)

From `tests/mutos_as/float_coverage/fltovf.o.golden` (real hardware, committed
2026-09-29; moved from `float_open/` with its source this session) and the real
assembler's refusal of `tests/mutos_as/float_open/fltsig.s`. Full derivation in
`docs/DEVLOG.md`'s section "`mutos_as`: out of range - `ldexp` wraps, an overflow
aborts".

- **A result only `ldexp` takes out of range wraps.** All nine constants of
  `fltovf.s` have exactly the bytes `libcatof.py` predicted: `libc.a`'s
  `ldexp` adds the exponent to `fac`'s exponent byte as a 16-bit sum and stores
  the low byte, so `.float 2.00000000000000000e+38` is `99 76 16 00` (exponent
  byte 256 → 0), `.double 1.0e-39` ends in `ff` (-1 → 255), `.double 1.0e+50`
  in `27` (295 → 39) - no diagnostic. The two in-range controls just inside
  both ends are as modelled. `fltconst.c` and `fltmodel.py` write the wrapped
  byte now; `mutos_as` reproduces `fltovf.o.golden` byte for byte.
- **An overflow inside the arithmetic aborts the real assembler.** `as -o
  fltsig.o fltsig.s` printed `***ERROR*** floating point over/under flow-
  assembly aborted` (followed by `W` and `?h), line 41`) twice, exit status 4,
  no object; line 41 is the first constant, `.float 1.00000000000000000e-38`
  (5\*\*55 overflows). Emulated, `libc.a`'s runtime raises `SIGFPE` twice for
  it - `__ovfl` in `dmul`, then `__div0` in the division by the zero left
  behind - which fits two messages if the real `as` catches the signal, prints
  and gives up after the constant. `mutos_as`'s refusal (`FLT_RANGE`, its
  message now quoting the real one) is the faithful behaviour; it exits with 1,
  the real `as` with 4 (unchanged). `fltsig.s` stays in `float_open/` as the
  evidence, without a golden.
- **What this means for the compiler's constants.** `mutos_as` accepts every
  `%.17e` constant `mutos_c1` writes from `e-37` up (`e-38` when `atof` drops
  the 18th digit), with the real bytes - among them the `%.17e` zero (`bc a2
  31 00`) and `.double 3.00000000000000000e-02` (`ecvt.o`'s `.03`), which the
  zero and non-float constants still refused in `mutos_c0`/`mutos_c1` will
  need (Milestone 4's "Next up"). Below that, 5\*\*55 overflows and the real
  assembler aborts, so a float or double constant below about 1e-37 cannot go
  through the real toolchain as `%.17e` text at all: `mutos_c1` writes it as
  the real compiler would, and both assemblers refuse it.
- **`FLT_UNKNOWN`** (new status): LOGHUGE after a dropped digit - `fac` holds
  `fcmp`'s difference, never observed - and a text of more than 100,000 digits,
  both `FLT_RANGE` before. `FLT_RANGE` now means only what the real assembler
  aborts on.
- **`libcatof.py`**: `__ovfl`/`__div0` stop the emulation (`atof` prints
  `SIGFPE`); `check` requires every text `fltconst_test` refuses as `RANGE` to
  raise it and no accepted one to. A build without the wrap (refusing those
  results) fails it with 57 `RANGE` texts that raise no `SIGFPE`; one with the
  exact product with 749 of 7,588 compared texts different.
- **Tests**: `float_coverage/fltovf.s` in `make test` (8/8 there);
  `float_open/` has no open probe.
