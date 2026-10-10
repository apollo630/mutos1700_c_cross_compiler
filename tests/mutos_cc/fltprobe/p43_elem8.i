

























f(n)
{
	return n + 10;
}

g(n)
{
	return n * 20;
}

main()
{
	int b[4];
	int i, x, y, c, d, s, r;

	r = 0;
	b[0] = 1;
	b[1] = 6;
	b[2] = 3;
	b[3] = 12;
	i = 1;
	x = 5;
	y = 3;
	c = 17;
	d = 5;
	s = 2;
	x = f(1) * 2;
	r = r + x;
	x = f(1) * 4;
	r = r + x;
	x = f(1) * 8;
	r = r + x;
	x = c / d * 2;
	r = r + x;
	x = c % d * 2;
	r = r + x;
	x = c * d * 2;
	r = r + x;
	x = f(1) << 1;
	r = r + x;
	x = f(1) >> 1;
	r = r + x;
	x = c / d << 3;
	r = r + x;
	x = c % d >> 1;
	r = r + x;
	x = 5 - f(1);
	r = r + x;
	x = 50;
	x = x - c / d;
	r = r + x;
	x = x - c % d;
	r = r + x;
	x = c * 3 - f(1);
	r = r + x;
	x = b[i] - f(1);
	r = r + x;
	x = f(1) - b[i];
	r = r + x;
	x = c * d - x;
	r = r + x;
	x = c / d - x;
	r = r + x;
	x = c % d - x;
	r = r + x;
	x = 2;
	x = (y > 2) - (x < y);
	r = r + x;
	x = (y > 2) - (x < 3) * 2;
	r = r + x;
	x = 4;
	x = (x < y) + (y > x);
	r = r + x;
	x = (x < y) - (y < 9);
	r = r + x;
	x = f(1) | b[i];
	r = r + x;
	x = f(1) ^ b[i];
	r = r + x;
	x = f(1) & g(2);
	r = r + x;
	x = f(1) ^ g(2);
	r = r + x;
	x = c * 3 + c * 5;
	r = r + x;
	x = c * d + c * 3;
	r = r + x;
	y = y % (c / d - s);
	r = r + y;
	x = f(1) * 2 + c / d;
	r = r + x;
	x = f(1) * 3 - f(2);
	r = r + x;
	x = c * d + f(1) * 3;
	r = r + x;
	return r;
}
