| CORRECTED: L-10 - docs/RULES.md, "The corrected copy of libc.a". This
| is ../orig/stkmath.s with those corrections, each change marked "| L-nn".
|
| stkmath.s - RECONSTRUCTED source of libc.a's stkmath.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/stkmath.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/stkmath.s where one exists.
|
| The operations on the floating-point stack's top two (stacks.s), the
| arithmetic of dmath.s:
|   fadd fsub fmul fdiv  second op top, into the second; the top popped
|   fneg                 the top's sign flipped
|   fcmp                 second - top, both popped: the flags (SF, ZF;
|                        OF and CF 0) and AH (lahf, for c1's sahf)
|   ftest                the top popped: the flags of its value

.text
.comm	fpstk,64.
.comm	fpsp,2.
.comm	fac,8.
.globl	fadd
fadd:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
call	decfpsp
mov	di,si
sub	di,*8.
call	dadd
j	stkmathc
.globl	fsub
fsub:
push	bp
mov	bp,sp
push	di
push	si
mov	di,fpsp
call	decfpsp
mov	si,di
sub	si,*8.
call	dsub
| the result from fac into the new top
stkmathc:
mov	di,fpsp
lea	si,fac
movs
movs
movs
movs
jmp	cret
.globl	fmul
fmul:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
call	decfpsp
mov	di,si
sub	di,*8.
push	bp
call	dmul
pop	bp
j	stkmathc
.globl	fdiv
fdiv:
push	bp
mov	bp,sp
push	di
push	si
mov	di,fpsp
call	decfpsp
mov	si,di
sub	si,*8.
push	bp
call	ddiv
pop	bp
j	stkmathc
.globl	fneg
fneg:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
add	si,*6.
mov	al,(si)
xor	al,*/80
mov	(si),al
jmp	cret
.globl	fcmp
| the comparison: as the sign and exponent of second - top (L-10)
fcmp:
push	bp
mov	bp,sp
push	di
push	si
mov	di,fpsp
call	decfpsp
call	decfpsp
mov	si,di
sub	si,*8.
| L-10: compared without subtracting (no SIGFPE, no underflow): every
| zero (exponent byte 0) taken as +0; signs that differ decide; else the
| top words, then the words below from the top, unsigned, the order
| reversed for negative values. AX then as the difference's top word
| would be: 0 for equal, else a nonzero exponent byte and its sign.
mov	ax,6.(si)
or	ah,ah
jne	Lfc1
sub	ax,ax
Lfc1:
mov	bx,6.(di)
or	bh,bh
jne	Lfc2
sub	bx,bx
Lfc2:
mov	dx,ax
xor	dx,bx
test	dl,*/80
jne	Lfcsg
cmp	ax,bx
jne	Lfcne
or	ax,ax
je	fcmpcom
mov	dx,4.(si)
cmp	dx,4.(di)
jne	Lfcne
mov	dx,2.(si)
cmp	dx,2.(di)
jne	Lfcne
mov	dx,(si)
cmp	dx,(di)
jne	Lfcne
sub	ax,ax
j	fcmpcom
| the second larger in magnitude: its own sign
Lfcne:
ja	fcmpcom
| the top larger in magnitude, or the signs differ and the second is not
| negative: the top's sign flipped
Lfcb:
mov	ax,bx
xor	al,*/80
j	fcmpcom
| the signs differ: less if the second is negative
Lfcsg:
test	al,*/80
je	Lfcb
fcmpcom:
or	ah,ah
je	fcmpdone
or	al,*/7f
fcmpdone:
lahf
jmp	cret
.globl	ftest
ftest:
push	bp
mov	bp,sp
push	di
push	si
mov	di,fpsp
call	decfpsp
mov	ax,6.(di)
j	fcmpcom
.byte	0.
