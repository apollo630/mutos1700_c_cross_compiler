.globl	_busintr
.text
.even
_busintr:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|NREG	2
|	_p=di
mov	di,#_proc
j	L4
L20001:cmp	*12.(di),*3.
jl	L6
mov	si,*16.
push	si
push	di
call	_psignal
add	sp,*4.
L6:add	di,*32.
L4:cmp	di,_Nproca
jb	L20001
|NREG	3
|RTYP	0
br	cret
.data
