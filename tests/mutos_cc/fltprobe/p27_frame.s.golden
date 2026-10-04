.globl	_f82
.text
.even
_f82:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:| _buf=-86.
movb	*-86.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-86.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*82.
jmp	L2
.globl	_f90
.text
.even
_f90:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L4
L5:| _buf=-94.
movb	*-94.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-94.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*90.
jmp	L5
.globl	_f100
.text
.even
_f100:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _buf=-104.
movb	*-104.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-104.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:mov	ax,*100.
call	chkstk
jmp	L8
.globl	_f110
.text
.even
_f110:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L10
L11:| _buf=-114.
movb	*-114.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-114.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L12
L12:|RTYP 0
jmp	cret
L10:mov	ax,*110.
call	chkstk
jmp	L11
.globl	_f120
.text
.even
_f120:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L13
L14:| _buf=-124.
movb	*-124.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-124.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L15
L15:|RTYP 0
jmp	cret
L13:mov	ax,*120.
call	chkstk
jmp	L14
.globl	_f124
.text
.even
_f124:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L16
L17:| _buf=-128.
movb	*-128.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-128.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L18
L18:|RTYP 0
jmp	cret
L16:mov	ax,*124.
call	chkstk
jmp	L17
.globl	_f126
.text
.even
_f126:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L19
L20:| _buf=-130.
movb	#-130.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,#-130.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L21
L21:|RTYP 0
jmp	cret
L19:mov	ax,*126.
call	chkstk
jmp	L20
.globl	_f127
.text
.even
_f127:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L22
L23:| _buf=-132.
movb	#-132.(bp),*1.
movb	*-6.(bp),*2.
movb	ax,#-132.(bp)
cbw
mov	di,ax
movb	ax,*-6.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L24
L24:|RTYP 0
jmp	cret
L22:mov	ax,#128.
call	chkstk
jmp	L23
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L25
L26:call	_f127
push	ax
call	_f126
push	ax
call	_f124
push	ax
call	_f120
push	ax
call	_f110
push	ax
call	_f100
push	ax
call	_f90
push	ax
call	_f82
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
pop	bx
add	ax,bx
jmp	L27
L27:|RTYP 0
jmp	cret
L25:jmp	L26
.data
