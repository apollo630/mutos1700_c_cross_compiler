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
| _v=-28.
| _w=-36.
| _y=-44.
| _p=-46.
| _i=-48.
| _x=-50.
| _r=-52.
mov	*-52.(bp),*0.
mov	*-48.(bp),*2.
mov	*-50.(bp),*1.
lea	di,*-36.(bp)
mov	*-46.(bp),di
.data
L10000:	.float 1.50000000000000000e+00
.text
lea	ax,L10000
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10001:	.float 5.00000000000000000e-01
.text
lea	ax,L10001
call	flds
lea	ax,*-20.(bp)
call	fstdp
.data
L10002:	.float 3.00000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,*-28.(bp)
call	fstdp
.data
L10003:	.float 4.00000000000000000e+00
.text
lea	ax,L10003
call	flds
lea	ax,*-36.(bp)
call	fstdp
.data
L10004:	.float 1.00000000000000000e+01
.text
lea	ax,L10004
call	flds
lea	ax,*-44.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10005:	.float 2.00000000000000000e+00
.text
lea	ax,L10005
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10006
mov	di,*0.
jmp	L10007
L10006:mov	di,*1.
L10007:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10008:	.float 1.50000000000000000e+00
.text
lea	ax,L10008
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-20.(bp)
call	fsubd
lea	ax,*-12.(bp)
call	fstdp
.data
L10009:	.float 0.00000000000000000e+00
.text
lea	ax,L10009
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bgt	L10010
mov	di,*0.
jmp	L10011
L10010:mov	di,*1.
L10011:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10012:	.float 1.50000000000000000e+00
.text
lea	ax,L10012
call	flds
lea	ax,*-12.(bp)
call	fstdp
mov	di,*-46.(bp)
lea	ax,(di)
call	fldd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10013:	.float 2.00000000000000000e+00
.text
lea	ax,L10013
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10014
mov	di,*0.
jmp	L10015
L10014:mov	di,*1.
L10015:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10016:	.float 1.50000000000000000e+00
.text
lea	ax,L10016
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-28.(bp)
call	fldd
lea	ax,*-44.(bp)
call	faddd
lea	ax,*-28.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-28.(bp)
call	fldd
call	fcmp
sahf
bgt	L10017
mov	di,*0.
jmp	L10018
L10017:mov	di,*1.
L10018:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-28.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10019:	.float 1.50000000000000000e+00
.text
lea	ax,L10019
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10022:	.float 1.00000000000000000e+00
.text
lea	ax,*-36.(bp)
call	fldd
lea	ax,L10022
call	fadds
lea	ax,*-36.(bp)
call	fstdp
.data
L10020:	.float 1.00000000000000000e+00
.text
lea	ax,L10020
call	flds
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10021:	.float 2.00000000000000000e+00
.text
lea	ax,L10021
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L10023
mov	di,*0.
jmp	L10024
L10023:mov	di,*1.
L10024:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10025:	.float 1.50000000000000000e+00
.text
lea	ax,L10025
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10026:	.float 4.00000000000000000e+00
.text
lea	ax,L10026
call	flds
lea	ax,*-36.(bp)
call	fstdp
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-44.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10027:	.float 2.00000000000000000e+00
.text
lea	ax,L10027
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L10028
mov	di,*3.
jmp	L10029
L10028:mov	di,*4.
L10029:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10030:	.float 1.50000000000000000e+00
.text
lea	ax,L10030
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-44.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10031:	.float 2.00000000000000000e+00
.text
lea	ax,L10031
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bge	L10033
cmp	*-50.(bp),*0
bne	L10032
L10033:mov	di,*0.
jmp	L10034
L10032:mov	di,*1.
L10034:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10035:	.float 1.50000000000000000e+00
.text
lea	ax,L10035
call	flds
lea	ax,*-12.(bp)
call	fstdp
L4:lea	ax,*-36.(bp)
call	fldd
lea	ax,*-44.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10036:	.float 3.00000000000000000e+01
.text
lea	ax,L10036
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
ble	L5
inc	*-50.(bp)
jmp	L4
L5:mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10037:	.float 1.50000000000000000e+00
.text
lea	ax,L10037
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10038:	.float 0.00000000000000000e+00
.text
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-44.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
lea	ax,*-12.(bp)
call	fldd
call	fneg
lea	ax,L10038
call	flds
call	fcmp
sahf
blt	L10039
mov	di,*0.
jmp	L10040
L10039:mov	di,*1.
L10040:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10041:	.float 1.50000000000000000e+00
.text
lea	ax,L10041
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10042:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10042
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	fneg
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-52.(bp)
mov	ax,di
call	itof
.data
L10043:	.float 2.00000000000000000e+00
.text
lea	ax,L10043
call	flds
lea	ax,*-20.(bp)
call	fmuld
call	fadd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10044:	.float 1.50000000000000000e+00
.text
lea	ax,L10044
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-44.(bp)
call	fmuld
call	fsub
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	fneg
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-52.(bp)
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10045:	.float 1.50000000000000000e+00
.text
lea	ax,L10045
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10047:	.float 2.00000000000000000e+00
.text
.data
L10046:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10046
call	fadds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	fneg
lea	ax,L10047
call	fmuls
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-52.(bp)
mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	faddd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10048:	.float 1.50000000000000000e+00
.text
lea	ax,L10048
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10049:	.float 1.00000000000000000e+00
.text
lea	ax,*-12.(bp)
call	fldd
lea	ax,L10049
call	fsubs
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
call	fneg
lea	ax,*-36.(bp)
call	faddd
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-52.(bp)
mov	ax,di
call	itof
.data
L10050:	.float 2.00000000000000000e+00
.text
lea	ax,L10050
call	flds
lea	ax,*-20.(bp)
call	fmuld
call	fadd
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10051:	.float 1.50000000000000000e+00
.text
lea	ax,L10051
call	flds
lea	ax,*-12.(bp)
call	fstdp
mov	di,*-48.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstd
.data
L10052:	.float 2.00000000000000000e+00
.text
lea	ax,L10052
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10053
mov	di,*0.
jmp	L10054
L10053:mov	di,*1.
L10054:mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10055:	.float 1.50000000000000000e+00
.text
lea	ax,L10055
call	flds
lea	ax,*-12.(bp)
call	fstdp
lea	ax,*-36.(bp)
call	fldd
lea	ax,*-44.(bp)
call	fmuld
lea	ax,*-12.(bp)
call	faddd
lea	ax,*-12.(bp)
call	fstdp
.data
L10056:	.float 2.00000000000000000e+00
.text
lea	ax,L10056
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
blt	L10057
mov	di,*0.
jmp	L10058
L10057:mov	di,*1.
L10058:add	di,*-50.(bp)
mov	*-50.(bp),di
mov	di,*-52.(bp)
add	di,*-50.(bp)
mov	ax,di
call	itof
lea	ax,*-12.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
.data
L10059:	.float 1.50000000000000000e+00
.text
lea	ax,L10059
call	flds
lea	ax,*-12.(bp)
call	fstdp
.data
L10060:	.float 5.00000000000000000e-01
.text
lea	ax,L10060
call	flds
lea	ax,*-20.(bp)
call	fstdp
lea	ax,*-12.(bp)
call	fldd
.data
L10061:	.float 2.00000000000000000e+00
.text
lea	ax,L10061
call	flds
lea	ax,*-20.(bp)
call	fmuld
call	fsub
lea	ax,*-12.(bp)
call	fstd
.data
L10062:	.float 0.00000000000000000e+00
.text
lea	ax,L10062
call	flds
lea	ax,*-12.(bp)
call	fldd
call	fcmp
sahf
bgt	L10063
mov	di,*0.
jmp	L10064
L10063:mov	di,*1.
L10064:mov	ax,di
call	itof
lea	ax,*-20.(bp)
call	fstdp
mov	di,*-52.(bp)
mov	ax,di
call	itof
.data
L10065:	.float 2.00000000000000000e+00
.text
lea	ax,L10065
call	flds
lea	ax,*-12.(bp)
call	fmuld
call	fadd
lea	ax,*-20.(bp)
call	faddd
call	ftoi
mov	*-52.(bp),ax
mov	di,*-52.(bp)
mov	ax,di
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*48.
jmp	L2
.globl	fltused
.data
