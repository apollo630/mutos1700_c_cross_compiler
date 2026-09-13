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
L2:| _names=-10.
| _i=-12.
| _total=-14.
mov	*-10.(bp),#L4
mov	*-8.(bp),#L5
mov	*-6.(bp),#L6
mov	*-14.(bp),*0.
mov	*-12.(bp),*0.
L7:cmp	*-12.(bp),*3.
bge	L8
lea	di,*-10.(bp)
mov	si,*-12.(bp)
sal	si,*1
add	di,si
mov	di,(di)
push	di
call	_strlen
add	sp,*2.
add	ax,*-14.(bp)
mov	*-14.(bp),ax
L9:mov	di,*-12.(bp)
inc	di
mov	*-12.(bp),di
jmp	L7
L8:mov	di,*-14.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*10.
jmp	L2
.data
L4:.byte	/6f,/6e,/65,/0
L5:.byte	/74,/77,/6f,/0
L6:.byte	/74,/68,/72,/65,/65,/0
