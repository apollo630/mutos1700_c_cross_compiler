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
jmp	L1
L2:.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,*4.(bp)
call	fldd
lea	ax,L10000
call	fdivs
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L3
L3:|RTYP 3
jmp	cret
L1:jmp	L2
.globl	_f1
.text
.even
_f1:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L4
L5:| _d=-12.
| _x=-14.
.data
L10001:	.float 3.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
call	ftoi
mov	*-14.(bp),ax
mov	di,*-14.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*10.
jmp	L5
.globl	_f2
.text
.even
_f2:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _d=-12.
.data
L10002:	.float 2.50000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*8.
jmp	L8
.globl	_f3
.text
.even
_f3:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L10
L11:| _d=-12.
| _e=-20.
| _x=-22.
.data
L10003:	.float 1.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10004:	.float 2.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10005:	.float 3.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
.data
L10006:	.float 4.00000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-20.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fsubd
call	fmul
call	ftoi
mov	*-22.(bp),ax
mov	di,*-22.(bp)
mov	ax,di
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*18.
jmp	L11
.globl	_f4
.text
.even
_f4:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L13
L14:| _d=-12.
| _e=-20.
| _x=-22.
.data
L10007:	.float 1.00000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10008:	.float 2.00000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10009:	.float 3.00000000000000000e+00
.text
lea	ax,L10009
call	flds
lea	ax,*-12.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
.data
L10010:	.float 4.00000000000000000e+00
.text
lea	ax,L10010
call	flds
lea	ax,*-20.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
mov	*-22.(bp),*0.
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fsubd
call	fcmp
sahf
ble	L16
mov	*-22.(bp),*1.
L16:mov	di,*-22.(bp)
mov	ax,di
jmp	L15
L15:|RTYP 0
jmp	cret
L13:sub	sp,*18.
jmp	L14
.globl	_f5
.text
.even
_f5:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L17
L18:| _d=-12.
.data
L10011:	.float 3.00000000000000000e+00
.text
lea	ax,L10011
call	flds
lea	ax,*-12.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
lea	ax,*-12.(bp)
call	fldd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L19
L19:|RTYP 3
jmp	cret
L17:sub	sp,*8.
jmp	L18
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L20
L21:call	_f4
push	ax
call	_f3
push	ax
call	_f2
push	ax
call	_f1
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
call	itof
call	_f5
call	fldd
call	fadd
call	ftoi
jmp	L22
L22:|RTYP 0
jmp	cret
L20:jmp	L21
.globl	fltused
.data
