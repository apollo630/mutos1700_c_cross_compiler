.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _argc=4.
| _argv=6.
jmp	L1
L2:cmp	*4.(bp),*1.
ble	L4
mov	di,*6.(bp)
mov	di,*2.(di)
push	di
call	_strlen
add	sp,*2.
jmp	L3
L4:mov	di,*0.
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.data
