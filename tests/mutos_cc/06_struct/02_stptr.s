.globl	_move
.text
.even
_move:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _pp=4.
| _dx=6.
| _dy=8.
jmp	L1
L2:push	*4.(bp)
mov	di,*4.(bp)
mov	di,(di)
add	di,*6.(bp)
pop	bx
mov	(bx),di
mov	di,*4.(bp)
mov	di,*2.(di)
add	di,*8.(bp)
mov	si,*4.(bp)
mov	*2.(si),di
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
L5:| _p=-8.
mov	*-8.(bp),*0.
mov	*-6.(bp),*0.
mov	di,*7.
push	di
mov	di,*5.
push	di
lea	di,*-8.(bp)
push	di
call	_move
add	sp,*6.
mov	di,*-8.(bp)
add	di,*-6.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*4.
jmp	L5
.data
