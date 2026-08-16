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
.comm	_io_info,222
.globl	_cxaddr
.data
_cxaddr:_shutup
_locking
_nosys
_nosys
_nosys
_nosys
_nosys
_rdchk
_stkgrow
.globl	_xctab
.data
.data
_xctab:.byte	/73,/68,/75,/74,/75,/70,/0
.blkb	1.
.even
.byte	/6c,/6f,/63,/6b,/69,/6e,/67,/0
.even
.byte	/6e,/6f,/73,/79,/73,/0
.blkb	2.
.even
.byte	/6e,/6f,/73,/79,/73,/0
.blkb	2.
.even
.byte	/6e,/6f,/73,/79,/73,/0
.blkb	2.
.even
.byte	/6e,/6f,/73,/79,/73,/0
.blkb	2.
.even
.byte	/6e,/6f,/73,/79,/73,/0
.blkb	2.
.even
.byte	/72,/64,/63,/68,/6b,/0
.blkb	2.
.even
.byte	/73,/74,/6b,/67,/72,/6f,/77,/0
.even
.globl	_cspec
.text
.even
_cspec:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|NREG	2
|	_uap=di
|NREG	1
|	_cn=si
mov	di,122.+_u
mov	si,*8.(di)
mov	cx,*8.
shr	si,cl
and	si,#255.
cmp	si,*8.
jle	L13
movb	111.+_u,*22.
j	L14
L13:mov	bx,si
sal	bx,*1
call	@_cxaddr(bx)
L14:|NREG	3
|RTYP	0
br	cret
.globl	_shutup
.even
_shutup:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
sub	sp,*14.
|NREG	2
|	_mp=di
|NREG	1
|	_fp=si
|	_uap=-6.
|	_time=-10.
|	_type=-12.
|	_msg=-18.
mov	*-18.(bp),#L18
mov	*-16.(bp),#L19
mov	*-14.(bp),#L20
call	_suser
test	ax,ax
beq	L21
mov	dx,120.+_u
mov	_Hogproc,dx
mov	di,#_mount
j	L22
L20001:cmp	*2.(di),*0
je	L24
mov	bx,*2.(di)
mov	si,*14.(bx)
cmpb	#640.(si),*0
jne	L24
movb	#640.(si),*70.
movb	#612.(si),*1.
L24:add	di,*6.
L22:cmp	di,14.+_v
jb	L20001
mov	_updlock,*0.
call	_update
mov	bx,122.+_u
mov	*-6.(bp),bx
mov	dx,(bx)
test	dx,dx
je	L27
push	bx
mov	dx,*2.(bx)
not	dx
pop	bx
cmp	(bx),dx
jne	L27
mov	bx,2.+_mount
mov	si,*14.(bx)
mov	dx,#642.
push	dx
push	si
mov	bx,*-6.(bp)
push	(bx)
call	_copyin
add	sp,*6.
test	ax,ax
jne	L27
movb	#612.(si),*1.
call	_update
L27:mov	*-12.(bp),*0.
mov	bx,*-6.(bp)
mov	dx,(bx)
test	dx,dx
jne	L29
mov	dx,*2.(bx)
mov	*-12.(bp),dx
cmp	dx,*0
je	L29
cmp	dx,*1.
je	L29
cmp	dx,*2.
je	L29
mov	*-12.(bp),*0.
L29:cmp	_Stand,*0
jne	L31
mov	bx,*-12.(bp)
sal	bx,*1
lea	dx,*-18.(bp)
add	dx,bx
mov	bx,dx
push	(bx)
mov	dx,#L32
push	dx
call	_printf
add	sp,*4.
L31:sub	ax,ax
cwd
mov	*-8.(bp),ax
mov	*-10.(bp),dx
j	L33
L20003:jg	L35
cmp	ax,*-8.(bp)
jbe	L34
L35:add	*-8.(bp),*1.
adc	*-10.(bp),*0
L33:mov	ax,#3392.
mov	dx,*3.
cmp	dx,*-10.(bp)
jge	L20003
L34:call	_spl7
push	*-12.(bp)
call	_haltcpu
add	sp,*2.
L21:|NREG	3
|RTYP	0
br	cret
.globl	_stkgrow
.even
_stkgrow:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|NREG	2
|	_ss=di
|NREG	1
|	_uap=si
mov	si,122.+_u
mov	di,(si)
push	di
call	_grow
add	sp,*2.
sub	di,di
mov	dx,248.+_u
mov	cx,*11.
sal	dx,cl
sub	di,dx
cmpb	111.+_u,*0
jne	L39
mov	si,122.+_u
cmp	(si),di
jae	L39
movb	111.+_u,*12.
L39:cmpb	111.+_u,*0
jne	L40
mov	124.+_u,di
L40:|NREG	3
|RTYP	0
br	cret
.data
L18:.byte	/4e,/6f,/72,/6d,/61,/6c,/20,/53,/79
.byte	/73,/74,/65,/6d,/20
.byte	/53,/68,/75,/74,/64,/6f,/77,/6e,/0
L19:.byte	/53,/79,/73,/74,/65,/6d,/20,/52,/65
.byte	/62,/6f,/6f,/74,/0
L20:.byte	/53,/79,/73,/74,/65,/6d,/20,/50,/6f
.byte	/77,/65,/72,/20,/4f
.byte	/66,/66,/0
L32:.byte	/a,/2a,/2a,/20,/20,/25,/73,/20,/20
.byte	/2a,/2a,/a,/0
