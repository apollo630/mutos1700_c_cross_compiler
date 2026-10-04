/*
 * fltprobe/p31_long4.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-7 goldens, and the ones it still refuses. Inferred:
 * "l -= 1" and "--l" (p25's "l += 1" / "l--": "sub" / "sbb *0"?), "l +=
 * 300" (a constant that is no signed byte), "m = -l" (another variable's
 * negation), two longs under '&&', "l << 5" and "l >> 7" ("loop .-4"),
 * "l * 7", "m * i" (an int variable widened for "lmul"), a long as the
 * condition of a '?:' with a variable arm. Refused: "x = l++" (the value
 * of a long '++'), "l += 70000" (a constant that is no int), "l << i" (a
 * variable count), "x = !l" (a long's '!' as a value), "10 - (int) (l -
 * 4995)" (a constant minus a long's low word).
 * Result: 140.
 */
main()
{
	long l, m;
	int i, x, r;

	l = 70000;
	m = 5;
	i = 3;
	x = 8;
	r = 0;
	l -= 1;
	--l;
	r = r + (int) (l - 69990);
	l += 300;
	r = r + (int) (l - 70290);
	m = -l;
	r = r + (int) (m + 70300);
	if (l && m)
		r = r + 1;
	l = 70000;
	l = l << 5;
	r = r + (int) (l - 2239990);
	l = l >> 7;
	r = r + (int) (l - 17490);
	l = l * 7;
	r = r + (int) (l - 122490);
	m = 6;
	m = m * i;
	r = r + (int) m;
	r = r + (l ? x : 7);
	l = 300;
	x = l++;
	r = r + x - 290;
	l += 70000;
	r = r + (int) (l - 70291);
	l = 5;
	l = l << i;
	r = r + (int) l;
	x = !l;
	r = r + x;
	x = 10 - (int) (l - 35);
	r = r + x;
	return r;
}
