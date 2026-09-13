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
L2:| _p1=-8.
| _p2=-12.
mov	*-8.(bp),*11.
mov	*-6.(bp),*22.
mov	si,*-6.(bp)
mov	di,*-8.(bp)
mov	*-10.(bp),si
mov	*-12.(bp),di
mov	di,*-12.(bp)
add	di,*-10.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*8.
jmp	L2
.data
