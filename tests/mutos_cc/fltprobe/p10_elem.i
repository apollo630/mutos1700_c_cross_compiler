













struct s8 {
	int a, b, c, d;
};
struct s16 {
	int a, b, c, d, e, f, g, h;
};

sub(i)
{
	struct s8 ps[2];
	struct s16 qs[2];

	ps[i].c = 3;
	qs[i].g = 4;
	return ps[i].c * qs[i].g;
}

main()
{
	double arr[3], d;
	float fa[3];
	int i, j, r;

	i = 1;
	j = 2;
	r = 0;
	arr[0] = 0.5;
	arr[i] = 2.5;
	arr[j] = 4.0;
	d = 1.5 + arr[i];
	d = d - arr[j];
	if (d < arr[i])
		r = r + 1;
	if (arr[i] < arr[j])
		r = r + 2;
	d = arr[i] + arr[j];
	fa[j] = d;
	fa[i] = arr[j] - fa[j];
	d = fa[i] * arr[i];
	r = r + (int) d;
	r = r + sub(i);
	return r + sub(0);
}
