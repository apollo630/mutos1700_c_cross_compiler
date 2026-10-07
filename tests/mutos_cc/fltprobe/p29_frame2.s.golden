.globl	_f92
.text
.even
_f92:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:| _buf=-96.
movb	*-96.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-96.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*92.
jmp	L2
.globl	_f94
.text
.even
_f94:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L4
L5:| _buf=-98.
movb	*-98.(bp),*3.
movb	*-5.(bp),*4.
movb	ax,*-98.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*94.
jmp	L5
.globl	_f96
.text
.even
_f96:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _buf=-100.
movb	*-100.(bp),*5.
movb	*-5.(bp),*6.
movb	ax,*-100.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*96.
jmp	L8
.globl	_f98
.text
.even
_f98:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L10
L11:| _buf=-102.
movb	*-102.(bp),*7.
movb	*-5.(bp),*22.
movb	ax,*-102.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L12
L12:|RTYP 0
jmp	cret
L10:sub	sp,*98.
jmp	L11
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L13
L14:call	_f98
push	ax
call	_f96
push	ax
call	_f94
push	ax
call	_f92
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
jmp	L15
L15:|RTYP 0
jmp	cret
L13:jmp	L14
.data
