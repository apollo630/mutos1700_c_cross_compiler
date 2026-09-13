.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:| _a=-8.
| _b=-12.
| _c=-16.
.data
L10000:	.float 3.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-8.(bp)
call	fstsp
.data
L10001:	.float 2.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstsp
lea	ax,*-8.(bp)
call	flds
lea	ax,*-12.(bp)
call	fadds
lea	ax,*-16.(bp)
call	fstsp
lea	ax,*-8.(bp)
call	flds
lea	ax,*-12.(bp)
call	fmuls
lea	ax,*-16.(bp)
call	fstsp
lea	ax,*-16.(bp)
call	flds
call	ftoi
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*12.
jmp	L2
.globl	fltused
.data
