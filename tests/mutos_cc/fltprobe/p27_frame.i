










f82()
{
	char buf[82];

	buf[0] = 1;
	buf[81] = 2;
	return buf[0] + buf[81];
}

f90()
{
	char buf[90];

	buf[0] = 1;
	buf[89] = 2;
	return buf[0] + buf[89];
}

f100()
{
	char buf[100];

	buf[0] = 1;
	buf[99] = 2;
	return buf[0] + buf[99];
}

f110()
{
	char buf[110];

	buf[0] = 1;
	buf[109] = 2;
	return buf[0] + buf[109];
}

f120()
{
	char buf[120];

	buf[0] = 1;
	buf[119] = 2;
	return buf[0] + buf[119];
}

f124()
{
	char buf[124];

	buf[0] = 1;
	buf[123] = 2;
	return buf[0] + buf[123];
}

f126()
{
	char buf[126];

	buf[0] = 1;
	buf[125] = 2;
	return buf[0] + buf[125];
}

f127()
{
	char buf[127];

	buf[0] = 1;
	buf[126] = 2;
	return buf[0] + buf[126];
}

main()
{
	return f82() + f90() + f100() + f110() + f120() + f124() + f126() +
	    f127();
}
