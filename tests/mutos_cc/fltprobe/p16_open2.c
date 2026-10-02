/*
 * fltprobe/p16_open2.c
 *
 * Floating shapes mutos_c0/mutos_c1 still refuse after the round-3
 * goldens, for the real compiler's code: an int converted after a value
 * read through a pointer ("*p + i") and after an assignment ("(e = d * 2)
 * + i" - which register?), a remainder converted ("d * (i % j)" - from
 * DX?), a negated zero ("-0.0" - its text?), a constant converted to an
 * int ("(int) 2.5", "i = 2.5" - folded, or "ftoi"?), a char added into a
 * double ("d += c"), an unsigned converted ("d = u" - v7 makes it a long
 * first).
 * Result: 79.
 */
main()
{
	double d, e, *p;
	int i, j, r;
	char c;
	unsigned u;

	i = 2;
	j = 3;
	c = 4;
	u = 5;
	r = 0;
	d = 1.5;
	p = &d;
	d = *p + i;
	r = r + (int) d;
	d = (e = d * 2) + i;
	r = r + (int) d;
	d = d * (i % j);
	r = r + (int) d;
	e = -0.0;
	d = d + e;
	r = r + (int) d;
	r = r + (int) 2.5;
	i = 2.5;
	r = r + i;
	d += c;
	r = r + (int) d;
	d = u;
	return r + (int) d;
}
