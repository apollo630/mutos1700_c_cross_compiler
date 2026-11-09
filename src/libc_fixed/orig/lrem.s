| lrem.s - RECONSTRUCTED source of libc.a's lrem.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/lrem.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/lrem.s where one exists.
|
| lrem(long a, long b) - what compiled code calls for "l % m": alrem's
| code (alrem.s) on the dividend passed by value (6(bp) and 8(bp) its high
| and low word, 10(bp) and 12(bp) the divisor's); the remainder in DX:AX.
| The private convention of the long helpers (docs/MUTOS_C_ABI.md 1.8):
| BX is pushed before "mov bp,sp", so the first argument is at 6(bp); the
| result is left in DX:AX (but see L-08); BX, SI, DI and BP are kept.

.text
.globl	lrem
| the short path's test, as alrem's
lrem:
push	bp
push	bx
mov	bp,sp
push	di
push	si
mov	ax,12.(bp)
cwd
or	dx,dx
jns	L1
neg	ax
L1:
cmp	dx,10.(bp)
jne	hardlrem
mov	cx,ax
mov	si,8.(bp)
mov	ax,6.(bp)
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
lea	sp,-4.(bp)
pop	si
pop	di
pop	bx
pop	bp
ret
| the long way, as alrem's; the result moved to DX:AX at the end
hardlrem:
mov	si,8.(bp)
mov	di,6.(bp)
or	di,di
jns	L4
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
L5:
mov	cx,#/10
loopa:
shl	si,#1
rcl	di,#1
rcl	ax,#1
cmp	dx,ax		| L-04: compared signed: the high word of -2**31 (0x8000) is "negative"
jl	L7
jg	L6
cmp	12.(bp),di
jbe	L7
L6:
loop	loopa
jmp	L8
L7:
sub	di,12.(bp)
sbb	ax,#0.
sub	ax,dx
loop	loopa
L8:
test	6.(bp),#/ffff
jns	L9
neg	ax
neg	di
sbb	ax,#0.
L9:
mov	dx,ax
mov	ax,di
lea	sp,-4.(bp)
pop	si
pop	di
pop	bx
pop	bp
ret
