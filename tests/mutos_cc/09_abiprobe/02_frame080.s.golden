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
L2:| _buf=-84.
movb	*-84.(bp),*1.
movb	*-5.(bp),*2.
movb	ax,*-84.(bp)
cbw
mov	di,ax
movb	ax,*-5.(bp)
cbw
add	di,ax
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*80.
jmp	L2
.data
