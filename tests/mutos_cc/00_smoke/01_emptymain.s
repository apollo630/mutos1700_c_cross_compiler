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
L2:L3:|RTYP 0
jmp	cret
L1:jmp	L2
.data
