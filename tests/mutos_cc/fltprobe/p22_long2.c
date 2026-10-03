/*
 * fltprobe/p22_long2.c
 *
 * No floating point: 'long' shapes left open by the round-5 goldens
 * (p19_open3 settled "l + i", "l > i", "l == i", "l += i", "l = u", "m &
 * 255", "u > 39999"). Inferred by mutos_c1: "l + 1" / "l - 2" (an int
 * constant widened - "c + 1L"'s "cwd" / "push ax" / "push dx"?), "l ^ 7",
 * "l -= i", "x = 40000" into an int, "l < 0L", "l >= 0L", "x = l > 0L" as
 * a value, "i < l" as written. Refused: "l - i", "i - l", "l *= i", "l /
 * i", "l % i", "l > 2" and "l > 0" (an int constant widened - v7's
 * longrel() tests an ITOL of 0 with "tst"), "if (l)", "!l", "u < l", "l >
 * m", "l == m", "-100000" (LCON, NEG?), "~l", "l << 2", "l >> 1", "x ? l :
 * m", "x = (int) (l - 5)", "(long) u", "l & m".
 * Result: 83.
 */
main()
{
	long l, m;
	int i, x, r;
	unsigned u;

	l = 100000;
	m = 3;
	i = 7;
	r = 0;
	l = l - i;
	r = r + (int) (l - 99990);
	l = i - l;
	r = r + (int) (l + 99990);
	l = 100000;
	l = l + 1;
	l = l - 2;
	r = r + (int) (l - 99990);
	l = l ^ 7;
	r = r + (int) (l - 99990);
	l -= i;
	r = r + (int) (l - 99980);
	l *= i;
	r = r + (int) (l - 699890);
	l = l / i;
	r = r + (int) (l - 99980);
	l = l % i;
	r = r + (int) l;
	x = 40000;
	r = r + x + 25536;
	l = 5;
	if (l < 0L)
		r = r + 100;
	if (l >= 0L)
		r = r + 1;
	x = l > 0L;
	r = r + x;
	if (i < l)
		r = r + 100;
	if (l > 2)
		r = r + 1;
	if (l > 0)
		r = r + 1;
	if (l)
		r = r + 1;
	if (!l)
		r = r + 100;
	u = 4;
	if (u < l)
		r = r + 1;
	if (l > m)
		r = r + 1;
	if (l == m)
		r = r + 100;
	l = -100000;
	r = r + (int) (l + 100005);
	l = ~l;
	r = r + (int) (l - 99990);
	l = l << 2;
	r = r + (int) (l - 399990);
	l = l >> 1;
	r = r + (int) (l - 199990);
	x = 0;
	l = x ? l : m;
	r = r + (int) l;
	x = (int) (l - 5);
	r = r + x;
	u = 50000;
	l = (long) u;
	r = r + (int) (l - 49990);
	l = l & m;
	r = r + (int) l;
	return r;
}
