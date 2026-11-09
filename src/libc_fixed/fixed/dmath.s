| CORRECTED: L-05, L-14 - docs/RULES.md, "The corrected copy of libc.a". This
| is ../orig/dmath.s with those corrections, each change marked "| L-nn".
|
| dmath.s - RECONSTRUCTED source of libc.a's dmath.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/dmath.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/dmath.s where one exists.
|
| The double arithmetic everything on the floating-point stack calls,
| on two operands in memory, [si] and [di], the result in fac (8 bytes,
| global data - the accumulator; __dac is the same, __fac its exponent
| byte):
|   dadd  [si] + [di]        dsub  [si] - [di]
|   dmul  [si] * [di]        ddiv  [si] / [di] (BP is used: callers save it)
| rounded to nearest, ties to even; a result below the smallest double is
| 0, above the largest __ovfl (SIGFPE), a division by 0 __div0 (SIGFPE).
| dnorm and dround are the normalization and rounding (also global),
| pmuld the 56 x 56 bit product of the mantissas (op1man and op2man hold
| the top bytes of the two). SI, DI and every other register are not kept.

.data
.globl	fac
fac:
__dac:
.byte	0.,0.,0.,0.,0.,0.,0.
__fac:
.byte	0.
op1man:
.byte	0.,0.
op2man:
.byte	0.,0.
.text
.globl	dadd
.globl	dsub
| [si] - [di]: [di]'s sign flipped in CX
dsub:
mov	cx,6.(di)
xor	cl,*/80
.globl	dmul
.globl	ddiv
.globl	pmuld
.globl	dnorm
.globl	dround
.globl	__ovfl
.globl	__div0
jmp	doadd
| an operand that does not count: the other one copied to fac
savdi:
xchg	cx,ax
mov	si,di
mov	di,#fac
movs
movs
movs
stow
ret
| [si] + [di]: the operand with the smaller exponent aligned
dadd:
mov	cx,6.(di)
doadd:
mov	ax,6.(si)
or	ah,ah
je	savdi
cmp	ch,ah
ja	signfidd
xchg	si,di
xchg	cx,ax
or	ah,ah
je	savdi
| AH = the difference of the exponent bytes
signfidd:
sub	ah,ch
neg	ah
cmp	ah,*/39		| L-14: dropped from 58 binary orders below; 57 is aligned and rounded
ja	savdi
shl	al,#1
rcr	cl,#1
xchg	cx,ax
mov	cl,ch
mov	ch,*0.
push	ax
push	di
mov	di,cx
lodb
mov	ah,al
xor	al,al
xchg	dx,ax
lodw
xchg	cx,ax
lodw
xchg	bx,ax
lodw
or	ah,*/80
or	di,di
je	aligned
chkaln:
cmp	di,#/e
jl	bytshft
or	dx,dx
je	nostik
or	cl,*1.
nostik:
mov	dx,cx
mov	cx,bx
mov	bx,ax
xor	ax,ax
sub	di,#/10
ja	chkaln
je	aligned
jb	shflef
bytshft:
cmp	di,#6.
jl	bitshft
or	dl,dl
je	nstik
or	dh,*1.
nstik:
mov	dl,dh
mov	dh,cl
mov	cl,ch
mov	ch,bl
mov	bl,bh
mov	bh,al
mov	al,ah
xor	ah,ah
sub	di,*8.
ja	bitshft
je	aligned
shflef:
shl	dx,#1
rcl	cx,#1
rcl	bx,#1
rcl	ax,#1
inc	di
jne	shflef
jmp	aligned
bitshft:
xchg	cx,di
test	dl,*/3f
je	shfrig
or	dl,*/20
shfrig:
shr	ax,#1
rcr	bx,#1
rcr	di,#1
rcr	dx,#1
loop	shfrig
xchg	cx,di
| the aligned mantissa in DI:BX:CX:DX, the other added or subtracted
aligned:
pop	si
xchg	di,ax
test	dl,*/3f
je	getsign
or	dl,*/20
getsign:
pop	ax
shl	al,#1
jo	subman
add	dh,(si)
adc	cx,1.(si)
adc	bx,3.(si)
pushf
mov	si,5.(si)
or	si,#/8000
popf
adc	di,si
jae	roundj
rcr	di,#1
rcr	bx,#1
rcr	cx,#1
rcr	dx,#1
inc	ah
roundj:
jne	round
call	__ovfl
subman:
not	al
sub	dh,(si)
sbb	cx,1.(si)
sbb	bx,3.(si)
pushf
mov	si,5.(si)
or	si,#/8000
popf
sbb	di,si
jae	norm
xor	si,si
not	al
not	di
not	bx
not	cx
neg	dx
cmc
adc	cx,si
adc	bx,si
adc	di,si
| normalize DI:BX:CX:DH (DL the guard byte), AH the exponent
dnorm:
norm:
mov	si,#4.
dnor:
or	di,di
jne	hibyt
sub	ah,*/10
jbe	nrzero
dec	si
je	nrzero
mov	di,bx
mov	bx,cx
mov	cx,dx
xor	dx,dx
jmp	dnor
nrzero:
jmp	zero
hibyt:
test	di,#/ff00
jne	normshf
sub	ah,*8.
jbe	zero
xchg	di,ax
mov	ah,al
mov	al,bh
mov	bh,bl
mov	bl,ch
mov	ch,cl
mov	cl,dh
mov	dh,dl
mov	dl,*0.
or	ah,ah
xchg	di,ax
normshf:
js	round
normlp:
dec	ah
je	zero
shl	dx,#1
rcl	cx,#1
rcl	bx,#1
rcl	di,#1
jno	normlp
| round to nearest, ties to even, on DL; store into fac
dround:
round:
cmp	dl,*/80
ja	roundup
jb	save
test	dh,*1.
je	save
roundup:
add	dh,*1.
adc	cx,*0.
adc	bx,*0.
adc	di,*0.
jae	save
inc	ah
je	overflow
save:
and	al,*/80
xchg	bx,di
and	bh,*/7f
or	al,bh
mov	fac+6.,ax
mov	fac+5.,bl
mov	bx,di
mov	di,#fac
mov	al,dh
stob
xchg	cx,ax
stow
xchg	bx,ax
stow
ret
ovchk:
jns	zero
overflow:
call	__ovfl
| a zero result: eight zero bytes
zero:
lea	di,fac
sub	ax,ax
stow
stow
stow
stow
ret
| [si] * [di]: exponents added, the mantissas multiplied by pmuld
dmul:
mov	ax,6.(si)
or	ah,ah
je	zero
mov	cx,6.(di)
or	ch,ch
je	zero
xor	al,cl
sub	ah,*/81
sub	ch,*/80
add	ah,ch
ovchkj:
jo	ovchk
add	ah,*/80
push	ax
call	pmuld
je	normchk
or	dl,*1.
normchk:
pop	ax
or	di,di
js	incexp
shl	dx,#1
rcl	cx,#1
rcl	bx,#1
rcl	di,#1
or	ah,ah
jne	rndjmp
jmp	zero
incexp:
inc	ah
je	overflow
rndjmp:
jmp	round
| [si] / [di]: four 16-bit quotient digits (div16)
ddiv:
mov	ax,6.(si)
mov	cx,6.(di)
xor	al,cl
or	ch,ch
je	div0
or	ah,ah
je	zero
sub	ah,*/80
sub	ch,*/80
sub	ah,ch
jo	ovchkj
add	ah,*/80
push	ax
lodb
mov	ch,al
xor	cl,cl
lodw
xchg	bx,ax
lodw
xchg	dx,ax
lodw
or	ah,*/80
xchg	dx,ax
mov	si,di
add	si,*5.
mov	di,#fac+6.
std
movs
movs
movs
inc	si
inc	di
movsb
cld
movb	(di),*0.
or	__fac,#/80
shr	dx,#1
rcr	ax,#1
rcr	bx,#1
rcr	cx,#1
call	div16
push	di
call	div16
push	di
call	div16
push	di
call	div16
or	ax,bx
or	ax,cx
or	ax,dx
mov	dx,di
je	nstk1
or	dl,*1.
nstk1:
pop	cx
pop	bx
pop	di
jmp	normchk
div0:
call	__div0
div16:
mov	si,fac+6.
xor	di,di
cmp	dx,si
jae	maxquo
or	dx,dx
jne	dodiv
cmp	si,ax
ja	zerquo
dodiv:
div	si
push	dx
xchg	di,ax
xor	bp,bp
mov	si,bp
mov	ax,fac
or	ax,ax
je	rem2
mul	di
mov	si,dx
rem2:
push	ax
mov	ax,fac+2.
or	ax,ax
je	rem3
mul	di
add	si,ax
adc	bp,dx
rem3:
mov	ax,fac+4.
or	ax,ax
je	rem4
mul	di
add	bp,ax
adc	dx,*0.
xchg	dx,ax
rem4:
pop	dx
neg	dx
sbb	cx,si
sbb	bx,bp
pop	bp
sbb	bp,ax
xchg	bp,ax
zerquo:
xchg	dx,ax
xchg	cx,ax
xchg	bx,ax
jae	ret
restore:
dec	di
add	cx,fac
adc	bx,fac+2.
adc	ax,fac+4.
adc	dx,fac+6.
jae	restore
ret:
ret
maxquo:
dec	di
sub	cx,fac
sbb	bx,fac+2.
sbb	ax,fac+4.
add	cx,fac+2.
adc	bx,fac+4.
adc	ax,dx
mov	dx,fac
cmc
jmp	zerquo
| the product of the two 56-bit mantissas, sixteen partial products
pmuld:
mov	al,6.(si)
mov	ah,*0.
or	al,*/80
mov	op1man,ax
mov	al,6.(di)
mov	ah,*0.
or	al,*/80
mov	op2man,ax
xor	bx,bx
mov	bp,bx
mov	cx,(si)
jcxz	prod3
xchg	cx,ax
mov	cx,(di)
jcxz	prod2
mul	cx
mov	bp,ax
mov	cx,dx
mov	ax,(si)
prod2:
mov	dx,2.(di)
or	dx,dx
je	prod3
mul	dx
add	cx,ax
adc	bx,dx
prod3:
push	bp
xor	bp,bp
mov	ax,2.(si)
or	ax,ax
je	prod4
mov	dx,(di)
or	dx,dx
je	prod4
mul	dx
add	cx,ax
adc	bx,dx
rcl	bp,#1
prod4:
pop	ax
or	ax,cx
push	ax
mov	ax,2.(si)
or	ax,ax
je	prod5
mov	dx,2.(di)
or	dx,dx
je	prod5
mul	dx
add	bx,ax
adc	bp,dx
prod5:
xor	cx,cx
mov	ax,(si)
or	ax,ax
je	prod6
mov	dx,4.(di)		| L-05: b2's word, for the a0*b2 product
or	dx,dx
je	prod6
mul	dx
add	bx,ax
adc	bp,dx
rcl	cx,#1
prod6:
mov	ax,4.(si)
or	ax,ax
je	prod7
mov	dx,(di)
or	dx,dx
je	prod7
mul	dx
add	bx,ax
adc	bp,dx
adc	cx,*0.
prod7:
pop	ax
or	ax,bx
push	ax
mov	ax,(si)
or	ax,ax
je	prod8
mul	op2man
add	bp,ax
adc	cx,dx
prod8:
mov	ax,(di)
or	ax,ax
je	prod9
mul	op1man
add	bp,ax
adc	cx,dx
prod9:
xor	bx,bx
mov	ax,2.(si)
or	ax,ax
je	prod10
mov	dx,4.(di)
or	dx,dx
je	prod10
mul	dx
add	bp,ax
adc	cx,dx
rcl	bx,#1
prod10:
mov	ax,4.(si)
or	ax,ax
je	prod11
mov	dx,2.(di)
or	dx,dx
je	prod11
mul	dx
add	bp,ax
adc	cx,dx
adc	bx,*0.
prod11:
push	bp
xor	bp,bp
mov	ax,2.(di)
or	ax,ax
je	prod12
mul	op1man
add	cx,ax
adc	bx,dx
prod12:
mov	ax,2.(si)
or	ax,ax
je	prod13
mul	op2man
add	cx,ax
adc	bx,dx
prod13:
mov	ax,4.(si)
or	ax,ax
je	prod15
mov	dx,4.(di)
or	dx,dx
je	prod14
mul	dx
add	cx,ax
adc	bx,dx
rcl	bp,#1
mov	ax,4.(si)
prod14:
mul	op2man
add	bx,ax
adc	bp,dx
prod15:
mov	ax,4.(di)
or	ax,ax
je	prod16
mul	op1man
add	bx,ax
adc	bp,dx
prod16:
mov	al,op1man
mulb	op2man
add	ax,bp
xchg	di,ax
pop	dx
pop	si
or	si,si
ret
