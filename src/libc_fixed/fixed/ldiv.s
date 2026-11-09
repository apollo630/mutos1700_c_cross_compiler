| CORRECTED: L-04 - docs/RULES.md, "The corrected copy of libc.a". This
| is ../orig/ldiv.s with those corrections, each change marked "| L-nn".
|
| ldiv.s - RECONSTRUCTED source of libc.a's ldiv.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/ldiv.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/ldiv.s where one exists.
|
| ldiv(long a, long b) - what compiled code calls for "l / m": aldiv's
| code (aldiv.s) on the dividend passed by value (6(bp) and 8(bp) its high
| and low word, 10(bp) and 12(bp) the divisor's); the quotient in DX:AX.
| The private convention of the long helpers (docs/MUTOS_C_ABI.md 1.8):
| BX is pushed before "mov bp,sp", so the first argument is at 6(bp); the
| result is left in DX:AX (but see L-08); BX, SI, DI and BP are kept.

.text
.globl	ldiv
| the short path's test, as aldiv's (L-04)
ldiv:
push	bp
push	bx
mov	bp,sp
push	di
push	si
mov	ax,12.(bp)
cwd
or	dx,dx
js	hardldiv		| L-04: only a divisor of 0 to 32767 takes the short path here
cmp	dx,10.(bp)
jne	hardldiv
push	dx
mov	cx,ax
mov	si,8.(bp)
mov	di,6.(bp)
or	di,di
jge	L2
neg	di
neg	si
sbb	di,*0.
not	-6.(bp)
L2:
mov	ax,di
sub	dx,dx
div	cx
mov	di,ax
mov	ax,si
div	cx
mov	si,ax
leave:
cmp	-6.(bp),*0.
jns	L3
neg	di
neg	si
sbb	di,*0.
L3:
mov	ax,si
mov	dx,di
lea	sp,-4.(bp)
pop	si
pop	di
pop	bx
pop	bp
ret
| the long way, as aldiv's (L-04)
hardldiv:
mov	ax,#0.
push	ax
mov	si,8.(bp)
mov	di,6.(bp)
or	di,di
jns	L4
not	-6.(bp)
neg	di
neg	si
sbb	di,*0.
L4:
sub	ax,ax
mov	dx,10.(bp)
or	dx,dx
jns	L5
neg	dx
neg	12.(bp)
sbb	dx,*0.
not	-6.(bp)
L5:
or	dx,dx		| L-04: below 2**16 in magnitude: the two divs
jne	Lsteps
mov	cx,12.(bp)
jmp	L2
| L-04: the sixteen steps, for a divisor from 2**16 in magnitude - the
| quotient is below 2**16
Lsteps:
mov	cx,#/10
loopa:
shl	si,#1
rcl	di,#1
rcl	ax,#1
cmp	dx,ax		| L-04: compared unsigned
ja	L6
jb	L7
cmp	12.(bp),di
jbe	L7
L6:
loop	loopa
jmp	L8
L7:
sub	di,12.(bp)
sbb	ax,dx
inc	si
loop	loopa
L8:
sub	di,di		| L-04: the quotient is below 2**16 here
jmp	leave
.byte	0.
