| floatdat.s - .float constants whose real bytes are in libc.a.
|
| Not a reconstruction of any one object: each value below is a floating
| constant that the real MUTOS 1700 compiler and assembler wrote into a
| libc.a object's data segment (tests/mutos1700_libc), spelled the way
| mutos_c1 - like the real compiler (tests/mutos_cc/08_float's .s
| goldens) - writes one, "%.17e". check_floatdat.sh assembles this file
| and compares each value's 4 bytes with the real object's bytes at the
| offset named on its line. (A zero constant - atof.o data+4, ecvt.o
| data+0, bytes bc a2 31 00 - is not here: mutos_as refuses a zero .float,
| see README.md.)

.data
L1:	.float 7.20575940379279360e+16		| 2**56: atof.o data+0 (00 00 00 b9)
L2:	.float 1.00000000000000000e+01		| 10.0: atof.o data+8, ecvt.o data+12 (00 00 20 84)
L3:	.float 1.00000000000000000e+00		| 1.0: atof.o data+20, ecvt.o data+32 (00 00 00 81)
L4:	.float 5.00000000000000000e+00		| 5.0: atof.o data+24 (00 00 20 83)
