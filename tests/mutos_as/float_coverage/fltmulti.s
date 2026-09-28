| fltmulti.s - more than one ".float" constant addressed from code in the
| same object, exercising the ".text"/".data" segment switch and the
| "each segment is rounded up to even once, at the last switch" rule
| docs/DEVLOG.md's 08_float section derives from malloc.o/mch.o/atof.o/
| ecvt.o - never tested as a whole object before (floatdat.s has no
| code; ldexp.s has one "lea", no ".float"). Three "lea ax,<label>" /
| runtime-call pairs in a row (load, add, multiply), matching the shape
| mutos_c1 emits for a chained float expression (src/mutos_cc/README.md).
|
| Both constants are exact in a float's 24-bit mantissa, so this
| assembles cleanly with the CURRENT mutos_as - a regression golden:
|   PI = 3.140625 = 11.001001b (9/64 = 0.140625, exact)
|   EE = 2.71875   = 10.10111b (23/32 = 0.71875, exact)
| (Deliberately not real pi/e - those are irrational, not exactly
| representable, and when this was written fltconst.h refused anything
| inexact rather than round or truncate it.)

.text
.globl	_fltmulti_probe
_fltmulti_probe:
push	bp
mov	bp,sp
lea	ax,PI
call	flds
lea	ax,EE
call	fadds
lea	ax,PI
call	fmuls
pop	bp
ret
.data
PI:	.float 3.14062500000000000e+00
EE:	.float 2.71875000000000000e+00
