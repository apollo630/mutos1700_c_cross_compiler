#include "param.h"
#include "dir.h"
#include "a.out.h"
#include "user.h"

mmread(dev)
{
	register y,x;
	char *a;
	int b,c;
	int ds;
	y = minor(dev);
	if(y == 2)
		return;
	if(y == 3)
	{
		x = (int)u.u_offset;
		while(passc(y = inb(x)) >= 0)
			;
		u.u_offset = (long)x;
		return;
	}
	else if(y == 4)
	{
		x = (int)u.u_offset;
		do
		    passc(major(y = in(x)));
		while(passc(y) >= 0)
			;
		u.u_offset = (long)x;
		return;
	}
	ds = 0;
	if(u.u_offset&0xffff0000)
	{
		x = u.u_offset % (long)MMPGSZ;
		ds = u.u_offset / (long)MMPGSZ;
		ds = ptosr(ds);
		u.u_offset = x;
	}
	do
	    {
#ifdef MMU
		x = atopn(u.u_offset);
		if(minor(dev) == 1)
			x &= MAXKPAGE;
		x &= MAXPAGE;
		c = spl7();
		a = mapwork(x) + ((int)u.u_offset&0x7ff);

		y = (int)*a;
		splx(c);
#else
		if(ds)
		{
			c = spl7();
			y = getbyte(ds,(int)u.u_offset);
			splx(c);
		}
		else
		{
			a = (int)(u.u_offset);
			y = (int)*a;
		}
#endif MMU
		if(u.u_error)
			return;
	}
	while(passc(y) >= 0);
}


mmwrite(dev)
{
	register y,x;
	char *a;
	int b,c,d;
	int ds;
	y = minor(dev);
	if(y == 2)
	{
		u.u_count = 0;
		return;
	}
	if(y == 3)
	{
		x = (int)u.u_offset;
		while((y = cpass()) >= 0)
			if(u.u_error == 0)
				outb(x,y);
			else
				break;
		u.u_offset = (long)x;
		return;
	}
	else if(y == 4)
	{
		x = (int)u.u_offset;
		while((y = cpass()) >= 0)
		{
			if(u.u_error ==0)
				if((d = cpass()) >= 0)
					if(u.u_error == 0)
						out(x,makedev(y,minor(d)));
					else
						break;
				else
					break;
			else
				break;
		}
		u.u_offset = (long)x;
		return;
	}
	ds = 0;
	if(u.u_offset&0xffff0000)
	{
		x = u.u_offset % (long)MMPGSZ;
		ds = u.u_offset / (long)MMPGSZ;
		ds = ptosr(ds);
		u.u_offset = x;
	}
	while((y = cpass()) >= 0)
	{
		if(u.u_error == 0)
		{
#ifdef MMU
			x = atopn(u.u_offset);
			if(minor(dev) == 1)
				x &=MAXKPAGE;
			x &= MAXPAGE;
			c = spl7();
			a = mapwork(x) + ((int)u.u_offset&0x7ff);
			*a = (char)y;
			splx(c);
#else
		if(ds)
		{
			c = spl7();
			setbyte(ds,(int)u.u_offset-1,(char)y);
			splx(c);
		}
		else
		{
			a = (int)(u.u_offset) - 1;
			*a = (char)y;
		}
#endif MMU
		}
		else
			return;
	}
}
