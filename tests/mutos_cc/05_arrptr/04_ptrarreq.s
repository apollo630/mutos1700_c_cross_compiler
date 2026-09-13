.globl	_sumarr
.text
.even
_sumarr:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _n=6.
jmp	L1
L2:| _i=-6.
| _s=-8.
mov	*-8.(bp),*0.
mov	*-6.(bp),*0.
L4:mov	di,*6.(bp)
cmp	*-6.(bp),di
bge	L5
mov	di,*-6.(bp)
sal	di,*1
add	di,*4.(bp)
mov	di,(di)
add	di,*-8.(bp)
mov	*-8.(bp),di
L6:mov	di,*-6.(bp)
inc	di
mov	*-6.(bp),di
jmp	L4
L5:mov	di,*-8.(bp)
mov	ax,di
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
jmp	L7
L8:| _v=-12.
mov	*-12.(bp),*1.
mov	*-10.(bp),*2.
mov	*-8.(bp),*3.
mov	*-6.(bp),*4.
mov	di,*4.
push	di
lea	di,*-12.(bp)
push	di
call	_sumarr
add	sp,*4.
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*8.
jmp	L8
.data
