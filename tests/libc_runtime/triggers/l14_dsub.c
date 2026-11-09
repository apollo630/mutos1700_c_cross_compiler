/* L-14: 2 - 1.5 * 2**-56 is 2 in the original (an operand 57 binary
 * orders below a power of two dropped); the double below 2 is right.
 * expect: original 0, corrected 1 */
main()
{
	double x;
	int i;

	x = 3.0;
	for (i = 0; i < 57; i++)
		x = x / 2.0;
	return 2.0 - x < 2.0;
}
