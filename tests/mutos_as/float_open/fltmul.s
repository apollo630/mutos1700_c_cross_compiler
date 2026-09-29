| fltmul.s - OPEN-QUESTION PROBE (see README.md), not a regression
| golden. The fourth round of floating-point probes, after
| ../float_coverage/fltmode.s pinned the rounding: every double
| operation rounds to nearest, ties to even (see src/mutos_as/fltconst.h).
|
| What is left is WHICH PRODUCT the real dmul rounds. libc.a's own
| dmath.o forms a*b plus (a0*b1 - a0*b2) * 2**32 - its partial-product
| routine loads b1's word where b2's was meant - and libc.a's atof run
| on libc.a's runtime reproduces every nonzero real constant so far,
| because none of them needed a product where that differs from the
| exact one. mutos_as refuses every constant whose bytes the two
| products disagree on (FLT_ROUNDING), e.g. exact floats in the
| compiler's own "%.17e" form from e+20 up. This file asks, five times
| over, and asks the zero questions fltmode.s left open:
|
|   P1, P2  two exact floats in the compiler's form (2**70, 2**100):
|           the exact product gives the value, libc.a's one float unit
|           less.
|   P3, P4  a ".double" on the multiplication path (e+25) and one whose
|           flexp = 5**50 already differs (e-33).
|   Z54     a zero with 54 fraction places: its bytes ARE flexp = 5**54
|           under the zero rule, so they show the product directly.
|   K1..K3  zeros with decimal exponent 0, like fltmode.s's Z0
|           (ff ff ff 00): one digit instead of 18, a ".double" (Z0's low
|           half), and a minus sign on top of Z0's set bit 7.
|   LH      a value atof() gives up on (nd - k < -39: fl = 0,
|           exponent 0) - what the digit loop leaves in fac for "1".
|   NG      a negative nonzero constant: none has been observed yet.
|
| All are plain atof texts (MUTOS1700_Assembler_as.pdf sect. 3.2), so
| the real "as" is expected to accept every one. The CURRENT mutos_as
| refuses nine of the ten by design; that is why this file lives in
| ../float_open/ and not in make test. See README.md for the predicted
| bytes and how to read the result.

.text
.globl	_fltmul_probe
_fltmul_probe:
push	bp
mov	bp,sp
lea	ax,P1
call	flds
lea	ax,P2
call	flds
lea	ax,P3
call	fldd
lea	ax,P4
call	fldd
lea	ax,Z54
call	fldd
lea	ax,K1
call	flds
lea	ax,K2
call	fldd
lea	ax,K3
call	flds
lea	ax,LH
call	fldd
lea	ax,NG
call	flds
pop	bp
ret
.data
P1:	.float 1.18059162071741130e+21
P2:	.float 1.26765060022822940e+30
P3:	.double 1.23456789012345678e+25
P4:	.double 1.23456789012345678e-33
Z54:	.double 0.00000000000000000e-37
K1:	.float 0e0
K2:	.double 0.00000000000000000e+17
K3:	.float -0.00000000000000000e+17
LH:	.double 1e-41
NG:	.float -1.50000000000000000e+00
