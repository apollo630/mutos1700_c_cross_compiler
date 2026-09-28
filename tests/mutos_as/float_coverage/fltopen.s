| fltopen.s - written as an OPEN-QUESTION PROBE (in ../float_open/, see
| README.md), now a regression golden. Four spellings STATUS.md's open
| item 7 left unresolved after the 2026-09-28 implementation of a zero
| ".float" and ".double", each loaded the way mutos_c1 emits a floating
| operand (lea <reg>,<label> / call flds|fldd):
|
|   Z1  .float 0.0                          - a short zero spelling
|   Z2  .float -0.00000000000000000e+00     - the confirmed zero, negated
|   Z3  .double 0.00000000000000000e+00     - a zero ".double"
|   IN  .float 0.10000000000000000e+00      - a value not exact in 24 bits
|
| RESOLVED 2026-09-28 - the real "as" wrote (fltopen.o.golden, data
| segment in declaration order):
|
|   Z1  00 00 20 00                  exponent byte 0, mantissa of 5**1
|   Z2  bc a2 b1 00                  the confirmed zero with the sign bit
|   Z3  00 00 c5 2e bc a2 31 00      mantissa of 5**17 in 56 bits; its
|                                    high half is fltzero.o's bc a2 31 00
|   IN  cc cc 4c 7d                  0.1 TRUNCATED to 24 bits (correct
|                                    rounding would give cd cc 4c 7d)
|
| Z1 and Z3 are exactly the bytes docs/DEVLOG.md predicted before the
| run from v7 atof()'s algorithm (a zero dividend keeps the divisor
| flexp = 5**k's mantissa); IN shows a ".float" is the high half of the
| double conversion. src/mutos_as/fltconst.c now implements that model
| (see fltconst.h) and reproduces this object byte for byte;
| fltmodel.py in this directory re-derives the model's remaining
| unknowns from every golden here.

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
