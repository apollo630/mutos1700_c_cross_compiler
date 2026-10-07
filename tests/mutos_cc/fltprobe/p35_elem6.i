



















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
	int b[4], *ip;
	int i, x, y, c, d, r;

	r = 0;
	b[0] = 1;
	b[1] = 2;
	b[2] = 3;
	b[3] = 4;
	ip = &b[2];
	i = 1;
	x = 5;
	y = 3;
	c = 17;
	d = 5;
	*ip -= y;
	r = r + b[2];
	*ip |= b[i];
	r = r + b[2];
	*ip &= x + 1;
	r = r + b[2];
	*ip += f(1);
	r = r + b[2];
	b[i] += f(1);
	r = r + b[1];
	x += y * 2;
	r = r + x;
	x -= b[i];
	r = r + x;
	x = 7;
	x &= b[i] + 1;
	r = r + x;
	x = x + f(1) + y;
	r = r + x;
	x = f(1) + g(2) + y + f(3);
	r = r + x;
	x = f(1) + c / d;
	r = r + x;
	x = c / d + f(2);
	r = r + x;
	x = f(1) + c % d;
	r = r + x;
	x = c / d + c / y;
	r = r + x;
	x = (y > 2) + (y < 9) * 2 + y;
	r = r + x;
	x = (y > 2) + (x < 3);
	r = r + x;
	x = c % d + c / d;
	r = r + x;
	x = c * d + f(1);
	r = r + x;
	x = f(1) - g(2);
	r = r + x;
	x = f(1) * g(2);
	r = r + x;
	x = b[i] + f(1);
	r = r + x;
	x = f(1) + g(2) * 3;
	r = r + x;
	return r;
}
