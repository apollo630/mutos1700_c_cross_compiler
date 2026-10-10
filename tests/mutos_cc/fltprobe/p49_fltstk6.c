/*
 * fltprobe/p49_fltstk6.c
 *
 * NOT a program to run, like p28_fltstk, p37_fltstk3, p41_fltstk4 and
 * p45_fltstk5: the real c1's model of the floating-point stack after
 * round 11. p45 showed the push of a float variable checked like a
 * double's, and an element subscripted by a variable ("half(a[i])") and a
 * member through a pointer ("half(q->y)") NOT checked - loaded through an
 * address, v7's '*' node, where a variable is a NAME. mutos_c1 infers the
 * rest: f1 leaves the model one below the bottom with no message (p28's
 * "half(d = 3.0);"); then a file-scope double, a constant-indexed element
 * of a file-scope array and a local struct's member (f2), and a local
 * static double (f3) pushed - checked, as NAMEs to v7's optim()?; a value
 * through a pointer variable ("*p", "p[1]") and a float element (f4) -
 * not checked, as '*' nodes?; two arguments, a member and an element (f5).
 * Every function starts where the one before left the model (p28: it is
 * never reset) - please bring back the messages ("round12.log") with the
 * files.
 */
struct pt {
	double x, y;
};

double gd, ga[3];

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
	struct pt s;
	double e;

	gd = 1.0;
	ga[1] = 2.0;
	s.y = 4.0;
	e = half(gd) + 1.0;
	e = half(ga[1]) + 1.0;
	e = half(s.y) + 1.0;
	return e;
}

f3()
{
	static double sd;
	double e;

	sd = 2.0;
	e = half(sd) + 1.0;
	return e;
}

f4()
{
	double a[3], e, *p;
	float fa[3];
	int i;

	i = 1;
	a[0] = 1.0;
	a[1] = 2.0;
	fa[1] = 3.0;
	p = a;
	e = half(*p) + 1.0;
	e = half(p[1]) + 1.0;
	e = half(fa[i]) + 1.0;
	return e;
}

f5()
{
	struct pt s;
	double a[3], e;
	int i;

	i = 1;
	s.y = 1.0;
	a[1] = 2.0;
	e = two(s.y, a[i]) + 1.0;
	return e;
}

main()
{
	return f1() + f2() + f3() + f4() + f5();
}
