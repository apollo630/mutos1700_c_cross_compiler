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
| _p=-14.
| _arr=-38.
| _l=-42.
| _i=-44.
mov	si,#4464.
mov	di,*1.
mov	*-40.(bp),si
mov	*-42.(bp),di
mov	di,*-40.(bp)
push	di
mov	di,*-42.(bp)
push	di
call	ltof
add	sp,*4
lea	ax,*-12.(bp)
call	fstdp
lea	di,*-30.(bp)
mov	*-14.(bp),di
.data
L10000:	.float 2.50000000000000000e+00
.text
lea	ax,L10000
call	flds
mov	di,*-14.(bp)
lea	ax,(di)
call	fstdp
.data
L10001:	.float 1.50000000000000000e+00
.text
mov	di,*-14.(bp)
lea	ax,(di)
call	fldd
lea	ax,L10001
call	fadds
lea	ax,*-12.(bp)
call	fstdp
mov	*-44.(bp),*2.
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	*-44.(bp),ax
lea	ax,*-12.(bp)
call	fldd
call	ftoi
sub	*-44.(bp),ax
lea	ax,*-12.(bp)
call	fldd
lea	di,*-38.(bp)
mov	si,*-44.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fstdp
.data
L10002:	.float 5.00000000000000000e-01
.text
lea	di,*-38.(bp)
mov	si,*-44.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
lea	ax,L10002
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
add	ax,*-44.(bp)
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*40.
jmp	L2
.globl	fltused
.data
