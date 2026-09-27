.globl	_delay
.text
.even
_delay:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _d=4.
jmp	L1
L2:| _i=-6.
L4:mov	di,*4.(bp)
dec	*4.(bp)
or	di,di
beq	L5
mov	*-6.(bp),*0.
L6:cmp	*-6.(bp),#192.
bge	L7
inc	*-6.(bp)
jmp	L6
L7:jmp	L4
L5:L3:|RTYP 0
jmp	cret
L1:sub	sp,*2.
jmp	L2
.data
