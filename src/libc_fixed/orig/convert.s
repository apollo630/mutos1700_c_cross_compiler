| convert.s - RECONSTRUCTED source of libc.a's convert.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/convert.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/convert.s where one exists.
|
| The conversions between int and the floating-point stack (stacks.s):
|   itof, itod  the int in AX as a new top of the stack (one routine);
|   ftoi, dtoi  the top, popped, truncated toward 0, in AX; from 2**16
|               in magnitude SIGFPE (__ovfl).
| A slot of the stack is a double: eight bytes, the words little-endian,
| byte 7 the exponent (0: zero; value 0.1mmm... * 2**(byte 7 - 128)),
| bit 7 of byte 6 the sign, the other 55 bits the mantissa below its
| hidden leading 1.

.text
.comm	fpsp,2.
.comm	fac,8.
.globl	itod
| int -> double: the magnitude normalized in BX, exponent byte /90 (16)
itod:
.globl	itof
itof:
push	bp
mov	bp,sp
push	di
push	si
mov	bx,ax
mov	ax,#/9000
or	al,bh
jns	pos
neg	bx
pos:
or	bx,bx
je	zero
hibyt:
or	bh,bh
jne	normshf
sub	ah,*8.
jbe	zero
mov	bh,bl
mov	bl,*0.
or	bh,bh
normshf:
js	save
normlp:
dec	ah
je	zero
shl	bx,#1
jns	normlp
| sign | top 7 mantissa bits into byte 6, the exponent into byte 7
save:
and	al,*/80
and	bh,*/7f
or	al,bh
call	incfpsp
mov	di,fpsp
mov	6.(di),ax
mov	ah,bl
sub	al,al
mov	4.(di),ax
j	zextend
| 0: eight zero bytes
zero:
call	incfpsp
mov	di,fpsp
sub	ax,ax
stow
stow
zextend:
stow		| L-01: AX = BL<<8 here (byte 5): bytes 0..3 become 0,BL,0,BL
stow
call	cret
.globl	dtoi
| double -> int, truncated toward 0
dtoi:
.globl	ftoi
ftoi:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
call	decfpsp
mov	ax,6.(si)
mov	cx,4.(si)
xor	bx,bx
or	ah,ah
je	cxret
sub	ah,*/80
jbe	cxret
mov	bh,al
mov	bl,ch
xchg	cx,ax
mov	cl,*/10
sub	cl,ch
mov	ah,bh
jb	overflow
je	ovchk
or	bh,*/80
shr	bx,cl
or	ah,ah
jns	notneg
neg	bx
notneg:
not	ah
and	ah,bh
js	overflow
cxret:
mov	ax,bx
call	cret
| 2**15 <= |x| < 2**16: only -32768 fits
ovchk:
cmp	bx,/8000		| L-03: the word at address 8000 - an immediate 8000 was meant
jne	overflow
test	al,*/80
je	cxret
| out of int's range: SIGFPE
overflow:
call	__ovfl
