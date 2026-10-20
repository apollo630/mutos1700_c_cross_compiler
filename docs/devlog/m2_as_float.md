# Milestone 2 — `mutos_as`: floating constants (2026-09-27 to 2026-09-29)

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). `.float`, `.double` and `lea <reg>,<label>` for `mutos_c1`'s floating output; the real conversion re-enacted, pinned by real-hardware probes.

---

## `mutos_as`: `.float`, `lea <reg>,<label>` - `08_float` assembles, links and runs (`mutos_as` extended and verified this session)

The first "Next up" item after `08_float`: make `mutos_c1`'s floating output
assemble. Both goldens now assemble, link with `mutos_ld` against the real
`crt0.o` and `libc.a`, and - run under an 8086 emulator - return their C
sources' values. On the way, two older `mutos_as` bugs surfaced, one of them
fatal for exactly this code.

**`lea <reg>,<label>`: the encoding is in `libc.a`'s machine code**, not just
analogous to `mov`. Scanning every text segment in `tests/mutos1700_libc/` for
`8D` with ModRM mod=00 rm=110 and a relocation on the disp16 finds 31: `atof.o`
(`8d 06 00 00`, `8d 06 04 00`, ...) and `ecvt.o` with R_DATA - the compiled-C
objects' own floating constants, `lea ax,<constant>` / `call flds` exactly as in
the goldens - and `ldexp.o`, `modf.o`, `doubles.o`, `singles.o`, `stkmath.o`,
`dmath.o` with R_EXT `fac` or R_DATA, at even and odd offsets (`0x8004`,
`0x8038`: the relocation word's shift bit). So `encode_lea()` takes an
`ADDR_DIRECT` operand through `emit_modrm_direct()` - the same mod=00 rm=110
form and non-PC-relative relocation classification as `mov`'s direct form, no
accumulator short form (LEA has none: `atof.o`'s `lea ax` is `8d 06`). It also
now refuses a byte-register destination (`lea al,...`), which it used to
encode.

**`.float`: the format, and what is (not) known about the conversion.** The
format is the `08_float` section's (and `CLAUDE.md` rule 5's):
excess-128 exponent in the highest byte, sign in bit 7 of the byte below
(`stkmath.o`'s `fneg` XORs bit 7 of a double's byte 6, a float's byte 2 since
`flds` loads a float into a double's high half), 23 stored mantissa bits after
the leading 1, exponent 0 = zero. What the real assembler does with a decimal
text is only partly visible:

- Every nonzero 4-byte constant in `libc.a` is exact (2\*\*56, 10.0, 1.0, 5.0),
  and so is every constant `mutos_c1` writes (it refuses the rest). For those,
  any correct conversion gives the same bytes.
- `ecvt.o`'s one 8-byte constant, `.03` (`c3 f5 28 5c 8f c2 75 7b`), is the
  correctly ROUNDED 56-bit value; truncation would end in `c2`. One sample.
- **Zero is not a plain zero.** `atof.o` (data+4, +16) and `ecvt.o` (+0, +4,
  +8, +28) hold every zero constant as `bc a2 31 00`: exponent byte 0, mantissa
  bits `0.1011 0001 1010 0010 1011 1100` - the top of 5\*\*17 =
  762939453125 = `0xB1A2BC2EC5` (and of 10\*\*17 = 5\*\*17 \* 2\*\*17). A text with
  17 fraction digits and exponent 0 is `%.17e` of zero -
  `0.00000000000000000e+00` - and v7's `atof()` algorithm scales such a text by
  `flexp` = 5\*\*17, so the bytes look like a zero result that kept 5\*\*17's
  mantissa. But `libc.a`'s own `atof` (`atof.o`, read in full: v7's algorithm,
  `fl /= flexp` via `fdivd`) cannot produce them: `dmath.o`'s `ddiv` tests the
  dividend's exponent byte and jumps to `zero:`, which clears all 8 bytes of
  `fac`, and `ldexp()` returns a zero exponent unchanged. So the real
  assembler converts with something else (another `atof`/`ddiv` version, its
  own code), or zero constants never went through `.float` text. Either way the
  bytes `.float 0.00000000000000000e+00` must produce are not known.
- The manual (`MUTOS1700_Assembler_as.pdf` sect. 3.2) spells a float constant
  `0f` + "characters atof accepts" and a double `0d` with a `d` exponent; the
  real compiler writes `.float` operands bare (`3.50000000000000000e+00`).

So `.float` (`fltconst.c`) accepts an optional `0f`, then atof syntax (sign,
digits with optional `.`, optional `e`/`E` exponent; at least one digit;
nothing after), and encodes a value **only if it is exactly representable** -
decided exactly: the text becomes D \* 10\*\*E (D an integer held in a small
bignum, trailing zeros folded into E); for E < 0 the value is dyadic only if
5\*\*-E divides D; the remaining odd integer must fit 24 bits and the exponent
the byte's 1..255. No host floating point is involved. An inexact value
(`0.1`) and zero are explicit errors naming the reason; so, still, is every
`.double`. Operands are re-scanned raw from the source, like `.asciz` - the
tokenizer splits `3.5e+00` into `3.` `5` `e` `+` `00`, and punctuation tokens'
text does not point into the source - up to `,`, newline, `;` or a `|`
comment.

**Bug 1 - padding at a segment switch put a byte into the code.** The first
assembled `01_floatbas` disassembled with a `00` after `call fstsp`: `.data` at
an odd text location padded the text segment, the rule `assemble.c` had carried
since `malloc.o` ("an odd-length `.text` immediately followed by `.data`") and
its mirror from `mch.o`. `mutos_c1` writes `.data` / `L10001: .float ...` /
`.text` between two instructions, so the pad executed as `add [bx+si],al`. The
real objects settle it: in `atof.o` the `lea ax,<constant>` follows the previous
instruction directly at text 71 and 407 (both odd, both right after such a
block), in `ecvt.o` at 201 and 345. Each segment is rounded up to even once, at
the end of the file; `malloc.s`'s and `mch.s`'s switches were their files' last,
where both rules produce the same bytes. The change leaves all 136 other `.s`
inputs (67 kernel files, 69 compiler goldens) byte-identical to the previous
build's objects.

**Bug 2 - `mov` with a byte register.** A regression test for `lea` with real
bytes: `ldexp.o` (hand-written; `lea si,*4(bp)`, `lea di,fac`, `lea si,huge`,
`lea ax,fac`) reconstructed from its disassembly as
`tests/mutos_as/libc_recon/ldexp.s`, the real object as its golden. The first
attempt differed in three bytes - `A1`/`A3`/`8B` where the real object has
`A0`/`A2`/`8A`: `encode_mov()` chose the byte opcodes of its reg↔mem and
reg↔reg forms only for `movb`, so `mov al,fac+7` loaded AX. The reg,imm form
already inferred byte size from a byte register (`mch.s`'s real `mov al,*0`);
the other forms now do too. With that, `ldexp.s` reproduces `ldexp.o` byte for
byte - header, text, data, both relocation tables, symbol table (its order is
first mention, so the reconstruction keeps the original's: `ERANGE`, `DOFF`,
`_ldexp`, `fac`, `ldexp3`, `ldexp2`, `_errno`, `huge`, `cret`). No kernel file
writes a plain `mov` with a byte register and a memory operand, so no golden
moved.

**Why no compiled-C `libc.a` object is a golden yet.** `atof.o`/`ecvt.o` would
test `.float` and `lea` together, but both hold zero constants, `ecvt.o` a
`.double`, and none of `libc.a`'s 167 objects has an `L`-number symbol - the
real `as`'s default without `-L` - while `mutos_as` always writes them, because
the kernel goldens compiled from C have them (the deliberate `-L` deviation, see
Milestone 2's CLI reference). Maybe the kernel was assembled with `-L`; unknown.
Instead `tests/mutos_as/libc_recon/floatdat.s` holds four of those objects'
constants as `%.17e` text, and `check_floatdat.sh` compares each assembled
value with the real object's bytes at test time: 6/6 identical (2\*\*56, 10.0
twice, 1.0 twice, 5.0).

**End to end.** Both `08_float` goldens assembled by `mutos_as`, linked by
`mutos_ld` with the real `crt0.o` and `libc.a` (every floating entry point and
`fltused` resolved), and run from `_main` under the Unicorn engine's 8086 mode
(a scratch harness: the 0407 image loaded as one 64K segment, a HLT as the
return address, no syscalls) return **7** and **3** - `(int)(3.5 * 2.0)` and
`(int)(7 / 2.0)` - computed by the real runtime from the assembled constants;
with the constants edited to 2.5 and -3.0 the first returns -7. Ten more
programs (float and double locals, `+ - * /`, int/long conversions, loops,
float code in a called function, 2\*\*24-1) through `mutos_cpp`, `mutos_c0`,
`mutos_c1`, `mutos_as`, `mutos_ld` and the same harness return what the host C
compiler's build returns. Before this change the same executables would have
run into the pad byte.

**Verification.** `make test` (clean build, zero warnings): `mutos_as` 68/68
(62 `kernel_opt`, 5 `kernel_nonopt`, 1 `libc_recon`), `check_floatdat.sh` 6/6,
`assemble_cc_goldens.sh` 71/71 (was 69), `mutos_cpp` 5/5, `mutos_c0`/`mutos_c1`
62/62. Against the previous assembler, every `.s` it accepted (136 of 138)
assembles to an identical object. ASan/UBSan build of `mutos_as` over 143
inputs (those plus hand-written `.float`/`lea` probes and their error paths): no
reports, objects identical to the `-O2` build's. `fltconst.c` against an
independent exact-fraction encoder (Python `fractions`) and `x86sim.py`'s
`mbf_encode()`: about 59,000 random exact and inexact texts (random 24-bit
mantissas over the whole exponent range, plain, scientific, `0f`-prefixed and
trailing-zero spellings, plus inexact neighbours), 0 differences, sanitizer
clean.

## Real-hardware confirmation: `-L` is a manual/binary mismatch, and the two open `.float`/`.double` gaps are closed (2026-09-28)

Two open items from the previous section — the "-L" question left open in
"Why no compiled-C `libc.a` object is a golden yet", and the zero-`.float`/
`.double` gaps described under "`.float`: the format, and what is (not)
known about the conversion" — both got real-hardware evidence this
session, from two small test directories built for exactly this purpose
(`tests/mutos_as/README.md` and `tests/mutos_as/float_coverage/README.md`).

**`-L` is not implemented by the real `as` at all.** The plan was to
assemble every real kernel `.s` both with and without `-L` and diff each
result against the already-committed `.o.golden` files, to settle whether
the kernel build's goldens (all carrying `L`-number labels) came from an
`as -L` invocation that the lost `libc.a` build never used (none of its
167 objects has an `L`-number label — the documented default). That
comparison never had to run: the very first `-L` invocation tried,
`as -L -o v30ide.oL v30ide.s`, printed `Unknown option L ignored` and
assembled anyway. `docs/MUTOS1700_Assembler_as.pdf` sect. 3.1 documents
`-L` as a real switch; the binary on this hardware simply does not have
it. Since the flag is silently ignored rather than merely defaulted to
off, an `as`/`as -L` pair is necessarily byte-identical for any input on
this hardware — there is nothing left to diff, so no `.oL` goldens were
generated. This rules out "the kernel build used `-L`" as an explanation
for the `L`-label discrepancy (the flag has exactly one behavior,
unconditionally), but does not resolve the discrepancy itself: the
kernel-vs-libc difference is still open, and the next angle is
source-level — whether the *compiler* emits `L`-labels only under
conditions the lost `libc.a` C sources never met, not a policy the
assembler applies per invocation. `mutos_as`'s own always-emit-`L`-labels
behavior (the "Known, deliberate exception" in `CLAUDE.md`) is unaffected
either way, since it was already chosen for golden parity rather than for
matching any hypothesized real invocation.

**Zero `.float` and `.double` now have real, known-source ground truth.**
`tests/mutos_as/float_coverage/`'s four hand-written sources all assembled
successfully on real MUTOS 1700 hardware. Two are plain regression
confirmations — `fltaddr.o.golden`/`fltmulti.o.golden` (`lea <reg>,<label>`
+ `.float` in one whole object) are byte-identical to current `mutos_as`'s
own output. The other two close gaps the previous section left open:

- `fltzero.o.golden` (source: `.float 0.00000000000000000e+00`) has data
  segment `bc a2 31 00` — the exact bytes `atof.o`/`ecvt.o` were already
  known to hold for a zero constant, now confirmed from a known,
  hand-written source rather than lost compiled-C input whose conversion
  path could only be guessed at. Where the constant's bytes come from is
  still not derivable from first principles (the previous section's
  `ddiv`-clears-`fac` puzzle stands unexplained), but what they *are* is
  no longer in doubt, which is what `mutos_as`'s `FLT_ZERO` refusal was
  waiting on.
- `fltdbl.o.golden` (source: `.double 1.00000000023283064365386962890625`,
  i.e. `1 + 2**-32`, chosen to need more than a float's 24 mantissa bits)
  has data segment `00 00 80 00 00 00 00 81`: exponent byte `0x81`
  (excess-128 → `2**1`, correct for a value just above 1.0), sign bit 0,
  and a single mantissa bit set exactly 32 bits in — precisely where
  `2**-32` should land under the same `0.1mmm * 2**(e-128)` scheme
  `.float` already uses, just with 55 stored mantissa bits instead of 23.
  This is the first real evidence pinning down `.double`'s bit layout
  beyond `ecvt.o`'s single already-known `.03` sample.

Both gaps are implementation work now, not open questions — see
`CLAUDE.md`'s "Next up" and `STATUS.md`'s open item 7. `tests/mutos_as/
float_coverage/README.md`'s own "Results" section has the full byte
breakdown. (Implemented the same day - next section.)

## `mutos_as`: zero `.float` and `.double` implemented (2026-09-28)

From the two goldens above. `fltconst.c` has one encoder for both formats now,
`flt_encode(kind, ...)` with `kind` `FP_FLOAT` or `FP_DOUBLE`, driven by a small format table (size,
significant bits, prefix, limits); `assemble.c` handles `.double` in the same
`run_pass()` branch as `.float` (raw operand re-scan, no alignment, no
relocation, the location counter advanced by the full size even for a refused
operand) instead of refusing it in `handle_directive()`.

**`.double`.** 8 bytes: the float layout with 56 significant bits - the byte-7
excess-128 exponent, the sign in bit 7 of byte 6, 55 mantissa bits below it.
`fltdbl.o.golden` pins it (`1 + 2**-32` → `00 00 80 00 00 00 00 81`: the one
mantissa bit 32 places after the leading 1 is bit 23 of the value, byte 2's
`0x80`). As for `.float`, only exactly representable values are accepted and
encoded exactly - the golden's value is exact, and the one inexact 8-byte
sample (`ecvt.o`'s `.03`, correctly rounded) has a lost source spelling and
cannot show how the real conversion rounds in general (v7's `atof()` scheme -
up to 17 digits accumulated in a double, `flexp` built by repeated squaring,
one `fdivd`/`fmuld` - is not obviously correctly rounded, and `libc.a`'s own
`atof` is not what the assembler uses, see the zero puzzle above). The
bounds grow with the precision: an exact double has at most 183 binary
fraction digits (2\*\*-127 times a 56-bit mantissa) and at most 145
significant decimal digits, so `.double` accepts up to 160 significant digits
and decimal exponents down to -190; `.float` keeps its 120 and -160 so every
`.float` text gets the same answer as before. Syntax: the bare form `fltdbl.s`
uses (with atof's `e`/`E` exponent), or the manual's constant spelling (sect.
3.2): `0d`/`0D` prefix, then a `d`/`D` exponent - neither prefixed form (`0f`
nor `0d`) is confirmed by real bytes. A zero `.double` is refused: `libc.a`
holds no 8-byte zero anywhere (all six zero constants in its data segments
are 4-byte `bc a2 31 00`), and under the 5\*\*17-leftover reading below its
low bytes would carry more of 5\*\*17 (`00 00 c5 2e bc a2 31 00`?) - a guess.

**Zero `.float`: only the confirmed spelling.** `bc a2 31 00` is confirmed
for `.float 0.00000000000000000e+00` - the `%.17e` text `mutos_c1` (and the
real compiler) writes - and matches all six zero constants in `atof.o`/
`ecvt.o`. It is NOT confirmed for zero in general: the mantissa is 5\*\*17's,
which fits a v7-`atof()`-style conversion that scales the digits by `flexp` =
5\*\*(fraction digits) and keeps the divisor's mantissa in a zero result, so
`.float 0.0` (`flexp` = 5) would plausibly come out `00 00 20 00`, and a
minus sign might set bit 7 of byte 2. So a zero is accepted only in the
confirmed shape - `.float`, no `-`, exactly 17 digits after the `.`,
exponent 0 (any count of integer zeros, a `+`, `e-00` or no exponent at all,
and the `0f` prefix are the same text to such a conversion) - and every other
zero is refused with a diagnostic naming the accepted spelling. A real-hardware
`.float 0.0`/`-0.00000000000000000e+00` probe would settle whether this can be
widened.

**Tests.** `tests/mutos_as/float_coverage/` joins the top-level `make test`
(`run_goldens.sh`: 4/4 byte-identical - `fltzero.s` and `fltdbl.s` were
refused before). `libc_recon/floatdat.s` gains the zero, and
`check_floatdat.sh` compares it with all six of its occurrences in `atof.o`/
`ecvt.o`: 12/12 (was 6/6). The float_coverage directory's four raw real-hardware
`.o` files are tracked alongside their `.o.golden` copies (byte-identical);
`run_goldens.sh` writes its own `<name>.o` over them, with identical bytes
while `mutos_as` matches.

**Verification.** Clean `make clean && make all && make test`: zero warnings,
`mutos_as` 72/72 (62 `kernel_opt`, 5 `kernel_nonopt`, 1 `libc_recon`, 4
`float_coverage`), `check_floatdat.sh` 12/12, `assemble_cc_goldens.sh` 71/71,
`mutos_cpp` 5/5, `mutos_c0`/`mutos_c1` 62/62. Against the previous assembler,
over 151 `.s` inputs (the 72 golden sources, `floatdat.s`, the seven
`v30_speculative` sources and the 71 compiler `.s` goldens), the 148 it
accepted assemble to identical objects; the three it refused (`fltzero.s`,
`fltdbl.s`, the new `floatdat.s`) are the only differences. `fltconst.c` against an independent exact-fraction encoder
(Python `fractions`, the MBF layout computed separately): 136,818 random texts
over both formats (random mantissas up to 2 bits past each format's precision,
exponents over the whole range, positional, scientific, `%.17e`, prefixed and
trailing-zero spellings, inexact neighbours, both formats for each value, zero
spellings and syntax errors) - 0 differences; the 69,296 `.float` texts among
them give the old encoder's answer everywhere except the seven accepted zero
spellings. ASan/UBSan (`-O0 -g`, LeakSanitizer off - on a Pass 1 error the
assembler exits without freeing its buffers, as before this change): the
encoder driver over all 136,818 texts and `mutos_as` over the 151 inputs plus
`.double`/zero probes (success and every error path) - no reports, objects
identical to the `-O2` build's. **End to end**: five programs with `.double`
constants (`1 + 2**-32`, `1 + 12345 * 2**-50`, `-1 - 7 * 2**-52`, `3 + 0x5a5a *
2**-54`, and the manual spelling `0d1.5d3`/`0D1.0D0`), each `fldd` / `fsubd` /
`fmuld` / `ftoi` through the real runtime, assembled by `mutos_as`, linked by
`mutos_ld` with the real `crt0.o` and `libc.a` and run from `_main` under
Unicorn's 8086 mode (scratch harness, not part of `make test`), return 1,
12345, -7, 23130 (`0x5a5a`) and 1499 - so the low mantissa bytes land where
the real runtime reads them.

(Its two restrictions - one zero spelling, exact values only - were superseded
the same day by the next section.)

## `mutos_as`: the real conversion, re-enacted (2026-09-28)

**The probe.** `tests/mutos_as/float_open/fltopen.s` asked the real `as` for
the four spellings the previous section left refused; `fltopen.o.golden`
(real hardware) answered, data segment in declaration order:

| Text | Real bytes |
|---|---|
| `.float 0.0` | `00 00 20 00` |
| `.float -0.00000000000000000e+00` | `bc a2 b1 00` |
| `.double 0.00000000000000000e+00` | `00 00 c5 2e bc a2 31 00` |
| `.float 0.10000000000000000e+00` | `cc cc 4c 7d` |

The first and third are exactly the bytes the previous section predicted
from v7 `atof()` - "`.float 0.0` (`flexp` = 5) would plausibly come out
`00 00 20 00`", and for a zero `.double` "`00 00 c5 2e bc a2 31 00`?" -
before the real assembler ran: a zero dividend keeps the divisor
`flexp` = 5\*\*k's mantissa (k = 1 and 17 here) with exponent byte 0, and the
zero `.double`'s high half is `fltzero.o`'s `bc a2 31 00`. The second is that
zero with the sign bit set: `fl = -fl` after the division. The fourth is
0.1 **truncated** to 24 bits (`0xcccccc`; correct rounding gives `0xcccccd`):
a `.float` is the high four bytes of the double conversion, like `stacks.o`'s
`fstsp`, which also truncates.

**What truncation implies for exact values.** The previous encoder wrote the
exact value of any exactly representable decimal text. That is right only if
the real double conversion is exact too, or errs upward: an error of one
56-bit ulp downward, truncated, costs a whole float ulp. v7 `atof()` has
rounding steps an exact value can pass through: `fl = 10*fl + digit` once `fl`
nears 2\*\*56 (the 18th digit of a `%.17e` text often makes `10*fl` inexact),
`flexp` for k > 24 (5\*\*25 has 59 bits), and the final `fl /= flexp` or
`fl *= flexp`. Digits beyond `fl >= 2**56` are dropped entirely.
`2.93572534179687500e+03` - an exact float, as `mutos_c1` would write it -
comes out `9b 7b 37 8c` (exact) if `dmul` rounds `10*fl` up or to nearest,
but `9a 7b 37 8c` if it truncates.

**The implementation.** `fltconst.c` now re-enacts `atof()` step by step on a
56-bit mantissa (`uint64_t` plus a portable 64x64->128 multiply and a
restoring division - still no host floating point): the `fl < 2**56`
accumulation with v7's exponent bookkeeping, `flexp` by v7's repeated
squaring (including where it squares), the division or multiplication,
`ldexp`, negation, the zero rule, and a range check after every step (the
real arithmetic's overflow behaviour is unknown, so any step outside the
format's exponent range is `FLT_RANGE` - e.g. `1.00000000000000000e-38`, whose
`flexp` = 5\*\*55 overflows although the value fits). Each double operation's
rounding is a parameter - truncate, nearest-even, nearest-away, away from
zero - for `dmul` (which `10*fl` and the squaring also use), `dadd` and
`ddiv`. `fltdbl.o.golden` rules out a truncating `ddiv`: under `atof()` its
33-digit text keeps 18 digits and its one division lands 0.13 ulp below the
real result, so the division rounded up. No other golden constrains
anything, which leaves 48 of 64 combinations. `flt_encode()` runs all 48 and
accepts a constant only if they agree on the stored bytes; otherwise
`FLT_ROUNDING` (replacing `FLT_INEXACT`). Zeros are accepted where they are
observed: `atof()`'s division path (negative decimal exponent) with k <= 24,
so 5\*\*k is exact - either sign, either directive. A zero on the
multiplication path (`0`, `0e5`) or with k > 24 stays `FLT_ZERO`.

Consequences, measured with the model over compiler-style texts:
- newly accepted: inexact values whose stored bytes no remaining rounding
  reaches - `.float 0.1`, `.double 0.1` (`cd cc cc cc cc cc 4c 7d`: every
  non-truncating `ddiv` rounds its 0.8-ulp remainder up), and
  `.double 3.00000000000000000e-02`/`.03`, which gives `ecvt.o`'s real
  `c3 f5 28 5c 8f c2 75 7b` - a further, independent fit of the model
  (the source spelling of `ecvt.o`'s constant is unknown, but both plausible
  ones give these bytes);
- newly refused: about 6% of the exact `%.17e` floats `mutos_c1` can write
  (636 of 10,184 in a random sample of exact floats below 2\*\*24 with at most
  18 significant digits), and many long exact expansions. These were a latent
  mismatch: the previous encoder's exact bytes were a guess for them. No `.s`
  in the repository uses one - every one of the 153 inputs below that
  assembled before assembles to the same object.

**The independent model.** `tests/mutos_as/float_coverage/fltmodel.py` is the
same conversion written separately in Python (arbitrary-precision integers).
`survivors` reads every `<name>.s` in `float_coverage/` with a golden, takes
each `.float`/`.double` constant's real bytes from the golden's data segment
(9 constants in 5 goldens), and reports the combinations that reproduce all
of them - failing if that set is not the one `fltconst.c` hard-codes, if a
golden contradicts the model outright, or if a golden holds a constant the
model does not cover. `check` compares `fltconst.c`, via the new test driver
`src/mutos_as/fltconst_test`, with the model on 49 edge cases and 2,000 seeded
random texts. Both run in `make test`; a doctored golden (0.1 as the correctly
rounded `cd`) makes `survivors` fail with "MODEL CONTRADICTED".

**The next probe.** `tests/mutos_as/float_open/fltmode.s` (not yet run on real
hardware): four `.double` constants in the compiler's form, chosen from about
7,500 candidates by greedy refinement to split the 48 combinations into 32
classes - all that is left is `ddiv`'s tie rule, and a division by 5\*\*k
never ties - plus the refused `2.93572534179687500e+03` itself, `.double 0.1`
and a negative zero `.double` (accepted now on the model's prediction alone),
and three zeros outside the zero rule (decimal exponent 0 and +4 - `atof()`'s
multiplication path - and 25 fraction digits, where 5\*\*25 is rounded). Its
`README.md` lists every predicted outcome; a simulated golden built from one
assumed combination decodes back to exactly that combination with
`fltmodel.py survivors . ../float_open`.

**Verification.** Clean `make clean && make all && make test`: zero warnings,
`mutos_as` 73/73 (62 `kernel_opt`, 5 `kernel_nonopt`, 1 `libc_recon`, 5
`float_coverage`), `check_floatdat.sh` 13/13 (`ecvt.o`'s `.03` added, all 8
bytes), `fltmodel.py survivors` ok, `fltmodel.py check` 0 differences in
2,049 texts, `assemble_cc_goldens.sh` 71/71, `mutos_cpp` 5/5,
`mutos_c0`/`mutos_c1` 62/62. `fltconst_test` against the Python model on
170,778 texts (the previous section's 136,818 random ones plus 33,960
targeted: `%.17e` of exact floats and doubles, integers around 2\*\*56...2\*\*66,
decimal exponents to -60, zeros with 0 to 29 fraction digits and exponents
-30...+7, prefixed spellings) - 0 differences, and the same under ASan/UBSan.
Against the previous commit's assembler, over 153 `.s` inputs (the 73 golden
sources, `floatdat.s`, the seven `v30_speculative` sources, `fltmode.s` and
the 71 compiler goldens): the 150 it assembled give identical objects;
`fltopen.s` and the extended `floatdat.s`, which it refused, now assemble
(to their golden and real bytes); `fltmode.s` is refused by both, by design.
The ASan/UBSan build over the same 153: no reports, output identical to the
`-O2` build's. The five end-to-end `.double` programs from the
previous section: the three with short or exact-conversion texts still
assemble and still return 1, 12345 and 1499 through the real runtime; the
two with 53- and 54-digit texts are now refused (`atof()` keeps 17 digits,
and the rounding decides the last bit).

## `mutos_as`: rounding pinned, zeros on both paths, and `libc.a`'s own `atof` as a second oracle (2026-09-29)

**The golden.** `tests/mutos_as/float_open/fltmode.s` ran on real
hardware; `fltmode.o.golden`'s data segment, in declaration order:

| Label | Text | Real bytes |
|---|---|---|
| `M1` | `.double 1.47397409833160963e-08` | `ff ff ff ff 10 3a 7d 66` |
| `M2` | `.double 2.97967517326469533e-09` | `ff ff ff ff ff c2 4c 64` |
| `M3` | `.double 1.45615926012396812e-02` | `fe ff ff ff be 93 6e 7a` |
| `M4` | `.double 1007211910940938336` | `05 d5 52 58 5e a5 5f bc` |
| `CF` | `.float 2.93572534179687500e+03` | `9b 7b 37 8c` |
| `D1` | `.double 0.10000000000000000e+00` | `cd cc cc cc cc cc 4c 7d` |
| `NZ` | `.double -0.00000000000000000e+00` | `00 00 c5 2e bc a2 b1 00` |
| `Z0` | `.float 0.00000000000000000e+17` | `ff ff ff 00` |
| `Z4` | `.float 0.0e+05` | `00 40 1c 00` |
| `Z25` | `.double 0.0000000000000000000000000` | `85 14 40 61 51 59 04 00` |

`fltmodel.py survivors . ../float_open`: every modelled constant fits,
and 2 of the 64 rounding-mode combinations are left - `dmul` and `dadd`
to nearest, ties to even, `ddiv` to nearest with either tie rule. The
tie rule cannot matter: `atof()`'s one division has the divisor 5\*\*k,
and an exact quotient needs the divisor mantissa's odd part to divide
the dividend's, leaving at most 56 - log2(odd part) significant bits -
too few for a tie (57) whenever the odd part is 3 or more. The smallest
odd part among the in-range `flexp` mantissas (k = 1..54, 5\*\*55
overflows) is 5. `CF` is the exact value (a truncating `dmul` would have
given `9a`), `D1` and `NZ` are the predicted bytes, `Z4` is 5\*\*4's
mantissa with exponent byte 0 - the division path's zero rule, on the
multiplication path - and `Z25` the mantissa of 5\*\*25 as a
nearest-even `dmul` builds it (truncation would end in `84`). `Z0` fits
nothing: 1.0's mantissa would be `00 00 00`.

**`libc.a`'s own `atof`, run.** `atof.o` is v7's algorithm compiled by
the real compiler; `dmath.o`, `doubles.o`, `stacks.o`, `stkmath.o` and
`convert.o` are the software floating-point runtime it calls (`fmuld`
passes the operand it addresses as `dmul`'s `[si]` and the stack top as
`[di]`; `fdivd` the stack top, the dividend, as `[si]`). Linked by
`mutos_ld` from the base64 objects - `crt0.o`, a one-instruction
`_main`, those members named explicitly, then `libc.a` (whose
`__.SYMDEF` is stale, so `-u _atof` alone does not pull the runtime in)
- and `_atof(text)` called directly under Unicorn's 8086 mode (scratch
harness, not part of `make test`), with the result read from `fac`:
**every nonzero constant in `float_coverage/`'s goldens comes out byte
for byte** (11 of 11, `M1`...`M4` included), a `.float` again being the
double's high half. Every zero comes out as eight zero bytes. Reading
the runtime explains both. `dmath.o`'s `round` (text 0x161) compares a
guard byte with 0x80, rounds a tie to even by the low mantissa bit, and
every alignment or product step folds the bits it drops into the guard
byte as a sticky bit - nearest-even; checked against exact rounding on
4,000 random operand pairs each for `ddiv` and `dadd` (including
`atof`'s shape, an integer below 2\*\*60 plus a digit): 0 differences.
`dmul` (0x1ac) tests each operand's exponent byte, `ddiv` (0x1ed) the
dividend's, and both jump to `zero` (0x1a1), which clears all eight bytes
of `fac`.

**The zero rule.** Changing only `zero` - store 0 into `fac`'s exponent
byte, leave the other seven - makes the emulated `atof` reproduce all
seven real zeros with k >= 1 (`fltzero`'s, `fltopen`'s `Z1`/`Z2`/`Z3`,
`NZ`, `Z4`, `Z25`) and changes no nonzero result. The runtime computes
`fl /= flexp` as `fldd fl` / `fdivd flexp` and `fl *= flexp` as
`fldd flexp` / `fmuld fl`; neither load touches `fac`, and for k >= 1
the repeated squaring ends with `flexp *= exp5`, so `fac` still holds
`flexp` when the zero is found - on either path, which is why the
division-path rule of the previous section and `Z4` agree. For k = 0 no
multiplication builds `flexp`, and `fac` holds whatever the digit loop
last left there: `-2**56` from `fcmp`'s subtraction in `libc.a`'s
runtime (so `00 00 80 00`), `ff ff ff 00` in the real assembler's.
Removing `dadd`'s zero shortcuts too, or its "exponents more than 56
apart" shortcut, does not produce `ff ff ff`; the real runtime differs
somewhere else as well, which one probe cannot show. So `fltconst.c`
(and `fltmodel.py`) now accept a zero wherever k >= 1 - either path,
any k whose `flexp` stays in range - and for k = 0 exactly `Z0`'s
shape: a `.float`, 18 digits (integer and fraction digits run the same
floating operations), no minus sign. A `.double` with k = 0, a negative
one (`fneg` on `Z0`'s set bit 7: flip or set?), and the LOGHUGE path
(nd - k < -39: `fl = 0`, exponent 0, so again the digit loop's
leftover) stay `FLT_ZERO`.

**`libc.a`'s multiplication is not exact - an open question the goldens
could not raise.** Checking the model against the emulated runtime on
random texts turned up zeros with k = 50..54 that differed in the low
byte - `.double 0e54`: `6d 4e a6 40 3c 0c 27 00` from the model,
`45 4e ...` from the runtime, 40 units apart. `flexp` itself differed.
On 3,000 random pairs of full 56-bit mantissas `dmul` missed the
correctly rounded product 2,217 times, by up to 403 units. The cause is
one instruction in `pmuld` (text 0x2e4..0x425, sixteen 16-bit partial
products of the mantissas' words a0..a3, b0..b3, a3/b3 the top byte
with the leading 1): the a0\*b2 term loads its multiplier with
`mov dx,2[di]` (b1) where `4[di]` (b2) was meant. The product it rounds
is therefore a\*b + (a0\*b1 - a0\*b2)\*2\*\*32 (a = `[si]`), always
between 2\*\*110 and 2\*\*112; that formula, rounded to nearest-even,
matches the emulated `dmul` on 20,000 random pairs (including
all-ones mantissas and ones with long runs of zeros), 0 differences.
It equals the exact product whenever a0 = 0 or b1 = b2 - true of every
product `atof` needs for every real constant so far: 10\*fl (10.0's
b1 = b2 = 0), the squarings up to 5\*\*32 (the low word of 5\*\*16 is 0)
and `flexp` up to 5\*\*49 (whenever the multiplier `exp5` has b1 != b2,
`flexp`'s low word is still 0), and `fl *= flexp` for k <= 3 (5,
25, 125 have b1 = b2 = 0). It differs in `fl *= flexp` for k >= 4 with
a nonzero a0 - a positive decimal exponent of 4 or more, counting digits
dropped past 2\*\*56, i.e. `%.17e` text from `e+20` up - and inside
`flexp` for k = 50..54 (`e-33` down). The real assembler's runtime
rounds like `libc.a`'s but handles zeros differently, so it is a
different version; whether it has the same `pmuld` is not known.

So `dmul` keeps two candidates: nearest-even of the exact product, and
`RM_LIBC_MUL` - nearest-even of `libc.a`'s product (`fltmodel.py`: mode
`libc`, a fifth `dmul` candidate beside the four rounding modes, so
`survivors` now checks 80 combinations; 4 survive). `MODES_MUL_ADD` is
split into `MODES_MUL` (`RM_NEAR_EVEN`, `RM_LIBC_MUL`), `MODES_ADD`
(`RM_NEAR_EVEN`) and `MODES_DIV` (`RM_NEAR_EVEN`, `RM_NEAR_AWAY`), and a
constant is accepted only if all four runs agree, as before
(`FLT_ROUNDING` otherwise; its diagnostic now names the product).
Measured over the compiler's `%.17e` form: every text with an exponent
from `e-32` to `e+19` is accepted (60,000 random 18-digit texts plus
zeros: 0 refused apart from the k = 0 zeros) - the 6% of exact floats
the previous 48 combinations refused, `CF` among them, included. Over
the whole range the `dmul` question refuses 19% of random exact floats
and 29% of random exact doubles (binary exponents -126..127), e.g.
`.float 1.26765060022822940e+30` = 2\*\*100: `00 00 00 e5` from the exact
product, `ff ff 7f e4` from `libc.a`'s. The previous 48 combinations
refused far more of that sample (49% of the floats, 93% of the doubles);
of the 20,000 floats, 224 it accepted are refused now - latent
mismatches if the real `pmuld` is `libc.a`'s - and no double. Every text
both versions accept gets the same bytes.

**The next probe**, `tests/mutos_as/float_open/fltmul.s`: five constants
whose bytes the two products decide (`.float` 2\*\*70 and 2\*\*100 in the
compiler's form, a `.double` at `e+25`, one at `e-33`, and the zero
`0.00000000000000000e-37`, whose bytes are 5\*\*54 itself under the zero
rule), three zeros with k = 0 (one digit instead of 18, `Z0`'s text as
a `.double`, `Z0` negated), the LOGHUGE path (`.double 1e-41`) and the
first negative nonzero constant. Its `README.md` has both rows of
predicted bytes; a simulated golden built from either decodes back to
exactly that `dmul` candidate.

**Verification.** Clean `make clean && make all && make test`: zero
warnings, `mutos_as` 74/74 (62 `kernel_opt`, 5 `kernel_nonopt`, 1
`libc_recon`, 6 `float_coverage` - `fltmode.s` new), `check_floatdat.sh`
13/13, `fltmodel.py survivors` ok (19 constants, 4 of 80), `fltmodel.py
check` 0 differences in 2,074 texts, `assemble_cc_goldens.sh` 71/71,
`mutos_cpp` 5/5, `mutos_c0`/`mutos_c1` 62/62. One-off: `fltmodel.py
check` with 150,000 random texts and 40,000 targeted ones (`%.17e` of
exact floats and doubles over the whole exponent range, integers
2\*\*54...2\*\*67, zeros with 0 to 40 fraction digits and exponents
-60...+25, prefixed spellings) - 0 differences. Every constant
`fltconst_test` accepted among 26,319 texts (random, targeted
large-exponent and zero texts) against the emulated `libc.a` `atof` -
nonzero ones on the unmodified runtime, zeros with k >= 1 on the one
with the changed `zero` - 0 differences; and `fltconst.c` built with
`MODES_MUL` = `RM_LIBC_MUL` alone, over 9,512 nonzero texts (most of them
exact values in the compiler's form with large exponents, where the two
products disagree), against `fltmodel.py`'s `libc` mode and the emulated
`libc.a` `atof`: 0 differences. Against the previous commit's
assembler over 154 `.s` inputs (the 74 golden sources, `floatdat.s`, the
seven `v30_speculative` sources, `float_open/fltmul.s` and the 71
compiler goldens): 152 identical objects; `fltmode.s`, refused before,
now assembles to its golden; `fltmul.s` is refused by both, by design.
ASan/UBSan (`-O0 -g`, LeakSanitizer off) over the same inputs plus a
probe of every new acceptance and refusal path: no reports, objects
and diagnostics identical to the `-O2` build's; `fltconst_test` under
both sanitizers over 60,074 texts: output identical.

## `mutos_as`: the real product, zeros at exponent 0, and `libcatof.py` (2026-09-29)

**The golden.** `float_open/fltmul.s` ran on real hardware the same day;
`fltmul.o.golden`'s data segment, in declaration order:

| Label | Text | Real bytes | Exact product | `libc.a`'s product |
|---|---|---|---|---|
| `P1` | `.float 1.18059162071741130e+21` | `ff ff 7f c6` | `00 00 00 c7` | `ff ff 7f c6` |
| `P2` | `.float 1.26765060022822940e+30` | `ff ff 7f e4` | `00 00 00 e5` | `ff ff 7f e4` |
| `P3` | `.double 1.23456789012345678e+25` | `4d ea 27 82 c9 64 23 d4` | `a6 ea ...` | `4d ea ...` |
| `P4` | `.double 1.23456789012345678e-33` | `d5 c8 7d e1 b5 20 4d 13` | `cb c8 ...` | `d5 c8 ...` |
| `Z54` | `.double 0.00000000000000000e-37` | `45 4e a6 40 3c 0c 27 00` | `6d 4e ...` | `45 4e ...` |
| `K1` | `.float 0e0` | `ff ff ff 00` | | |
| `K2` | `.double 0.00000000000000000e+17` | `00 00 00 00 ff ff ff 00` | | |
| `K3` | `.float -0.00000000000000000e+17` | `ff ff 7f 00` | | |
| `LH` | `.double 1e-41` | `00 00 00 00 00 00 00 00` | | |
| `NG` | `.float -1.50000000000000000e+00` | `00 00 c0 81` | `00 00 c0 81` | `00 00 c0 81` |

`fltmodel.py survivors . ../float_open`: 2 of 80 combinations,
`M=libc A=ne D=ne|na`. **The real assembler's multiplication rounds the
product `libc.a`'s `dmath.o` forms**, with its misplaced partial product -
five times over, two of them exact floats in the compiler's own form,
which the real assembler stores one unit below their value.
`fltconst.c`'s `MODES_MUL` becomes `RM_LIBC_MUL` alone, `fltmodel.py`'s
`EXPECTED` likewise; with `ddiv`'s tie rule the only choice left, and
`atof` never dividing to a tie, every constant the model covers is
determined and `FLT_ROUNDING` cannot occur any more. Over 130,092 texts
(random, compiler-style over the whole range, zeros, LOGHUGE) through the
previous commit's `fltconst_test` and this one: 9,773 refused as
`FLT_ROUNDING` before are accepted now, and no text both accept has
different bytes.

**Zeros with decimal exponent 0.** `K1` has one digit, `Z0` (fltmode) 18,
and both are `ff ff ff 00`; `K2` shows the low half of the same double,
`00 00 00 00`; `K3` shows the sign: `fneg` flips bit 7 of byte 6 (`ff` →
`7f`), as `libc.a`'s `stkmath.o` does - the first observation where
flipping and setting differ. `Z0`, `K1`, `K2`, `K3` follow four
different constants in their files (`NZ`, `Z54`, `K1`, `K2`), so the
leftover does not come from the previous conversion either. So the
digit loop, run over any number of zero digits, leaves the same double in
`fac`: `00 00 00 00 ff ff ff`, exponent byte then cleared by the zero
rule. Why is still not known - `libc.a`'s runtime leaves `-2**56` there
(`fcmp`'s difference; `dadd` of two zeros returns one of them), so one of
the real runtime's operations on zero operands differs from `libc.a`'s in
a way these bytes do not identify. Its float-shaped low half (0) suggests
a value produced at float precision, but that is a guess. `fltconst.c`
accepts every such zero now, both sizes and signs, as `ZERO_LOOP_FAC`
with bit 7 of byte 6 flipped for a minus sign.

**LOGHUGE.** `.double 1e-41` (nd - k = -40) is eight 0 bytes. `atof`
gives up here - `fl = 0` (the dirty-zero constant `bc a2 31 00`),
exponent 0 - so the result is a k = 0 zero, and `fac` holds whatever the
digit loop left: the last operation was the `fadd` that made `fl` = 1.0
(0 + 1 through the zero rule's `10*fl`), whose stored mantissa bits are
all 0. The emulated runtime with the changed `zero` routine gives the
same, and for other LOGHUGE texts whose last digit was accumulated it
gives `fl`'s mantissa with exponent byte 0 (`12e-45` → `00 ... 40 00`,
`123456789e-50` → `00 00 00 a0 a2 79 6b 00`; `libcatof.py check` compares
thousands of such texts); a nonzero leftover is the same arithmetic the
model already tracks. So
LOGHUGE is accepted when the text's last digit was accumulated (`fl`'s
mantissa) or all digits are 0 (`ZERO_LOOP_FAC`); after a dropped digit
the last operation was `fcmp`, whose difference `fl - 2**56` no real
constant shows, and the text stays `FLT_RANGE` (its value is below the
format's range anyway). `FLT_ZERO` is gone from `fltconst.h` and
`fltconst_test`: nothing returns it.

**`NG`** (`-1.5` → `00 00 c0 81`) is the first negative nonzero constant
observed; the model's sign handling (now a flip of bit 7 of byte 6
everywhere) already gave it.

**Still refused**: a step outside the format's exponent range. The
practical case is `flexp` = 5\*\*k for k >= 55 - `%.17e` text from `e-38`
down (`e-39` when `atof` drops the 18th digit), e.g. `.float
1.00000000000000000e-38`, whose value would fit. In `libc.a`'s runtime
the overflow reaches `__ovfl` (`fperr.o`), which sets `errno` and sends
the process `SIGFPE` - the real assembler may simply die there, which a
probe would show.

**`libcatof.py`.** The emulator harness of the previous section is now
a tool, `tests/mutos_as/float_coverage/libcatof.py` (`make
check-libcatof`; needs the Python module `unicorn`, so not part of `make
test`). It decodes the real objects from `tests/mutos1700_libc/` and
`crt0.o`, assembles a one-instruction `_main` with `mutos_as`, links with
`mutos_ld` (the members named explicitly - `libc.a`'s `__.SYMDEF` is
stale), loads the 0407 image into one 64K segment and calls `_atof` (or
`dmul`/`ddiv`/`dadd` directly) with a HLT as return address; each call
starts from a fresh copy of the image. Before use it checks the linked
code it depends on: `dmath.o`'s `zero` routine (`lea di,fac` / `sub
ax,ax` / four `stosw` / `ret`), its entry points, and `pmuld`'s
`mov dx,2[di]`. By default `zero` is patched to `mov byte fac+7,0` /
`ret` (the real assembler's zero handling), `--libc` leaves it alone.
Subcommands: `atof` (bytes for given texts), `goldens` (every golden
constant; the all-zero exponent-0 texts are reported as "not emulated",
not as differences), `check` (`fltconst_test`'s accepted output on edge
cases, seeded random texts and targeted ones - `%.17e` over the whole
range, zeros, LOGHUGE), `ops` (the runtime's `dmul`/`ddiv`/`dadd` on
random operand pairs against `fltmodel.py`'s `libc` product and
nearest-even). A build of `fltconst.c` with the exact product fails its
`check` (251 of 2,558 compared constants differ).

**Verification.** Clean `make clean && make all && make test`: zero
warnings, `mutos_as` 75/75 (62 `kernel_opt`, 5 `kernel_nonopt`, 1
`libc_recon`, 7 `float_coverage` - `fltmul.s` new), `check_floatdat.sh`
13/13, `fltmodel.py survivors` ok (29 constants, 2 of 80), `fltmodel.py
check` 0 differences in 2,092 texts, `assemble_cc_goldens.sh` 71/71,
`mutos_cpp` 5/5, `mutos_c0`/`mutos_c1` 62/62. `make check-libcatof`: 25
of 29 golden constants identical to the emulated `atof`, 4 "not
emulated", 0 different; 7,528 accepted constants compared, 0 different;
6,000 operations, 0 different. One-off: `fltmodel.py check` with 150,000
random texts, 0 differences; `libcatof.py check` with 30,000 (82,592
texts, 75,150 accepted constants compared), 0 differences. Against the
previous commit's assembler over 154 `.s` inputs: 153 identical objects,
`fltmul.s` newly assembled to its golden. ASan/UBSan over the same inputs
plus a probe of every new acceptance and refusal path: no reports,
objects and diagnostics identical to the `-O2` build's; `fltconst_test`
under both sanitizers over 75,092 texts: output identical.

## The overflow probes: `float_open/fltovf.s` and `fltsig.s` (2026-09-29)

(Written before the real run; the results are in the next section.)

What `mutos_as` still refuses is a conversion that leaves the format's
exponent range at some step (`FLT_RANGE`). Reading `libc.a`'s runtime,
and running it under `libcatof.py` with a hook on `__ovfl`/`__div0`,
shows two different mechanisms:

- **The last step, `ldexp`, wraps.** `ldexp.o` (reconstructed in
  `libc_recon/ldexp.s`) does `mov al,fac+7` / `sub ah,ah` / `add ax,bx` /
  `jo ldexp2` / `mov fac+7,al`: a 16-bit sum of the exponent byte and the
  exponent, checked only for signed 16-bit overflow, stored as its low
  byte. An exponent byte past 255 or below 1 therefore wraps, silently:
  `2.00000000000000000e+38` (the product before `ldexp` has exponent byte
  235, plus 21) becomes exponent byte 0, `1.0e+39` 2, `1.0e+50` 39;
  `2.5e-39` 0, `1.0e-39` 255, `5.0e-40` 254 - each with the value's
  mantissa. `ldexp2` (`huge`, `errno` = `ERANGE`) is reachable only for a
  16-bit overflow, i.e. an exponent far beyond anything `flexp` survives.
  Inside `atof` nothing else can leave the range for these texts: the
  digit loop stays below 2\*\*60, and a quotient `fl / flexp` with k <= 54
  has an exponent between about -125 and +60.
- **An overflow inside `atof` raises `SIGFPE`.** `flexp` = 5\*\*k overflows
  from k = 55 on (`flexp *= exp5` for k = 55..63, the squaring
  `exp5 *= exp5` itself from k = 64). In `dmul` the exponent sum
  overflows (`jo ovchk`); `ovchk` sends a wrapped-positive sum (an
  underflow) straight to `zero` - silently - and anything else through
  `call __ovfl` first. `__ovfl` (`fperr.o`) sets `errno` = `ERANGE` and
  calls `kill(getpid(), 8)`. The hooked emulation reaches `__ovfl` for
  `1.00000000000000000e-38` (5\*\*23 × 5\*\*32), `2.93873587705571877e-39`
  (5\*\*24 × 5\*\*32) and `1.0e+65` (5\*\*32 × 5\*\*32), and for none of the
  `ldexp` cases above. If the process survives the signal, `dmul` still
  ends in `zero`, and the division by that "zero" `flexp` reaches `__div0`
  - `SIGFPE` again.

So the probe is two files: `fltovf.s` holds the `ldexp` cases (two in-range
controls at the edges, `E1` 2\*\*127 in the compiler's form and `E2`
`3.0e-39`, which `mutos_as` already writes, then four above and three
below the range) and is expected to assemble, with the wrapped bytes
`libcatof.py` gives as its prediction - if the real `as` uses `libc.a`'s
`ldexp`. `fltsig.s` holds the three `__ovfl` cases, the compiler-relevant
`1.00000000000000000e-38` first; if the real `as` does not catch
`SIGFPE`, it dies there and writes no object, and the message is the
finding. Kept in one file, that death would have taken `fltovf.s`'s data
with it. `Makefile.mutos` assembles `fltovf.s` first; the modern
`Makefile`'s `goldens` now skips an empty object. A simulated golden built
from `libcatof.py`'s predictions decodes with `fltmodel.py survivors` as
expected: the controls check against the model, the seven others are
listed as "NOT MODELLED" with their bytes.

## `mutos_as`: out of range - `ldexp` wraps, an overflow aborts (2026-09-29)

Both probes of the previous section were run on real MUTOS 1700 hardware.

**`fltovf.o.golden`** (pushed 2026-09-29). Decoded with `fltmodel.py
survivors . ../float_open` before any change: `E1` and `E2` check against
the model, and the seven constants the model refused are listed as "NOT
MODELLED" with exactly the bytes `libcatof.py` had predicted:

| Label | Text | Real bytes | Exponent byte |
|---|---|---|---|
| `E1` | `.double 1.70141183460469232e+38` | `c9 ff ff ff ff ff 7f ff` | 255 (in range) |
| `E2` | `.double 3.0e-39` | `14 11 ff 27 1e ab 02 01` | 1 (in range) |
| `O1` | `.float 2.00000000000000000e+38` | `99 76 16 00` | 256 → 0 |
| `O2` | `.float -2.00000000000000000e+38` | `99 76 96 00` | 256 → 0 |
| `O3` | `.double 1.0e+39` | `eb 50 e2 a4 3f 14 3c 02` | 258 → 2 |
| `O4` | `.double 1.0e+50` | `ce 24 f3 2b 76 d8 08 27` | 295 → 39 |
| `U0` | `.double 2.5e-39` | `21 c7 53 ed dc c7 59 00` | 0 |
| `U1` | `.double 1.0e-39` | `1b 6c a9 8a 7d 39 2e ff` | -1 → 255 |
| `U2` | `.float 5.0e-40` | `7d 39 2e fe` | -2 → 254 |

The real assembler's `ldexp` is `libc.a`'s: a 16-bit sum of `fac`'s
exponent byte and the exponent, its low byte stored, no diagnostic. The
mantissa is always the value's own; the sign sits where `fneg` puts it
(`O2`), and a wrapped exponent byte of 0 (`O1`, `O2`, `U0`) gives bytes
that read as a "zero" with a nonzero mantissa - the same shape as the
real zeros.

**`fltsig.s`** - `as -o fltsig.o fltsig.s`, verbatim:

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

Exit status 4, no object. Line 41 is `S1`, `.float
1.00000000000000000e-38` (5\*\*23 × 5\*\*32 overflows in `dmul`), so the
assembly stops at the first constant and `S2`/`S3` are never converted.
There was no "Floating exception" and no core: the real `as` catches
`SIGFPE`. The emulation explains the two messages if its handler prints
and returns: with `__ovfl` and `__div0` hooked to return instead of
signalling, `atof` on `S1` reaches `__ovfl` (the overflowing `dmul`) and
then `__div0` (the division by the zero `dmul` left in `flexp`); `S2`
the same; `S3` (`1.0e+65`, on the multiplication path) only `__ovfl`.
Two signals at line 41, two messages, then the assembler gives up with
exit status 4. The `W` and `?h)` lines stand where a file name might be
expected; they are recorded as they appeared, not interpreted.

**Changes.**

- `fltconst.c`: the last step stores `(x + 128 + exp10) & 0xFF` as the
  exponent byte instead of refusing a result outside 1..255. `|exp10|` is
  at most 54 there (a larger k has already overflowed in `flexp`), so the
  16-bit sum of the real `ldexp` never overflows and its `huge` branch is
  unreachable. `convert()` returns `CV_OK`, `CV_RANGE` (an operation of the
  arithmetic overflows: `flexp`, or `fl` × `flexp` as in `.double 9.9e+54`)
  or `CV_UNKNOWN`.
- New status `FLT_UNKNOWN` (`fltconst.h`): LOGHUGE after a dropped digit
  (`fac` holds `fcmp`'s difference, never observed) and a text of more
  than 100,000 digits - both were `FLT_RANGE`. `FLT_RANGE` now means
  exactly what the real assembler aborts on, and its message quotes the
  real one. `mutos_as` keeps its exit status 1 for every error; the real
  `as` exits with 4 here - a difference noted, not changed, since nothing
  shows the real status of any other error.
- `fltconst_test` prints `UNKNOWN`; `fltmodel.py` implements the same wrap
  and the `UNKNOWN` status, and its edge cases gain the nine `fltovf.s`
  texts, `fltsig.s`'s `S2` and `S3` (`S1` was one already) and four more
  (`9.9e+38`, `1.0e+38` as a float, `9.9e+54`, an 18-digit text with
  exponent +54).
- `libcatof.py`: `__ovfl` and `__div0` are hooked; reaching either stops
  the emulation, and `atof` prints `SIGFPE (__ovfl)` instead of bytes.
  `check` now requires that every text `fltconst_test` refuses as `RANGE`
  raises `SIGFPE` and that no accepted one does. Mutation checks: a
  `fltconst.c` that refuses the wrapped results instead fails it with 57
  `RANGE` texts that raise no `SIGFPE` (of 8,357 texts at `LIBCATOF_N` =
  3000); one with the exact product fails with 749 of 7,588 compared
  texts different.
- `fltovf.s` and its golden moved to `float_coverage/` (header rewritten as
  resolved, code unchanged; its `Makefile`/`Makefile.mutos` now list eight
  files). `fltsig.s` stays in `float_open/` unchanged, so that "line 41"
  keeps meaning `S1`; that directory has no open probe, its
  `Makefile.mutos` has only the `fltsig` rule and is expected to stop with
  `*** Error code 4`, and its `README.md` records the output above.

**Verification.** Clean `make clean && make all && make test`: zero
warnings, `mutos_as` 76/76 (62 `kernel_opt`, 5 `kernel_nonopt`, 1
`libc_recon`, 8 `float_coverage` - `fltovf.s` new), `check_floatdat.sh`
13/13, `fltmodel.py survivors` ok (38 constants, 2 of 80), `fltmodel.py
check` 0 differences in 2,107 texts (150,107 in a one-off run),
`assemble_cc_goldens.sh` 71/71, `mutos_cpp` 5/5, `mutos_c0`/`mutos_c1`
62/62; `make check-docs` clean. `make check-libcatof`: 34 of 38 golden
constants identical to the emulated `atof`, 4 "not emulated" (the known
all-zero exponent-0 texts), 0 different; 8,357 texts, 0 differences, 210
`RANGE` all raising `SIGFPE`; `ops` 0 differences. A one-off `check` with
30,000: 82,607 texts, 75,589 compared, 1,717 `RANGE` all raising `SIGFPE`,
0 differences. The previous commit's assembler against this one over 156
`.s` inputs (every golden source, `floatdat.s`, `float_open/fltsig.s`, the
seven `v30_speculative` sources, the 71 compiler goldens): 155 identical
results, `fltovf.s` newly assembled to its golden; over 130,107 texts
through both `fltconst_test`s no text both accept has different bytes,
1,853 are newly accepted (wrapped), 2 went from `RANGE` to `UNKNOWN`.
ASan/UBSan over the same 156 inputs plus a probe of every new acceptance
and refusal path: no reports, objects, diagnostics and exit statuses
identical to the `-O2` build's; `fltconst_test` under both sanitizers over
the 130,107 texts: output identical.
