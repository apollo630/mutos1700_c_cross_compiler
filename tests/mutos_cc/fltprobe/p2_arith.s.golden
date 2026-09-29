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
| _a=-24.
| _b=-28.
| _i=-30.
| _j=-32.
.data
L10000:	.float 4.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.float 2.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10002:	.float 1.50000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-24.(bp)
call	fstsp
.data
L10003:	.float 5.00000000000000000e-01
.text
lea	ax,L10003
call	flds
lea	ax,*-28.(bp)
call	fstsp
mov	*-30.(bp),*3.
mov	*-32.(bp),*2.
mov	di,*-30.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
mov	di,*-30.(bp)
mov	ax,di
call	itof
call	fsub
lea	ax,*-12.(bp)
call	fstdp
mov	di,*-30.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
mov	di,*-30.(bp)
mov	ax,di
call	itof
call	fdiv
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-24.(bp)
call	flds
mov	di,*-30.(bp)
mov	ax,di
call	itof
call	fsub
lea	ax,*-12.(bp)
call	fstdp
mov	di,*-30.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fsubd
lea	ax,*-12.(bp)
call	fstdp
mov	ax,*-30.(bp)
imul	*-32.(bp)
call	itof
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
call	fmul
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	flds
lea	ax,*-24.(bp)
call	flds
lea	ax,*-20.(bp)
call	faddd
call	fmul
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-24.(bp)
call	flds
lea	ax,*-28.(bp)
call	fadds
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
call	fmul
lea	ax,*-12.(bp)
call	fstdp
.data
L10004:	.float 2.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,L10004
call	fmuls
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.float 5.00000000000000000e-01
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,L10005
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fsubd
lea	ax,*-12.(bp)
call	fstdp
.data
L10006:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10006
call	fadds
lea	ax,*-12.(bp)
call	fstdp
.data
L10007:	.float 5.00000000000000000e-01
.text
lea	ax,*-24.(bp)
call	flds
lea	ax,L10007
call	fsubs
lea	ax,*-24.(bp)
call	fstsp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
call	fdiv
lea	ax,*-12.(bp)
call	fstdp
mov	di,*-30.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstd
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
mov	ax,ax
imul	*-30.(bp)
mov	*-30.(bp),ax
lea	ax,*-20.(bp)
call	fldd
call	ftoi
mov	*-32.(bp),ax
mov	di,*-30.(bp)
add	di,*-32.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*28.
jmp	L2
.globl	fltused
.data
