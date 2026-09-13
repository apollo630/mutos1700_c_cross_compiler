/*
 * 05_arrptr/05_arrofptr.c
 *
 * An array of "char *", the same shape argv itself has in crt0.
 */
int strlen();

main()
{
	char *names[3];
	int i, total;

	names[0] = "one";
	names[1] = "two";
	names[2] = "three";

	total = 0;
	for (i = 0; i < 3; i = i + 1)
		total = total + strlen(names[i]);
	return total;
}
