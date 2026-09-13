.globl	_addto
.text
.even
_addto:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _n=4.
jmp	L1
L2:mov	di,_total
add	di,*4.(bp)
mov	_total,di
mov	di,_total
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.comm	_total,2
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
L5:mov	di,*3.
push	di
call	_addto
add	sp,*2.
mov	di,*4.
push	di
call	_addto
add	sp,*2.
mov	di,*5.
push	di
call	_addto
add	sp,*2.
jmp	L6
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.data
