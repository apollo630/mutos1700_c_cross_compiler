/*
 * 05_arrptr/07_strlibc.c
 *
 * String handling via the already-ported MUTOS 1700 libc
 * (strlen/strcpy/strcmp are present in tests/mutos1700_libc/).
 * Pointer-returning library functions MUST be declared before use in
 * K&R, otherwise the compiler assumes a plain "int" return.
 */
char *strcpy();
int strlen(), strcmp();

main()
{
	char src[20];
	char dst[20];
	int len, cmp;

	strcpy(src, "hello, mutos");
	strcpy(dst, src);
	len = strlen(dst);
	cmp = strcmp(src, dst);
	return len + cmp;
}
