






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
