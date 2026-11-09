/* L-05: (1 + 2**-30) * (1 + 2**-40) is 1 + 2**-30 + 2**-40 + 2**-54 in
 * the original, not the correctly rounded 1 + 2**-30 + 2**-40: b * a is
 * "fldd b" / "fmuld a", and dmul takes a, whose low mantissa word is not
 * 0, as [si]. (The powers of two, sums and quotients are exact.)
 * expect: original 0, corrected 1 */
main()
{
	double p30, p40, a, b, c;
	int i;

	p30 = 1.0;
	for (i = 0; i < 30; i++)
		p30 = p30 * 2.0;
	p40 = p30 * 1024.0;
	a = 1.0 + 1.0 / p40;
	b = 1.0 + 1.0 / p30;
	c = b * a;
	return c == 1.0 + 1.0 / p30 + 1.0 / p40;
}
