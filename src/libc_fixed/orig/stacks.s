| stacks.s - RECONSTRUCTED source of libc.a's stacks.o (not the original).
|
| Drafted from the disassembly of the real object
| tests/mutos1700_libc/stacks.o by src/libc_fixed/objsrc.py and annotated;
| "mutos_as -C" assembles it to that object byte for byte - header, text,
| data, both relocation tables and the symbol table (check_copy.py, in
| "make test"). Its statements keep the original's order of first
| mention, which is the order of the symbol table; L1, L2 ... label jump
| targets the original reached without a symbol of its own, and -C leaves
| them out of the symbol table (the original cc runs as -gC). A comment
| "| L-nn" marks the site of a bug of docs/MUTOS_ORIGINAL_BUGS.md; the
| corrected copy of libc.a assembles ../fixed/stacks.s where one exists.
|
| The floating-point stack: fpstk, eight slots of a double (64 bytes, a
| common symbol), fpsp the address of the top slot (fpstk - 8: empty).
|   fltused     two words that pull in atof.o and fltpr.o (c1 writes
|               ".globl fltused" into every file with floating code)
|   flds, fldd  push the float or double at AX's address
|   fdup        push the top again
|   fstsp, fstdp  store the top as a float (its high four bytes) or a
|               double at AX's address, and pop it
|   fsts, fstd  the same without the pop
|   incfpsp, decfpsp  move fpsp; a ninth slot or a pop of an empty stack
|               raises SIGFPE (__ovfl; the message pushed is not printed)

.data
.comm	fpstk,64.
.globl	fpsp
fpsp:
.word	fpstk-8.
.text
.comm	fac,8.
.globl	__ovfl
.globl	__fltu2
.globl	fltused
fltused:
.word	__fltu2
.word	__fltu3
.globl	flds
flds:
push	bp
mov	bp,sp
push	di
push	si
mov	si,ax
.globl	incfpsp
call	incfpsp
mov	di,fpsp
push	di
add	di,*4.
movs
movs
sub	ax,ax
pop	di
mov	(di),ax
mov	2.(di),ax
jmp	cret
.globl	fdup
fdup:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
j	incstkco
.globl	fldd
fldd:
push	bp
mov	bp,sp
push	di
push	si
mov	si,ax
incstkco:
call	incfpsp
mov	di,fpsp
movs
movs
movs
movs
jmp	cret
.globl	fstsp
fstsp:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
add	si,*4.
mov	di,ax
movs
movs
.globl	decfpsp
call	decfpsp
jmp	cret
.globl	fstdp
fstdp:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
mov	di,ax
movs
movs
movs
movs
call	decfpsp
jmp	cret
.globl	fsts
fsts:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
add	si,*4.
mov	di,ax
movs
movs
jmp	cret
.globl	fstd
fstd:
push	bp
mov	bp,sp
push	di
push	si
mov	si,fpsp
mov	di,ax
movs
movs
movs
movs
jmp	cret
| one slot more; past the eighth: overflow
incfpsp:
add	fpsp,*8.
cmp	fpsp,#fpstk+64.
jge	fpstovf		| L-12: signed: a stack across address 0x8000 overflows at once
ret
| one slot less; below the first: underflow
decfpsp:
sub	fpsp,*8.
cmp	fpsp,#fpstk-8.
jl	fpstunf		| L-12: signed, as above
ret
fpstunf:
mov	ax,#undmsg
j	stkcomm
fpstovf:
mov	ax,#ovmsg
stkcomm:
push	ax
call	__ovfl
.byte	0.
.data
undmsg:
.byte	102.,108.,111.,97.,116.,105.,110.,103.,32.,115.,116.,97.
.byte	99.,107.,32.,117.,110.,100.,101.,114.,102.,108.,111.,119.
.byte	0.
ovmsg:
.byte	102.,108.,111.,97.,116.,105.,110.,103.,32.,115.,116.,97.
.byte	99.,107.,32.,111.,118.,101.,114.,102.,108.,111.,119.,0.
.byte	0.
