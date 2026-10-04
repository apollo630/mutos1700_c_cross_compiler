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
| _a=-48.
| _d=-56.
| _e=-64.
| _f=-68.
| _i=-70.
| _j=-72.
| _x=-74.
| _r=-76.
mov	*-70.(bp),*1.
mov	*-72.(bp),*2.
mov	*-74.(bp),*5.
mov	*-76.(bp),*0.
.data
L10000:	.float 1.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-22.(bp)
call	fstdp
.data
L10001:	.float 2.25000000000000000e+00
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
lea	ax,*-48.(bp)
call	fstdp
.data
L10003:	.float 1.75000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-40.(bp)
call	fstdp
.data
L10004:	.float 3.00000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-32.(bp)
call	fstdp
.data
L10005:	.float 4.50000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-56.(bp)
call	fstdp
.data
L10006:	.float 2.50000000000000000e-01
.text
lea	ax,L10006
call	flds
lea	ax,*-64.(bp)
call	fstdp
.data
L10007:	.float 1.50000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-68.(bp)
call	fstsp
.data
L10008:	.float 1.00000000000000000e+00
.text
lea	ax,*-56.(bp)
call	fldd
lea	ax,L10008
call	fadds
lea	ax,*-56.(bp)
call	fstdp
lea	ax,*-56.(bp)
call	fldd
lea	ax,*-64.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
lea	ax,*-64.(bp)
call	faddd
lea	ax,*-56.(bp)
call	faddd
call	ftoi
mov	*-76.(bp),ax
.data
L10009:	.float 1.00000000000000000e+00
.text
lea	ax,*-56.(bp)
call	fldd
lea	ax,L10009
call	fsubs
lea	ax,*-56.(bp)
call	fstdp
lea	ax,*-56.(bp)
call	fldd
lea	ax,*-64.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
lea	ax,*-64.(bp)
call	faddd
lea	ax,*-56.(bp)
call	faddd
call	ftoi
mov	*-76.(bp),ax
cmp	*-74.(bp),*0
beq	L10012
.data
L10010:	.float 1.50000000000000000e+00
.text
lea	ax,L10010
call	flds
jmp	L10013
L10012:.data
L10011:	.float 2.50000000000000000e+00
.text
lea	ax,L10011
call	flds
L10013:lea	ax,*-64.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10014:	.float 2.00000000000000000e+00
.text
lea	ax,L10014
call	flds
lea	ax,*-64.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-76.(bp),ax
mov	di,*-72.(bp)
cmp	*-70.(bp),di
ble	L10015
lea	ax,*-64.(bp)
call	fldd
jmp	L10016
L10015:lea	ax,*-56.(bp)
call	fldd
L10016:lea	ax,*-56.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10017:	.float 2.00000000000000000e+00
.text
lea	ax,L10017
call	flds
lea	ax,*-56.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-76.(bp),ax
mov	di,*-70.(bp)
neg	di
mov	ax,di
call	itof
lea	ax,*-64.(bp)
call	fmuld
lea	ax,*-56.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10018:	.float 2.00000000000000000e+00
.text
lea	ax,L10018
call	flds
lea	ax,*-56.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-76.(bp),ax
mov	di,*-24.(bp)
lea	ax,*8.(di)
|
push	ax
call	fldd
lea	ax,*-64.(bp)
call	fsubd
pop	ax
call	fstdp
mov	di,*-24.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,*-64.(bp)
call	fldd
call	fdiv
pop	ax
call	fstdp
.data
L10019:	.float 4.00000000000000000e+00
.text
lea	ax,*-22.(bp)
call	fldd
lea	ax,*-14.(bp)
call	faddd
lea	ax,L10019
call	fmuls
mov	ax,*-76.(bp)
call	itof
call	fadd
call	ftoi
mov	*-76.(bp),ax
.data
L10020:	.float 4.00000000000000000e+00
.text
mov	di,*-24.(bp)
lea	ax,*8.(di)
call	fldd
lea	ax,L10020
call	fmuls
call	ftoi
mov	*-74.(bp),ax
mov	di,*-76.(bp)
add	di,*-74.(bp)
mov	*-76.(bp),di
mov	di,*-24.(bp)
lea	ax,(di)
call	fldd
lea	ax,*-64.(bp)
call	fldd
call	fcmp
sahf
ble	L7
mov	di,*-76.(bp)
add	di,*100.
mov	*-76.(bp),di
L7:lea	di,*-48.(bp)
mov	si,*-70.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
mov	di,*-24.(bp)
lea	ax,*8.(di)
call	fldd
call	fcmp
sahf
bge	L8
mov	di,*-76.(bp)
add	di,*100.
mov	*-76.(bp),di
L8:lea	di,*-48.(bp)
mov	si,*-70.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	fldd
mov	di,*-24.(bp)
lea	ax,(di)
call	fldd
lea	ax,*-14.(bp)
call	fmuld
call	fadd
lea	ax,*-56.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10021:	.float 2.00000000000000000e+00
.text
lea	ax,L10021
call	flds
lea	ax,*-56.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-76.(bp),ax
mov	ax,*-70.(bp)
imul	*-72.(bp)
call	itof
lea	ax,*-56.(bp)
call	faddd
lea	ax,*-56.(bp)
call	fstdp
mov	di,*-70.(bp)
inc	di
mov	ax,di
call	itof
lea	ax,*-56.(bp)
call	fmuld
lea	ax,*-64.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10022:	.float 2.00000000000000000e+00
.text
lea	ax,L10022
call	flds
lea	ax,*-56.(bp)
call	fmuld
call	fadd
lea	ax,*-64.(bp)
call	faddd
call	ftoi
mov	*-76.(bp),ax
.data
L10023:	.float 2.00000000000000000e+00
.text
cmp	*-74.(bp),*0
beq	L10024
lea	ax,*-56.(bp)
call	fldd
jmp	L10025
L10024:lea	ax,*-64.(bp)
call	fldd
L10025:lea	ax,L10023
call	fmuls
mov	ax,*-76.(bp)
call	itof
call	fadd
call	ftoi
mov	*-76.(bp),ax
mov	di,*-24.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,*-56.(bp)
call	fldd
lea	ax,*-64.(bp)
call	fmuld
call	fadd
pop	ax
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10026:	.float 2.00000000000000000e+00
.text
lea	ax,L10026
call	flds
lea	ax,*-22.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-76.(bp),ax
.data
L10027:	.float 2.00000000000000000e+00
.text
lea	ax,L10027
call	flds
lea	di,*-48.(bp)
mov	si,*-70.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,*8.(di)
call	fstdp
lea	di,*-48.(bp)
mov	si,*-70.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,*8.(di)
call	fldd
lea	di,*-48.(bp)
mov	si,*-72.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
call	faddd
lea	ax,*-56.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
lea	ax,*-56.(bp)
call	faddd
call	ftoi
mov	*-76.(bp),ax
mov	di,*-24.(bp)
mov	di,*16.(di)
mov	ax,di
call	itof
lea	ax,*-56.(bp)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
lea	ax,*-56.(bp)
call	faddd
call	ftoi
mov	*-76.(bp),ax
.data
L10028:	.float 2.50000000000000000e+00
.text
lea	ax,L10028
call	flds
mov	di,*1.
push	di
lea	di,*-48.(bp)
push	di
call	_pick
add	sp,*4.
mov	di,ax
lea	ax,(di)
call	fstdp
mov	di,*-76.(bp)
mov	ax,di
call	itof
.data
L10029:	.float 2.00000000000000000e+00
.text
lea	ax,L10029
call	flds
lea	ax,*-40.(bp)
call	fmuld
call	fadd
call	ftoi
mov	*-76.(bp),ax
.data
L10030:	.float 1.00000000000000000e+00
.text
lea	ax,*-68.(bp)
call	flds
lea	ax,L10030
call	fadds
lea	ax,*-68.(bp)
call	fstsp
.data
L10031:	.float 2.00000000000000000e+00
.text
lea	ax,*-68.(bp)
call	flds
lea	ax,L10031
call	fmuls
mov	ax,*-76.(bp)
call	itof
call	fadd
call	ftoi
mov	*-76.(bp),ax
mov	di,*-76.(bp)
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*72.
jmp	L5
.globl	fltused
.data
