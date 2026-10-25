/*
 *      wf.h
 *	Hard disk and floppy Driver declarations.
 *
 */


#define	UNIT(dev)	((minor(dev)>>0)&0x01)	/* dev -> unit# map */
#define	DRTAB(dev)	((minor(dev)>>1)&0x0f)	/* dev -> drtab-index map */
#define	PARTITION(dev)	((minor(dev)>>5)&0x07)	/* dev -> partition-index map */
#define F_SINGLE	0
#define F_DOUBLE	1

/*
 * Partition structure.  One per drtab[] entry.
 */

struct	wfpart {
	daddr_t	p_fsec;			/* first sector */
	daddr_t	p_nsec;			/* number sectors */
};


struct	wfcfg	{
	unsigned	c_wua;
	char		c_devcod;
	char		c_level;
	struct wfcdrt	*c_drtab[8];
};

struct	wfcdrt {
	unsigned	cdr_ncyl;	/* # cylinders */
	char		cdr_nfhead;	/* # fixed heads */
	char		cdr_nrhead;	/* # removable heads */
	char		cdr_nsec;	/* # sectors per track */
	unsigned	cdr_secsiz;	/* sector-size */
	char		cdr_nalt;	/* # alternate cylinders */
	struct wfpart	*cdr_part;	/* partition table pointer */
};


/*
 * Volume structure. Get cylinders, haeds, sectors, sector size,
 * first sector and number of sectors for a specified file
 * system device (raw interface) by the system call
 * ioctl(fildes,WF_VOL,(caddr_t)wfvol)
 */

struct	wfvol {
	struct wfcdrt	v_cdrt;
	struct wfpart	v_part;
};


struct	wfftk	{
	int	f_track;		/* track # */
	int	f_intl;			/* interleave factor */
	int	f_skew;			/* track skew */
	char	f_type;			/* format type-code */
	char	f_pat[4];		/* pattern data */
};

#define	B_FORMAT	040000		/* "new" buf.h flag: must NOT overlap buf.h! */
#define	DEV5FLPY	3		/* 5.25" Floppy */

/*
 * Misc Format definitions, for wfftk.f_type.
 */

#define	FORMAT_DATA		0x00	/* format data track */
#define	FORMAT_BAD		0x80	/* format bad track */

/*
 *  Ioctl mnemonics.
 */

#define	WF_IOC_FMT	(('W'<<8)|0)
#define WF_VOL		(('V'<<8)|0)
