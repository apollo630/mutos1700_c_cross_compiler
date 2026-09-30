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
| _r=-26.
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
L10002:	.float 5.00000000000000000e-01
.text
lea	ax,L10002
call	flds
lea	ax,*-24.(bp)
call	fstsp
mov	*-26.(bp),*0.
.data
L10003:	.double 1.00000000000000000e-01
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10003
call	faddd
lea	ax,*-20.(bp)
call	fstdp
.data
L10004:	.double 1.00000000000000000e-01
.text
lea	ax,L10004
call	fldd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-20.(bp)
call	fstdp
.data
L10005:	.double 1.00000000000000000e-01
.text
lea	ax,*-24.(bp)
call	flds
lea	ax,L10005
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
.data
L10006:	.double 1.00000000000000000e-01
.text
lea	ax,*-24.(bp)
call	flds
lea	ax,L10006
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
.data
L10007:	.double 1.00000000000000000e-01
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10007
call	fmuld
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-20.(bp)
call	fstdp
.data
L10008:	.double 1.00000000000000000e-01
.text
lea	ax,L10008
call	fldd
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
ble	L4
mov	di,*-26.(bp)
inc	di
mov	*-26.(bp),di
L4:.data
L10009:	.double 1.00000000000000000e-01
.text
lea	ax,L10009
call	fldd
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L5
mov	di,*-26.(bp)
add	di,*2.
mov	*-26.(bp),di
L5:.data
L10010:	.double 1.00000000000000000e-01
.text
lea	ax,*-24.(bp)
call	flds
lea	ax,L10010
call	fldd
call	fcmp
sahf
ble	L6
mov	di,*-26.(bp)
add	di,*4.
mov	*-26.(bp),di
L6:.data
L10011:	.double 1.00000000000000000e-01
.text
lea	ax,*-24.(bp)
call	flds
lea	ax,L10011
call	fldd
call	fcmp
sahf
bge	L7
mov	di,*-26.(bp)
add	di,*32.
mov	*-26.(bp),di
L7:.data
L10012:	.double 1.00000000000000000e-01
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,L10012
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
.data
L10013:	.float 2.00000000000000000e+01
.text
lea	ax,L10013
call	flds
lea	ax,*-20.(bp)
call	fmuld
call	ftoi
add	ax,*-26.(bp)
mov	*-26.(bp),ax
mov	di,*-26.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*22.
jmp	L2
.globl	fltused
.data
