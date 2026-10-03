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
| _f=-28.
| _g=-32.
| _l=-36.
| _u=-38.
| _i=-40.
| _j=-42.
| _x=-44.
| _r=-46.
| _c=-48.
mov	*-40.(bp),*3.
mov	*-42.(bp),*4.
movb	*-48.(bp),*2.
mov	si,#4464.
mov	di,*1.
mov	*-34.(bp),si
mov	*-36.(bp),di
.data
L10001:	.float 2.50000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-46.(bp),ax
.data
L10002:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10002
call	fadds
lea	ax,*-12.(bp)
call	fstdp
.data
L10003:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10003
call	fadds
lea	ax,*-12.(bp)
call	fstdp
.data
L10004:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
call	fdup
lea	ax,L10004
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-46.(bp)
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-46.(bp),ax
movb	ax,*-48.(bp)
cbw
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.float 2.50000000000000000e+00
.text
movb	ax,*-48.(bp)
cbw
call	itof
lea	ax,L10005
call	fmuls
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-46.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-46.(bp),ax
mov	di,*-40.(bp)
add	di,*-42.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fstdp
.data
L10006:	.float 5.00000000000000000e-01
.text
mov	ax,*-40.(bp)
imul	*-42.(bp)
call	itof
lea	ax,L10006
call	fadds
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-46.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-46.(bp),ax
mov	di,*-40.(bp)
neg	di
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fstdp
mov	ax,*1.
cwd
push	ax
push	dx
mov	si,*-34.(bp)
mov	di,*-36.(bp)
pop	bx
pop cx
add	si,cx
adc	di,bx
push	si
push	di
call	ltof
add	sp,*4
lea	ax,*-20.(bp)
call	fstdp
.data
L10007:	.float 7.00000000000000000e+04
.text
lea	ax,*-20.(bp)
call	fldd
lea	ax,L10007
call	fsubs
mov	di,*-46.(bp)
mov	ax,di
call	itof
call	fadd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-46.(bp),ax
.data
L10008:	.float 4.00005000000000000e+04
.text
lea	ax,L10008
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftol
mov	*-38.(bp),ax
mov	di,*-46.(bp)
add	di,*-38.(bp)
add	di,#25546.
mov	*-46.(bp),di
mov	*-44.(bp),*1.
cmp	*-44.(bp),*0
beq	L10009
lea	ax,*-12.(bp)
call	fldd
jmp	L10010
L10009:lea	ax,*-20.(bp)
call	fldd
L10010:lea	ax,*-12.(bp)
call	fstdp
mov	*-40.(bp),*2.
.data
L10011:	.float 4.00000000000000000e+04
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10011
call	fsubs
lea	ax,*-32.(bp)
call	fstsp
.data
L10012:	.float 4.00000000000000000e+00
.text
lea	ax,*-32.(bp)
call	flds
lea	ax,L10012
call	fmuls
mov	ax,*-46.(bp)
add	ax,*-40.(bp)
call	itof
call	fadd
call	ftoi
mov	*-46.(bp),ax
.data
L10013:	.float 3.00000000000000000e+00
.text
lea	ax,L10013
call	flds
lea	ax,*-12.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,*-28.(bp)
call	fstdp
mov	di,*-46.(bp)
mov	ax,di
call	itof
.data
L10014:	.float 2.00000000000000000e+00
.text
lea	ax,L10014
call	flds
lea	ax,*-28.(bp)
call	fmuld
call	fadd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-46.(bp),ax
mov	di,*-46.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*44.
jmp	L5
.globl	fltused
.data
