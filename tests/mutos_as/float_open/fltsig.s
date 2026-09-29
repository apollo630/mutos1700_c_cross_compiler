| fltsig.s - OPEN-QUESTION PROBE (see README.md), not a regression
| golden. The partner of fltovf.s: an overflow INSIDE atof()'s
| arithmetic, in the repeated squaring that builds flexp = 5**k, which
| overflows from k = 55 on - the compiler's "%.17e" text from e-38
| down, although such a value may well fit the format.
|
| In libc.a's runtime (dmath.o) an overflowing dmul calls __ovfl
| (fperr.o), which sets errno to ERANGE and sends the process SIGFPE
| (kill(getpid(), 8)). If the real "as" does not catch SIGFPE, it dies
| at S1 - "Floating exception", maybe a core file - and writes no
| object: THAT is the finding, together with the exact message. If it
| survives, the bytes show what it writes instead (no prediction:
| libc.a's runtime would go on to divide by the resulting zero, which
| raises SIGFPE again through __div0).
|
|   S1  .float 1.00000000000000000e-38 - k = 55: flexp = 5**23 * 5**32
|       overflows; the value itself fits. The case the compiler can
|       write.
|   S2  .double 2.93873587705571877e-39 - 2**-128, the format's
|       smallest value, in the compiler's form: k = 56, 5**24 * 5**32.
|   S3  .float 1.0e+65 - k = 64: the squaring 5**32 * 5**32 itself
|       overflows (the value is above the range anyway).
|
| All are plain atof texts. The CURRENT mutos_as refuses all three (out
| of range) by design. See README.md for how to run and read this.

.text
.globl	_fltsig_probe
_fltsig_probe:
push	bp
mov	bp,sp
lea	ax,S1
call	flds
lea	ax,S2
call	fldd
lea	ax,S3
call	flds
pop	bp
ret
.data
S1:	.float 1.00000000000000000e-38
S2:	.double 2.93873587705571877e-39
S3:	.float 1.0e+65
