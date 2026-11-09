/* L-03: (int)-32768.0 compares with the word at address 8000 - SIGFPE
 * in the original (that word is 0 here).
 * expect: original SIGFPE, corrected 1 */
main()
{
	double d;
	int i;

	d = -32768.0;
	i = d;
	return i == -32768;
}
