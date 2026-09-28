| fltaddr.s - "lea <reg>,<label>" combined with a real ".float" constant
| in ONE whole object - a gap neither existing float test closes:
| tests/mutos_as/libc_recon/floatdat.s has ".float" data but no code (its
| own header: "not a whole object"), and ldexp.s has "lea"+code but no
| ".float" (its constant, "huge", is emitted as raw ".word"s). This is a
| genuine NEW hand-written source (not reconstructed from a real object),
| shaped like what mutos_c1 actually emits for a float operand
| (docs/DEVLOG.md's 08_float section / src/mutos_cc/README.md: "an
| operand's address in AX (`lea ax,<x>` / `call flds` ... loads)").
|
| HALF = 2.5 = 1.01b * 2**1: exact in a float's 24-bit mantissa (when
| this was written, src/mutos_as/fltconst.h accepted only an exactly
| representable value), so this assembles cleanly with mutos_as - this
| is a regression golden, not an open-question probe (see fltzero.s /
| fltdbl.s in this directory for those).

.text
.globl	_fltaddr_probe
_fltaddr_probe:
push	bp
mov	bp,sp
lea	ax,HALF
call	flds
pop	bp
ret
.data
HALF:	.float 2.50000000000000000e+00
