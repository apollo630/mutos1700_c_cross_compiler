/*
 * fltprobe/p41_fltstk4.c
 *
 * NOT a program to run, like p28_fltstk and p37_fltstk3: the real c1's
 * model of the floating-point stack after round 9. p37 showed that the
 * push of a floating argument is checked against it - with the upper-case
 * "Floating point stack underflow" - for a constant ("two(d = 1.0, 2.0)"),
 * but not for an assignment's value nor for a call's result. mutos_c1
 * infers the rest, and which store an assignment passed as an argument
 * gets: f1 leaves the model one below the bottom with no message (p28's
 * "half(d = 3.0);"); then a variable (f2), a sum (f3) and an int converted
 * (f4) pushed as arguments - checked?; a call nested in an int conversion
 * (f5 - the inner store kept, "fstd"?); a call nested in a call whose value
 * goes nowhere (f6 - the inner store popped, "fstdp"?) and two of them
 * (f7); a call's value multiplied, negated and compared (f8 - kept?); a
 * constant argument of a call whose value goes nowhere (f9). Every function
 * starts where the one before left the model (p28: it is never reset) -
 * please bring back the messages ("round10.log") with the files.
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

	half(d = 3.0);
	return 1;
}

f2()
{
	double d, e;

	d = 1.0;
	e = half(d) + 1.0;
	return e;
}

f3()
{
	double d, e;

	d = 1.0;
	e = 2.0;
	e = half(d + e) + 1.0;
	return e;
}

f4()
{
	double e;
	int i;

	i = 4;
	e = half(i) + 1.0;
	return e;
}

f5()
{
	double d;
	int x;

	x = half(half(d = 3.0));
	return x;
}

f6()
{
	double d, e;

	two(d = 1.0, half(e = 2.0));
	return d + e;
}

f7()
{
	double d, e;

	e = two(half(d = 1.0), half(e = 2.0));
	return e;
}

f8()
{
	double d, e;
	int x;

	e = half(d = 3.0) * 2.0;
	e = -half(d = 3.0);
	x = half(d = 3.0) > 1.0 ? 1 : 2;
	return x;
}

f9()
{
	half(2.0);
	return 2;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f6() + f7() + f8() + f9();
}
