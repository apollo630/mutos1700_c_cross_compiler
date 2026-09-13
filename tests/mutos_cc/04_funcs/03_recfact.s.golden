.globl	_fact
.text
.even
_fact:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _n=4.
jmp	L1
L2:cmp	*4.(bp),*1.
bgt	L4
mov	di,*1.
mov	ax,di
jmp	L3
L4:mov	di,*4.(bp)
dec	di
push	di
call	_fact
add	sp,*2.
mov	ax,ax
imul	*4.(bp)
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
jmp	L5
L6:mov	di,*6.
push	di
call	_fact
add	sp,*2.
jmp	L7
L7:|RTYP 0
jmp	cret
L5:jmp	L6
.data
