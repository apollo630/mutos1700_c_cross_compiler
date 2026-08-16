.comm	_gc_buf,128
.globl	_gc_cnt
.data
_gc_cnt:/0
.globl	_gc_idx
.data
.data
_gc_idx:/0
.globl	_gc_flg
.data
.data
_gc_flg:/0
.globl	_gc_char
.data
.data
_gc_char:/0
.globl	_getchar
.text
.even
_getchar:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
sub	sp,*2.
|	_c=-6.
mov	di,_gc_cnt
cmp	_gc_idx,di
blt	L4
mov	_gc_cnt,*0.
br	L5
L10000:cmp	_gc_cnt,*0
ble	L5
mov	di,*8.
push	di
call	_putch
add	sp,*2.
mov	di,*32.
push	di
call	_putch
add	sp,*2.
mov	di,*8.
push	di
call	_putch
add	sp,*2.
dec	_gc_cnt
j	L5
L20001:cmp	*-6.(bp),*8.
je	L10000
cmp	*-6.(bp),*127.
je	L10000
cmp	*-6.(bp),*21.
jne	L10
L11:cmp	_gc_cnt,*0
jle	L5
mov	di,*8.
push	di
call	_putch
add	sp,*2.
mov	di,*32.
push	di
call	_putch
add	sp,*2.
mov	di,*8.
push	di
call	_putch
add	sp,*2.
dec	_gc_cnt
j	L11
L10:cmp	*-6.(bp),*32.
jb	L5
cmp	*-6.(bp),*126.
ja	L5
push	*-6.(bp)
call	_putch
add	sp,*2.
mov	bx,_gc_cnt
inc	_gc_cnt
mov	dx,*-6.(bp)
movb	_gc_buf(bx),dx
L5:call	_ci
mov	*-6.(bp),ax
cmp	ax,*13.
jne	L20001
mov	bx,_gc_cnt
inc	_gc_cnt
movb	_gc_buf(bx),*10.
mov	di,*10.
push	di
call	_putch
add	sp,*2.
mov	_gc_idx,*0.
mov	_gc_flg,*0.
L4:mov	di,_gc_idx
inc	_gc_idx
movb	ax,_gc_buf(di)
cbw
|RTYP	0
br	cret
.globl	_getflus
.even
_getflus:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
mov	_gc_cnt,*0.
|RTYP	0
br	cret
.comm	_msgbuf,1024
.globl	_msgbufp
.data
_msgbufp:_msgbuf
.globl	_putch
.text
.even
_putch:
push	bp
mov	bp,sp
push	di
push	si
|NREG	3
|	_c=4.
cmpb	*4.(bp),*0
je	L21
cmpb	*4.(bp),*13.
je	L21
movb	ax,*4.(bp)
cbw
cmp	ax,#177.
je	L21
mov	bx,_msgbufp
movb	dx,*4.(bp)
movb	(bx),dx
inc	_msgbufp
cmp	_msgbufp,#1024.+_msgbuf
jb	L21
mov	_msgbufp,#_msgbuf
L21:cmpb	*4.(bp),*10.
jne	L23
mov	di,*13.
push	di
call	_co
add	sp,*2.
L23:movb	ax,*4.(bp)
cbw
push	ax
call	_co
add	sp,*2.
movb	ax,*4.(bp)
cbw
|RTYP	0
br	cret
.data
