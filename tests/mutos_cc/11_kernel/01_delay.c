#define LOOPV	192

delay(d)
{
	int i;
	while(d--)
	{
		i=0;
		while(i < LOOPV)
			i++;
	}
}
