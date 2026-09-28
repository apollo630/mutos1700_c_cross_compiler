| fltdbl.s - OPEN-QUESTION PROBE, not a regression golden (see
| README.md). ".double" (8-byte floating data, MUTOS1700_Assembler_as.pdf
| sect. 7.2.1) is not implemented at all in mutos_as - src/mutos_as/
| assemble.c refuses it outright, before even trying to parse the
| operand: "an explicit error until the real assembler's conversion of a
| value that needs more than a float's 24 bits is pinned down (one real
| 8-byte constant is known, ecvt.o's .03, correctly rounded)". This is a
| REAL, real-hardware-assembled golden from a KNOWN, hand-written
| ".double" source, specifically to gather that ground truth for the
| first time from a controlled input - the same evidence gap ecvt.o's
| ".03" only partly closes (its own real assembler-input spelling is
| unknown too).
|
| W = 1 + 2**-32 = 1.00000000023283064365386962890625 exactly (a
| terminating binary fraction: 32 fractional bits after the leading 1,
| i.e. 33 significant mantissa bits total - deliberately more than a
| float's 24, the whole reason ".double" needs its own encoding path
| rather than reusing flt_encode()'s).
|
| mutos_as is expected to refuse this file (run_goldens.sh's category
| 3) until ".double" is implemented FROM this golden's real bytes once
| captured. That mismatch against the real "as" (which is expected to
| accept it - see README.md) is the entire point.

.text
.globl	_fltdbl_probe
_fltdbl_probe:
push	bp
mov	bp,sp
lea	ax,W
call	fldd
pop	bp
ret
.data
W:	.double 1.00000000023283064365386962890625
