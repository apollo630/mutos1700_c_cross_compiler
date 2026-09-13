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
| _j=-8.
| _count=-10.
mov	*-10.(bp),*0.
mov	*-6.(bp),*0.
L4:cmp	*-6.(bp),*5.
bge	L5
mov	*-8.(bp),*0.
L7:cmp	*-8.(bp),*5.
bge	L8
cmp	*-8.(bp),*3.
beq	L8
mov	di,*-8.(bp)
cmp	*-6.(bp),di
beq	L9
mov	di,*-10.(bp)
inc	di
mov	*-10.(bp),di
L9:mov	di,*-8.(bp)
inc	di
mov	*-8.(bp),di
jmp	L7
L8:L6:mov	di,*-6.(bp)
inc	di
mov	*-6.(bp),di
jmp	L4
L5:mov	di,*-10.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*6.
jmp	L2
.data
