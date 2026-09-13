.globl	_add
.text
.even
_add:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _b=6.
jmp	L1
L2:mov	di,*4.(bp)
add	di,*6.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L4
L5:mov	di,*4.
push	di
mov	di,*3.
push	di
call	_add
add	sp,*4.
jmp	L6
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.data
