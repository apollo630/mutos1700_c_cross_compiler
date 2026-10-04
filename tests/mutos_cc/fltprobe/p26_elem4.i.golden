
















struct s8 {
	int a, b, c, d;
};

int g;

main()
{
	struct s8 ps[2];
	int b[4], *ip;
	int i, j, x, r;

	i = 1;
	j = 2;
	x = 5;
	r = 0;
	g = 7;
	b[0] = 1;
	b[1] = 4;
	b[2] = 9;
	b[3] = 0;
	ip = &b[1];
	ps[1].c = 3;
	x = x * *ip;
	r = r + x;
	x = *ip * j;
	r = r + x;
	if (b[i])
		r = r + 1;
	if (b[j] != 0)
		r = r + 1;
	x = b[j] > 0;
	r = r + x;
	if (b[i] > 0 && b[j] > 5)
		r = r + 1;
	if (ps[i].c)
		r = r + 1;
	if (ps[i].c > 2)
		r = r + 1;
	x = b[i] * b[j] + b[j - 1];
	r = r + x;
	x = g * b[j];
	r = r + x;
	x = b[j] - g;
	r = r + x;
	x = (b[i] + 1) * b[j];
	r = r + x;
	b[3] = b[j] + b[0];
	r = r + b[3];
	x = b[j] / b[i];
	r = r + x;
	x = b[j] % b[i];
	r = r + x;
	r = r + (b[i] < b[j]) + (b[i] > b[j]) * 2 + (b[i] == b[j]) * 4;
	b[i] += b[j];
	b[j] -= x;
	b[0] *= b[i];
	*ip += 2;
	ps[i].c += x;
	r = r + b[0] + b[1] + b[2] + ps[i].c;
	return r;
}
