.globl	_pick
.text
.even
_pick:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _i=6.
jmp	L1
L2:mov	di,*6.(bp)
mov	cx,*3.
sal	di,cl
add	di,*4.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 11
jmp	cret
L1:jmp	L2
.globl	_twice
.text
.even
_twice:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _p=4.
jmp	L4
L5:.data
L10000:	.float 2.00000000000000000e+00
.text
mov	di,*4.(bp)
lea	ax,(di)
call	fldd
lea	ax,L10000
call	fmuls
mov	ax,*4.(bp)
mov	bx,ax
lea	ax,(bx)
call	fstdp
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _s=-20.
| _q=-22.
| _a=-46.
| _p=-48.
| _d=-56.
| _i=-58.
| _r=-60.
mov	*-58.(bp),*1.
.data
L10001:	.float 2.50000000000000000e+00
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
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
lea	di,*-20.(bp)
mov	*-22.(bp),di
.data
L10003:	.float 1.50000000000000000e+00
.text
mov	di,*-22.(bp)
lea	ax,(di)
call	fldd
lea	ax,L10003
call	fadds
mov	di,*-22.(bp)
lea	ax,*8.(di)
call	fstdp
.data
L10004:	.float 5.00000000000000000e-01
.text
mov	di,*-22.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,L10004
call	fadds
pop	ax
call	fstdp
.data
L10005:	.float 1.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-46.(bp)
call	fstdp
.data
L10006:	.float 2.50000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-38.(bp)
call	fstdp
.data
L10007:	.float 4.00000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-30.(bp)
call	fstdp
.data
L10008:	.float 1.50000000000000000e+00
.text
lea	di,*-46.(bp)
mov	si,*-58.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,L10008
call	fadds
pop	ax
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	di,*-46.(bp)
mov	si,*-58.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
|
push	ax
call	fmuld
pop	ax
call	fstdp
lea	di,*-46.(bp)
mov	*-48.(bp),di
mov	di,*-48.(bp)
lea	ax,*16.(di)
call	fldd
mov	di,*-58.(bp)
mov	cx,*3.
sal	di,cl
add	di,*-48.(bp)
lea	ax,(di)
call	faddd
lea	ax,*-56.(bp)
call	fstdp
add	*-48.(bp),*8.
mov	di,*-48.(bp)
lea	ax,(di)
call	fldd
lea	ax,*-56.(bp)
call	faddd
lea	ax,*-56.(bp)
call	fstdp
lea	di,*-56.(bp)
push	di
call	_twice
add	sp,*2.
mov	di,*2.
push	di
lea	di,*-46.(bp)
push	di
call	_pick
add	sp,*4.
mov	bx,ax
lea	ax,(bx)
call	fldd
lea	ax,*-56.(bp)
call	faddd
lea	ax,*-56.(bp)
call	fstdp
mov	di,*-22.(bp)
lea	ax,*8.(di)
call	fldd
mov	di,*-22.(bp)
lea	ax,(di)
call	faddd
lea	ax,*-56.(bp)
call	faddd
call	ftoi
mov	*-60.(bp),ax
mov	di,*-60.(bp)
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*56.
jmp	L8
.globl	fltused
.data
