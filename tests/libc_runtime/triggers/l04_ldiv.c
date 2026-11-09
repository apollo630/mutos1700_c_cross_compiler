/* L-04: 99999L / 100000 is 2 in the original.
 * expect: original 2, corrected 0 */
main()
{
	long l, m;

	l = 99999L;
	m = 100000L;
	return (int)(l / m);
}
