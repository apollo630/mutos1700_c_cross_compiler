.globl	_tw
.text
.even
_tw:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L1
L2:lea	ax,*4.(bp)
call	fldd
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L3
L3:|RTYP 3
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
L5:| _d=-12.
| _e=-20.
.data
L10000:	.double 1.00000000000000000e-01
.text
lea	ax,L10000
call	fldd
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.double 3.00000000000000000e-02
.text
lea	ax,L10001
call	fldd
lea	ax,*-12.(bp)
call	fstdp
.data
L10002:	.double 3.14159265358979001e+00
.text
lea	ax,L10002
call	fldd
lea	ax,*-12.(bp)
call	fstdp
.data
L10003:	.double 1.00000000000000000e-05
.text
lea	ax,L10003
call	fldd
lea	ax,*-12.(bp)
call	fstdp
.data
L10004:	.double 1.00000000000000005e+30
.text
lea	ax,L10004
call	fldd
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.double 1.67772170000000000e+07
.text
lea	ax,L10005
call	fldd
lea	ax,*-12.(bp)
call	fstdp
.data
L10006:	.float 7.20575940379279360e+16
.text
lea	ax,L10006
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10008:	.float 2.00000000000000000e+00
.text
.data
L10007:	.float 1.50000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,L10007
call	fmuls
lea	ax,*-12.(bp)
call	fstdp
.data
L10009:	.float 1.50000000000000000e+00
.text
.data
L10010:	.float 2.00000000000000000e+00
.text
lea	ax,L10009
call	flds
lea	ax,L10010
call	fmuls
lea	ax,*-12.(bp)
call	fstdp
.data
L10011:	.float 2.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10011
call	fsubs
lea	ax,*-20.(bp)
call	fstdp
.data
L10012:	.float 2.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10012
call	fsubs
lea	ax,*-20.(bp)
call	fstdp
.data
L10013:	.float 1.50000000000000000e+00
.text
lea	ax,L10013
call	flds
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
lea	ax,*-20.(bp)
call	fstdp
.data
L10014:	.float 0.00000000000000000e+00
.text
lea	ax,L10014
call	flds
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
lea	ax,*-20.(bp)
call	fstdp
.data
L10015:	.float 3.50000000000000000e+00
.text
lea	ax,L10015
call	flds
sub	sp,*8
mov	ax,sp
call	fstdp
call	_tw
add	sp,*8.
call	fldd
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
call	ftoi
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*16.
jmp	L5
.globl	fltused
.data
