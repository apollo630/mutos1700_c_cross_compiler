.globl	_gz
.data
_gz:	.double	0.00000000000000000e+00
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
| _e=-20.
| _f=-24.
| _i=-26.
| _j=-28.
| _r=-30.
| _l=-34.
mov	*-26.(bp),*7.
mov	*-28.(bp),*4.
mov	*-30.(bp),*0.
.data
L10001:	.float 1.50000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10002:	.float 2.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10003:	.float 5.00000000000000000e-01
.text
lea	ax,L10003
call	flds
lea	ax,*-24.(bp)
call	fstsp
.data
L10004:	.float 2.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-20.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-30.(bp)
mov	*-30.(bp),ax
mov	di,*-26.(bp)
mov	ax,di
call	itof
lea	ax,*-24.(bp)
call	fadds
lea	ax,*-24.(bp)
call	fstsp
lea	ax,*-24.(bp)
call	flds
call	ftoi
add	ax,*-30.(bp)
mov	*-30.(bp),ax
.data
L10005:	.float 2.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10005
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-30.(bp)
mov	*-30.(bp),ax
.data
L10006:	.float 4.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10006
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-30.(bp)
mov	*-30.(bp),ax
mov	ax,*-26.(bp)
cwd
idiv	*-28.(bp)
mov	ax,dx
call	itof
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-30.(bp)
mov	*-30.(bp),ax
.data
L10007:	.float 3.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
lea	ax,L10007
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-30.(bp)
mov	*-30.(bp),ax
.data
L10008:	.float 3.75000000000000000e+00
.text
lea	ax,L10008
call	flds
call	ftol
mov	di,dx
mov	si,ax
mov	*-32.(bp),si
mov	*-34.(bp),di
mov	di,*-30.(bp)
add	di,*-32.(bp)
mov	*-30.(bp),di
lea	ax,_gz
call	fldd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-30.(bp)
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*30.
jmp	L2
.globl	fltused
.data
