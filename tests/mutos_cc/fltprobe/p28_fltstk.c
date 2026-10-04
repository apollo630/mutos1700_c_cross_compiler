/*
 * fltprobe/p28_fltstk.c
 *
 * NOT a program to run: the real compiler's own wrong code for an
 * assignment passed as a floating argument (p21_fltexp's "half(d = 3.0)",
 * the value popped twice - "cc -S" printed "56: floating point stack
 * underflow" and "57: Floating point stack underflow"), used on purpose to
 * see how its c1's compile-time model of the floating-point stack goes on.
 * Asked: is the model reset for every function (f2 reports nothing after
 * f1?); does the argument push count as a pop, and the call's unused
 * result as nothing ("half(d = 3.0);" as a statement - silent, one below
 * the bottom?); which message, if any, '*' of two computed values ("fmul",
 * f3) and a comparison ("fcmp", f4) report below the bottom; does
 * "return d" in a double function (f5) report.
 * Expected on MUTOS: "cc -S" prints messages and ends with status 1, the
 * ".s" still written (as for p21) - please bring back the messages too.
 */
double half(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d;
	int x;

	x = half(d = 3.0);
	return x;
}

f2()
{
	double d;

	d = 2.5;
	return d;
}

f3()
{
	double d, e;
	int x;

	d = 1.0;
	e = 2.0;
	half(d = 3.0);
	half(e = 4.0);
	x = (d + e) * (d - e);
	return x;
}

f4()
{
	double d, e;
	int x;

	d = 1.0;
	e = 2.0;
	half(d = 3.0);
	half(e = 4.0);
	x = 0;
	if (d + e > d - e)
		x = 1;
	return x;
}

double f5()
{
	double d;

	half(d = 3.0);
	return d;
}

main()
{
	return f1() + f2() + f3() + f4() + f5();
}
