.globl	_swapch
.text
.even
_swapch:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _b=6.
jmp	L1
L2:| _t=-6.
mov	dx,*4.(bp)
mov	bx,dx
movb	dx,(bx)
movb	*-6.(bp),dx
push	*4.(bp)
mov	dx,*6.(bp)
mov	bx,dx
movb	dx,(bx)
pop	bx
movb	(bx),dx
push	*6.(bp)
movb	dx,*-6.(bp)
pop	bx
movb	(bx),dx
L3:|RTYP 0
jmp	cret
L1:sub	sp,*2.
jmp	L2
.globl	_reverse
.text
.even
_reverse:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _s=4.
| _lo=6.
| _hi=8.
jmp	L4
L5:mov	di,*8.(bp)
cmp	*6.(bp),di
bge	L6
mov	di,*4.(bp)
add	di,*8.(bp)
push	di
mov	di,*4.(bp)
add	di,*6.(bp)
push	di
call	_swapch
add	sp,*4.
mov	di,*8.(bp)
dec	di
push	di
mov	di,*6.(bp)
inc	di
push	di
push	*4.(bp)
call	_reverse
add	sp,*6.
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _buf=-24.
| _len=-26.
mov	di,#L10
push	di
lea	di,*-24.(bp)
push	di
call	_strcpy
add	sp,*4.
lea	di,*-24.(bp)
push	di
call	_strlen
add	sp,*2.
mov	*-26.(bp),ax
mov	di,*-26.(bp)
dec	di
push	di
mov	di,*0.
push	di
lea	di,*-24.(bp)
push	di
call	_reverse
add	sp,*6.
movb	ax,*-24.(bp)
cbw
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*22.
jmp	L8
.data
L10:.byte	/6d,/75,/74,/6f,/73,/31,/37,/30,/30
.byte	/0
