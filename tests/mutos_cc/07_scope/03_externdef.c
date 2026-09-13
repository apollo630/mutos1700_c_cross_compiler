/*
 * 07_scope/03_externdef.c
 *
 * An "extern" forward declaration of a global that is defined later
 * in the same file -- a genuine K&R idiom, still legal even though
 * declared before its tentative definition.
 */
extern int total;

addto(n)
int n;
{
	total = total + n;
	return total;
}

int total;

main()
{
	addto(3);
	addto(4);
	return addto(5);
}
