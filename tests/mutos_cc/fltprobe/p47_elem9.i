



















f(n)
{
	return n + 10;
}

main()
{
	int b[4];
	int i, x, y, c, d, s, r;

	r = 0;
	b[0] = 1;
	b[1] = 60;
	b[2] = 3;
	b[3] = 12;
	i = 1;
	x = 5;
	c = 17;
	d = 5;
	s = 2;
	y = 300;
	y = y / (c % d + s);
	r = r + y;
	y = y / f(1);
	r = r + y;
	y = 300;
	y = y % f(2);
	r = r + y;
	y = 300;
	y = y / (c / d);
	r = r + y;
	x = b[i] / (c / d - s);
	r = r + x;
	y = 300;
	y = y % (f(1) - s);
	r = r + y;
	y = 300;
	y = y % (c - s);
	r = r + y;
	y = 300;
	y = y % (c * d - s);
	r = r + y;
	x = 50;
	x = c % d + x;
	r = r + x;
	x = c % d + 3;
	r = r + x;
	x = f(1) * 2 + c % d;
	r = r + x;
	x = f(1) * 3 - c / d;
	r = r + x;
	x = c * d + f(1) * 2;
	r = r + x;
	x = f(1) * 3 - x;
	r = r + x;
	x = x - f(1) * 3;
	r = r + x;
	x = f(2) * 4 - f(1);
	r = r + x;
	x = f(1) % c;
	r = r + x;
	x = 7;
	x = x * (c / d);
	r = r + x;
	y = 3;
	x = f(1) - (x < y);
	r = r + x;
	y = 300;
	x = y % (c * d);
	r = r + x;
	x = c % d - b[i];
	r = r + x;
	x = c * 3 + f(1) * 5;
	r = r + x;
	x = f(1) * 2 + f(2) * 3;
	r = r + x;
	return r;
}
