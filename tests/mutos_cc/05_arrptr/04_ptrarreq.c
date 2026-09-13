/*
 * 05_arrptr/04_ptrarreq.c
 *
 * Classic K&R equivalence: a[i] and *(a + i) must compile to the
 * same effective address; also demonstrates passing an array to a
 * function, which decays to a pointer.
 */
sumarr(a, n)
int a[];
int n;
{
	int i, s;

	s = 0;
	for (i = 0; i < n; i = i + 1)
		s = s + *(a + i);
	return s;
}

main()
{
	int v[4];

	v[0] = 1;
	v[1] = 2;
	v[2] = 3;
	v[3] = 4;
	return sumarr(v, 4);
}
