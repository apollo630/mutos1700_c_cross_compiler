.globl	_seven
.text
.even
_seven:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:mov	di,*7.
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L4
L5:| _d=-12.
| _e=-20.
| _i=-22.
| _j=-24.
| _r=-26.
mov	*-22.(bp),*13.
mov	*-24.(bp),*4.
mov	*-26.(bp),*0.
.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10001:	.float 5.00000000000000000e-01
.text
lea	ax,L10001
call	flds
mov	di,*-22.(bp)
sal	di,*1
sal	di,*1
mov	ax,di
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-26.(bp)
mov	*-26.(bp),ax
mov	di,*-22.(bp)
add	di,*-6.
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-26.(bp)
mov	*-26.(bp),ax
mov	di,*0.
sub	di,*-22.(bp)
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-26.(bp)
mov	*-26.(bp),ax
mov	ax,*-22.(bp)
cwd
idiv	*-24.(bp)
call	itof
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-26.(bp)
mov	*-26.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fdivd
mov	ax,*-22.(bp)
mov	cx,*3.
imul	cx
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-26.(bp)
mov	*-26.(bp),ax
call	_seven
add	*-22.(bp),ax
call	_seven
sub	*-22.(bp),ax
mov	di,*-26.(bp)
add	di,*-22.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*22.
jmp	L5
.globl	fltused
.data
