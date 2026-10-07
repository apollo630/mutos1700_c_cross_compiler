.globl	_pick
.text
.even
_pick:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _a=4.
| _i=6.
jmp	L1
L2:mov	di,*6.(bp)
mov	cx,*3.
sal	di,cl
add	di,*4.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 11
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
L5:| _s=-22.
| _q=-24.
| _a=-56.
| _d=-64.
| _e=-72.
| _f=-76.
| _i=-78.
| _j=-80.
| _x=-82.
| _r=-84.
mov	*-78.(bp),*1.
mov	*-80.(bp),*2.
mov	*-82.(bp),*1.
mov	*-84.(bp),*0.
.data
L10000:	.float 1.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-22.(bp)
call	fstdp
.data
L10001:	.float 8.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-14.(bp)
call	fstdp
mov	*-6.(bp),*6.
lea	di,*-22.(bp)
mov	*-24.(bp),di
.data
L10002:	.float 5.00000000000000000e-01
.text
lea	ax,L10002
call	flds
lea	ax,*-56.(bp)
call	fstdp
.data
L10003:	.float 1.75000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-48.(bp)
call	fstdp
.data
L10004:	.float 3.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-40.(bp)
call	fstdp
.data
L10005:	.float 2.50000000000000000e-01
.text
lea	ax,L10005
call	flds
lea	ax,*-32.(bp)
call	fstdp
.data
L10006:	.float 4.50000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-64.(bp)
call	fstdp
.data
L10007:	.float 5.00000000000000000e-01
.text
lea	ax,L10007
call	flds
lea	ax,*-72.(bp)
call	fstdp
.data
L10008:	.float 1.50000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,*-76.(bp)
call	fstsp
.data
L10009:	.float 1.00000000000000000e+00
.text
lea	ax,*-76.(bp)
call	flds
lea	ax,L10009
call	fadds
lea	ax,*-76.(bp)
call	fstsp
.data
L10010:	.float 1.00000000000000000e+00
.text
lea	ax,*-76.(bp)
call	flds
lea	ax,L10010
call	fsubs
lea	ax,*-76.(bp)
call	fstsp
.data
L10011:	.float 1.00000000000000000e+00
.text
lea	ax,*-76.(bp)
call	flds
lea	ax,L10011
call	fsubs
lea	ax,*-76.(bp)
call	fstsp
.data
L10012:	.float 4.00000000000000000e+00
.text
lea	ax,*-76.(bp)
call	flds
lea	ax,L10012
call	fmuls
mov	ax,*-84.(bp)
call	itof
call	fadd
call	ftoi
mov	*-84.(bp),ax
.data
L10013:	.float 1.50000000000000000e+00
.text
cmp	*-82.(bp),*0
beq	L10014
lea	ax,*-64.(bp)
call	fldd
jmp	L10015
L10014:lea	ax,*-72.(bp)
call	fldd
L10015:lea	ax,L10013
call	fadds
lea	ax,*-72.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-72.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
mov	di,*-80.(bp)
cmp	*-78.(bp),di
ble	L10016
lea	ax,*-64.(bp)
call	fldd
jmp	L10017
L10016:lea	ax,*-72.(bp)
call	fldd
L10017:lea	ax,*-76.(bp)
call	fmuls
lea	ax,*-72.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-72.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
.data
L10018:	.float 2.00000000000000000e+00
.text
mov	di,*-24.(bp)
lea	ax,*8.(di)
|
push	ax
call	fldd
lea	ax,L10018
call	flds
call	fdiv
pop	ax
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-14.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
.data
L10019:	.float 2.00000000000000000e+00
.text
lea	ax,L10019
call	flds
lea	ax,*-64.(bp)
call	fstdp
.data
L10020:	.float 5.00000000000000000e-01
.text
lea	ax,L10020
call	flds
lea	ax,*-72.(bp)
call	fstdp
mov	di,*-24.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,*-64.(bp)
call	fldd
lea	ax,*-72.(bp)
call	fmuld
call	fsub
pop	ax
call	fstdp
mov	di,*-24.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,*-64.(bp)
call	fldd
lea	ax,*-72.(bp)
call	faddd
call	fdiv
pop	ax
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
.data
L10021:	.float 1.00000000000000000e+01
.text
lea	ax,L10021
call	flds
lea	ax,*-22.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-84.(bp),ax
.data
L10022:	.float 1.00000000000000000e+00
.text
lea	di,*-56.(bp)
mov	si,*-78.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,*8.(di)
|
push	ax
call	fldd
lea	ax,L10022
call	fadds
pop	ax
call	fstdp
.data
L10023:	.float 2.00000000000000000e+00
.text
lea	di,*-56.(bp)
mov	si,*-80.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
lea	ax,L10023
call	fmuls
lea	di,*-56.(bp)
mov	si,*-78.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,*16.(di)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-40.(bp)
call	faddd
lea	ax,*-32.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
.data
L10024:	.float 2.00000000000000000e+00
.text
mov	di,*-24.(bp)
mov	di,*16.(di)
mov	ax,di
call	itof
lea	ax,L10024
call	fmuls
lea	ax,*-64.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-64.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
lea	ax,*-64.(bp)
call	fldd
lea	ax,*-72.(bp)
call	faddd
push	*-78.(bp)
lea	di,*-56.(bp)
push	di
call	_pick
add	sp,*4.
mov	di,ax
lea	ax,(di)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
.data
L10025:	.float 2.00000000000000000e+00
.text
lea	ax,L10025
call	flds
lea	ax,*-48.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-84.(bp),ax
.data
L10026:	.float 1.00000000000000000e+00
.text
lea	ax,*-76.(bp)
call	flds
call	fdup
lea	ax,L10026
call	fadds
lea	ax,*-76.(bp)
call	fstsp
lea	ax,*-72.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-76.(bp)
call	fadds
lea	ax,*-72.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
.data
L10027:	.float 1.00000000000000000e+00
.text
lea	ax,*-76.(bp)
call	flds
lea	ax,L10027
call	fadds
lea	ax,*-76.(bp)
call	fstsp
lea	ax,*-76.(bp)
call	flds
lea	ax,*-64.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-64.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
lea	ax,*-64.(bp)
call	fldd
mov	di,*-24.(bp)
lea	ax,(di)
|
push	ax
call	faddd
pop	ax
call	fstd
lea	ax,*-72.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-72.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
.data
L10029:	.float 1.00000000000000000e+00
.text
lea	ax,*-64.(bp)
call	fldd
lea	ax,L10029
call	fadds
lea	ax,*-64.(bp)
call	fstdp
.data
L10028:	.float 2.00000000000000000e+00
.text
lea	ax,L10028
call	flds
lea	ax,*-64.(bp)
call	fmuld
lea	ax,*-72.(bp)
call	fstdp
mov	di,*-84.(bp)
mov	ax,di
call	itof
lea	ax,*-72.(bp)
call	faddd
lea	ax,*-64.(bp)
call	faddd
call	ftoi
mov	*-84.(bp),ax
mov	di,*-84.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*80.
jmp	L5
.globl	fltused
.data
