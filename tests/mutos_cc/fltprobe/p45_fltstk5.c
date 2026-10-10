/*
 * fltprobe/p45_fltstk5.c
 *
 * NOT a program to run, like p28_fltstk, p37_fltstk3 and p41_fltstk4: the
 * real c1's model of the floating-point stack after round 10. p41 showed
 * that the push of a floating argument is checked - with the upper-case
 * "Floating point stack underflow" - for a variable ("half(d)") and a
 * constant, but not for a sum ("half(d + e)"): a value loaded for the push
 * is checked, one computed before it is not. mutos_c1 infers the rest: f1
 * leaves the model one below the bottom with no message (p28's "half(d =
 * 3.0);"); then a float variable (f2), an element and a member through a
 * pointer (f3) pushed - checked, as loads?; a negation, a conversion and a
 * product (f4), a call's value multiplied (f5) - not checked, as
 * computed?; two arguments, a variable and a sum (f6). Every function starts
 * where the one before left the model (p28: it is never reset) - please
 * bring back the messages ("round11.log") with the files.
 */
struct pt {
	double x, y;
};

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

	half(d = 3.0);
	return 1;
}

f2()
{
	double e;
	float f;

	f = 1.0;
	e = half(f) + 1.0;
	return e;
}

f3()
{
	struct pt s, *q;
	double a[3], e;
	int i;

	i = 1;
	a[1] = 2.0;
	s.y = 4.0;
	q = &s;
	e = half(a[i]) + 1.0;
	e = half(q->y) + 1.0;
	return e;
}

f4()
{
	double d, e;
	int i;

	d = 1.0;
	i = 2;
	e = half(-d) + 1.0;
	e = half((double) i) + 1.0;
	e = half(d * 2.0) + 1.0;
	return e;
}

f5()
{
	double d, e;

	d = 1.0;
	e = half(half(d) * 2.0) + 1.0;
	return e;
}

f6()
{
	double d, e;

	d = 1.0;
	e = 2.0;
	e = two(d, d + e) + 1.0;
	return e;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f6();
}
