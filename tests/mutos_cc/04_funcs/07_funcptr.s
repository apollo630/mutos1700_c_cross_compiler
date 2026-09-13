.globl	_square
.text
.even
_square:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L1
L2:mov	ax,*4.(bp)
imul	*4.(bp)
jmp	L3
L3:|RTYP 0
jmp	cret
L1:jmp	L2
.globl	_cube
.text
.even
_cube:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L4
L5:mov	ax,*4.(bp)
imul	*4.(bp)
mov	ax,ax
imul	*4.(bp)
jmp	L6
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.globl	_apply
.text
.even
_apply:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _f=4.
| _x=6.
jmp	L7
L8:push	*6.(bp)
call	@*4.(bp)
add	sp,*2.
jmp	L9
L9:|RTYP 0
jmp	cret
L7:jmp	L8
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L10
L11:| _fp=-6.
| _r=-8.
mov	*-6.(bp),#_square
mov	di,*5.
push	di
push	*-6.(bp)
call	_apply
add	sp,*4.
mov	*-8.(bp),ax
mov	*-6.(bp),#_cube
mov	di,*3.
push	di
push	*-6.(bp)
call	_apply
add	sp,*4.
mov	*-8.(bp),ax
mov	di,*-8.(bp)
mov	ax,di
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*4.
jmp	L11
.data
