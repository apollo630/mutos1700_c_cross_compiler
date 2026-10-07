




















struct s8 {
	int a, b, c, d;
};

f(n)
{
	return n + 10;
}

g(n)
{
	return n * 20;
}

h(n)
{
	return n - 1;
}

main()
{
	struct s8 ps[2], v, *p;
	int a[2][4], b[5], *ip;
	int i, j, x, r;

	i = 1;
	j = 2;
	x = 5;
	r = 0;
	b[0] = 3;
	b[1] = 0;
	b[2] = 9;
	b[3] = -6;
	b[4] = 4;
	a[1][2] = 0;
	a[0][3] = 7;
	ps[1].d = 12;
	v.b = 2;
	p = &v;
	ip = &b[2];
	if (!b[i])
		r = r + 1;
	x = !b[j];
	r = r + x;
	r = r + (b[j] ? 3 : 9);
	if (a[i][j])
		r = r + 100;
	if (a[0][3])
		r = r + 4;
	x = 5;
	b[i] = 4;
	x = (b[i] - x) * b[j];
	r = r + x;
	x = ~b[i] * b[j];
	r = r + x;
	x = (b[j] + 3) / b[i];
	r = r + x;
	x = (b[3] & 7) % b[4];
	r = r + x;
	b[i] += 7;
	r = r + b[i];
	x = 5;
	ps[i].d ^= x;
	r = r + ps[i].d;
	p->b += x;
	r = r + v.b;
	a[i][j] += x;
	r = r + a[1][2];
	x = (x > 2) * 2 + (x < 9) * 4 + (x == 5) + x;
	r = r + x;
	x = (b[i] < b[j]) * 2 + (b[j] < b[i]) * 2 + x;
	r = r + x;
	x = f(1) + g(2);
	r = r + x;
	x = f(1) + g(2) + h(3);
	r = r + x;
	*ip += x;
	r = r + b[2];
	b[3] ^= b[i];
	r = r + b[3];
	x = x + f(1) + g(2);
	r = r + x;
	x = b[j] / b[i] + b[j] % b[i];
	r = r + x;
	return r;
}
