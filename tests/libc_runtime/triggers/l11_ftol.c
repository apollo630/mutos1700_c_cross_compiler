/* L-11: (long)1.5e9 raises SIGFPE in the original (from 2**30).
 * expect: original SIGFPE, corrected 1 */
main()
{
	double d;
	long l;

	d = 1.5e9;
	l = d;
	return l == 1500000000L;
}
