.globl	_gf
.data
_gf:	.float	4.00000000000000000e+00
.globl	_gm
.data
_gm:	.double	-2.00000000000000000e+00
.globl	_gg
.data
_gg:	.float	-9.99999940395355225e-02
.data
_gs:	.float	2.50000000000000000e+00
.globl	_gt
.data
_gt:	.float	2.99999982118606567e-01
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
L2:| _d=-12.
.data
L10005:	.float 1.00000000000000000e+01
.text
lea	ax,_gg
call	flds
lea	ax,L10005
call	fmuls
.data
L10006:	.float 1.00000000000000000e+01
.text
lea	ax,_gt
call	flds
lea	ax,L10006
call	fmuls
call	fadd
lea	ax,_gf
call	fadds
lea	ax,_gs
call	fadds
lea	ax,_gm
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10007:	.float 5.00000000000000000e-01
.text
lea	ax,L10007
call	flds
lea	ax,*-12.(bp)
call	faddd
call	ftoi
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*8.
jmp	L2
.globl	fltused
.data
