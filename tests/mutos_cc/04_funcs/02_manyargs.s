.globl	_sum6
.text
.even
_sum6:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _b=6.
| _c=8.
| _d=10.
| _e=12.
| _f=14.
jmp	L1
L2:mov	di,*4.(bp)
add	di,*6.(bp)
add	di,*8.(bp)
add	di,*10.(bp)
add	di,*12.(bp)
add	di,*14.(bp)
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
L5:mov	di,*6.
push	di
mov	di,*5.
push	di
mov	di,*4.
push	di
mov	di,*3.
push	di
mov	di,*2.
push	di
mov	di,*1.
push	di
call	_sum6
add	sp,*12.
jmp	L6
L6:|RTYP 0
jmp	cret
L4:jmp	L5
.data
