







myseek(fd, offset, whence)
int fd;
long offset;
int whence;
{
	return fd + (int) offset + whence;
}

main()
{
	return myseek(3, 90000L, 1);
}
