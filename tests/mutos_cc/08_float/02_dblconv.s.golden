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
| _i=-14.
| _l=-18.
mov	*-14.(bp),*7.
mov	di,*-14.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fstdp
.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10000
call	fdivs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftol
mov	di,dx
mov	si,ax
mov	*-16.(bp),si
mov	*-18.(bp),di
lea	ax,*-12.(bp)
call	fldd
call	ftoi
mov	*-14.(bp),ax
mov	di,*-14.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*14.
jmp	L2
.globl	fltused
.data
