struct cblock {
	struct cblock *c_next;
	char	c_info[CBSIZE];
};

extern struct cblock cfree[];
