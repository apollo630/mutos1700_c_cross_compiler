.globl	_gi
.data
_gi:	.double	2.00000000000000000e+00
.globl	_gx
.data
_gx:	.double	1.00000000000000000e-01
.globl	_gy
.data
_gy:	.float	9.99999940395355225e-02
.globl	_gn
.data
_gn:	.double	-1.50000000000000000e+00
.data
_gs:	.double	2.50000000000000000e+00
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
L10005:	.float 3.50000000000000000e+00
.text
lea	ax,_gy
call	flds
lea	ax,L10005
call	fadds
lea	ax,_gi
call	faddd
lea	ax,_gn
call	faddd
lea	ax,_gs
call	faddd
lea	ax,_gx
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*8.
jmp	L2
.globl	fltused
.data
