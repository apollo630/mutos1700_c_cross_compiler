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
L2:| _a=-8.
| _b=-12.
| _c=-16.
mov	si,#-31072.
mov	di,*1.
mov	*-6.(bp),si
mov	*-8.(bp),di
mov	ax,#23456.
cwd
mov	di,dx
mov	si,ax
mov	*-10.(bp),si
mov	*-12.(bp),di
mov	si,*-6.(bp)
mov	di,*-8.(bp)
add	si,*-10.(bp)
adc	di,*-12.(bp)
mov	*-14.(bp),si
mov	*-16.(bp),di
mov	si,*-6.(bp)
mov	di,*-8.(bp)
sub	si,*-10.(bp)
sbb	di,*-12.(bp)
mov	*-14.(bp),si
mov	*-16.(bp),di
cmp	*-16.(bp),*0
blt	L4
bgt	L10000
cmp	*-14.(bp),*0.
blos	L4
L10000:mov	ax,*1.
cwd
push	ax
push	dx
mov	si,*-14.(bp)
mov	di,*-16.(bp)
pop	bx
pop cx
add	si,cx
adc	di,bx
mov	*-14.(bp),si
mov	*-16.(bp),di
L4:mov	di,*-14.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*12.
jmp	L2
.data
