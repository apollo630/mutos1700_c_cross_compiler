| fltzero.s - written as an OPEN-QUESTION PROBE (see README.md), now a
| regression golden. mutos_as used to REFUSE a zero ".float" outright
| (src/mutos_as/fltconst.h's FLT_ZERO: "a zero's real bytes are not a
| plain zero" - known only from the ambiguous compiled-C atof.o/ecvt.o
| zero bytes, "bc a2 31 00", whose real assembler-input spelling is
| unknown - see tests/mutos_as/libc_recon/README.md's "Why only these").
| This is a REAL, real-hardware-assembled golden from a KNOWN,
| hand-written zero ".float" source, specifically to pin that down for
| the first time from a controlled input rather than an inference.
|
| RESOLVED 2026-09-28: the real "as" wrote bc a2 31 00 (fltzero.o.golden),
| and fltconst.c now encodes exactly this spelling of zero that way -
| mutos_as reproduces the golden byte for byte. Other spellings of zero
| ("0.0", a minus sign, ...) stay refused: their real bytes may differ
| (see src/mutos_as/fltconst.h and README.md's "Implemented").

.text
.globl	_fltzero_probe
_fltzero_probe:
push	bp
mov	bp,sp
lea	ax,Z
call	flds
pop	bp
ret
.data
Z:	.float 0.00000000000000000e+00
