/*
 * 04_funcs/07_funcptr.c
 *
 * A pointer to a function, and calling through it.
 */
square(x)
int x;
{
	return x * x;
}

cube(x)
int x;
{
	return x * x * x;
}

apply(f, x)
int (*f)();
int x;
{
	return (*f)(x);
}

main()
{
	int (*fp)();
	int r;

	fp = square;
	r = apply(fp, 5);
	fp = cube;
	r = apply(fp, 3);
	return r;
}
