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
| _f=-24.
.data
L10000:	.float 0.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.float 0.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10002:	.float 4.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-24.(bp)
call	fstsp
.data
L10003:	.float 1.00000000000000000e+01
.text
lea	ax,L10003
call	flds
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10004:	.float 1.00000000000000000e+01
.text
lea	ax,L10004
call	flds
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.float 4.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10005
call	fdivs
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-24.(bp)
call	flds
call	fneg
lea	ax,*-24.(bp)
call	fstsp
.data
L10006:	.float -2.00000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10007:	.float -1.50000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-24.(bp)
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	ftoi
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*20.
jmp	L2
.globl	fltused
.data
