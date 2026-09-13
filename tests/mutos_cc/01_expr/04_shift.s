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
| _n=-8.
| _r=-10.
mov	*-6.(bp),*1.
mov	*-8.(bp),*4.
mov	di,*-6.(bp)
mov	cx,*-8.(bp)
sal	di,cl
mov	*-10.(bp),di
mov	di,*-10.(bp)
sar	di,*1
sar	di,*1
mov	*-10.(bp),di
mov	di,*-6.(bp)
sal	di,*1
mov	*-10.(bp),di
mov	di,*-10.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*6.
jmp	L2
.data
