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
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*8.
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
| _e=-20.
| _w=-28.
| _y=-36.
| _x=-38.
.data
L10002:	.float 1.50000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10003:	.float 4.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-28.(bp)
call	fstdp
.data
L10004:	.float 1.00000000000000000e+01
.text
lea	ax,L10004
call	flds
lea	ax,*-36.(bp)
call	fstdp
.data
L10006:	.float 1.00000000000000000e+00
.text
lea	ax,*-28.(bp)
call	fldd
lea	ax,L10006
call	fadds
lea	ax,*-28.(bp)
call	fstdp
.data
L10007:	.float 1.00000000000000000e+00
.text
lea	ax,*-36.(bp)
call	fldd
lea	ax,L10007
call	fadds
lea	ax,*-36.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10005:	.float 2.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10008
mov	di,*0.
jmp	L10009
L10008:mov	di,*1.
L10009:mov	*-38.(bp),di
.data
L10010:	.float 1.00000000000000000e+00
.text
lea	ax,L10010
call	flds
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-20.(bp)
call	fstdp
.data
L10011:	.float 2.00000000000000000e+00
.text
lea	ax,L10011
call	flds
lea	ax,*-20.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
mov	di,*-38.(bp)
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*34.
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
.data
L10012:	.float 1.00000000000000000e+00
.text
lea	ax,L10012
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10013:	.float 2.00000000000000000e+00
.text
lea	ax,L10013
call	flds
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*16.
jmp	L11
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L13
L14:call	_f3
push	ax
call	_f2
push	ax
call	_f1
pop	bx
add	ax,bx
pop	bx
add	ax,bx
jmp	L15
L15:|RTYP 0
jmp	cret
L13:jmp	L14
.globl	fltused
.data
