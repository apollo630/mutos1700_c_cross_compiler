.globl	_dd
.text
.even
_dd:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _b=12.
jmp	L1
L2:.data
L10000:	.float 1.00000000000000000e+00
.text
lea	ax,*4.(bp)
call	fldd
lea	ax,L10000
call	fadds
lea	ax,*4.(bp)
call	fstdp
.data
L10001:	.float 1.00000000000000000e+00
.text
lea	ax,*12.(bp)
call	fldd
lea	ax,L10001
call	fsubs
lea	ax,*12.(bp)
call	fstdp
lea	ax,*4.(bp)
call	fldd
lea	ax,*12.(bp)
call	faddd
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
L5:| _s=-20.
| _q=-22.
| _a=-46.
| _d=-54.
| _e=-62.
| _w=-70.
| _y=-78.
| _f=-82.
| _g=-86.
| _i=-88.
| _x=-90.
| _r=-92.
mov	*-92.(bp),*0.
mov	*-88.(bp),*1.
.data
L10002:	.float 1.50000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10003:	.float 2.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	di,*-20.(bp)
mov	*-22.(bp),di
.data
L10004:	.float 5.00000000000000000e-01
.text
lea	ax,L10004
call	flds
lea	ax,*-46.(bp)
call	fstdp
.data
L10005:	.float 1.50000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-38.(bp)
call	fstdp
.data
L10006:	.float 2.50000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-30.(bp)
call	fstdp
.data
L10007:	.float 1.50000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10008:	.float 5.00000000000000000e-01
.text
lea	ax,L10008
call	flds
lea	ax,*-62.(bp)
call	fstdp
.data
L10009:	.float 2.50000000000000000e+00
.text
lea	ax,L10009
call	flds
lea	ax,*-82.(bp)
call	fstsp
.data
L10010:	.float 3.00000000000000000e+00
.text
lea	ax,L10010
call	flds
lea	ax,*-86.(bp)
call	fstsp
.data
L10011:	.float 4.00000000000000000e+00
.text
lea	ax,L10011
call	flds
lea	ax,*-70.(bp)
call	fstdp
.data
L10012:	.float 1.00000000000000000e+01
.text
lea	ax,L10012
call	flds
lea	ax,*-78.(bp)
call	fstdp
.data
L10015:	.float 5.00000000000000000e-01
.text
lea	ax,*-82.(bp)
call	flds
lea	ax,L10015
call	fadds
lea	ax,*-82.(bp)
call	fstsp
.data
L10013:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10013
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10014:	.float 2.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10014
call	fsubs
lea	ax,*-62.(bp)
call	fstdp
lea	ax,*-82.(bp)
call	flds
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-82.(bp)
call	fadds
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-62.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10016:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10016
call	fadds
lea	ax,*-54.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-70.(bp)
call	faddd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10017:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10017
call	fadds
lea	ax,*-54.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-70.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
.data
L10018:	.float 4.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10018
call	fdivs
mov	ax,*-92.(bp)
call	itof
call	fadd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10019:	.float 1.50000000000000000e+00
.text
lea	ax,L10019
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10020:	.float 5.00000000000000000e-01
.text
lea	ax,L10020
call	flds
lea	ax,*-62.(bp)
call	fstdp
.data
L10023:	.float 2.00000000000000000e+00
.text
.data
L10021:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10021
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10022:	.float 2.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10022
call	fadds
lea	ax,*-62.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
lea	ax,L10023
call	fmuls
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10024:	.float 1.50000000000000000e+00
.text
lea	ax,L10024
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10025:	.float 5.00000000000000000e-01
.text
lea	ax,L10025
call	flds
lea	ax,*-62.(bp)
call	fstdp
.data
L10027:	.float 1.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10027
call	fsubs
lea	ax,*-62.(bp)
call	fstdp
.data
L10026:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10026
call	fadds
lea	ax,*-54.(bp)
call	fstdp
lea	ax,*-78.(bp)
call	fldd
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	faddd
call	fsub
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10028:	.float 1.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10028
call	fadds
lea	ax,*-62.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
.data
L10029:	.float 2.00000000000000000e+00
.text
lea	ax,L10029
call	flds
lea	ax,*-62.(bp)
call	faddd
call	fsub
lea	ax,*-54.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
lea	ax,*-62.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
lea	ax,*-54.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_dd
add	sp,*16.
call	fldd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10030:	.float 1.00000000000000000e+00
.text
lea	ax,*-82.(bp)
call	flds
lea	ax,L10030
call	fadds
lea	ax,*-82.(bp)
call	fstsp
lea	ax,*-82.(bp)
call	flds
lea	ax,*-86.(bp)
call	flds
call	fcmp
sahf
bgt	L10031
mov	di,*0.
jmp	L10032
L10031:mov	di,*1.
L10032:mov	*-90.(bp),di
mov	di,*-92.(bp)
add	di,*-90.(bp)
mov	*-92.(bp),di
.data
L10033:	.float 1.00000000000000000e+00
.text
lea	ax,*-82.(bp)
call	flds
lea	ax,L10033
call	fadds
lea	ax,*-82.(bp)
call	fstsp
lea	ax,*-82.(bp)
call	flds
lea	ax,*-86.(bp)
call	flds
call	fcmp
sahf
bgt	L10034
mov	di,*0.
jmp	L10035
L10034:mov	di,*1.
L10035:mov	*-90.(bp),di
mov	di,*-92.(bp)
add	di,*-90.(bp)
mov	*-92.(bp),di
.data
L10036:	.float 1.00000000000000000e+00
.text
lea	ax,L10036
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10037:	.float 2.00000000000000000e+00
.text
lea	ax,L10037
call	flds
lea	ax,*-62.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
mov	ax,*-22.(bp)
mov	bx,ax
lea	ax,*8.(bx)
|
push	ax
call	faddd
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
mov	ax,*-22.(bp)
mov	bx,ax
lea	ax,(bx)
|
push	ax
call	fmuld
pop	ax
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10038:	.float 5.00000000000000000e-01
.text
lea	ax,L10038
call	flds
lea	ax,*-62.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
mov	ax,*-22.(bp)
mov	bx,ax
lea	ax,*8.(bx)
|
push	ax
call	fmuld
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
mov	ax,*-22.(bp)
mov	bx,ax
lea	ax,*8.(bx)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
.data
L10039:	.float 4.00000000000000000e+00
.text
lea	ax,L10039
call	flds
lea	ax,*-12.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-92.(bp),ax
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	faddd
mov	di,*-22.(bp)
lea	ax,(di)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
.data
L10040:	.float 2.00000000000000000e+00
.text
lea	ax,L10040
call	flds
lea	ax,*-20.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-92.(bp),ax
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
lea	di,*-46.(bp)
mov	si,*-88.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
|
push	ax
call	faddd
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-92.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-38.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10043:	.float 1.00000000000000000e+00
.text
lea	ax,*-70.(bp)
call	fldd
lea	ax,L10043
call	fadds
lea	ax,*-70.(bp)
call	fstdp
.data
L10041:	.float 1.00000000000000000e+00
.text
lea	ax,L10041
call	flds
lea	ax,*-70.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-54.(bp)
call	fstdp
.data
L10042:	.float 2.00000000000000000e+00
.text
lea	ax,L10042
call	flds
lea	ax,*-54.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
.data
L10044:	.float 4.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10044
call	fdivs
.data
L10045:	.float 4.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10045
call	fdivs
call	fadd
mov	ax,*-92.(bp)
call	itof
call	fadd
lea	ax,*-70.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
.data
L10047:	.float 1.00000000000000000e+00
.text
lea	ax,*-70.(bp)
call	fldd
lea	ax,L10047
call	fadds
lea	ax,*-70.(bp)
call	fstdp
.data
L10048:	.float 1.00000000000000000e+00
.text
lea	ax,*-78.(bp)
call	fldd
lea	ax,L10048
call	fadds
lea	ax,*-78.(bp)
call	fstdp
lea	ax,*-70.(bp)
call	fldd
lea	ax,*-78.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-54.(bp)
call	fstdp
.data
L10046:	.float 2.00000000000000000e+00
.text
lea	ax,L10046
call	flds
lea	ax,*-54.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
.data
L10049:	.float 1.00000000000000000e+01
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10049
call	fdivs
.data
L10050:	.float 1.00000000000000000e+01
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10050
call	fdivs
call	fadd
.data
L10051:	.float 4.00000000000000000e+00
.text
lea	ax,*-78.(bp)
call	fldd
lea	ax,L10051
call	fdivs
call	fadd
mov	ax,*-92.(bp)
call	itof
call	fadd
lea	ax,*-70.(bp)
call	faddd
call	ftoi
mov	*-92.(bp),ax
mov	di,*-92.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*88.
jmp	L5
.globl	fltused
.data
