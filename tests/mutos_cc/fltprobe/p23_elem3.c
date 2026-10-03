/*
 * fltprobe/p23_elem3.c
 *
 * No floating point: element shapes left open by the round-5 goldens
 * (p17_elem2: a comparison of two elements at offset 0 pushes the LEFT
 * one's address - "cmp (bx),di"; p19_open3: "x - b[j]" pushes b[j]'s,
 * "b[j - 2]" with DI and SI taken indexes through DX). Open: a left
 * element with an offset compared with one at offset 0 ("ps[i].c >
 * b[j]"), the reverse ("a[i] > ps[i].c" - 02_bubsort's "cmp di,*N.(si)"?),
 * "x * b[j]" (pushed as "a[i] * b[j]" is?), "x - *p" with a leaf on the
 * left, a constant minus an element, a file-scope variable minus one,
 * "b[j + 1]" of a local array with DI free (inferred: "lea di,b" /
 * "mov si,j" / "sal si,*1" / "add di,si" / "*2.(di)"), "a[i] + b[j +
 * 1]", two-element comparisons as values and under "&&".
 * Result: 136.
 */
struct s8 {
	int a, b, c, d;
};

int g;

main()
{
	struct s8 ps[2];
	int a[4], b[4], *p;
	int i, j, x, s, r;

	i = 1;
	j = 2;
	x = 20;
	g = 30;
	r = 0;
	a[1] = 6;
	b[2] = 3;
	b[3] = 9;
	ps[1].c = 5;
	if (ps[i].c > b[j])
		r = r + 1;
	if (a[i] > ps[i].c)
		r = r + 2;
	s = x * b[j];
	r = r + s;
	p = &b[j];
	s = x - *p;
	r = r + s;
	s = 5 - b[j];
	r = r + s;
	s = g - b[j];
	r = r + s;
	s = b[j + 1];
	r = r + s;
	s = a[i] + b[j + 1];
	r = r + s;
	s = (a[i] < b[j]) + (a[i] > b[j]) * 2;
	r = r + s;
	if (a[i] > b[j] && b[j] > 0)
		r = r + 1;
	return r;
}
