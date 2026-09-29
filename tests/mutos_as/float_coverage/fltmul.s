| fltmul.s - written as an OPEN-QUESTION PROBE (in ../float_open/, see
| README.md), now a regression golden. The fourth round of floating-point
| probes, after fltmode.s pinned the rounding (nearest-even everywhere):
| WHICH PRODUCT the real dmul rounds - the exact one, or the one libc.a's
| dmath.o forms, a*b + (a0*b1 - a0*b2) * 2**32 (its partial-product
| routine loads b1's word where b2's was meant) - and the zeros
| fltmode.s's Z0 left open. Each constant is loaded the way mutos_c1
| emits a floating operand (lea <reg>,<label> / call flds|fldd):
|
|   P1, P2  two exact floats in the compiler's form (2**70, 2**100).
|   P3, P4  a ".double" on the multiplication path (e+25) and one whose
|           flexp = 5**50 already depends on the product (e-33).
|   Z54     a zero with 54 fraction places: flexp = 5**54 itself.
|   K1..K3  zeros with decimal exponent 0: one digit, Z0's text as a
|           ".double", Z0 negated.
|   LH      a value atof() gives up on (LOGHUGE: fl = 0, exponent 0).
|   NG      a negative nonzero constant.
|
| RESOLVED 2026-09-29 - the real "as" wrote (fltmul.o.golden, data
| segment in declaration order):
|
|   P1  ff ff 7f c6                libc.a's product (the exact one: 00 00 00 c7)
|   P2  ff ff 7f e4                libc.a's product (the exact one: 00 00 00 e5)
|   P3  4d ea 27 82 c9 64 23 d4    libc.a's product (the exact one: a6 ...)
|   P4  d5 c8 7d e1 b5 20 4d 13    libc.a's product (the exact one: cb ...)
|   Z54 45 4e a6 40 3c 0c 27 00    5**54 as libc.a's product builds it
|   K1  ff ff ff 00                like Z0 (18 digits): no digit-count effect
|   K2  00 00 00 00 ff ff ff 00    Z0's high half, low half 0
|   K3  ff ff 7f 00                fneg flips bit 7 of byte 6
|   LH  00 00 00 00 00 00 00 00    fl = 1.0 left in fac, exponent byte 0
|   NG  00 00 c0 81                as predicted
|
| All five product constants have libc.a's product: the real assembler
| multiplies like libc.a's dmath.o. src/mutos_as/fltconst.c implements
| that (see fltconst.h) and reproduces this object byte for byte;
| fltmodel.py in this directory re-derives the remaining unknowns from
| every golden here, and libcatof.py runs libc.a's own atof on these
| texts.

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
