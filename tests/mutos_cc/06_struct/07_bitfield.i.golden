






struct flags {
	unsigned ready : 1;
	unsigned error : 1;
	unsigned mode  : 2;
	unsigned count : 4;
};

main()
{
	struct flags f;

	f.ready = 1;
	f.error = 0;
	f.mode = 2;
	f.count = 9;
	return f.ready + f.mode + f.count;
}
