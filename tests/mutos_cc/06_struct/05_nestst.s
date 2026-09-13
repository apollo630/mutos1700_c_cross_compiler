.globl	_area
.text
.even
_area:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _rp=4.
jmp	L1
L2:| _w=-6.
| _h=-8.
push	*4.(bp)
mov	di,*4.(bp)
mov	di,*4.(di)
pop	bx
sub	di,(bx)
mov	*-6.(bp),di
mov	di,*4.(bp)
mov	di,*6.(di)
mov	si,*4.(bp)
sub	di,*2.(si)
mov	*-8.(bp),di
mov	ax,*-6.(bp)
imul	*-8.(bp)
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*4.
jmp	L2
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
L5:| _r=-12.
mov	*-12.(bp),*0.
mov	*-10.(bp),*0.
mov	*-8.(bp),*10.
mov	*-6.(bp),*4.
lea	di,*-12.(bp)
push	di
call	_area
add	sp,*2.
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*8.
jmp	L5
.data
