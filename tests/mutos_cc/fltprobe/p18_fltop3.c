/*
 * fltprobe/p18_fltop3.c
 *
 * Floating shapes mutos_c1 compiles by inference since the round-4
 * goldens: p16_open2's "d += c" computed the char first and added d from
 * memory ("call itof" / "lea ax,d" / "call faddd") - so "d += e * 2"
 * too? "f += i" into a float ("fadds f")? A remainder converted after a
 * '*' ("mov ax,dx", as first in p16_open2?), a long from a constant
 * ("(long) 3.75" - "flds" / "ftol"?), a negated zero as an initializer
 * (".double 0.0...", as in code?).
 * Result: 27.
 */
double gz = -0.0;

main()
{
	double d, e;
	float f;
	int i, j, r;
	long l;

	i = 7;
	j = 4;
	r = 0;
	d = 1.5;
	e = 2.0;
	f = 0.5;
	d += e * 2;
	r = r + (int) d;
	f += i;
	r = r + (int) f;
	d = d - 2;
	r = r + (int) d;
	d = d - 4;
	r = r + (int) d;
	d = (d * e) + (i % j);
	r = r + (int) d;
	d = (d * e) + 3;
	r = r + (int) d;
	l = (long) 3.75;
	r = r + (int) l;
	d = gz;
	return r + (int) d;
}
