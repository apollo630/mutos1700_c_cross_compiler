.globl	_busintr
.text
.even
_busintr:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:|NREG 2
| _p=di
mov	di,#_proc
L4:cmp	di,_Nproca
bhis	L5
cmp	*12.(di),*3.
blt	L7
mov	si,*16.
push	si
push	di
call	_psignal
add	sp,*4.
L7:L6:add	di,*32.
jmp	L4
L5:|NREG 3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.data
