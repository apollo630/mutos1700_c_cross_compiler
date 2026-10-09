
























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
	int i, x, y, c, d, r;

	r = 0;
	b[0] = 1;
	b[1] = 2;
	b[2] = 3;
	b[3] = 4;
	i = 1;
	x = 5;
	y = 3;
	c = 17;
	d = 5;
	x = c % d + c % y;
	r = r + x;
	x = f(1) - c / d;
	r = r + x;
	x = c / d - f(1);
	r = r + x;
	x = c % d - c / d;
	r = r + x;
	x = f(1) - c % d;
	r = r + x;
	x = f(1) + c * d;
	r = r + x;
	x = f(1) + b[i];
	r = r + x;
	x = (y > 2) + (x <= 3) * 4;
	r = r + x;
	x = (y > 2) + (5 < x);
	r = r + x;
	x = (y < 2) + (y > 1) * 2 + (x == 3);
	r = r + x;
	x = g(2) * 3 + f(1);
	r = r + x;
	x = c % d + f(1);
	r = r + x;
	x = f(2) * f(3) * 2;
	r = r + x;
	x = x - f(1);
	r = r + x;
	x = f(1) - x;
	r = r + x;
	x = c * f(1) + d;
	r = r + x;
	x = (y > 2) + (x < y);
	r = r + x;
	x = f(1) & b[i];
	r = r + x;
	x = c * d + c * y;
	r = r + x;
	x = c * d - f(1);
	r = r + x;
	x = f(1) + c * 3;
	r = r + x;
	x = (y > 2) - (x < 3);
	r = r + x;
	x = f(1) + g(2) - f(3);
	r = r + x;
	x = f(1) | g(2);
	r = r + x;
	return r;
}
