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
L2:| _a=-6.
| _b=-8.
| _m=-10.
mov	*-6.(bp),*4.
mov	*-8.(bp),*9.
mov	di,*-8.(bp)
cmp	*-6.(bp),di
ble	L10000
mov	di,*-6.(bp)
jmp	L10001
L10000:mov	di,*-8.(bp)
L10001:mov	*-10.(bp),di
mov	di,*-6.(bp)
inc	di
mov	*-6.(bp),di
mov	di,*-8.(bp)
inc	di
mov	*-8.(bp),di
mov	di,*-6.(bp)
add	di,*-8.(bp)
mov	*-10.(bp),di
mov	di,*-10.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*6.
jmp	L2
.data
