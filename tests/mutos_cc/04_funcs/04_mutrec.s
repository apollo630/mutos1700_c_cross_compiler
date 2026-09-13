.globl	_isodd
.text
.even
_isodd:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _n=4.
jmp	L1
L2:cmp	*4.(bp),*0
bne	L4
mov	di,*0.
mov	ax,di
jmp	L3
L4:mov	di,*4.(bp)
dec	di
push	di
call	_iseven
add	sp,*2.
jmp	L3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.globl	_iseven
.text
.even
_iseven:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _n=4.
jmp	L5
L6:cmp	*4.(bp),*0
bne	L8
mov	di,*1.
mov	ax,di
jmp	L7
L8:mov	di,*4.(bp)
dec	di
push	di
call	_isodd
add	sp,*2.
jmp	L7
L7:|RTYP 0
jmp	cret
L5:jmp	L6
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L9
L10:mov	di,*10.
push	di
call	_iseven
add	sp,*2.
jmp	L11
L11:|RTYP 0
jmp	cret
L9:jmp	L10
.data
