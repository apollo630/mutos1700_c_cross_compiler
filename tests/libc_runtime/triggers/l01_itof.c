/* L-01: itof(1001) is 1001.0000152... in the original.
 * expect: original 0, corrected 1 */
main()
{
	int i;
	double d;

	i = 1001;
	d = i;
	return d == 1001.0;
}
