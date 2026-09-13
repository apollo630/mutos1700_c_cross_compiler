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
L2:| _a=-6.
mov	*-6.(bp),*10.
add	*-6.(bp),*5.
sub	*-6.(bp),*3.
sal	*-6.(bp),*1
mov	ax,*-6.(bp)
cwd
mov	cx,*4.
idiv	cx
mov	*-6.(bp),ax
mov	ax,*-6.(bp)
cwd
mov	cx,*3.
idiv	cx
mov	*-6.(bp),dx
sal	*-6.(bp),*1
sar	*-6.(bp),*1
and	*-6.(bp),*15.
or	*-6.(bp),*48.
xor	*-6.(bp),*17.
mov	di,*-6.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*2.
jmp	L2
.data
