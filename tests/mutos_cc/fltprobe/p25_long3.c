/*
 * fltprobe/p25_long3.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-6 goldens, and the ones it still refuses. Inferred: "l |
 * m" and "l ^ m" of two variables (the table entry of p22's "l & m"), "x
 * = (int) (l + 5)" (p22 assigned "(int) (l - 5)"), "l += 1" (the
 * constant widened by "mov ax,*1." / "cwd"?), "l = c" (a char widened),
 * "l / 7" (an int constant divided by - widened like "l / i"'s i?), "l +
 * i * j" (a computed int widened). Refused: "l << 3" and "l >> 4" (an int
 * shifts by CL from 3 up - and a long?), "10 - (int) (l - 4995)", "-l",
 * a long under '&&', '||' and as the condition of '?:' ("if (l)" and "!l"
 * came back in round 6), "l++", "++l", "l--", "m = l = 5", "l * 3".
 * Result: 153.
 */
main()
{
	long l, m;
	int i, j, x, r;
	char c;

	l = 70000;
	m = 9;
	i = 1;
	j = 2;
	c = 3;
	r = 0;
	l = l | m;
	r = r + (int) (l - 69990);
	l = l ^ m;
	r = r + (int) (l - 69990);
	x = (int) (l + 5);
	r = r + x - 4460;
	l += 1;
	r = r + (int) (l - 69990);
	l = c;
	r = r + (int) l;
	l = 70000;
	l = l / 7;
	r = r + (int) (l - 9990);
	l = l + i * j;
	r = r + (int) (l - 9990);
	l = l << 3;
	r = r + (int) (l - 80000);
	l = l >> 4;
	r = r + (int) (l - 4990);
	r = r + 10 - (int) (l - 4995);
	l = -l;
	r = r + (int) (l + 5010);
	if (l && i)
		r = r + 1;
	if (l || j)
		r = r + 1;
	r = r + (l ? 2 : 50);
	l++;
	++l;
	l--;
	r = r + (int) (l + 5010);
	m = l = 5;
	r = r + (int) m + (int) l;
	l = l * 3;
	r = r + (int) l;
	return r;
}
