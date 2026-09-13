/*
 * 01_expr/04_shift.c
 *
 * Left/right shift by a variable amount and by a constant amount.
 * On plain 8086 a variable shift count must be loaded into CL; this
 * is the base case a later -mv30 build could replace with the
 * 80186 shift-by-immediate-count opcode, but for now this targets
 * plain 8086 only.
 */
main()
{
	int a, n, r;

	a = 1;
	n = 4;
	r = a << n;
	r = r >> 2;
	r = a << 1;
	return r;
}
