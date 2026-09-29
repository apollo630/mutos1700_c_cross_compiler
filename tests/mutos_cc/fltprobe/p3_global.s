.comm	_gd,8
.comm	_gf,4
.bss
_sd:.blkb	8.
.globl	_gi
.data
_gi:	.double	2.50000000000000000e+00
.globl	_gfi
.data
_gfi:	.float	1.50000000000000000e+00
.comm	_ga,24
.comm	_gp,2
.comm	_gs,12
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
L2:.bss
L4:.blkb	8.
.text
| _ld=L4
| _arr=-20.
| _p=-22.
.data
L10002:	.float 1.50000000000000000e+00
.text
lea	ax,L10002
call	flds
lea	ax,_gd
call	fstdp
lea	ax,_gd
call	fldd
lea	ax,_gf
call	fstsp
lea	ax,_gf
call	flds
lea	ax,_sd
call	fstdp
lea	ax,_sd
call	fldd
lea	ax,L4
call	fstdp
lea	ax,_gfi
call	flds
lea	ax,L4
call	faddd
lea	ax,_xd
call	fstdp
mov	_gp,#_gd
lea	ax,_gd
call	fldd
lea	ax,*-12.(bp)
call	fstdp
lea	di,*-20.(bp)
mov	*-22.(bp),di
.data
L10003:	.float 2.00000000000000000e+00
.text
lea	ax,L10003
call	flds
mov	di,*-22.(bp)
lea	ax,(di)
call	fstdp
mov	di,_gp
lea	ax,(di)
call	fldd
lea	ax,_gs
call	fstdp
lea	ax,_gs
call	fldd
lea	ax,8.+_gs
call	fstsp
lea	ax,*-12.(bp)
call	fldd
lea	ax,_xd
call	faddd
lea	ax,16.+_ga
call	fstdp
lea	ax,8.+_gs
call	flds
lea	ax,_gi
call	faddd
lea	ax,16.+_ga
call	faddd
lea	ax,*-20.(bp)
call	faddd
lea	ax,_gd
call	faddd
call	ftoi
jmp	L3
L3:|RTYP 0
jmp	cret
L1:sub	sp,*18.
jmp	L2
.comm	_xd,8
.globl	fltused
.data
