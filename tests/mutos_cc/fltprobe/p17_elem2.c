/*
 * fltprobe/p17_elem2.c
 *
 * No floating point: products and sums of two int elements subscripted
 * by a variable, after p14_axint's goldens showed the right one computed
 * first when it is at offset 0 ("a[i] * b[j]": its value pushed, "pop cx"
 * / "imul cx"; "a[i] & b[j]": its address pushed, "pop bx" / "and
 * di,(bx)") but after the left one, into SI, when it has an offset
 * (p10_elem's "return ps[i].c * qs[i].g" - "imul *12.(si)"). Does the
 * offset decide, or the context (a return there, an assignment here)?
 * "return a[i] * b[j]" and "s = ps[i].c * qs[i].d" tell. Then the other
 * operators v7 gives the same table entry as '&' ('+', '-', '|' - their
 * address pushed too?), '^' (another entry), a '+' chain, and a
 * comparison of two elements as a condition.
 * Result: 84.
 */
struct s8 {
	int a, b, c, d;
};

f1(a, b, i, j)
int *a, *b;
{
	return a[i] * b[j];
}

f2(i)
{
	struct s8 ps[2], qs[2];
	int s;

	ps[i].c = 3;
	qs[i].d = 4;
	s = ps[i].c * qs[i].d;
	return s;
}

main()
{
	int a[3], b[3];
	int i, j, x, s, r;

	i = 1;
	j = 2;
	x = 20;
	a[1] = 6;
	b[2] = 3;
	r = f1(a, b, i, j);
	r = r + f2(i);
	s = a[i] + b[j];
	r = r + s;
	s = a[i] - b[j];
	r = r + s;
	s = a[i] | b[j];
	r = r + s;
	s = a[i] ^ b[j];
	r = r + s;
	s = a[i] + b[j] + x;
	r = r + s;
	if (a[i] > b[j])
		r = r + 1;
	return r;
}
