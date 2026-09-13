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
| _b=-8.
| _c=-10.
mov	*-6.(bp),*17.
mov	*-8.(bp),*5.
mov	di,*-6.(bp)
add	di,*-8.(bp)
mov	*-10.(bp),di
mov	di,*-6.(bp)
sub	di,*-8.(bp)
mov	*-10.(bp),di
mov	ax,*-6.(bp)
imul	*-8.(bp)
mov	*-10.(bp),ax
mov	ax,*-6.(bp)
cwd
idiv	*-8.(bp)
mov	*-10.(bp),ax
mov	ax,*-6.(bp)
cwd
idiv	*-8.(bp)
mov	*-10.(bp),dx
mov	di,*-10.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*6.
jmp	L2
.data
