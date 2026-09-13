.comm	_counter,2
.bss
_hidden:.blkb	2.
.globl	_bump
.text
.even
_bump:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:mov	di,_counter
inc	di
mov	_counter,di
mov	di,_hidden
inc	di
mov	_hidden,di
mov	di,_counter
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
L5:call	_bump
call	_bump
call	_bump
jmp	L6
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.data
