/*
 * 08_float/01_floatbas.c
 *
 * Basic float arithmetic.  8086-era MUTOS 1700 machines have no
 * FPU, so this compiles via software float support (see
 * tests/mutos1700_libc/'s dmath.o/doubles.o/ffltpr.o/fperr.o) --
 * this file is compile-only, not a runtime/link smoke test.
 */
main()
{
	float a, b, c;

	a = 3.5;
	b = 2.0;
	c = a + b;
	c = a * b;
	return (int) c;
}
