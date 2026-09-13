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
L2:| _n=-6.
mov	*-6.(bp),#258.
movb	ax,*-6.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*2.
jmp	L2
.data
