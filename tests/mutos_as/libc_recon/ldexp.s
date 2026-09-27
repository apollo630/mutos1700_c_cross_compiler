| ldexp.s - RECONSTRUCTED source of libc.a's ldexp.o (not the original).
|
| Written from a disassembly of the real object tests/mutos1700_libc/
| ldexp.o (real MUTOS 1700 assembler output) so that mutos_as reproduces
| that object byte for byte - header, text, data, both relocation tables
| and the symbol table (whose order is the order of first mention, so the
| statements below keep the original's order of first mention: ERANGE,
| DOFF, _ldexp, fac, ldexp3, ldexp2, _errno, huge, cret). The golden,
| ldexp.o.golden, is a copy of that real object. See README.md.
|
| double ldexp(double x, int n): x * 2**n, done on the exponent byte of
| the runtime's accumulator fac. It is the regression test for
| "lea <reg>,<label>" (8d /r, mod=00 rm=110, a relocated disp16): an
| external label at even and odd offsets ("lea di,fac" R_EXT, "lea ax,
| fac") and a local data label at an odd offset ("lea si,huge" R_DATA).

ERANGE = 34.
DOFF = 1023.
.globl	_ldexp
.text
_ldexp:
push	bp
mov	bp,sp
push	di
push	si
lea	si,*4.(bp)		| fac = x
lea	di,fac
movs
movs
movs
movs
mov	al,fac+7		| exponent byte; 0 is zero
sub	ah,ah
or	al,al
je	ldexp3
mov	bx,*12.(bp)		| exponent += n
add	ax,bx
jo	ldexp2
mov	fac+7,al
j	ldexp3
ldexp2:
mov	_errno,#ERANGE		| overflow: +-huge, errno = ERANGE
lea	di,fac
lea	si,huge
movs
movs
movs
movs
mov	al,*10.(bp)		| x's sign byte
and	al,*/80
je	ldexp3
xorb	*6(si),*/80
ldexp3:
lea	ax,fac
call	cret
.data
huge:	.word	-1,-1,-1,/ff7f
.comm	_errno,2
