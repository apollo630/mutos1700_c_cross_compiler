/*
 * fltprobe/p6_dblcon.c
 *
 * 8-byte floating constants (not exactly a float - ".double", loaded
 * with "fldd") where the operand order depends on their degree: a '+'
 * or '*' with a double or a float variable, each side, a comparison
 * each way, one inside a computed operand. A ".float" constant has
 * degree 1 (typed FLOAT); is a ".double" one 0? mutos_c1 refuses these.
 * Result: 9.
 */
main()
{
	double d, e;
	float a;
	int r;

	d = 1.5;
	e = 2.5;
	a = 0.5;
	r = 0;
	e = d + 0.1;
	e = 0.1 + d;
	e = a * 0.1;
	e = 0.1 * a;
	e = d * 0.1 + e;
	if (d < 0.1)
		r = r + 1;
	if (0.1 < d)
		r = r + 2;
	if (a > 0.1)
		r = r + 4;
	if (0.1 > a)
		r = r + 32;
	e = (d + e) * 0.1;
	r = r + (int) (e * 20);
	return r;
}
