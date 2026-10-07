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
.globl	_two
.text
.even
_two:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
| _y=12.
jmp	L4
L5:lea	ax,*4.(bp)
call	fldd
lea	ax,*12.(bp)
call	faddd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L6
L6:|RTYP 3
jmp	cret
L4:jmp	L5
.globl	_f1
.text
.even
_f1:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _d=-12.
| _x=-14.
.data
L10001:	.float 1.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10002:	.float 3.00000000000000000e+00
.text
lea	ax,L10002
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
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*10.
jmp	L8
.globl	_f2
.text
.even
_f2:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L10
L11:| _d=-12.
| _x=-14.
.data
L10003:	.float 1.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
mov	*-14.(bp),ax
.data
L10004:	.float 3.00000000000000000e+00
.text
lea	ax,L10004
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
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*10.
jmp	L11
.globl	_f3
.text
.even
_f3:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L13
L14:| _d=-12.
| _e=-20.
.data
L10006:	.float 1.00000000000000000e+00
.text
.data
L10005:	.float 3.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10006
call	fadds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L15
L15:|RTYP 0
jmp	cret
L13:sub	sp,*16.
jmp	L14
.globl	_f4
.text
.even
_f4:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L16
L17:| _d=-12.
| _x=-14.
mov	*-14.(bp),*0.
.data
L10008:	.float 1.00000000000000000e+00
.text
.data
L10007:	.float 3.00000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-12.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10008
call	flds
call	fcmp
sahf
ble	L19
mov	*-14.(bp),*1.
L19:mov	di,*-14.(bp)
mov	ax,di
jmp	L18
L18:|RTYP 0
jmp	cret
L16:sub	sp,*10.
jmp	L17
.globl	_f5
.text
.even
_f5:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L20
L21:| _d=-12.
| _x=-14.
.data
L10010:	.float 1.00000000000000000e+00
.text
.data
L10009:	.float 3.00000000000000000e+00
.text
lea	ax,L10009
call	flds
lea	ax,*-12.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10010
call	flds
call	fcmp
sahf
bgt	L10011
mov	di,*0.
jmp	L10012
L10011:mov	di,*1.
L10012:mov	*-14.(bp),di
mov	di,*-14.(bp)
mov	ax,di
jmp	L22
L22:|RTYP 0
jmp	cret
L20:sub	sp,*10.
jmp	L21
.globl	_f6
.text
.even
_f6:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L23
L24:| _d=-12.
.data
L10013:	.float 3.00000000000000000e+00
.text
lea	ax,L10013
call	flds
lea	ax,*-12.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L25
L25:|RTYP 3
jmp	cret
L23:sub	sp,*8.
jmp	L24
.globl	_f7
.text
.even
_f7:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L26
L27:| _d=-12.
| _e=-20.
| _x=-22.
.data
L10015:	.float 2.00000000000000000e+00
.text
lea	ax,L10015
call	flds
lea	ax,*-20.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
.data
L10014:	.float 1.00000000000000000e+00
.text
lea	ax,L10014
call	flds
lea	ax,*-12.(bp)
call	fstd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_two
add	sp,*16.
call	fldd
call	ftoi
mov	*-22.(bp),ax
mov	di,*-22.(bp)
mov	ax,di
jmp	L28
L28:|RTYP 0
jmp	cret
L26:sub	sp,*18.
jmp	L27
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L29
L30:call	_f7
push	ax
call	_f5
push	ax
call	_f4
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
pop	bx
add	ax,bx
pop	bx
add	ax,bx
jmp	L31
L31:|RTYP 0
jmp	cret
L29:jmp	L30
.globl	fltused
.data
