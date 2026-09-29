| fltovf.s - written as an OPEN-QUESTION PROBE (in ../float_open/, see
| README.md), now a regression golden. The fifth round of floating-point
| probes: what the real "as" writes when the conversion's result leaves
| the format's exponent range - above the largest value (about 1.7e38)
| or below the smallest (2**-128, about 2.94e-39) - while no step before
| the last one does. Its partner ../float_open/fltsig.s covered the
| other kind, an overflow inside atof's arithmetic (the real "as"
| aborts there).
|
| The last step of atof() is ldexp(fl, exponent). libc.a's ldexp.o
| (tests/mutos_as/libc_recon/ldexp.s) adds the exponent to fac's
| exponent byte as a 16-bit sum, checks only for a signed 16-bit
| overflow ("jo") and stores the low byte - so an exponent byte past 255
| or below 1 WRAPS. libcatof.py (libc.a's own runtime) predicted every
| constant here:
|
|   E1, E2  controls just inside the range: the compiler's text for
|           2**127, and 3.0e-39.
|   O1, O2  2.0e38 in the compiler's form, either sign: exponent byte
|           128+128 = 256, stored as 0.
|   O3, O4  1.0e+39 and 1.0e+50: 258 -> 2, 295 -> 39.
|   U0      2.5e-39, just below the smallest value: exponent byte 0.
|   U1, U2  1.0e-39 and 5.0e-40: -1 -> 255, -2 -> 254.
|
| RESOLVED 2026-09-29 - the real "as" wrote exactly the predicted bytes
| (fltovf.o.golden, data segment in declaration order), without any
| diagnostic:
|
|   E1  c9 ff ff ff ff ff 7f ff    E2  14 11 ff 27 1e ab 02 01
|   O1  99 76 16 00                O2  99 76 96 00
|   O3  eb 50 e2 a4 3f 14 3c 02    O4  ce 24 f3 2b 76 d8 08 27
|   U0  21 c7 53 ed dc c7 59 00    U1  1b 6c a9 8a 7d 39 2e ff
|   U2  7d 39 2e fe
|
| src/mutos_as/fltconst.c implements the wrap (see fltconst.h) and
| reproduces this object byte for byte.

.text
.globl	_fltovf_probe
_fltovf_probe:
push	bp
mov	bp,sp
lea	ax,E1
call	fldd
lea	ax,E2
call	fldd
lea	ax,O1
call	flds
lea	ax,O2
call	flds
lea	ax,O3
call	fldd
lea	ax,O4
call	fldd
lea	ax,U0
call	fldd
lea	ax,U1
call	fldd
lea	ax,U2
call	flds
pop	bp
ret
.data
E1:	.double 1.70141183460469232e+38
E2:	.double 3.0e-39
O1:	.float 2.00000000000000000e+38
O2:	.float -2.00000000000000000e+38
O3:	.double 1.0e+39
O4:	.double 1.0e+50
U0:	.double 2.5e-39
U1:	.double 1.0e-39
U2:	.float 5.0e-40
