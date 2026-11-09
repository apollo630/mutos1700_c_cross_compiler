/* L-12: big places the floating-point stack (fpstk, 64 bytes, a common
 * after the program's own) at 7fd0, across 8000: the first push
 * overflows - SIGFPE in the original. Linked with -i, the data segment
 * its own: 32394 is 7fd0 less big's address 0146.
 * cc: -i
 * expect: original SIGFPE, corrected 1 */
char big[32394];

main()
{
	double d;

	d = 2.0;
	d = d * d;
	return d == 4.0;
}
