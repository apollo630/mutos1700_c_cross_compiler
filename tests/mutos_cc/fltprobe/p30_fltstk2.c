/*
 * fltprobe/p30_fltstk2.c
 *
 * NOT a program to run, like p28_fltstk: an assignment passed as a
 * floating argument, which the real compiler stores either WITH a pop
 * ("fstdp" - its own wrong code, the value popped again by the argument
 * push: p21_fltexp's "f = half(d = 3.0)", p28's "half(d = 3.0);") or
 * without ("fstd" - right: p28's "x = half(d = 3.0);"). Which decides?
 * mutos_c1 infers the statement's own type (floating: pop, int: keep);
 * the other reading is a state the previous floating statement leaves
 * behind (p28's "x = ..." followed half()'s "return"). Asked: an int
 * statement right after a floating one (f1), after an int-valued use of
 * a double (f2); a floating sum of the call (f3); the call compared, as
 * a condition (f4) and as a value (f5); "return half(d = 3.0)" in a double
 * function (f6); two such arguments (f7). Every function starts where
 * the one before left the real c1's stack model (p28: it is never
 * reset) - please bring back the messages ("round8.log") with the files.
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

f1()
{
	double d;
	int x;

	d = 1.0;
	x = half(d = 3.0);
	return x;
}

f2()
{
	double d;
	int x;

	d = 1.0;
	x = d;
	x = half(d = 3.0);
	return x;
}

f3()
{
	double d, e;

	e = half(d = 3.0) + 1.0;
	return e;
}

f4()
{
	double d;
	int x;

	x = 0;
	if (half(d = 3.0) > 1.0)
		x = 1;
	return x;
}

f5()
{
	double d;
	int x;

	x = half(d = 3.0) > 1.0;
	return x;
}

double f6()
{
	double d;

	return half(d = 3.0);
}

f7()
{
	double d, e;
	int x;

	x = two(d = 1.0, e = 2.0);
	return x;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f7();
}
