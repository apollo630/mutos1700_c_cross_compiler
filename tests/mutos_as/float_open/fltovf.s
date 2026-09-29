| fltovf.s - OPEN-QUESTION PROBE (see README.md), not a regression
| golden. The fifth round of floating-point probes: what the real "as"
| writes when the conversion's result leaves the format's exponent
| range - above the largest value (about 1.7e38) or below the smallest
| (2**-128, about 2.94e-39) - while no step before the last one does.
| Its partner fltsig.s covers the other kind, an overflow inside atof's
| arithmetic; they are two files because that one may kill "as".
|
| The last step of atof() is ldexp(fl, exponent). libc.a's ldexp.o
| (tests/mutos_as/libc_recon/ldexp.s) adds the exponent to fac's
| exponent byte as a 16-bit sum, checks only for a signed 16-bit
| overflow ("jo"), and stores the low byte - so an exponent byte past
| 255 or below 1 WRAPS. Run on libc.a's own runtime (libcatof.py), none
| of these texts reaches __ovfl, so nothing raises SIGFPE, and each gives
| the value's mantissa with a wrapped exponent byte:
|
|   E1, E2  controls just inside the range (mutos_as accepts them): the
|           compiler's text for 2**127 and 3.0e-39.
|   O1, O2  2.0e38 in the compiler's form, either sign: exponent byte
|           128+128 = 256, stored as 0.
|   O3, O4  1.0e+39 and 1.0e+50: 258 -> 2, 295 -> 39.
|   U0      2.5e-39, just below the smallest value: exponent byte 0.
|   U1, U2  1.0e-39 and 5.0e-40: -1 -> 255, -2 -> 254.
|
| If the real ldexp checks the range instead, these show how (a
| saturated value, zero, an error message). All are plain atof texts
| (MUTOS1700_Assembler_as.pdf sect. 3.2). The CURRENT mutos_as refuses
| seven of the nine (out of range) by design; that is why this file
| lives in ../float_open/ and not in make test. See README.md for the
| predicted bytes and how to read the result.

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
