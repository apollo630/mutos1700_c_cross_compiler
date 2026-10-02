















struct s8 {
	int a, b, c, d;
};

f1(a, b, i, j)
int *a, *b;
{
	return a[i] * b[j];
}

f2(i)
{
	struct s8 ps[2], qs[2];
	int s;

	ps[i].c = 3;
	qs[i].d = 4;
	s = ps[i].c * qs[i].d;
	return s;
}

main()
{
	int a[3], b[3];
	int i, j, x, s, r;

	i = 1;
	j = 2;
	x = 20;
	a[1] = 6;
	b[2] = 3;
	r = f1(a, b, i, j);
	r = r + f2(i);
	s = a[i] + b[j];
	r = r + s;
	s = a[i] - b[j];
	r = r + s;
	s = a[i] | b[j];
	r = r + s;
	s = a[i] ^ b[j];
	r = r + s;
	s = a[i] + b[j] + x;
	r = r + s;
	if (a[i] > b[j])
		r = r + 1;
	return r;
}
