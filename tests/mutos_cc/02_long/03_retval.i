






long
addlong(a, b)
long a, b;
{
	return a + b;
}

main()
{
	long r;

	r = addlong(100000L, 5L);
	return (int) r;
}
