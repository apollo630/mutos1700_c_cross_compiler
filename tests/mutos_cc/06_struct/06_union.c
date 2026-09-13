/*
 * 06_struct/06_union.c
 */
union number {
	int i;
	char b[2];
};

main()
{
	union number n;

	n.i = 0x0102;
	return n.b[0] + n.b[1];
}
