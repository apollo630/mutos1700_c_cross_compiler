| fltmode.s - written as an OPEN-QUESTION PROBE (in ../float_open/, see
| README.md), now a regression golden. The third round of floating-point
| probes, after fltzero.s/fltdbl.s and fltopen.s: how the real double
| arithmetic ROUNDS - dmul, dadd, ddiv - when 48 rounding-mode
| combinations still fit every golden, and zeros the model did not
| cover yet. Each constant is loaded the way mutos_c1 emits a floating
| operand (lea <reg>,<label> / call flds|fldd):
|
|   M1..M4  four ".double" constants in the compiler's form, chosen from
|           about 7,500 candidates to split the 48 combinations.
|   CF      ".float 2.93572534179687500e+03" - an exact float whose 18th
|           digit makes atof's 10*fl inexact (9b or, under a truncating
|           dmul, 9a).
|   D1, NZ  ".double 0.1" and a negative zero ".double" - predicted.
|   Z0, Z4  zeros on atof's MULTIPLICATION path (decimal exponent 0 and
|           +4: fl *= flexp) - never observed before.
|   Z25     a zero with 25 fraction digits: flexp = 5**25 is rounded.
|
| RESOLVED 2026-09-29 - the real "as" wrote (fltmode.o.golden, data
| segment in declaration order):
|
|   M1  ff ff ff ff 10 3a 7d 66    M2  ff ff ff ff ff c2 4c 64
|   M3  fe ff ff ff be 93 6e 7a    M4  05 d5 52 58 5e a5 5f bc
|   CF  9b 7b 37 8c                the exact value
|   D1  cd cc cc cc cc cc 4c 7d    NZ  00 00 c5 2e bc a2 b1 00 (both
|                                  as predicted)
|   Z0  ff ff ff 00                not flexp = 1.0's mantissa (00 00 00)
|   Z4  00 40 1c 00                mantissa of 5**4, exponent byte 0
|   Z25 85 14 40 61 51 59 04 00    the rounded 5**25's mantissa
|
| Only 2 of the 64 combinations fit all of it: dmul and dadd round to
| nearest, ties to even; ddiv to nearest (its tie rule never matters).
| Which product dmul rounds is still open: the exact one, or the one
| libc.a's dmath.o forms (a partial product from the wrong word) - no
| constant here tells them apart.
| libc.a's own atof on libc.a's runtime, run under an 8086 emulator,
| gives every nonzero constant here byte for byte. Zeros: the real
| arithmetic zeroes only the exponent byte of its accumulator, whose
| mantissa is left from the previous operation - flexp for k >= 1 (Z4,
| Z25, and every earlier zero); for Z0 (k = 0) something the digit loop
| left there. src/mutos_as/fltconst.c implements this (see fltconst.h)
| and reproduces this object byte for byte; fltmodel.py in this
| directory re-derives the remaining unknowns from every golden here.

.text
.globl	_fltmode_probe
_fltmode_probe:
push	bp
mov	bp,sp
lea	ax,M1
call	fldd
lea	ax,M2
call	fldd
lea	ax,M3
call	fldd
lea	ax,M4
call	fldd
lea	ax,CF
call	flds
lea	ax,D1
call	fldd
lea	ax,NZ
call	fldd
lea	ax,Z0
call	flds
lea	ax,Z4
call	flds
lea	ax,Z25
call	fldd
pop	bp
ret
.data
M1:	.double 1.47397409833160963e-08
M2:	.double 2.97967517326469533e-09
M3:	.double 1.45615926012396812e-02
M4:	.double 1007211910940938336
CF:	.float 2.93572534179687500e+03
D1:	.double 0.10000000000000000e+00
NZ:	.double -0.00000000000000000e+00
Z0:	.float 0.00000000000000000e+17
Z4:	.float 0.0e+05
Z25:	.double 0.0000000000000000000000000
