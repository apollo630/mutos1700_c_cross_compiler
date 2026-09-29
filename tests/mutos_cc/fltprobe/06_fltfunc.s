.globl	_scale
.text
.even
_scale:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
| _n=12.
jmp	L1
L2:.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*4.(bp)
call	fmuld
lea	ax,*4.(bp)
call	fstdp
lea	ax,*4.(bp)
call	fldd
lea	ax,*4.(bp)
call	faddd
mov	di,*12.(bp)
mov	ax,di
call	itof
call	fmul
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L3
L3:|RTYP 3
jmp	cret
L1:jmp	L2
.globl	_mid
.text
.even
_mid:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _b=12.
jmp	L4
L5:| _two=-12.
.data
L10001:	.float 2.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*4.(bp)
call	fldd
lea	ax,*12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fdivd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L6
L6:|RTYP 3
jmp	cret
L4:sub	sp,*8.
jmp	L5
.globl	_half
.text
.even
_half:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L7
L8:.data
L10002:	.float 2.00000000000000000e+00
.text
lea	ax,*4.(bp)
call	fldd
lea	ax,L10002
call	fdivs
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L9
L9:|RTYP 3
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
| _f=-24.
| _i=-26.
.data
L10003:	.float 1.50000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10004:	.float 2.50000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-24.(bp)
call	fstsp
mov	*-26.(bp),*3.
push	*-26.(bp)
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_scale
add	sp,*10.
call	fldd
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
lea	ax,*-24.(bp)
call	flds
sub	sp,*8
mov	ax,sp
call	fstdp
call	_mid
add	sp,*16.
call	fldd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
call	ftoi
mov	*-26.(bp),ax
mov	di,*-26.(bp)
mov	ax,di
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*22.
jmp	L11
.globl	fltused
.data
