











double scale(x, n)
double x;
int n;
{
	x *= 2;
	return (x + x) * n;
}

double mid(a, b)
float a;
double b;
{
	double two;

	two = 2;
	return (a + b) / two;
}

double half(x)
double x;
{
	return x / 2;
}

main()
{
	double d, e;
	float f;
	int i;

	d = 1.5;
	f = 2.5;
	i = 3;
	e = scale(d, i);
	d = mid(f, e);
	e = half(d) * d;
	i = half(e);
	return i;
}
