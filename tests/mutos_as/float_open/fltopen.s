| fltopen.s - OPEN-QUESTION PROBE (see README.md), not a regression
| golden - the four spellings STATUS.md's open item 7 still lists as
| unresolved after the 2026-09-28 mutos_as implementation of ".float"
| zero and ".double" (see src/mutos_as/fltconst.h and CLAUDE.md's
| "Next up"):
|
|   Z1  .float 0.0                             - a short zero spelling,
|       not the confirmed "%.17e" one (0.00000000000000000e+00,
|       ../float_coverage/fltzero.o.golden's bc a2 31 00). Real bytes
|       for THIS spelling are not known: the confirmed bytes look like
|       a leftover of scaling 17 fraction digits by 5**17 (v7 atof()'s
|       flexp), so a text with a different fraction-digit count may
|       well convert to something else - or to a clean zero.
|   Z2  .float -0.00000000000000000e+00        - the CONFIRMED zero
|       spelling, negated. Does the sign bit (byte 2's top bit) get
|       set on an otherwise-zero exponent byte, or is zero special-
|       cased to ignore the sign?
|   Z3  .double 0.00000000000000000e+00        - zero in the 8-byte
|       ".double" format (implemented 2026-09-28, but only for
|       exactly representable non-zero values - see fltconst.h). No
|       real 8-byte zero exists anywhere in tests/mutos1700_libc's
|       objects to compare against.
|   IN  .float 0.10000000000000000e+00         - 0.1 is not exactly
|       representable in a float's 24-bit mantissa (it is a repeating
|       binary fraction). Whether the real assembler rounds it
|       (to nearest? by truncation?) or refuses it outright is not
|       known - fltconst.c refuses every inexact value rather than
|       guess (FLT_INEXACT).
|
| All four are plain "atof"-syntax decimal texts per
| docs/MUTOS1700_Assembler_as.pdf sect. 3.2 ("Zeichen ..., die atof als
| Floating-Point-Zahl akzeptiert"), which does not say "exactly
| representable" anywhere, so the real "as" is expected to accept all
| four without error - this is an open-question probe about what BYTES
| it writes, not about whether it errors. The CURRENT mutos_as refuses
| all four (see README.md): Z1/Z2 do not match the one confirmed zero
| shape, Z3 is a zero seen only in the .float format so far, and IN is
| caught by the same "inexact is refused" rule that already applies to
| .float. That refusal is by design, not a bug to "fix" some other way
| - see fltconst.h's header comment.
|
| Each constant is loaded exactly as mutos_c1 emits a floating operand
| (lea <reg>,<label> / call flds|fldd - docs/DEVLOG.md's 08_float
| section), so the object is a normal whole-object test, not a
| data-only file like ../libc_recon/floatdat.s.

.text
.globl	_fltopen_probe
_fltopen_probe:
push	bp
mov	bp,sp
lea	ax,Z1
call	flds
lea	ax,Z2
call	flds
lea	ax,Z3
call	fldd
lea	ax,IN
call	flds
pop	bp
ret
.data
Z1:	.float 0.0
Z2:	.float -0.00000000000000000e+00
Z3:	.double 0.00000000000000000e+00
IN:	.float 0.10000000000000000e+00
