



















main()
{
	long l, m;
	int i, x, r;

	r = 0;
	i = 3;
	l = 300;
	m = 7;
	l <<= 1;
	r = r + (int) (l - 590);
	l >>= 1;
	r = r + (int) (l - 290);
	l <<= 0;
	r = r + (int) (l - 290);
	l >>= 0;
	r = r + (int) (l - 290);
	l = -300;
	l >>= 1;
	r = r + (int) (l + 160);
	l <<= 1;
	r = r + (int) (l + 290);
	l <<= i;
	r = r + (int) (l + 2390);
	l >>= i;
	r = r + (int) (l + 290);
	l = 40;
	l += 5L;
	r = r + (int) l;
	l -= 5L;
	r = r + (int) l;
	l += 300L;
	r = r + (int) (l - 300);
	l -= 300;
	r = r + (int) l;
	l = 5L - l;
	r = r + (int) (l + 30);
	l = 5 - l;
	r = r + (int) l;
	l = 300 - l;
	r = r + (int) (l - 200);
	l = -5L - l;
	r = r + (int) (l + 300);
	l = 40;
	l = (l - m) + 3;
	r = r + (int) l;
	l = (l + m) + 300;
	r = r + (int) (l - 300);
	l = l * m + 3;
	r = r + (int) (l - 300);
	l = l / m + 7;
	r = r + (int) l;
	l = l * m * 2;
	r = r + (int) (l - 800);
	l = 40;
	l = l * m * 3;
	r = r + (int) (l - 800);
	l = l / m * 2;
	r = r + (int) l;
	l = (l * m) / 3;
	r = r + (int) l;
	l = (l * m) % 5;
	r = r + (int) l;
	l = 40;
	l = l * m + l;
	r = r + (int) l;
	l = 40;
	x = (int) ((l - m) + 3);
	r = r + x;
	l = (l + m) * 2;
	r = r + (int) l;
	x = (int) (l * m * 2);
	r = r + x;
	return r;
}
