/*
 * fltprobe/p4_const.c
 *
 * Floating constants mutos_c1 still refuses: ones that are not
 * exactly a float (kept in 8 bytes, as ecvt.o's .03 - but written as
 * what text?), 2**24 and more (the digits of the real printf), a
 * written constant after an int constant converted in the same
 * expression (the labels' order), "d - 2.0" (v7 turns "x - c" into
 * "x + -c"), and constants as call arguments.
 * Result: 3.
 */
double tw(x)
double x;
{
	return x;
}

main()
{
	double d, e;

	d = 0.1;
	d = .03;
	d = 3.14159265358979;
	d = 1e-5;
	d = 1e30;
	d = 16777217.;
	d = 72057594037927936.;
	d = 2 * 1.5;
	d = 1.5 * 2;
	e = d - 2.0;
	e = d - 2;
	e = tw(1.5);
	e = tw(0.0);
	e = tw(3.5);
	return (int) e;
}
