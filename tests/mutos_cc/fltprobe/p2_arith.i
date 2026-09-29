










main()
{
	double d, e;
	float a, b;
	int i, j;

	d = 4;
	e = 2;
	a = 1.5;
	b = 0.5;
	i = 3;
	j = 2;
	d = d + i;
	d = d - i;
	d = d * i;
	d = d / i;
	d = a - i;
	d = i - d;
	d = (d + e) * (i * j);
	d = b * (a + e);
	d = (a + b) * (d + e);
	d = (d + e) * 2.0;
	d = (d + e) - 0.5;
	d = -(-e);
	d += e;
	d -= e;
	d += 1;
	a -= 0.5;
	d /= (e + e);
	d *= i;
	e = (d *= e);
	i *= e;
	j = e;
	return i + j;
}
