.globl	_half
.text
.even
_half:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
| _x=4.
jmp	L1
L2:.data
L10000:	.float 2.00000000000000000e+00
.text
lea	ax,*4.(bp)
call	fldd
lea	ax,L10000
call	fdivs
lea	ax,fac
call	fstdp
lea	ax,fac
jmp	L3
L3:|RTYP 3
jmp	cret
L1:jmp	L2
.globl	_f1
.text
.even
_f1:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L4
L5:| _d=-12.
.data
L10001:	.float 3.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
mov	di,*1.
mov	ax,di
jmp	L6
L6:|RTYP 0
jmp	cret
L4:sub	sp,*8.
jmp	L5
.globl	_f2
.text
.even
_f2:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L7
L8:| _s=-20.
| _q=-22.
| _d=-30.
| _e=-38.
| _w=-46.
| _y=-54.
| _x=-56.
lea	di,*-20.(bp)
mov	*-22.(bp),di
.data
L10002:	.float 1.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-30.(bp)
call	fstdp
.data
L10003:	.float 1.00000000000000000e+00
.text
lea	ax,*-30.(bp)
call	fldd
lea	ax,L10003
call	fadds
lea	ax,*-30.(bp)
call	fstdp
.data
L10004:	.float 1.00000000000000000e+00
.text
lea	ax,*-30.(bp)
call	fldd
lea	ax,L10004
call	fadds
lea	ax,*-30.(bp)
call	fstdp
.data
L10005:	.float 1.00000000000000000e+00
.text
lea	ax,*-30.(bp)
call	fldd
lea	ax,L10005
call	fsubs
lea	ax,*-30.(bp)
call	fstdp
.data
L10006:	.float 2.00000000000000000e+00
.text
lea	ax,L10006
call	flds
lea	ax,*-38.(bp)
call	fstdp
.data
L10007:	.float 2.00000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-38.(bp)
call	fmuld
lea	ax,*-38.(bp)
call	fstdp
.data
L10008:	.float 1.00000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10009:	.float 1.00000000000000000e+00
.text
mov	di,*-22.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,L10009
call	fadds
pop	ax
call	fstdp
.data
L10010:	.float 1.00000000000000000e+00
.text
lea	ax,*-30.(bp)
call	fldd
call	fdup
lea	ax,L10010
call	fadds
lea	ax,*-30.(bp)
call	fstdp
lea	ax,*-38.(bp)
call	fstdp
.data
L10011:	.float 4.00000000000000000e+00
.text
lea	ax,L10011
call	flds
lea	ax,*-46.(bp)
call	fstdp
.data
L10012:	.float 1.00000000000000000e+01
.text
lea	ax,L10012
call	flds
lea	ax,*-54.(bp)
call	fstdp
mov	*-56.(bp),*0.
lea	ax,*-46.(bp)
call	fldd
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-30.(bp)
call	faddd
lea	ax,*-30.(bp)
call	fstdp
.data
L10013:	.float 2.00000000000000000e+00
.text
lea	ax,L10013
call	flds
lea	ax,*-30.(bp)
call	fldd
call	fcmp
sahf
bge	L10
mov	*-56.(bp),*1.
L10:mov	di,*-56.(bp)
mov	ax,di
jmp	L9
L9:|RTYP 0
jmp	cret
L7:sub	sp,*52.
jmp	L8
.globl	_f3
.text
.even
_f3:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L11
L12:| _d=-12.
| _e=-20.
| _w=-28.
| _y=-36.
| _x=-38.
.data
L10014:	.float 1.50000000000000000e+00
.text
lea	ax,L10014
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10015:	.float 4.00000000000000000e+00
.text
lea	ax,L10015
call	flds
lea	ax,*-28.(bp)
call	fstdp
.data
L10016:	.float 1.00000000000000000e+01
.text
lea	ax,L10016
call	flds
lea	ax,*-36.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10017:	.float 2.00000000000000000e+00
.text
lea	ax,L10017
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10018
mov	di,*0.
jmp	L10019
L10018:mov	di,*1.
L10019:mov	*-38.(bp),di
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10020:	.float 2.00000000000000000e+00
.text
lea	ax,L10020
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10021
mov	di,*0.
jmp	L10022
L10021:mov	di,*1.
L10022:mov	*-38.(bp),di
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10023:	.float 2.00000000000000000e+00
.text
lea	ax,L10023
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10024
mov	di,*0.
jmp	L10025
L10024:mov	di,*1.
L10025:mov	*-38.(bp),di
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10026:	.float 2.00000000000000000e+00
.text
lea	ax,L10026
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10027
mov	di,*0.
jmp	L10028
L10027:mov	di,*1.
L10028:mov	*-38.(bp),di
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10029:	.float 2.00000000000000000e+00
.text
lea	ax,L10029
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10030
mov	di,*0.
jmp	L10031
L10030:mov	di,*1.
L10031:mov	*-38.(bp),di
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10032:	.float 2.00000000000000000e+00
.text
lea	ax,L10032
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10033
mov	di,*0.
jmp	L10034
L10033:mov	di,*1.
L10034:mov	*-38.(bp),di
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10035:	.float 2.00000000000000000e+00
.text
lea	ax,L10035
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10036
mov	di,*0.
jmp	L10037
L10036:mov	di,*1.
L10037:mov	*-38.(bp),di
.data
L10038:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
call	fdup
lea	ax,L10038
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-20.(bp)
call	fstdp
.data
L10039:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
sub	sp,*8
mov	ax,sp
call	fstdp
call	_half
add	sp,*8.
call	fldd
lea	ax,L10039
call	fadds
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-38.(bp)
mov	ax,di
jmp	L13
L13:|RTYP 0
jmp	cret
L11:sub	sp,*34.
jmp	L12
.globl	_main
.text
.even
_main:
push	bp
mov	bp,sp
push	di
push	si
|NREG 3
jmp	L14
L15:call	_f3
push	ax
call	_f2
push	ax
call	_f1
pop	bx
add	ax,bx
pop	bx
add	ax,bx
jmp	L16
L16:|RTYP 0
jmp	cret
L14:jmp	L15
.globl	fltused
.data
