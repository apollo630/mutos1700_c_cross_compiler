/* L-08: the value of l %= 70000 (l 1000000) is not 20000 in the
 * original: alrem leaves 65536 in DX:AX.
 * expect: original 0, corrected 1 */
main()
{
	long l, m;
	int i;

	l = 1000000L;
	m = 70000L;
	i = (l %= m);
	return i == 20000;
}
