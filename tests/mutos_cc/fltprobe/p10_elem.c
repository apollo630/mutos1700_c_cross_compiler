/*
 * fltprobe/p10_elem.c
 *
 * Elements of floating arrays subscripted by a variable, where no golden
 * shows them yet (p8_misc has "arr[i] + 0.5" and "arr[i] = d" only): the
 * second operand after a variable or constant ("1.5 + arr[i]", "d -
 * arr[i]" - the first loaded before the element's address?), two in one
 * expression ("arr[i] + arr[j]", "arr[i] < arr[j]" - the first loaded
 * before the second's address?), compared with a double ("d < arr[i]"),
 * a float array's element as target and operand; then, in sub(), arrays
 * of 8- and 16-byte structs subscripted by a variable ("mov cx,*3." /
 * "sal si,cl", "mov cx,*4."?). mutos_c1 compiles these by inference.
 * Result: 21.
 */
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
