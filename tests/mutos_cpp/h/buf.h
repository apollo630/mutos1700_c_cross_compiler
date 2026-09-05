/*
 * Each buffer in the pool is usually doubly linked into 2 lists:
 * the device with which it is currently associated (always)
 * and also on a list of blocks available for allocation
 * for other use (usually).
 * The latter list is kept in last-used order, and the two
 * lists are doubly linked to make it easy to remove
 * a buffer from one list when it was found by
 * looking through the other.
 * A buffer is on the available list, and is liable
 * to be reassigned to another disk block, if and only
 * if it is not marked BUSY.  When a buffer is busy, the
 * available-list pointers can be used for other purposes.
 * Most drivers use the forward ptr as a link in their I/O
 * active queue.
 * A buffer header contains all the information required
 * to perform I/O.
 * Most of the routines which manipulate these things
 * are in bio.c.
 */
struct buf
{
	int	b_flags;		/* see defines below */
	struct	buf *b_forw;		/* headed by d_tab of conf.c */
	struct	buf *b_back;		/*  "  */
	struct	buf *av_forw;		/* position on free list, */
	struct	buf *av_back;		/*     if not BUSY*/
	dev_t	b_dev;			/* major+minor device name */
	unsigned b_bcount;		/* transfer count */
	union {                 /* always points to buffer area */
	    caddr_t b_addr;             /* as low order core address */
	    int *b_words;               /* as words for clearing */
	    struct filsys *b_filsys;    /* as superblocks */
	    struct dinode *b_dino;      /* as ilist */
	    daddr_t *b_daddr;           /* as indirect block */
	} b_un;
	daddr_t	b_blkno;		/* block # on device */
	char	b_xmem;			/* high order core address */
	char	b_error;		/* returned after I/O */
	unsigned int b_resid;		/* bytes not transferred after error */
	unsigned int b_cylin;		/* cylinder number for disk i/o queue */
};

extern struct buf buf[];		/* The buffer pool itself */
extern struct buf bfreelist;		/* head of available list */

/*
 * These flags are kept in b_flags.
 */
#define	B_WRITE	0	/* non-read pseudo-flag */
#define	B_READ	01	/* read when I/O occurs */
#define	B_DONE	02	/* transaction finished */
#define	B_ERROR	04	/* transaction aborted */
#define	B_BUSY	010	/* not on av_forw/back list */
#define	B_PHYS	020	/* Physical IO */
#define	B_WANTED 0100	/* issue wakeup when BUSY goes off */
#define	B_AGE	0200	/* delayed write for correct aging */
#define	B_ASYNC	0400	/* don't wait for I/O completion */
#define	B_DELWRI 01000	/* don't write till block leaves available list */
#define	B_TAPE 02000	/* this is magtape (no bdwrite, raw i/o at any loc) */
#define	B_PBUSY	04000
#define	B_PACK	010000
#define	B_PURGE	020000	/* bpurge() in progress--invalidate buf when released */

/*
 * special redeclarations for
 * the head of the queue per
 * device driver.
 */
#define	b_actf	av_forw
#define	b_actl	av_back
#define	b_active b_bcount
#define	b_errcnt b_resid

/*
 * collect io statistics
 */
#define	DISKMON	1

#ifdef	DISKMON
struct iostat {
	int	nbuf;
	long	nread;
	long	nreada;
	long	ncache;
	long	nwrite;
	long    bufcount[NIOSTAT];
	long    nswapb;
} io_info;
#endif

