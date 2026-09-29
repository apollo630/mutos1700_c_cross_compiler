/*
 * fltprobe/03_fltcmp.c
 *
 * Floating comparisons as conditions. mutos_c1 compiles every one of
 * these (from libc.a's compiled atof.o/ecvt.o - see ../../../docs/
 * DEVLOG.md's "Floating shapes from libc.a's compiled C"): both
 * operands loaded, "call fcmp", "sahf", a signed branch, the operands
 * exchanged first by v7's degree() rule. The golden checks the order
 * of the loads for each case and where the constants' .data blocks go.
 * Result: 95.
 */
main()
{
	double d, e;
	float a, b;
	int i, r;

	d = 1.5;
	e = 2.5;
	a = 0.75;
	b = 3;
	i = 2;
	r = 0;
	if (d < e)
		r = r + 1;
	if (d >= e)
		r = r + 100;
	if (e > 0)
		r = r + 2;
	if (d != 0)
		r = r + 4;
	if (0 < d)
		r = r + 8;
	if (a < 1.5)
		r = r + 16;
	if (d < a)
		r = r + 1000;
	if (e < i)
		r = r + 32;
	if (i <= d)
		r = r + 2000;
	if (!(a > b))
		r = r + 64;
	while (d < b)
		d = d + e;
	return r;
}
