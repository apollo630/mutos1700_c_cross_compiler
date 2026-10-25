#define	VEC_KBD	0x20		/* Vector for keyboard */

struct kbdcfg {
	int	kb_data;	/* data port */
	int	kb_ctl;		/* control port */
};
