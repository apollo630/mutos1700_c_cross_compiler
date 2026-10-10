/*
 * fltprobe/p53_fltstk7.c
 *
 * NOT a program to run, like p28_fltstk, p37_fltstk3, p41_fltstk4,
 * p45_fltstk5 and p49_fltstk6: the real c1's model of the floating-point
 * stack after round 12. p48 showed "x = (d += ++w + ++y) > 2.0;" storing d
 * WITHOUT a pop ("fstd d") - the value left on the runtime stack, used by
 * nothing - with no message from "cc -S". Does the model count that value
 * (one higher from then on), or not? mutos_c1 infers that it does: f1
 * leaves the model one below the bottom with no message (p28's "half(d =
 * 3.0);"); in f2 the stores report as before, the comparison's "fcmp"
 * does not (the leftover keeps the model at the bottom), and from then on
 * the model is back at the bottom - f3's statements report nothing. Were
 * the leftover not counted, "fcmp" (upper case) and every store after it
 * (lower case) would report, in f2 and f3. Every function starts where the
 * one before left the model (p28: it is never reset) - please bring back
 * the messages ("round13.log") with the files.
 */
double half(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d;

	half(d = 3.0);
	return 1;
}

f2()
{
	double d, e, w, y;
	int x;

	d = 1.5;
	w = 4.0;
	y = 10.0;
	x = (d += ++w + ++y) > 2.0;
	e = d + 1.0;
	d = e * 2.0;
	return x;
}

f3()
{
	double d, e;

	d = 1.0;
	e = d + 2.0;
	return e;
}

main()
{
	return f1() + f2() + f3();
}
