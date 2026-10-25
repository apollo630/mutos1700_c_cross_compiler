struct map
{
	short	m_size;
	unsigned short m_addr;
};

extern struct map swapmap[];    /* space for swap allocation */
extern struct map coremap[];	/* space for core allocation */
