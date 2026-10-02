.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:| _d=-12.
| _e=-20.
| _arr=-36.
| _i=-38.
| _j=-40.
| _r=-42.
mov	*-38.(bp),*1.
mov	*-40.(bp),*3.
mov	*-42.(bp),*0.
.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.float 1.50000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10002:	.float 5.00000000000000000e-01
.text
lea	ax,L10002
call	flds
lea	ax,*-36.(bp)
call	fstdp
.data
L10003:	.float 4.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-28.(bp)
call	fstdp
mov	di,*-38.(bp)
mov	ax,di
call	itof
mov	di,*-40.(bp)
mov	ax,di
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-42.(bp)
mov	*-42.(bp),ax
lea	di,*-36.(bp)
mov	si,*-38.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
mov	di,*-40.(bp)
mov	ax,di
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-42.(bp)
mov	*-42.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
mov	ax,*-38.(bp)
sub	ax,*-40.(bp)
call	itof
call	fsub
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-42.(bp)
mov	*-42.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fdivd
mov	ax,*-38.(bp)
sal	ax,*1
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-42.(bp)
mov	*-42.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
mov	ax,*-40.(bp)
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-42.(bp)
mov	*-42.(bp),ax
lea	ax,*-20.(bp)
call	fldd
call	ftoi
mov	cx,ax
mov	ax,*-40.(bp)
cwd
idiv	cx
mov	*-40.(bp),ax
mov	di,*-42.(bp)
add	di,*-40.(bp)
mov	*-42.(bp),di
.data
L10004:	.float 0.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.float 0.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10005
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-42.(bp)
add	ax,*-30.
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*38.
jmp	L2
.globl	fltused
.data
