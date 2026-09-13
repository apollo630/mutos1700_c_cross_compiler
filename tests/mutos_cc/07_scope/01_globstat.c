/*
 * 07_scope/01_globstat.c
 *
 * A plain external global and a file-scope "static" global -- the
 * static one must never appear in the .globl symbol list.
 */
int counter;
static int hidden;

bump()
{
	counter = counter + 1;
	hidden = hidden + 1;
	return counter;
}

main()
{
	bump();
	bump();
	return bump();
}
