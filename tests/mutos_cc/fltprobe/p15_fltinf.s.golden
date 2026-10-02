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
L2:.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*4.(bp)
call	fmuld
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L3
L3:|RTYP 3
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
| _arr=-44.
| _i=-46.
| _j=-48.
| _r=-50.
mov	*-46.(bp),*1.
mov	*-48.(bp),*6.
mov	*-50.(bp),*0.
.data
L10001:	.float 2.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10002:	.float 5.00000000000000000e-01
.text
lea	ax,L10002
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10003:	.float 3.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-36.(bp)
call	fstdp
lea	di,*-44.(bp)
mov	si,*-46.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-12.(bp)
call	fmuld
call	fadd
mov	ax,*-46.(bp)
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-50.(bp)
mov	*-50.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
mov	ax,*-46.(bp)
mov	cx,*3.
sal	ax,cl
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-50.(bp)
mov	*-50.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fdivd
mov	ax,*-48.(bp)
sar	ax,*1
call	itof
call	fsub
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-50.(bp)
mov	*-50.(bp),ax
lea	ax,*-20.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
lea	di,*-44.(bp)
mov	si,*-46.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-50.(bp)
mov	*-50.(bp),ax
mov	di,*-50.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*46.
jmp	L5
.globl	fltused
.data
