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
L2:| _i=-6.
| _l=-10.
| _c=-12.
| _s1=-14.
| _s2=-16.
| _s3=-18.
| _s4=-20.
mov	si,#4464.
mov	di,*1.
mov	*-8.(bp),si
mov	*-10.(bp),di
mov	di,*-8.(bp)
mov	*-6.(bp),di
mov	dx,*-6.(bp)
movb	*-12.(bp),dx
movb	ax,*-12.(bp)
cbw
cwd
mov	di,dx
mov	si,ax
mov	*-8.(bp),si
mov	*-10.(bp),di
mov	*-14.(bp),*2.
mov	*-16.(bp),*1.
mov	*-18.(bp),*4.
mov	*-20.(bp),*2.
mov	di,*-14.(bp)
add	di,*-16.(bp)
add	di,*-18.(bp)
add	di,*-20.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*16.
jmp	L2
.data
