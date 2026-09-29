/*
 * fltprobe/05_fltasop.c
 *
 * atof.o's own statements: "10*fl + (c-'0')" (an int right operand of
 * a computed value - "call itof" / "call fadd" on the stack), "*=" and
 * "/=" ("flexp *= exp5": the right operand loaded first; "fl /=
 * flexp": the target first), ecvt.o's "arg *= 10", and an assignment
 * whose value is used ("fstd", no pop - ecvt.o's "(fj = arg*10) < 1").
 * Result: 33.
 */
main()
{
	double fl, flexp, exp5, t;
	float f;
	int c;

	fl = 0;
	c = '4';
	fl = 10*fl + (c-'0');
	c = '2';
	fl = 10*fl + (c-'0');
	flexp = 1;
	exp5 = 5;
	flexp *= exp5;
	exp5 *= exp5;
	flexp *= exp5;
	fl *= 2;
	fl /= 4;
	fl *= flexp;
	fl /= flexp;
	c = 2;
	t = (fl * 2) - c;
	t = (t + fl) / c;
	f = 3;
	f *= f;
	f /= 3;
	if ((exp5 = t) < fl)
		c = 0;
	c = t;
	return c + (int) f;
}
