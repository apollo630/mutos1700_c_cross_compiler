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
| the comparison: the sign and exponent of second - top
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
call	dsub		| L-10: compares by subtracting - overflow SIGFPE, underflow "equal"
mov	ax,fac+6.
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
