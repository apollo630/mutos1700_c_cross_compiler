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
L2:|NREG 2
| _i=di
| _sum=-6.
mov	*-6.(bp),*0.
mov	di,*0.
L4:cmp	di,*100.
bge	L5
mov	si,*-6.(bp)
add	si,di
mov	*-6.(bp),si
L6:inc	di
jmp	L4
L5:mov	si,*-6.(bp)
mov	ax,si
jmp	L3
|NREG 3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*2.
jmp	L2
.data
