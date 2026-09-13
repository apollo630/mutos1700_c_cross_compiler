









main()
{
	char buf[300];

	buf[0] = 1;
	buf[300 - 1] = 2;
	return buf[0] + buf[300 - 1];
}
