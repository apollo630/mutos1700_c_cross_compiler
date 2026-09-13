/*
 * 10_integ/04_strrev.c
 *
 * Recursive in-place string reversal using pointer arithmetic and
 * strcpy()/strlen() from libc.
 */
char *strcpy();
int strlen();

swapch(a, b)
char *a, *b;
{
	char t;

	t = *a;
	*a = *b;
	*b = t;
}

reverse(s, lo, hi)
char *s;
int lo, hi;
{
	if (lo >= hi)
		return;
	swapch(&s[lo], &s[hi]);
	reverse(s, lo + 1, hi - 1);
}

main()
{
	char buf[20];
	int len;

	strcpy(buf, "mutos1700");
	len = strlen(buf);
	reverse(buf, 0, len - 1);
	return buf[0];
}
