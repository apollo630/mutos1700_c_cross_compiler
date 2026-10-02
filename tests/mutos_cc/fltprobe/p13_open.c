/*
 * fltprobe/p13_open.c
 *
 * Shapes mutos_c0/mutos_c1 still refuse, for the real compiler's code:
 * an int converted after another conversion ("(double) i + (double) j"),
 * after an element ("arr[i] + j"), a difference and a shift computed for
 * AX ("(d * e) - (i - j)", "(d / e) + (i << 1)"), "x + 0" converted in AX,
 * "i /= d" into an int, a floating zero added ("d + 0.0" - does
 * acommute() drop it?) and subtracted.
 * Result: 41 (42 with v7's "j /= e", j / (int)e).
 */
main()
{
	double d, e, arr[2];
	int i, j, r;

	i = 1;
	j = 3;
	r = 0;
	d = 2.0;
	e = 1.5;
	arr[0] = 0.5;
	arr[1] = 4.0;
	d = (double) i + (double) j;
	r = r + (int) d;
	d = arr[i] + j;
	r = r + (int) d;
	d = (d * e) - (i - j);
	r = r + (int) d;
	d = (d / e) + (i << 1);
	r = r + (int) d;
	d = (d * e) + (j + 0);
	r = r + (int) d;
	j /= e;
	r = r + j;
	d = d + 0.0;
	d = d - 0.0;
	return r + (int) d - 30;
}
