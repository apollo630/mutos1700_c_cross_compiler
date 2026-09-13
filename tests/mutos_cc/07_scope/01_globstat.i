





int counter;
static int hidden;

bump()
{
	counter = counter + 1;
	hidden = hidden + 1;
	return counter;
}

main()
{
	bump();
	bump();
	return bump();
}
