/*
 * fltprobe/p37_fltstk3.c
 *
 * NOT a program to run, like p28_fltstk: which store an assignment passed
 * as a floating argument gets, after round 8. p30_fltstk2 showed it is
 * the call's own value that decides - the store pops ("fstdp", and the
 * argument push pops again: the real c1's "floating point stack
 * underflow") when the call's value goes nowhere or straight into the
 * statement's floating store, and keeps the value ("fstd", right code)
 * under anything else. mutos_c1 infers the rest: a call nested as an
 * argument (f1 - the inner call's value an argument: "fstd"?), two such
 * calls in a sum (f2), a function returning an int ("x = ihalf(d = 3.0)",
 * f3, and its value unused, f4 - "fstd": the call is not floating?), two
 * such arguments of a call whose value goes nowhere (f5 - both stored
 * with a pop, two messages?), one of two arguments with the value stored
 * (f6), the call's value returned from an int function (f7) and stored
 * into a float (f8). Every function starts where the one before left the
 * real c1's stack model (p28: it is never reset) - please bring back the
 * messages ("round9.log") with the files.
 */
double half(x)
double x;
{
	return x / 2.0;
}

double two(x, y)
double x, y;
{
	return x + y;
}

ihalf(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d, f;

	f = half(half(d = 3.0));
	return f;
}

f2()
{
	double d, e;

	e = half(d = 1.0) + half(e = 2.0);
	return e;
}

f3()
{
	double d;
	int x;

	x = ihalf(d = 3.0);
	return x;
}

f4()
{
	double d;

	ihalf(d = 3.0);
	return d;
}

f5()
{
	double d, e;

	two(d = 1.0, e = 2.0);
	return d + e;
}

f6()
{
	double d, e;

	e = two(d = 1.0, 2.0);
	return e;
}

f7()
{
	double d;

	return half(d = 3.0);
}

f8()
{
	double d;
	float g;

	g = half(d = 3.0);
	return g;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f6() + f7() + f8();
}
