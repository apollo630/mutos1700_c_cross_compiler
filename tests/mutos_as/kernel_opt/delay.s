.globl	_delay
.text
.even
_delay:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_d=4.
sub	sp,*2.
|	_i=-6.
L4:mov	di,*4.(bp)
dec	*4.(bp)
test	di,di
je	L3
mov	*-6.(bp),*0.
L6:cmp	*-6.(bp),#192.
jge	L4
inc	*-6.(bp)
j	L6
L3:|RTYP	0
br	cret
.data
