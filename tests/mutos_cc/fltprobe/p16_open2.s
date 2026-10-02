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
| _p=-22.
| _i=-24.
| _j=-26.
| _r=-28.
| _c=-30.
| _u=-32.
mov	*-24.(bp),*2.
mov	*-26.(bp),*3.
movb	*-30.(bp),*4.
mov	*-32.(bp),*5.
mov	*-28.(bp),*0.
.data
L10000:	.float 1.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	di,*-12.(bp)
mov	*-22.(bp),di
mov	di,*-22.(bp)
lea	ax,(di)
call	fldd
mov	di,*-24.(bp)
mov	ax,di
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-28.(bp)
mov	*-28.(bp),ax
.data
L10001:	.float 2.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-20.(bp)
call	fstd
mov	ax,*-24.(bp)
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-28.(bp)
mov	*-28.(bp),ax
mov	ax,*-24.(bp)
cwd
idiv	*-26.(bp)
mov	ax,dx
call	itof
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-28.(bp)
mov	*-28.(bp),ax
.data
L10002:	.float 0.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-28.(bp)
mov	*-28.(bp),ax
.data
L10003:	.float 2.50000000000000000e+00
.text
lea	ax,L10003
call	flds
call	ftoi
add	ax,*-28.(bp)
mov	*-28.(bp),ax
.data
L10004:	.float 2.50000000000000000e+00
.text
lea	ax,L10004
call	flds
call	ftoi
mov	*-24.(bp),ax
mov	di,*-28.(bp)
add	di,*-24.(bp)
mov	*-28.(bp),di
movb	ax,*-30.(bp)
cbw
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-28.(bp)
mov	*-28.(bp),ax
mov	si,*-32.(bp)
sub	di,di
push	si
push	di
call	ltof
add	sp,*4
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-28.(bp)
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*28.
jmp	L2
.globl	fltused
.data
