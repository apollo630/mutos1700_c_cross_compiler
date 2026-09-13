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
L2:| _i=-6.
| _j=-8.
| _a=-16.
| _p=-18.
mov	*-6.(bp),*0.
mov	di,*-6.(bp)
mov	*-8.(bp),di
inc	*-6.(bp)
inc	*-6.(bp)
mov	di,*-6.(bp)
mov	*-8.(bp),di
mov	di,*-6.(bp)
mov	*-8.(bp),di
dec	*-6.(bp)
dec	*-6.(bp)
mov	di,*-6.(bp)
mov	*-8.(bp),di
lea	di,*-16.(bp)
mov	*-18.(bp),di
mov	di,*-18.(bp)
mov	(di),*1.
add	*-18.(bp),*2.
add	*-18.(bp),*2.
mov	di,*-18.(bp)
mov	(di),*2.
mov	di,*-8.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*14.
jmp	L2
.data
