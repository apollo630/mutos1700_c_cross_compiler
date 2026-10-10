.comm	_gd,8
.comm	_ga,24
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
L11:| _s=-20.
| _e=-28.
.data
L10002:	.float 1.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,_gd
call	fstdp
.data
L10003:	.float 2.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,8.+_ga
call	fstdp
.data
L10004:	.float 4.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.float 1.00000000000000000e+00
.text
lea	ax,_gd
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10005
call	fadds
lea	ax,*-28.(bp)
call	fstdp
.data
L10006:	.float 1.00000000000000000e+00
.text
lea	ax,8.+_ga
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10006
call	fadds
lea	ax,*-28.(bp)
call	fstdp
.data
L10007:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10007
call	fadds
lea	ax,*-28.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
call	ftoi
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*24.
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
L14:.bss
L16:.blkb	8.
.text
| _sd=L16
| _e=-12.
.data
L10008:	.float 2.00000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,L16
call	fstdp
.data
L10009:	.float 1.00000000000000000e+00
.text
lea	ax,L16
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10009
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
jmp	L15
L15:|RTYP 0
jmp	cret
L13:sub	sp,*8.
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
jmp	L17
L18:| _a=-28.
| _e=-36.
| _p=-38.
| _fa=-50.
| _i=-52.
mov	*-52.(bp),*1.
.data
L10010:	.float 1.00000000000000000e+00
.text
lea	ax,L10010
call	flds
lea	ax,*-28.(bp)
call	fstdp
.data
L10011:	.float 2.00000000000000000e+00
.text
lea	ax,L10011
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10012:	.float 3.00000000000000000e+00
.text
lea	ax,L10012
call	flds
lea	ax,*-46.(bp)
call	fstsp
lea	di,*-28.(bp)
mov	*-38.(bp),di
.data
L10013:	.float 1.00000000000000000e+00
.text
mov	di,*-38.(bp)
lea	ax,(di)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10013
call	fadds
lea	ax,*-36.(bp)
call	fstdp
.data
L10014:	.float 1.00000000000000000e+00
.text
mov	di,*-38.(bp)
lea	ax,*8.(di)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10014
call	fadds
lea	ax,*-36.(bp)
call	fstdp
.data
L10015:	.float 1.00000000000000000e+00
.text
lea	di,*-50.(bp)
mov	si,*-52.(bp)
sal	si,*1
sal	si,*1
add	di,si
lea	ax,(di)
call	flds
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10015
call	fadds
lea	ax,*-36.(bp)
call	fstdp
lea	ax,*-36.(bp)
call	fldd
call	ftoi
jmp	L19
L19:|RTYP 0
jmp	cret
L17:sub	sp,*48.
jmp	L18
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
L21:| _s=-20.
| _a=-44.
| _e=-52.
| _i=-54.
mov	*-54.(bp),*1.
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
lea	ax,*-36.(bp)
call	fstdp
.data
L10018:	.float 1.00000000000000000e+00
.text
lea	di,*-44.(bp)
mov	si,*-54.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
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
lea	ax,*-52.(bp)
call	fstdp
lea	ax,*-52.(bp)
call	fldd
call	ftoi
jmp	L22
L22:|RTYP 0
jmp	cret
L20:sub	sp,*50.
jmp	L21
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L23
L24:call	_f5
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
jmp	L25
L25:|RTYP 0
jmp	cret
L23:jmp	L24
.globl	fltused
.data
