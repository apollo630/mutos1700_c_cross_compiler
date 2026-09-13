






main()
{
	int x, r;

	x = 2;
	r = 0;
	switch (x) {
	case 1:
		r = 10;
		break;
	case 2:
	case 3:
		r = 20;
		
	case 4:
		r = r + 1;
		break;
	default:
		r = -1;
		break;
	}
	return r;
}
