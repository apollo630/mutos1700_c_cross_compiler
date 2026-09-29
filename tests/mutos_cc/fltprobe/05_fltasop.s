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
L2:| _fl=-12.
| _flexp=-20.
| _exp5=-28.
| _t=-36.
| _f=-40.
| _c=-42.
.data
L10000:	.float 0.00000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
mov	*-42.(bp),*52.
.data
L10001:	.float 1.00000000000000000e+01
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fmuld
mov	ax,*-42.(bp)
add	ax,*-48.
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
mov	*-42.(bp),*50.
.data
L10002:	.float 1.00000000000000000e+01
.text
lea	ax,L10002
call	flds
lea	ax,*-12.(bp)
call	fmuld
mov	ax,*-42.(bp)
add	ax,*-48.
call	itof
call	fadd
lea	ax,*-12.(bp)
call	fstdp
.data
L10003:	.float 1.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10004:	.float 5.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-28.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-28.(bp)
call	fmuld
lea	ax,*-28.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fmuld
lea	ax,*-20.(bp)
call	fstdp
.data
L10005:	.float 2.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
.data
L10006:	.float 4.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10006
call	fdivs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fldd
lea	ax,*-12.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fdivd
lea	ax,*-12.(bp)
call	fstdp
mov	*-42.(bp),*2.
.data
L10007:	.float 2.00000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-12.(bp)
call	fmuld
mov	ax,*-42.(bp)
call	itof
call	fsub
lea	ax,*-36.(bp)
call	fstdp
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-12.(bp)
call	faddd
mov	di,*-42.(bp)
mov	ax,di
call	itof
call	fdiv
lea	ax,*-36.(bp)
call	fstdp
.data
L10008:	.float 3.00000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,*-40.(bp)
call	fstsp
lea	ax,*-40.(bp)
call	flds
lea	ax,*-40.(bp)
call	fmuls
lea	ax,*-40.(bp)
call	fstsp
.data
L10009:	.float 3.00000000000000000e+00
.text
lea	ax,*-40.(bp)
call	flds
lea	ax,L10009
call	fdivs
lea	ax,*-40.(bp)
call	fstsp
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-28.(bp)
call	fstd
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L4
mov	*-42.(bp),*0.
L4:lea	ax,*-36.(bp)
call	fldd
call	ftoi
mov	*-42.(bp),ax
lea	ax,*-40.(bp)
call	flds
call	ftoi
add	ax,*-42.(bp)
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*38.
jmp	L2
.globl	fltused
.data
