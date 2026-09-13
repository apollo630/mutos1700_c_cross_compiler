







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
