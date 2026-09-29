.globl	_tw
.text
.even
_tw:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L1
L2:lea	ax,*4.(bp)
call	fldd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L3
L3:|RTYP 3
jmp	cret
L1:jmp	L2
.globl	_ff
.text
.even
_ff:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L4
L5:lea	ax,*4.(bp)
call	fldd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L6
L6:|RTYP 2
jmp	cret
L4:jmp	L5
.globl	_fi
.text
.even
_fi:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _d=4.
| _i=12.
jmp	L7
L8:mov	di,*12.(bp)
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:jmp	L8
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L10
L11:| _d=-12.
| _e=-20.
| _l=-24.
| _c=-26.
|NREG 2
| _r=di
.data
L10000:	.float 2.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.float 1.50000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
lea	ax,*-20.(bp)
call	fstdp
mov	si,*3.
push	si
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_fi
add	sp,*10.
mov	di,ax
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_ff
add	sp,*8.
call	fldd
lea	ax,*-20.(bp)
call	fstdp
mov	ax,*7.
cwd
mov	si,dx
mov	dx,ax
mov	*-22.(bp),dx
mov	*-24.(bp),si
mov	si,*-22.(bp)
push	si
mov	si,*-24.(bp)
push	si
call	ltof
add	sp,*4
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftol
mov	si,dx
mov	dx,ax
mov	*-22.(bp),dx
mov	*-24.(bp),si
lea	ax,*-12.(bp)
call	fldd
call	ftoi
movb	*-26.(bp),ax
movb	ax,*-26.(bp)
cbw
call	itof
lea	ax,*-12.(bp)
call	fstdp
mov	si,di
mov	ax,si
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
lea	ax,*-20.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
call	fadd
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L12
|NREG 3
L12:|RTYP 0
jmp	cret
L10:sub	sp,*22.
jmp	L11
.globl	fltused
.data
