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
L2:| _x=-6.
| _y=-8.
| _p=-10.
mov	*-6.(bp),*10.
lea	di,*-6.(bp)
mov	*-10.(bp),di
mov	di,*-10.(bp)
mov	(di),*20.
mov	di,*-10.(bp)
mov	di,(di)
mov	*-8.(bp),di
lea	di,*-8.(bp)
mov	*-10.(bp),di
push	*-10.(bp)
mov	di,*-10.(bp)
mov	di,(di)
inc	di
pop	bx
mov	(bx),di
mov	di,*-8.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*6.
jmp	L2
.data
