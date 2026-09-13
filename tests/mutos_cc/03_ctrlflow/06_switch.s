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
| _r=-8.
mov	*-6.(bp),*2.
mov	*-8.(bp),*0.
mov	di,*-6.(bp)
mov	ax,di
jmp	L5
L6:mov	*-8.(bp),*10.
jmp	L4
L7:L8:mov	*-8.(bp),*20.
L9:mov	di,*-8.(bp)
inc	di
mov	*-8.(bp),di
jmp	L4
L10:mov	*-8.(bp),*-1.
jmp	L4
jmp	L4
L5:sub	ax,#/1
cmp	ax,*3.
bhi	L10
shl	ax,#1
xchg	bx,ax
seg	cs
jmp	@L10001(bx)
L10001:L6
L7
L8
L9
L4:mov	di,*-8.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*4.
jmp	L2
.data
