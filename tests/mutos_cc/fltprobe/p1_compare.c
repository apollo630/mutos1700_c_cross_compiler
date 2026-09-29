/*
 * fltprobe/p1_compare.c
 *
 * Floating comparisons mutos_c1 still refuses - no evidence of the
 * real compiler's code for any of them: a zero as the right operand
 * after v7's operand exchange (a float variable, or a computed value,
 * compared with 0 - v7's cbranch() tests instead of comparing, and
 * stkmath.o has "ftest"), a floating value tested for truth, a
 * comparison used as a value, "&&", a computed value compared with a
 * constant (where does the constant's .data block go?) or with another
 * computed value, and an int converted then compared with a constant.
 * Result: 32.
 */
main()
{
	double d, e;
	float a;
	int i, r;

	d = 1.5;
	e = 2.5;
	a = 0.75;
	i = 2;
	r = 0;
	if (a < 0)
		r = r + 100;
	if (a == 0)
		r = r + 200;
	if ((d + e) < 0)
		r = r + 300;
	if ((d - e) != 0)
		r = r + 1;
	if (d)
		r = r + 2;
	while (a)
		a = 0;
	if (!e)
		r = r + 400;
	r = r + (d < e);
	if (d < e && e < d)
		r = r + 500;
	if ((d + e) < 1.5)
		r = r + 600;
	if ((d + e) > (e - d))
		r = r + 4;
	if (1.5 < i)
		r = r + 8;
	if (i < 1.5)
		r = r + 700;
	if (e > i + 0.25)
		r = r + 16;
	return r;
}
