.globl	_counter
.text
.even
_counter:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L1
L2:.bss
L4:.blkb	2.
.text
| _n=L4
mov	di,L4
inc	di
mov	L4,di
mov	di,L4
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
jmp	L5
L6:| _a=-6.
| _b=-8.
| _c=-10.
call	_counter
mov	*-6.(bp),ax
call	_counter
mov	*-8.(bp),ax
call	_counter
mov	*-10.(bp),ax
mov	di,*-10.(bp)
mov	ax,di
jmp	L7
L7:|RTYP 0
jmp	cret
L5:sub	sp,*6.
jmp	L6
.data
