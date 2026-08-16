.globl	_syopen
.text
.even
_syopen:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_dev=4.
|	_flag=6.
cmp	336.+_u,*0
jne	L4
movb	111.+_u,*6.
j	L3
L4:push	*6.(bp)
push	338.+_u
mov	ax,338.+_u
mov	cx,*8.
sar	ax,cl
mov	cx,*14.
imul	cx
mov	di,ax
call	@_cdevsw(di)
add	sp,*4.
L3:|RTYP	0
br	cret
.globl	_syread
.even
_syread:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_dev=4.
push	338.+_u
mov	ax,338.+_u
mov	cx,*8.
sar	ax,cl
mov	cx,*14.
imul	cx
mov	di,ax
call	@4.+_cdevsw(di)
add	sp,*2.
|RTYP	0
br	cret
.globl	_sywrite
.even
_sywrite:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_dev=4.
push	338.+_u
mov	ax,338.+_u
mov	cx,*8.
sar	ax,cl
mov	cx,*14.
imul	cx
mov	di,ax
call	@6.+_cdevsw(di)
add	sp,*2.
|RTYP	0
br	cret
.globl	_syioctl
.even
_syioctl:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_dev=4.
|	_cmd=6.
|	_addr=8.
|	_flag=10.
push	*10.(bp)
push	*8.(bp)
push	*6.(bp)
push	338.+_u
mov	ax,338.+_u
mov	cx,*8.
sar	ax,cl
mov	cx,*14.
imul	cx
mov	di,ax
call	@8.+_cdevsw(di)
add	sp,*8.
|RTYP	0
br	cret
.data
