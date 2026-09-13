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
L2:| _p=-8.
mov	*-8.(bp),*3.
mov	*-6.(bp),*4.
mov	di,*-8.(bp)
add	di,*-6.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*4.
jmp	L2
.data
