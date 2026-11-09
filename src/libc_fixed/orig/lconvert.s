| lconvert.s - RECONSTRUCTED source of libc.a's lconvert.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/lconvert.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/lconvert.s where one exists.
|
| The conversions between long and the floating-point stack (stacks.s):
|   ftol  the top, popped, truncated toward 0, in DX:AX (also what
|         compiled code calls for "(unsigned)d"); out of range SIGFPE;
|   ltof  the long on the stack (4(bp) its high word, 6(bp) its low) as a
|         new top of the stack.

.text
.comm	fac,8.
.globl	ftol
| double -> long: the 32 top mantissa bits shifted right by 31 - e
ftol:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
call	decfpsp
mov	ax,6.(si)
or	ah,ah
je	ftol0
mov	cl,ah
sub	ch,ch
sub	cl,*/80
ja	ftol1
ftol0:
sub	dx,dx
sub	ax,ax
ftolret:
jmp	cret
ftol1:
mov	dx,5.(si)
mov	ax,3.(si)
mov	bh,dh
or	dh,*/80
shr	dx,#1
rcr	ax,#1
ftol2:
ftol3:
neg	cx
add	cx,*/1f
jle	ftolov		| L-11: a count of 0 (2**30 <= |x| < 2**31) taken for an overflow
ftol4:
sar	dx,#1
rcr	ax,#1
loop	ftol4
and	bh,*/80
jns	ftol5
neg	dx
neg	ax
sbb	dx,*0.
ftol5:
jmp	cret
| out of long's range: SIGFPE
ftolov:
call	__ovfl
j	ftol5
.globl	ltof
| long -> double: the magnitude normalized in DX:AX, 32 steps at most
ltof:
push	bp
mov	bp,sp
push	di
push	si
call	incfpsp
mov	di,fpsp
mov	dx,4.(bp)
mov	ax,6.(bp)
or	ax,ax
jne	ltof1
or	dx,dx
jne	ltof1
ltof0:
mov	6.(di),ax		| L-02: for 0, only bytes 6 and 7 of the slot are written
jmp	cret
ltof1:
sub	bl,bl
and	dh,dh
jns	ltof2
neg	dx
neg	ax
sbb	dx,*0.
mov	bl,*/80
ltof2:
mov	cl,*/20
sub	ch,ch
ltof3:
test	dh,*/80
jne	ltof4
shl	ax,#1
rcl	dx,#1
loop	ltof3
j	ltof0
ltof4:
and	dh,*/7f
or	dh,bl
mov	5.(di),dx
mov	3.(di),ax
sub	ax,ax
mov	2.(di),al
mov	(di),ax
xor	cl,*/80
mov	7.(di),cl
jmp	cret
