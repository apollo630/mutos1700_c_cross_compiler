| fltzero.s - OPEN-QUESTION PROBE, not a regression golden (see
| README.md). mutos_as currently REFUSES ".float 0.0" outright
| (src/mutos_as/fltconst.h's FLT_ZERO: "a zero's real bytes are not a
| plain zero" - known only from the ambiguous compiled-C atof.o/ecvt.o
| zero bytes, "bc a2 31 00", whose real assembler-input spelling is
| unknown - see tests/mutos_as/libc_recon/README.md's "Why only these").
| This is a REAL, real-hardware-assembled golden from a KNOWN,
| hand-written ".float 0.0" source, specifically to pin that down for
| the first time from a controlled input rather than an inference.
|
| mutos_as is expected to refuse this file (run_goldens.sh's category
| 3) until fltconst.c's zero handling is implemented FROM this golden's
| real bytes once captured. That mismatch against the real "as" (which
| is expected to accept it - see README.md) is the entire point; it is
| not a bug in mutos_as to "fix" by making this file pass some other
| way.

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
