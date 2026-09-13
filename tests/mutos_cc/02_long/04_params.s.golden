.globl	_myseek
.text
.even
_myseek:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _fd=4.
| _offset=6.
| _whence=10.
jmp	L1
L2:mov	di,*4.(bp)
add	di,*8.(bp)
add	di,*10.(bp)
mov	ax,di
jmp	L3
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
L5:mov	di,*1.
push	di
mov	di,#24464.
push	di
mov	di,*1.
push	di
mov	di,*3.
push	di
call	_myseek
add	sp,*8.
jmp	L6
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.data
