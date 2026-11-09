/* L-10: 1e38 > -1e38 subtracts - the difference overflows: SIGFPE in
 * the original.
 * expect: original SIGFPE, corrected 1 */
main()
{
	double a, b;

	a = 1e38;
	b = -a;
	return a > b;
}
