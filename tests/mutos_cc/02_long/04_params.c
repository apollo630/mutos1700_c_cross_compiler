/*
 * 02_long/04_params.c
 *
 * A function with a mix of int and long parameters, to check that a
 * long argument consumes exactly two stack words and that later
 * parameters shift accordingly (MUTOS_C_ABI.md sect. 1.3, modelled
 * directly on the real _lseek(fd, offset, whence) example).
 */
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
