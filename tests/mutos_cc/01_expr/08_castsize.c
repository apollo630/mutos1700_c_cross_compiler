/*
 * 01_expr/08_castsize.c
 *
 * Explicit type casts and sizeof on a handful of types.
 */
main()
{
	int i;
	long l;
	char c;
	int s1, s2, s3, s4;

	l = 70000;
	i = (int) l;
	c = (char) i;
	l = (long) c;

	s1 = sizeof(int);
	s2 = sizeof(char);
	s3 = sizeof(long);
	s4 = sizeof(i);
	return s1 + s2 + s3 + s4;
}
