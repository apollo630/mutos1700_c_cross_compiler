















struct s8 {
	int a, b, c, d;
};

int g;

main()
{
	struct s8 ps[2];
	int a[4], b[4], *p;
	int i, j, x, s, r;

	i = 1;
	j = 2;
	x = 20;
	g = 30;
	r = 0;
	a[1] = 6;
	b[2] = 3;
	b[3] = 9;
	ps[1].c = 5;
	if (ps[i].c > b[j])
		r = r + 1;
	if (a[i] > ps[i].c)
		r = r + 2;
	s = x * b[j];
	r = r + s;
	p = &b[j];
	s = x - *p;
	r = r + s;
	s = 5 - b[j];
	r = r + s;
	s = g - b[j];
	r = r + s;
	s = b[j + 1];
	r = r + s;
	s = a[i] + b[j + 1];
	r = r + s;
	s = (a[i] < b[j]) + (a[i] > b[j]) * 2;
	r = r + s;
	if (a[i] > b[j] && b[j] > 0)
		r = r + 1;
	return r;
}
