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
L2:| _i=-6.
| _sum=-8.
mov	*-8.(bp),*0.
mov	*-6.(bp),*0.
L4:cmp	*-6.(bp),*10.
bge	L5
mov	di,*-8.(bp)
add	di,*-6.(bp)
mov	*-8.(bp),di
L6:mov	di,*-6.(bp)
inc	di
mov	*-6.(bp),di
jmp	L4
L5:mov	di,*-8.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*4.
jmp	L2
.data
