.globl	_addlong
.text
.even
_addlong:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _b=8.
jmp	L1
L2:mov	si,*6.(bp)
mov	di,*4.(bp)
add	si,*10.(bp)
adc	di,*8.(bp)
mov	ax,si
mov	dx,di
jmp	L3
L3:|RTYP 6
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
L5:| _r=-8.
mov	ax,*5.
cwd
push	ax
push	dx
mov	di,#-31072.
push	di
mov	di,*1.
push	di
call	_addlong
add	sp,*8.
mov	di,dx
mov	si,ax
mov	*-6.(bp),si
mov	*-8.(bp),di
mov	di,*-6.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*4.
jmp	L5
.data
