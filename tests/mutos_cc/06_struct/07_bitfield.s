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
L2:| _f=-6.
or	*-6.(bp),*1.
and	*-6.(bp),*-3.
and	*-6.(bp),*/fff3
or	*-6.(bp),*8.
and	*-6.(bp),#/ff0f
or	*-6.(bp),#144.
mov	di,*-6.(bp)
and	di,*1.
mov	si,*-6.(bp)
sar	si,*1
sar	si,*1
and	si,*3.
add	di,si
mov	si,*-6.(bp)
mov	cx,*4.
sar	si,cl
and	si,*15.
add	di,si
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*2.
jmp	L2
.data
