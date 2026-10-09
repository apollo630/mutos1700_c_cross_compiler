




















main()
{
	long l, m;
	int i, x, r;

	r = 0;
	l = 300;
	m = 7;
	i = 3;
	l <<= 1;
	r = r + (int) (l - 590);
	l <<= i;
	r = r + (int) (l - 4790);
	l >>= i;
	r = r + (int) (l - 590);
	l += 5L;
	r = r + (int) (l - 600);
	l -= 70000;
	r = r + (int) (l + 69400);
	l = 1000;
	l /= i;
	r = r + (int) (l - 300);
	l %= m;
	r = r + (int) l;
	l = 50;
	l %= 7;
	r = r + (int) l;
	l = 6;
	l *= m;
	r = r + (int) l;
	l = 100;
	x = (int) (l / m);
	r = r + x;
	x = (int) (l % 7);
	r = r + x;
	l = 40;
	l = (l + m) - 3;
	r = r + (int) l;
	l = (l - m) + 70000;
	r = r + (int) (l - 70000);
	l = l * m - 1;
	r = r + (int) (l - 490200);
	l = l / m + l;
	r = r + (int) (l - 560200);
	l = 40;
	l = 5L - l;
	r = r + (int) (l + 50);
	l = -5L - l;
	r = r + (int) (l + 20);
	l = m + -i;
	r = r + (int) l;
	l = m - -i;
	r = r + (int) l;
	l = 40;
	l = (l - m) + 3;
	r = r + (int) l;
	l = 5 - l;
	r = r + (int) (l + 40);
	l = l * m * 2;
	r = r + (int) (l + 500);
	l <<= 0;
	r = r + (int) (l + 400);
	return r;
}
