| alrem.s - RECONSTRUCTED source of libc.a's alrem.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/alrem.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/alrem.s where one exists.
|
| alrem(long *p, long b): *p %= b - what compiled code calls for "l %= m"
| (6(bp) the pointer, 8(bp) and 10(bp) the divisor's high and low word);
| the remainder, with the dividend's sign, is stored through the pointer.
| The private convention of the long helpers (docs/MUTOS_C_ABI.md 1.8):
| BX is pushed before "mov bp,sp", so the first argument is at 6(bp); the
| result is left in DX:AX (but see L-08); BX, SI, DI and BP are kept.
|
| A divisor of one word (-32768..32767) takes two divs by its magnitude;
| any other the sixteen steps of hardlrem, which keep only the remainder.

.text
.globl	alrem
| the short path's test: CWD of the low word (made positive)
alrem:
push	bp
push	bx
mov	bp,sp
push	di
push	si
mov	bx,6.(bp)
mov	ax,10.(bp)
cwd
or	dx,dx
jns	L1
neg	ax
L1:
cmp	dx,8.(bp)
jne	hardlrem
mov	cx,ax
mov	si,2.(bx)
mov	ax,(bx)
mov	di,ax
or	ax,ax
jns	L2
neg	ax
neg	si
sbb	ax,#0.
L2:
sub	dx,dx
div	cx
mov	ax,si
div	cx
or	di,di
jns	L3
neg	dx
L3:
mov	ax,dx
cwd
mov	2.(bx),ax
mov	(bx),dx
lea	sp,-4.(bp)
pop	si
pop	di
pop	bx
pop	bp
ret
| the long way: |dividend| shifted through AX:DI:SI, sixteen times
hardlrem:
mov	si,2.(bx)
mov	di,(bx)
or	di,di
jns	L4
neg	di
neg	si
sbb	di,*0.
L4:
sub	ax,ax
mov	dx,8.(bp)
or	dx,dx
jns	L5
neg	dx
neg	10.(bp)
sbb	dx,*0.
L5:
mov	cx,#/10
loopa:
shl	si,#1
rcl	di,#1
rcl	ax,#1
cmp	dx,ax		| L-04: compared signed: the high word of -2**31 (0x8000) is "negative"
jl	L7
jg	L6
cmp	10.(bp),di
jbe	L7
L6:
loop	loopa
jmp	L8
L7:
sub	di,10.(bp)
sbb	ax,#0.
sub	ax,dx
loop	loopa
L8:
test	(bx),#/ffff
jns	L9
neg	ax
neg	di
sbb	ax,#0.
L9:
mov	2.(bx),di		| L-08: the remainder is stored, but DX:AX is not it
mov	(bx),ax
lea	sp,-4.(bp)
pop	si
pop	di
pop	bx
pop	bp
ret
