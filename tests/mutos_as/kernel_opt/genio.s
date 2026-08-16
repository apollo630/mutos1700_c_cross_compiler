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
.comm	_ramflbl,2
.comm	_ramflpa,2
.globl	_gen
.data
_gen:.byte	/0
.blkb	5.
/ffff
.even
.byte	/64,/78,/6d,/66,/30,/0
/2c
.even
.byte	/64,/78,/6d,/66,/31,/0
/2d
.even
.byte	/64,/78,/6d,/66,/32,/0
/2e
.even
.byte	/64,/78,/6d,/66,/33,/0
/2f
.even
.byte	/64,/6d,/66,/30,/0
.blkb	1.
/3c
.even
.byte	/64,/6d,/66,/31,/0
.blkb	1.
/3d
.even
.byte	/64,/6d,/66,/32,/0
.blkb	1.
/3e
.even
.byte	/64,/6d,/66,/33,/0
.blkb	1.
/3f
.even
.byte	/78,/6d,/66,/30,/0
.blkb	1.
/4
.even
.byte	/78,/6d,/66,/31,/0
.blkb	1.
/5
.even
.byte	/78,/6d,/66,/32,/0
.blkb	1.
/6
.even
.byte	/78,/6d,/66,/33,/0
.blkb	1.
/7
.even
.byte	/64,/62,/6d,/66,/30,/0
/1c
.even
.byte	/64,/62,/6d,/66,/31,/0
/1d
.even
.byte	/64,/62,/6d,/66,/32,/0
/1e
.even
.byte	/64,/62,/6d,/66,/33,/0
/1f
.even
.byte	/6d,/66,/30,/0
.blkb	2.
/34
.even
.byte	/6d,/66,/31,/0
.blkb	2.
/35
.even
.byte	/6d,/66,/32,/0
.blkb	2.
/36
.even
.byte	/6d,/66,/33,/0
.blkb	2.
/37
.even
.byte	/64,/34,/6d,/33,/0
.blkb	1.
/17
.even
.byte	/68,/64,/0
.blkb	3.
/300
.even
.byte	/68,/64,/31,/0
.blkb	2.
/301
.even
.byte	/68,/64,/32,/0
.blkb	2.
/302
.even
.byte	/68,/64,/33,/0
.blkb	2.
/303
.even
.byte	/68,/64,/34,/0
.blkb	2.
/304
.even
.byte	/63,/66,/0
.blkb	3.
/308
.even
.byte	/63,/66,/31,/0
.blkb	2.
/309
.even
.byte	/63,/66,/32,/0
.blkb	2.
/30a
.even
.byte	/63,/66,/33,/0
.blkb	2.
/30b
.even
.byte	/63,/66,/34,/0
.blkb	2.
/30c
.even
.byte	/20,/20,/20,/20,/20,/0
/ffff
.even
.globl	_getint
.text
.even
_getint:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
sub	sp,*2.
|NREG	2
|	_i=di
|	_c=-6.
sub	di,di
call	_getchar
movb	*-6.(bp),ax
cmpb	ax,*10.
jne	L38
mov	si,*-1.
j	L20000
L38:cmpb	*-6.(bp),*48.
jl	L39
cmpb	*-6.(bp),*57.
jg	L39
mov	ax,di
mov	cx,*10.
imul	cx
mov	si,ax
movb	ax,*-6.(bp)
cbw
add	si,ax
mov	di,si
add	di,*-48.
call	_getchar
movb	*-6.(bp),ax
j	L38
L39:cmpb	*-6.(bp),*48.
jl	L10000
cmpb	*-6.(bp),*57.
jle	L40
L10000:cmpb	*-6.(bp),*10.
je	L40
L41:call	_getchar
cmp	ax,*10.
jne	L41
mov	si,*-2.
j	L20000
L40:mov	si,di
L20000:mov	ax,si
|RTYP	0
br	cret
.globl	_getlong
.even
_getlong:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
sub	sp,*6.
|	_i=-8.
|	_c=-10.
sub	ax,ax
cwd
mov	*-6.(bp),ax
mov	*-8.(bp),dx
call	_getchar
movb	*-10.(bp),ax
cmpb	ax,*10.
jne	L47
mov	si,*-1.
L20003:mov	di,*-1.
L20002:mov	ax,si
mov	dx,di
|RTYP	6
br	cret
L47:cmpb	*-10.(bp),*48.
jl	L48
cmpb	*-10.(bp),*57.
jg	L48
movb	ax,*-10.(bp)
cbw
add	ax,*-48.
cwd
push	ax
push	dx
mov	ax,*10.
cwd
push	ax
push	dx
push	*-6.(bp)
push	*-8.(bp)
call	lmul
add	sp,*8.
pop	bx
pop	cx
add	ax,cx
adc	dx,bx
mov	*-6.(bp),ax
mov	*-8.(bp),dx
call	_getchar
movb	*-10.(bp),ax
j	L47
L48:cmpb	*-10.(bp),*48.
jl	L10001
cmpb	*-10.(bp),*57.
jle	L49
L10001:cmpb	*-10.(bp),*10.
je	L49
L50:call	_getchar
cmp	ax,*10.
jne	L50
mov	si,*-2.
j	L20003
L49:mov	si,*-6.(bp)
mov	di,*-8.(bp)
j	L20002
.globl	_getname
.even
_getname:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_line=4.
sub	sp,*4.
|	_c=-6.
|	_i=-8.
mov	*-8.(bp),*0.
j	L55
L20005:mov	bx,*4.(bp)
add	bx,*-8.(bp)
movb	dx,*-6.(bp)
movb	(bx),dx
inc	*-8.(bp)
L55:call	_getchar
movb	*-6.(bp),ax
cmpb	ax,*10.
jne	L20005
mov	bx,*4.(bp)
add	bx,*-8.(bp)
movb	(bx),*0.
|RTYP	0
br	cret
.globl	_rdisk
.data
_rdisk:.byte	/72,/61,/6d,/78,/0
.even
.globl	_hdisk
.data
.data
_hdisk:.byte	/68,/64,/78,/0
.globl	_generic
.text
.even
_generic:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_p=4.
sub	sp,*46.
|	_line=-14.
|	_mino=-16.
|	_rminor=-18.
|	_sminor=-20.
|	_pminor=-22.
|	_nnswap=-24.
|	_ram=-26.
|	_smajor=-28.
|	_rmajor=-30.
|	_pmajor=-32.
|	_rindex=-34.
|	_sindex=-36.
|	_pindex=-38.
|	_i=-40.
|	_wert=-42.
|	_unit=-44.
|	_nswplo=-48.
|	_dd=-50.
mov	*-50.(bp),#_k170dev
mov	bx,*-50.(bp)
movb	*26.(bx),*3.
mov	bx,*-50.(bp)
movb	*27.(bx),*3.
cmp	_Genboot,*0
beq	L61
mov	di,_rootdev
mov	cx,*8.
sar	di,cl
jne	L63
mov	ax,_rootdev
subb	ah,ah
cwd
mov	cx,*32.
idiv	cx
test	dx,dx
je	L63
mov	di,#L64
push	di
call	_printf
add	sp,*2.
L63:mov	di,#L65
push	di
call	_printf
add	sp,*2.
lea	di,*-14.(bp)
push	di
call	_getname
add	sp,*2.
cmpb	*-14.(bp),*89.
je	L10002
cmpb	*-14.(bp),*121.
jne	L66
L10002:mov	bx,*-50.(bp)
movb	*28.(bx),*1.
mov	bx,*-50.(bp)
movb	*29.(bx),*1.
L66:mov	di,#L67
push	di
call	_printf
add	sp,*2.
mov	di,_rootdev
and	di,#255.
mov	*-18.(bp),di
mov	di,_swapdev
and	di,#255.
mov	*-20.(bp),di
mov	di,_pipedev
and	di,#255.
mov	*-22.(bp),di
mov	di,_swapdev
mov	cx,*8.
sar	di,cl
mov	*-28.(bp),di
mov	di,_rootdev
mov	cx,*8.
sar	di,cl
mov	*-30.(bp),di
mov	di,_pipedev
mov	cx,*8.
sar	di,cl
mov	*-32.(bp),di
push	_rootdev
call	_devtst1
add	sp,*2.
mov	*-34.(bp),ax
push	_swapdev
call	_devtst1
add	sp,*2.
mov	*-36.(bp),ax
push	_pipedev
call	_devtst1
add	sp,*2.
mov	*-38.(bp),ax
L68:mov	di,*-38.(bp)
mov	cx,*3.
sal	di,cl
add	di,#_gen
push	di
mov	di,*-36.(bp)
mov	cx,*3.
sal	di,cl
add	di,#_gen
push	di
mov	di,*-34.(bp)
mov	cx,*3.
sal	di,cl
add	di,#_gen
push	di
call	_print
add	sp,*6.
mov	di,#L70
push	di
call	_printf
add	sp,*2.
mov	di,#L71
push	di
call	_printf
add	sp,*2.
lea	di,*-14.(bp)
push	di
call	_getname
add	sp,*2.
cmpb	*-14.(bp),*89.
je	L72
cmpb	*-14.(bp),*121.
je	L72
mov	di,_swapdev
mov	cx,*8.
sar	di,cl
jne	L61
mov	ax,_swapdev
subb	ah,ah
cwd
mov	cx,*32.
idiv	cx
test	dx,dx
je	L61
mov	_Bigswap,*0.
L61:|RTYP	0
br	cret
L72:mov	di,#L74
push	di
call	_printf
add	sp,*2.
L75:mov	di,#L76
push	di
call	_printf
add	sp,*2.
lea	di,*-14.(bp)
push	di
call	_getname
add	sp,*2.
cmpb	*-14.(bp),*0
je	L77
lea	di,*-14.(bp)
push	di
call	_devtst2
add	sp,*2.
mov	*-34.(bp),ax
cmp	ax,*-1.
je	L75
mov	di,ax
mov	cx,*3.
sal	di,cl
mov	di,#6.+_gen(di)
mov	_rootdev,di
L77:mov	di,#L78
push	di
call	_printf
add	sp,*2.
lea	di,*-14.(bp)
push	di
call	_getname
add	sp,*2.
cmpb	*-14.(bp),*0
je	L79
lea	di,*-14.(bp)
push	di
call	_devtst2
add	sp,*2.
mov	*-36.(bp),ax
cmp	ax,*-1.
je	L77
mov	di,ax
mov	cx,*3.
sal	di,cl
mov	di,#6.+_gen(di)
mov	_swapdev,di
j	L79
L20007:cmp	si,*-2.
jne	L81
mov	di,#L82
push	di
call	_printf
add	sp,*2.
L79:mov	di,#L80
push	di
call	_printf
add	sp,*2.
call	_getlong
mov	si,ax
mov	*-46.(bp),si
mov	*-48.(bp),dx
cmp	dx,*-1.
je	L20007
L81:cmp	*-48.(bp),*0
jl	L84
jg	L10003
cmp	*-46.(bp),*0.
jb	L84
L10003:mov	si,*-46.(bp)
mov	di,*-48.(bp)
mov	2.+_swplo,si
mov	_swplo,di
j	L84
L20009:mov	di,#L87
push	di
call	_printf
add	sp,*2.
L84:mov	di,#L85
push	di
call	_printf
add	sp,*2.
call	_getint
mov	*-24.(bp),ax
cmp	ax,*-2.
je	L20009
cmp	ax,*0
jl	L89
mov	_nswap,ax
L89:mov	di,#L90
push	di
call	_printf
add	sp,*2.
lea	di,*-14.(bp)
push	di
call	_getname
add	sp,*2.
cmpb	*-14.(bp),*0
beq	L68
lea	di,*-14.(bp)
push	di
call	_devtst2
add	sp,*2.
mov	*-38.(bp),ax
cmp	ax,*-1.
je	L89
mov	di,ax
mov	cx,*3.
sal	di,cl
mov	di,#6.+_gen(di)
mov	_pipedev,di
br	L68
.globl	_print
.even
_print:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_r=4.
|	_s=6.
|	_p=8.
sub	sp,*2.
|	_i=-6.
push	*4.(bp)
mov	di,#L94
push	di
call	_printf
add	sp,*4.
push	*6.(bp)
mov	di,#L95
push	di
call	_printf
add	sp,*4.
push	2.+_swplo
push	_swplo
mov	di,#L96
push	di
call	_printf
add	sp,*6.
push	_nswap
mov	di,#L97
push	di
call	_printf
add	sp,*4.
push	*8.(bp)
mov	di,#L98
push	di
call	_printf
add	sp,*4.
|RTYP	0
br	cret
.globl	_devtst1
.even
_devtst1:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|NREG	2
mov	di,*4.(bp)
|	_dev=di
sub	sp,*2.
|	_i=-6.
mov	*-6.(bp),*0.
L102:cmp	*-6.(bp),*37.
jge	L101
mov	si,*-6.(bp)
mov	cx,*3.
sal	si,cl
cmp	6.+_gen(si),di
jne	L104
mov	ax,*-6.(bp)
L101:|RTYP	0
br	cret
L104:inc	*-6.(bp)
j	L102
.globl	_devtst2
.even
_devtst2:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_line=4.
sub	sp,*2.
|NREG	2
|	_s1=di
|NREG	1
|	_s2=si
|	_i=-6.
mov	*-6.(bp),*0.
L20013:mov	di,*4.(bp)
mov	si,*-6.(bp)
mov	cx,*3.
sal	si,cl
add	si,#_gen
j	L112
L20011:cmpb	(di),*0
je	L113
cmpb	(si),*0
je	L113
inc	di
inc	si
L112:movb	dx,(si)
cmpb	(di),dx
je	L20011
L113:cmpb	(di),*0
jne	L111
cmpb	(si),*0
jne	L111
mov	dx,*-6.(bp)
j	L20014
L111:inc	*-6.(bp)
cmp	*-6.(bp),*37.
jl	L20013
mov	dx,#L115
push	dx
call	_printf
add	sp,*2.
mov	dx,*-1.
L20014:mov	ax,dx
|RTYP	0
br	cret
.globl	_strcpy
.even
_strcpy:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|NREG	2
mov	di,*4.(bp)
|	_s1=di
|NREG	1
mov	si,*6.(bp)
|	_s2=si
sub	sp,*2.
|	_os1=-6.
mov	*-6.(bp),di
L119:mov	bx,di
inc	di
movb	dx,(si)
inc	si
movb	(bx),dx
testb	dx,dx
jne	L119
mov	ax,*-6.(bp)
|RTYP	9
br	cret
.data
L64:.byte	/a,/49,/66,/20,/6e,/65,/63,/65,/73
.byte	/73,/61,/72,/79,/2c
.byte	/20,/79,/6f,/75,/20,/63,/61,/6e,/20
.byte	/63,/68,/61,/6e,/67,/65
.byte	/20,/74,/68,/65,/20,/42,/6f,/6f,/74
.byte	/2d,/46,/6c,/6f,/70,/70
.byte	/79,/20,/61,/67,/61,/69,/6e,/73,/74
.byte	/20,/52,/6f,/6f,/74,/2d
.byte	/46,/6c,/6f,/70,/70,/79,/20,/6e,/6f
.byte	/77,/0
L65:.byte	/a,/44,/6f,/20,/79,/6f,/75,/20,/68
.byte	/61,/76,/65,/20,/61
.byte	/6e,/20,/38,/22,/20,/73,/65,/70,/61
.byte	/72,/61,/74,/65,/20,/66
.byte	/6c,/6f,/70,/70,/79,/20,/64,/65,/76
.byte	/69,/63,/65,/20,/3f,/20
.byte	/5b,/79,/20,/6f,/72,/20,/43,/52,/5d
.byte	/3a,/20,/0
L67:.byte	/a,/0
L70:.byte	/a,/0
L71:.byte	/44,/6f,/20,/79,/6f,/75,/20,/77,/61
.byte	/6e,/74,/20,/63,/68
.byte	/61,/6e,/67,/65,/73,/20,/5b,/79,/2f
.byte	/6e,/5d,/3f,/3a,/20,/0
L74:.byte	/a,/0
L76:.byte	/72,/6f,/6f,/74,/64,/65,/76,/3a,/20
.byte	/0
L78:.byte	/73,/77,/61,/70,/64,/65,/76,/3a,/20
.byte	/0
L80:.byte	/20,/20,/20,/20,/20,/20,/20,/20,/42
.byte	/6c,/6f,/63,/6b,/20
.byte	/6e,/75,/6d,/62,/65,/72,/20,/6f,/66
.byte	/20,/73,/77,/61,/70,/20
.byte	/73,/70,/61,/63,/65,/3a,/20,/0
L82:.byte	/42,/61,/64,/20,/6e,/75,/6d,/62,/65
.byte	/72,/a,/0
L85:.byte	/20,/20,/20,/20,/20,/20,/20,/20,/53
.byte	/69,/7a,/65,/20,/6f
.byte	/66,/20,/73,/77,/61,/70,/20,/73,/70
.byte	/61,/63,/65,/3a,/20,/0
L87:.byte	/42,/61,/64,/20,/6e,/75,/6d,/62,/65
.byte	/72,/a,/0
L90:.byte	/70,/69,/70,/65,/64,/65,/76,/3a,/20
.byte	/0
L94:.byte	/72,/6f,/6f,/74,/64,/65,/76,/3a,/20
.byte	/25,/73,/a,/0
L95:.byte	/73,/77,/61,/70,/64,/65,/76,/3a,/20
.byte	/25,/73,/a,/0
L96:.byte	/20,/20,/20,/20,/20,/20,/20,/20,/42
.byte	/6c,/6f,/63,/6b,/20
.byte	/6e,/75,/6d,/62,/65,/72,/20,/6f,/66
.byte	/20,/73,/77,/61,/70,/20
.byte	/73,/70,/61,/63,/65,/3a,/20,/25,/44
.byte	/a,/0
L97:.byte	/20,/20,/20,/20,/20,/20,/20,/20,/53
.byte	/69,/7a,/65,/20,/6f
.byte	/66,/20,/73,/77,/61,/70,/20,/73,/70
.byte	/61,/63,/65,/3a,/20,/25
.byte	/64,/a,/0
L98:.byte	/70,/69,/70,/65,/64,/65,/76,/3a,/20
.byte	/25,/73,/a,/0
L115:.byte	/42,/61,/64,/20,/64,/65,/76,/69,/63
.byte	/65,/a,/0
