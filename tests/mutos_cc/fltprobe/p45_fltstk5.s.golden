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
.data
L10001:	.float 3.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
mov	di,*1.
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*8.
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
L11:| _e=-12.
| _f=-16.
.data
L10002:	.float 1.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-16.(bp)
call	fstsp
.data
L10003:	.float 1.00000000000000000e+00
.text
lea	ax,*-16.(bp)
call	flds
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10003
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*12.
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
L14:| _s=-20.
| _q=-22.
| _a=-46.
| _e=-54.
| _i=-56.
mov	*-56.(bp),*1.
.data
L10004:	.float 2.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-38.(bp)
call	fstdp
.data
L10005:	.float 4.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	di,*-20.(bp)
mov	*-22.(bp),di
.data
L10006:	.float 1.00000000000000000e+00
.text
lea	di,*-46.(bp)
mov	si,*-56.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10006
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10007:	.float 1.00000000000000000e+00
.text
mov	di,*-22.(bp)
lea	ax,*8.(di)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10007
call	fadds
lea	ax,*-54.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
call	ftoi
jmp	L15
L15:|RTYP 0
jmp	cret
L13:sub	sp,*52.
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
| _e=-20.
| _i=-22.
.data
L10008:	.float 1.00000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,*-12.(bp)
call	fstdp
mov	*-22.(bp),*2.
.data
L10009:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
call	fneg
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10009
call	fadds
lea	ax,*-20.(bp)
call	fstdp
.data
L10010:	.float 1.00000000000000000e+00
.text
mov	di,*-22.(bp)
mov	ax,di
call	itof
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10010
call	fadds
lea	ax,*-20.(bp)
call	fstdp
.data
L10012:	.float 1.00000000000000000e+00
.text
.data
L10011:	.float 2.00000000000000000e+00
.text
lea	ax,L10011
call	flds
lea	ax,*-12.(bp)
call	fmuld
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10012
call	fadds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L18
L18:|RTYP 0
jmp	cret
L16:sub	sp,*18.
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
jmp	L19
L20:| _d=-12.
| _e=-20.
.data
L10013:	.float 1.00000000000000000e+00
.text
lea	ax,L10013
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10015:	.float 1.00000000000000000e+00
.text
.data
L10014:	.float 2.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10014
call	fmuls
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10015
call	fadds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L21
L21:|RTYP 0
jmp	cret
L19:sub	sp,*16.
jmp	L20
.globl	_f6
.text
.even
_f6:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L22
L23:| _d=-12.
| _e=-20.
.data
L10016:	.float 1.00000000000000000e+00
.text
lea	ax,L10016
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10017:	.float 2.00000000000000000e+00
.text
lea	ax,L10017
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10018:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
sub	sp,*8
mov	ax,sp
call	fstdp
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_two
add	sp,*16.
call	fldd
lea	ax,L10018
call	fadds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L24
L24:|RTYP 0
jmp	cret
L22:sub	sp,*16.
jmp	L23
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L25
L26:call	_f6
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
jmp	L27
L27:|RTYP 0
jmp	cret
L25:jmp	L26
.globl	fltused
.data
