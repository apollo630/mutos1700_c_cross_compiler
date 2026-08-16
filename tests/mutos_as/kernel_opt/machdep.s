.comm	_canonb,256
.comm	_rootdir,2
.comm	_runq,2
.comm	_Hogproc,2
.comm	_inode86,2
.comm	_dbreak,2
.comm	_brkseg,2
.comm	_brkoff,2
.comm	_cputype,2
.comm	_lbolt,2
.comm	_time,4
.comm	_nblkdev,2
.comm	_nchrdev,2
.comm	_mpid,2
.comm	_runin,2
.comm	_runout,2
.comm	_runrun,2
.comm	_runtxt,2
.comm	_curpri,2
.comm	_maxmem,2
.comm	_lks,2
.comm	_swplo,4
.comm	_nswap,2
.comm	_updlock,2
.comm	_rablock,4
.comm	_msgbuf,1024
.comm	_rootdev,2
.comm	_swapdev,2
.comm	_pipedev,2
.comm	_acctp,2
.comm	_Nproca,2
.comm	_Nproc,2
.comm	_Timezon,2
.comm	_Dstflag,2
.comm	_dk_busy,2
.comm	_dk_time,128
.comm	_dk_numb,12
.comm	_dk_wds,12
.comm	_tk_nin,4
.comm	_tk_nout,4
.comm	_version,2
.comm	_systype,2
.comm	_mpxip,2
.comm	_io_info,222
.comm	_clknumb,2
.globl	_startup
.text
.even
_startup:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_p=4.
|NREG	2
|	_i=di
|NREG	1
|	_c=si
mov	_clknumb,#-4096.
mov	di,*4.(bp)
j	L4
L20001:push	di
call	_memchk
add	sp,*2.
mov	si,ax
cmp	si,*0
jl	L5
inc	_maxmem
inc	di
L4:cmp	di,#511.
jbe	L20001
L5:mov	_mm_size,di
j	L6
L20003:push	di
mov	dx,*1.
push	dx
mov	dx,#_coremap
push	dx
call	_mfree
add	sp,*6.
L6:dec	di
cmp	di,*4.(bp)
jae	L20003
call	_meminit
call	_dinit
|NREG	3
|RTYP	0
br	cret
.globl	_sendsig
.even
_sendsig:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_p=4.
sub	sp,*2.
|NREG	2
|	_a=di
|NREG	1
|	_b=si
|	_c=-6.
mov	bx,322.+_u
mov	di,*16.(bx)
add	di,*-6.
push	di
call	_grow
add	sp,*2.
mov	bx,322.+_u
mov	dx,*16.(bx)
mov	*-6.(bp),dx
sub	si,si
L13:mov	dx,di
add	dx,si
add	dx,*6.
push	dx
call	_fuword
add	sp,*2.
push	ax
mov	dx,di
add	dx,si
push	dx
call	_suword
add	sp,*4.
add	si,*2.
cmp	si,*14.
jl	L13
mov	bx,322.+_u
mov	*16.(bx),di
push	*4.(bp)
mov	dx,di
add	dx,*8.
push	dx
call	_suword
add	sp,*4.
|NREG	3
|RTYP	0
br	cret
.globl	_addupc
.even
_addupc:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_p=4.
|	_r=6.
|	_q=8.
sub	sp,*6.
|	_c=-8.
|	_d=-10.
cmp	*8.(bp),*0
je	L16
mov	di,328.+_u
sub	*4.(bp),di
mov	si,330.+_u
shr	si,*1
sub	di,di
push	si
push	di
mov	si,*4.(bp)
shr	si,*1
sub	di,di
push	si
push	di
call	lmul
add	sp,*8.
mov	di,dx
mov	si,ax
mov	*-6.(bp),si
mov	*-8.(bp),di
mov	cx,*14.
sar	di,*1
rcr	si,*1
loop	.-4
mov	*-10.(bp),si
inc	si
and	si,*-2.
mov	*-10.(bp),si
mov	di,326.+_u
cmp	si,di
ja	L16
add	si,324.+_u
push	si
call	_fuword
add	sp,*2.
add	ax,*8.(bp)
push	ax
mov	di,*-10.(bp)
add	di,324.+_u
push	di
call	_suword
add	sp,*4.
L16:|RTYP	0
br	cret
.globl	_meminit
.even
_meminit:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|RTYP	0
br	cret
.globl	_dinit
.even
_dinit:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|NREG	2
|	_i=di
sub	di,di
j	L23
L20005:mov	si,di
sal	si,*1
call	@_dinitsw(si)
inc	di
L23:mov	si,di
sal	si,*1
mov	si,#_dinitsw(si)
test	si,si
jne	L20005
|NREG	3
|RTYP	0
br	cret
.globl	_ucode
.data
_ucode:/7eeb
/13eb
/eeb
/ceb
/0
/0
/0
/0
/0
/0
/3fb8
/ba08
/8800
/20cd
/cb
.globl	_szucode
.data
.data
_szucode:/1e
.globl	_ucodech
.text
.even
_ucodech:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_p=4.
|NREG	2
|	_a=di
cmp	*4.(bp),*0
je	L10000
sub	si,si
push	si
call	_fuiword
j	L20006
L10000:sub	ax,ax
push	ax
call	_fuword
L20006:add	sp,*2.
mov	di,ax
cmp	di,_ucode
jne	L27
cmp	*4.(bp),*0
je	L10002
mov	si,*8.
push	si
call	_fuiword
j	L20007
L10002:mov	ax,*8.
push	ax
call	_fuword
L20007:add	sp,*2.
mov	di,ax
cmp	di,#-277.
jne	L27
cmp	*4.(bp),*0
je	L28
push	_szucode
sub	si,si
push	si
mov	si,#_ucode
push	si
call	_copyiou
j	L20008
L28:push	_szucode
sub	si,si
push	si
mov	si,#_ucode
push	si
call	_copyout
L20008:add	sp,*6.
|NREG	3
L27:|RTYP	0
br	cret
.data
