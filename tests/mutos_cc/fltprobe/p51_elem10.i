


























f(n)
{
	return n + 10;
}

main()
{
	int b[4];
	int i, x, y, c, d, a, s, r;

	r = 0;
	b[0] = 1;
	b[1] = 6;
	b[2] = 3;
	b[3] = 12;
	i = 1;
	a = 9;
	c = 17;
	d = 5;
	s = 2;
	x = a - (c - d);
	r = r + x;
	x = a - (c + 7);
	r = r + x;
	x = 5 - (c + d);
	r = r + x;
	x = a - (c < d);
	r = r + x;
	x = a - (c > 2) * 4;
	r = r + x;
	x = f(1) - (c + d);
	r = r + x;
	x = (c < d) - (a - s);
	r = r + x;
	x = (a + c) - (d - s);
	r = r + x;
	x = c % d + 1;
	r = r + x;
	x = c % d - 1;
	r = r + x;
	x = c % d + b[i];
	r = r + x;
	x = c / d - b[i];
	r = r + x;
	x = c / d + b[i];
	r = r + x;
	y = 300;
	y = y / (c + d);
	r = r + y;
	y = 300;
	y = y % (c << 1);
	r = r + y;
	y = b[i] / (c - d);
	r = r + y;
	y = 300;
	y = y / (-c);
	r = r + y;
	x = (c + d) % (a - s);
	r = r + x;
	x = 7;
	x = x - f(1) * 2;
	r = r + x;
	x = f(1) * 3 + f(2) * 2;
	r = r + x;
	x = f(1) * 4 + f(2) * 8;
	r = r + x;
	x = c * 5 + f(1);
	r = r + x;
	x = c * d + f(1) * 4;
	r = r + x;
	x = 50;
	x = x - c * d;
	r = r + x;
	x = c % d - (a - s);
	r = r + x;
	x = c % d * 3;
	r = r + x;
	x = c % d + c % s;
	r = r + x;
	x = f(1) * 3 - f(2) * 2;
	r = r + x;
	x = a - (c - d) * 2;
	r = r + x;
	x = f(1) / (c - d);
	r = r + x;
	x = c % d & b[i];
	r = r + x;
	x = c % d | f(1);
	r = r + x;
	x = c % d + f(1) * 2;
	r = r + x;
	return r;
}
