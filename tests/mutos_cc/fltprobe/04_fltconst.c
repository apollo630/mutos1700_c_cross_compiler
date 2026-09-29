/*
 * fltprobe/04_fltconst.c
 *
 * Int constants converted to floating (v7 c1's SFCON - libc.a's
 * atof.o "fl = 0", "10*fl"), the written zero, and unary minus
 * ("fneg" - atof.o's "fl = -fl"). Every constant here is exact, so
 * each ".float" text is "%.17e" of its value; the golden checks the
 * zero's text, a negative constant's, and a negated written one's.
 * Result: 7.
 */
main()
{
	double d, e;
	float f;

	d = 0;
	e = 0.0;
	f = 4;
	d = e + 10;
	d = 10 * d;
	e = d / 4;
	f = -f;
	e = -2;
	d = -1.5;
	d = d * e;
	d = d - f;
	return (int) d;
}
