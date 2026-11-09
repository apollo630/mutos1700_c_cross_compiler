| aldiv.s - RECONSTRUCTED source of libc.a's aldiv.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/aldiv.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/aldiv.s where one exists.
|
| aldiv(long *p, long b): *p /= b - what compiled code calls for "l /= m"
| (6(bp) the pointer, 8(bp) and 10(bp) the divisor's high and low word);
| the quotient is stored through the pointer and left in DX:AX.
| The private convention of the long helpers (docs/MUTOS_C_ABI.md 1.8):
| BX is pushed before "mov bp,sp", so the first argument is at 6(bp); the
| result is left in DX:AX (but see L-08); BX, SI, DI and BP are kept.
|
| Two ways: a divisor whose high word is the sign "extension" of its low
| word takes two divs by the low word; any other the sixteen steps of
| hardldiv. Not C's division (L-04): see the comments below.

.text
.globl	aldiv
| the short path's test: CWD of the low word, -1 negated to 1
aldiv:
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
neg	dx
L1:
cmp	dx,8.(bp)		| L-04: equal for 0..32767 and 0x18000..0x1ffff, not for -1..-32768
jne	hardldiv
push	dx
mov	cx,ax
mov	si,2.(bx)
mov	di,(bx)
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
mov	2.(bx),si
mov	(bx),di
mov	dx,di
mov	ax,si
lea	sp,-4.(bp)
pop	si
pop	di
pop	bx
pop	bp
ret
| the long way: |dividend| shifted through AX:DI:SI, sixteen times
hardldiv:
mov	ax,#0.
push	ax
mov	si,2.(bx)
mov	di,(bx)
or	di,di
jns	L4
not	-6.(bp)
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
not	-6.(bp)
L5:
mov	cx,#/10		| L-04: sixteen steps, from the dividend's high word
loopa:
shl	si,#1
rcl	di,#1
rcl	ax,#1
cmp	dx,ax		| L-04: compared signed: the high word of -2**31 (0x8000) is "negative"
jg	L6
jl	L7
cmp	10.(bp),di
jbe	L7
L6:
loop	loopa
jmp	L8
L7:
sub	di,10.(bp)
sbb	ax,dx
inc	si
loop	loopa
L8:
sub	di,di		| L-04: the quotient's high word cleared
jmp	leave
.byte	0.
