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
L2:| _s=-20.
| _q=-22.
| _a=-46.
| _d=-54.
| _e=-62.
| _f=-66.
| _i=-68.
| _x=-70.
| _r=-72.
mov	*-72.(bp),*0.
mov	*-68.(bp),*1.
.data
L10000:	.float 1.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10001:	.float 2.00000000000000000e+00
.text
lea	ax,L10001
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	di,*-20.(bp)
mov	*-22.(bp),di
.data
L10002:	.float 5.00000000000000000e-01
.text
lea	ax,L10002
call	flds
lea	ax,*-46.(bp)
call	fstdp
.data
L10003:	.float 1.50000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-38.(bp)
call	fstdp
.data
L10004:	.float 2.50000000000000000e+00
.text
lea	ax,L10004
call	flds
lea	ax,*-30.(bp)
call	fstdp
.data
L10005:	.float 1.50000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10006:	.float 5.00000000000000000e-01
.text
lea	ax,L10006
call	flds
lea	ax,*-62.(bp)
call	fstdp
.data
L10007:	.float 2.50000000000000000e+00
.text
lea	ax,L10007
call	flds
lea	ax,*-66.(bp)
call	fstsp
.data
L10008:	.float 5.00000000000000000e-01
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10008
call	fsubs
lea	ax,*-54.(bp)
call	fstdp
.data
L10009:	.float 2.00000000000000000e+00
.text
lea	ax,L10009
call	flds
lea	ax,*-54.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10011:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10011
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10010:	.float 3.00000000000000000e+00
.text
lea	ax,L10010
call	flds
lea	ax,*-54.(bp)
call	fsubd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
.data
L10012:	.float 2.00000000000000000e+00
.text
lea	ax,L10012
call	flds
lea	ax,*-62.(bp)
call	fmuld
call	fadd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10013:	.float 5.00000000000000000e-01
.text
lea	ax,L10013
call	flds
lea	ax,*-62.(bp)
call	fstdp
.data
L10014:	.float 2.00000000000000000e+00
.text
lea	ax,L10014
call	flds
lea	ax,*-62.(bp)
call	fmuld
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-54.(bp)
call	fstdp
.data
L10015:	.float 3.00000000000000000e+00
.text
lea	ax,L10015
call	flds
lea	ax,*-54.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
mov	di,*-68.(bp)
mov	ax,di
call	itof
lea	ax,*-54.(bp)
call	faddd
lea	ax,*-54.(bp)
call	fstdp
.data
L10016:	.float 2.00000000000000000e+00
.text
lea	ax,L10016
call	flds
lea	ax,*-54.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10017:	.float 1.00000000000000000e+00
.text
lea	ax,*-20.(bp)
call	fldd
lea	ax,L10017
call	fadds
lea	ax,*-20.(bp)
call	fstdp
.data
L10018:	.float 2.00000000000000000e+00
.text
lea	ax,L10018
call	flds
lea	ax,*-20.(bp)
call	fmuld
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10019:	.float 5.00000000000000000e-01
.text
lea	ax,*-66.(bp)
call	flds
lea	ax,L10019
call	fsubs
lea	ax,*-66.(bp)
call	fstsp
.data
L10020:	.float 2.00000000000000000e+00
.text
lea	ax,*-66.(bp)
call	flds
lea	ax,L10020
call	fmuls
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-66.(bp)
call	fadds
lea	ax,*-62.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10021:	.float 1.50000000000000000e+00
.text
lea	ax,L10021
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10022:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10022
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10023:	.float 2.00000000000000000e+00
.text
lea	ax,L10023
call	flds
lea	ax,*-54.(bp)
call	fldd
call	fcmp
sahf
blt	L10024
mov	di,*0.
jmp	L10025
L10024:mov	di,*1.
L10025:mov	*-70.(bp),di
mov	di,*-72.(bp)
add	di,*-70.(bp)
mov	*-72.(bp),di
.data
L10026:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10026
call	fsubs
lea	ax,*-54.(bp)
call	fstdp
.data
L10027:	.float 1.00000000000000000e+00
.text
lea	ax,L10027
call	flds
lea	ax,*-54.(bp)
call	fldd
call	fcmp
sahf
ble	L4
mov	di,*-72.(bp)
inc	di
mov	*-72.(bp),di
L4:.data
L10029:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10029
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10028:	.float 1.00000000000000000e+00
.text
lea	ax,L10028
call	flds
lea	ax,*-54.(bp)
call	fldd
call	fcmp
sahf
blt	L10030
mov	di,*0.
jmp	L10031
L10030:mov	di,*1.
L10031:mov	*-70.(bp),di
mov	di,*-72.(bp)
add	di,*-70.(bp)
mov	*-72.(bp),di
.data
L10033:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10033
call	fsubs
lea	ax,*-54.(bp)
call	fstdp
.data
L10032:	.float 1.00000000000000000e+00
.text
lea	ax,L10032
call	flds
lea	ax,*-54.(bp)
call	fldd
call	fcmp
sahf
ble	L5
mov	di,*-72.(bp)
inc	di
mov	*-72.(bp),di
L5:.data
L10035:	.float 1.00000000000000000e+00
.text
lea	ax,*-66.(bp)
call	flds
lea	ax,L10035
call	fadds
lea	ax,*-66.(bp)
call	fstsp
.data
L10034:	.float 2.00000000000000000e+00
.text
lea	ax,L10034
call	flds
lea	ax,*-66.(bp)
call	flds
call	fcmp
sahf
blt	L10036
mov	di,*0.
jmp	L10037
L10036:mov	di,*1.
L10037:mov	*-70.(bp),di
mov	di,*-72.(bp)
add	di,*-70.(bp)
mov	*-72.(bp),di
.data
L10038:	.float 1.00000000000000000e+00
.text
lea	ax,L10038
call	flds
lea	ax,*-62.(bp)
call	fstdp
.data
L10040:	.float 1.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
lea	ax,L10040
call	fsubs
lea	ax,*-62.(bp)
call	fstdp
.data
L10039:	.float 2.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10039
call	fadds
lea	ax,*-54.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10041:	.float 2.00000000000000000e+00
.text
lea	ax,L10041
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10043:	.float 3.00000000000000000e+00
.text
.data
L10042:	.float 2.00000000000000000e+00
.text
lea	ax,L10042
call	flds
lea	ax,*-54.(bp)
call	fmuld
lea	ax,*-54.(bp)
call	fstd
lea	ax,L10043
call	fmuls
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10045:	.float 1.00000000000000000e+00
.text
.data
L10044:	.float 2.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
lea	ax,L10044
call	fdivs
lea	ax,*-54.(bp)
call	fstd
lea	ax,L10045
call	fadds
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10046:	.float 5.00000000000000000e-01
.text
mov	di,*-22.(bp)
lea	ax,*8.(di)
|
push	ax
call	fldd
lea	ax,L10046
call	flds
call	fsub
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10047:	.float 2.00000000000000000e+00
.text
mov	di,*-22.(bp)
lea	ax,*8.(di)
|
push	ax
call	fldd
lea	ax,L10047
call	flds
call	fdiv
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
.data
L10048:	.float 4.00000000000000000e+00
.text
lea	ax,L10048
call	flds
lea	ax,*-62.(bp)
call	fmuld
call	fadd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10049:	.float 1.00000000000000000e+00
.text
lea	ax,L10049
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10050:	.float 2.00000000000000000e+00
.text
lea	ax,L10050
call	flds
lea	ax,*-62.(bp)
call	fstdp
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
mov	ax,*-22.(bp)
mov	bx,ax
lea	ax,(bx)
|
push	ax
call	faddd
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10051:	.float 5.00000000000000000e-01
.text
lea	ax,L10051
call	flds
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-22.(bp)
lea	ax,*8.(di)
|
push	ax
call	fldd
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	fmuld
call	fsub
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
.data
L10052:	.float 4.00000000000000000e+00
.text
lea	ax,L10052
call	flds
lea	ax,*-62.(bp)
call	fmuld
call	fadd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10053:	.float 1.00000000000000000e+00
.text
lea	ax,L10053
call	flds
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-22.(bp)
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,*-54.(bp)
call	fldd
lea	ax,*-62.(bp)
call	faddd
call	fdiv
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
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
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10054:	.float 1.50000000000000000e+00
.text
lea	ax,L10054
call	flds
lea	di,*-46.(bp)
mov	si,*-68.(bp)
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
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-38.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10055:	.float 5.00000000000000000e-01
.text
lea	di,*-46.(bp)
mov	si,*-68.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,L10055
call	flds
call	fsub
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-38.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10056:	.float 2.00000000000000000e+00
.text
lea	di,*-46.(bp)
mov	si,*-68.(bp)
mov	cx,*3.
sal	si,cl
add	di,si
lea	ax,(di)
|
push	ax
call	fldd
lea	ax,L10056
call	flds
call	fdiv
pop	ax
call	fstd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
.data
L10057:	.float 2.00000000000000000e+00
.text
lea	ax,L10057
call	flds
lea	ax,*-62.(bp)
call	fmuld
call	fadd
lea	ax,*-38.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10058:	.float 2.00000000000000000e+00
.text
lea	ax,L10058
call	flds
lea	ax,*-54.(bp)
call	fstdp
.data
L10059:	.float 2.00000000000000000e+00
.text
.data
L10060:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
call	fdup
lea	ax,L10060
call	fsubs
lea	ax,*-54.(bp)
call	fstdp
lea	ax,L10059
call	fmuls
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10061:	.float 2.00000000000000000e+00
.text
.data
L10062:	.float 1.00000000000000000e+00
.text
lea	ax,*-66.(bp)
call	flds
call	fdup
lea	ax,L10062
call	fadds
lea	ax,*-66.(bp)
call	fstsp
lea	ax,L10061
call	fmuls
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-66.(bp)
call	fadds
lea	ax,*-62.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
.data
L10063:	.float 1.00000000000000000e+00
.text
lea	ax,*-54.(bp)
call	fldd
call	fdup
lea	ax,L10063
call	fadds
lea	ax,*-54.(bp)
call	fstdp
.data
L10064:	.float 1.00000000000000000e+00
.text
lea	ax,*-62.(bp)
call	fldd
call	fdup
lea	ax,L10064
call	fsubs
lea	ax,*-62.(bp)
call	fstdp
call	fadd
lea	ax,*-62.(bp)
call	fstdp
mov	di,*-72.(bp)
mov	ax,di
call	itof
lea	ax,*-62.(bp)
call	faddd
lea	ax,*-54.(bp)
call	faddd
call	ftoi
mov	*-72.(bp),ax
mov	di,*-72.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*68.
jmp	L2
.globl	fltused
.data
