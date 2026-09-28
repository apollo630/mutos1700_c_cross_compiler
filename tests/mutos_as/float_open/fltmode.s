| fltmode.s - OPEN-QUESTION PROBE (see README.md), not a regression
| golden. The third round of floating-point probes, after
| ../float_coverage/fltzero.s/fltdbl.s and fltopen.s.
|
| mutos_as now re-enacts the real assembler's conversion (v7 atof() on
| the 56-bit double, ".float" = the double's high half - see
| src/mutos_as/fltconst.h). The one unknown left is how the real double
| arithmetic ROUNDS - dmul, dadd, ddiv - and 48 combinations still fit
| every golden (../float_coverage/fltmodel.py survivors). mutos_as
| refuses every constant whose bytes those combinations disagree on,
| including some exact floats in the compiler's own "%.17e" form. This
| file pins the rounding down:
|
|   M1..M4  four ".double" constants chosen by searching compiler-style
|           texts for the ones that split the 48 combinations best;
|           together they leave only ddiv's tie rule open (and a
|           division by 5**k never ties).
|   CF      ".float 2.93572534179687500e+03" - an exact float the
|           compiler can write, refused now: its 18th digit makes
|           atof's 10*fl inexact, and a truncating dmul would make the
|           real bytes 9a 7b 37 8c instead of 9b.
|   D1, NZ  ".double 0.1" and a negative zero ".double" - accepted now
|           purely on the model's prediction (cd cc cc cc cc cc 4c 7d,
|           00 00 c5 2e bc a2 b1 00): a direct check of it.
|   Z0, Z4  zeros on atof's MULTIPLICATION path (decimal exponent 0 and
|           +4: fl *= flexp) - never observed, refused now.
|   Z25     a zero with 25 fraction digits: flexp = 5**25 is no longer
|           exact in 56 bits, so its rounded mantissa shows dmul's
|           rounding in the repeated squaring too - refused now.
|
| All are plain atof texts (MUTOS1700_Assembler_as.pdf sect. 3.2), so
| the real "as" is expected to accept every one. The CURRENT mutos_as
| refuses eight of the ten by design; that is why this file lives in
| ../float_open/ and not in make test. See README.md for the predicted
| bytes and how to read the result.

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
