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
| _i=-26.
| _r=-28.
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
mov	*-26.(bp),*2.
mov	*-28.(bp),*0.
.data
L10003:	.float 0.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-24.(bp)
call	flds
call	fcmp
sahf
ble	L4
mov	di,*-28.(bp)
add	di,*100.
mov	*-28.(bp),di
L4:.data
L10004:	.float 0.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-24.(bp)
call	flds
call	fcmp
sahf
bne	L5
mov	di,*-28.(bp)
add	di,#200.
mov	*-28.(bp),di
L5:.data
L10005:	.float 0.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,L10005
call	flds
call	fcmp
sahf
bge	L6
mov	di,*-28.(bp)
add	di,#300.
mov	*-28.(bp),di
L6:.data
L10006:	.float 0.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fsubd
lea	ax,L10006
call	flds
call	fcmp
sahf
beq	L7
mov	di,*-28.(bp)
inc	di
mov	*-28.(bp),di
L7:lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
blt	L10007
mov	di,*0.
jmp	L10008
L10007:mov	di,*1.
L10008:add	di,*-28.(bp)
mov	*-28.(bp),di
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
bge	L8
lea	ax,*-20.(bp)
call	fldd
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L8
mov	di,*-28.(bp)
add	di,#500.
mov	*-28.(bp),di
L8:.data
L10009:	.float 1.50000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,L10009
call	flds
call	fcmp
sahf
bge	L9
mov	di,*-28.(bp)
add	di,#600.
mov	*-28.(bp),di
L9:lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-20.(bp)
call	fldd
lea	ax,*-12.(bp)
call	fsubd
call	fcmp
sahf
ble	L10
mov	di,*-28.(bp)
add	di,*4.
mov	*-28.(bp),di
L10:.data
L10010:	.float 1.50000000000000000e+00
.text
lea	ax,L10010
call	flds
mov	di,*-26.(bp)
mov	ax,di
call	itof
call	fcmp
sahf
bge	L11
mov	di,*-28.(bp)
add	di,*8.
mov	*-28.(bp),di
L11:.data
L10011:	.float 1.50000000000000000e+00
.text
mov	di,*-26.(bp)
mov	ax,di
call	itof
lea	ax,L10011
call	flds
call	fcmp
sahf
bge	L12
mov	di,*-28.(bp)
add	di,#700.
mov	*-28.(bp),di
L12:.data
L10012:	.float 2.50000000000000000e-01
.text
mov	di,*-26.(bp)
mov	ax,di
call	itof
lea	ax,L10012
call	fadds
lea	ax,*-20.(bp)
call	fldd
call	fcmp
sahf
bge	L13
mov	di,*-28.(bp)
add	di,*16.
mov	*-28.(bp),di
L13:mov	di,*-28.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*24.
jmp	L2
.globl	fltused
.data
