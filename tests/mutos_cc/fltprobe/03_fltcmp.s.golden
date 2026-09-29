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
L2:| _d=-12.
| _e=-20.
| _a=-24.
| _b=-28.
| _i=-30.
| _r=-32.
.data
L10000:	.float 1.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.float 2.50000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10002:	.float 7.50000000000000000e-01
.text
lea	ax,L10002
call	flds
lea	ax,*-24.(bp)
call	fstsp
.data
L10003:	.float 3.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-28.(bp)
call	fstsp
mov	*-30.(bp),*2.
mov	*-32.(bp),*0.
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
bge	L4
mov	di,*-32.(bp)
inc	di
mov	*-32.(bp),di
L4:lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
blt	L5
mov	di,*-32.(bp)
add	di,*100.
mov	*-32.(bp),di
L5:.data
L10004:	.float 0.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
bge	L6
mov	di,*-32.(bp)
add	di,*2.
mov	*-32.(bp),di
L6:.data
L10005:	.float 0.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
beq	L7
mov	di,*-32.(bp)
add	di,*4.
mov	*-32.(bp),di
L7:.data
L10006:	.float 0.00000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L8
mov	di,*-32.(bp)
add	di,*8.
mov	*-32.(bp),di
L8:.data
L10007:	.float 1.50000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-24.(bp)
call	flds
call	fcmp
sahf
ble	L9
mov	di,*-32.(bp)
add	di,*16.
mov	*-32.(bp),di
L9:lea	ax,*-24.(bp)
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
ble	L10
mov	di,*-32.(bp)
add	di,#1000.
mov	*-32.(bp),di
L10:mov	di,*-30.(bp)
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
ble	L11
mov	di,*-32.(bp)
add	di,*32.
mov	*-32.(bp),di
L11:mov	di,*-30.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bgt	L12
mov	di,*-32.(bp)
add	di,#2000.
mov	*-32.(bp),di
L12:lea	ax,*-24.(bp)
call	flds
lea	ax,*-28.(bp)
call	flds
call	fcmp
sahf
bgt	L13
mov	di,*-32.(bp)
add	di,*64.
mov	*-32.(bp),di
L13:L14:lea	ax,*-28.(bp)
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
ble	L15
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
jmp	L14
L15:mov	di,*-32.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*28.
jmp	L2
.globl	fltused
.data
