/*
 * 04_funcs/05_staticvar.c
 *
 * A "static" local variable retains its value across calls; it must
 * be allocated in .data/.bss, never on the stack frame.
 */
counter()
{
	static int n;

	n = n + 1;
	return n;
}

main()
{
	int a, b, c;

	a = counter();
	b = counter();
	c = counter();
	return c;
}
