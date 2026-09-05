#define NLP 1				/* Number of line printers */
#define SPL spl5			/* raised interrupt priority */
#define	LPPRI	(PZERO+1)		/* allow sleepers to wakeup early */
#define LPUNIT(dev) (minor(dev) >> 3)	/* macro to obtain device unit number */

/*
 *	Markers
 */
#define	LPLWAT	50	/* line printer low water mark */
#define	LPHWAT	75	/* line printer high water mark */
#define MAXCOL	132	/* page width */
#define MAXLINE 72	/* page length (if device doesn't support '\f') */

/*
 *	States
 */
#define	OPEN	01	/* device is open */
#define	ASLP	02	/* awaiting draining of printer */
#define	TOUT	04	/* the device needed a timeout for a terminator */
#define PALIVE	010	/* board alive indicator */

/*
 *	Hardware constants
 */
#define	SELINP	0x8	/* select input */
#define	INIPR	0x4	/* init printer */
#define PINIT	0xaa	/* KR580WW55A command word for port init */
#define INTRON	0x05	/* enable KR580WW55A generation of an intr */
#define TEST	0xaa	/* test pattern read back by probe */
#define READY	0x02	/* printer online bit active high */

#ifndef M1834
#define NOPAPER 0x08	/* paper out bit active high */
#define BUSY	0x01
#define ONSTROBE 7
#define OFFSTROBE 6
#else
#define NOPAPER 0x20	/* paper out bit active high */
#define BUSY	0x80
#define	ERROR	0x8
#define ONSTROBE 0x1d
#define OFFSTROBE 0x1c
#endif

#define HACK    0x88    /* hardware acknowledge bit active high */

/*
 *	Flags
 */
#define CAP	1		/* minor number for caps lock */
#define OPT	2		/* minor number for printer optimzer */
#define RAW	4		/* minor number for raw char printer */
/*
 *	Device Structures
 */
struct clist {
	int	c_cc;			/* character count */
	char	*c_cf;			/* pointer to the first character */
	char	*c_cl;			/* pointer to the last character */
};

struct lpcfg {
		int p_level;		/* intr level */
		int p_porta;		/* data out */
		int p_portb;		/* control port */
#ifndef M1834
		int stport;		/* status port C */
		int control;
#else
		int p_portc;		/* status port c */
#endif
};

struct lp_softc {
	struct	clist lp_outq;
	int	lp_physcol;
	int	lp_logcol;
	int	lp_phline;
	int	lp_lpchar;
	int	lp_state;
	int	lp_flags;
	int	lp_addr;
	int	lp_toflg;
	struct	buf	*lp_buffer;
};
