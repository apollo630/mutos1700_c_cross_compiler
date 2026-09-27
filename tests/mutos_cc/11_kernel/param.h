/*
 * tunable variables
 *
 */


/*
 * now the rest
 */
#define MAXMEM	64		/* max core per process - 64 2K pages */
#define SSIZE	1024		/* initial stack size (bytes) */
#define SINCR	1024		/* increment of stack (bytes) */
#define NOFILE	20		/* max open files per process */
#define CANBSIZ 256		/* max size of typewriter line */
#define HZ	20		/* Ticks/second of the clock */
#define DHZ	20		/* Ticks/second of the clock */
#define MSGBUFS 1024		/* Characters saved from error messages */
#define NCARGS	5120		/* # characters in exec arglist */
#define	MAXTTYS	9		/* Max # open ttys */
#define NIOSTAT 50              /* max number of bufs to keep stats for */

#define OLDAOUT 1               /* support old-format (non x.out) headers */
#define	EXIT
#define	SGTTY	1

/*
 * priorities
 * probably should not be
 * altered too much
 */

#define PSWP	0
#define PINOD	10
#define PRIBIO	20
#define PZERO	25
#define NZERO	20
#define PPIPE	26
#define PWAIT	30
#define PSLEP	40
#define PUSER	50

/*
 * signals
 * dont change
 */

#define NSIG	17
/*
 * No more than 16 signals (1-16) because they are
 * stored in bits in a word.
 */
#define SIGHUP	1	/* hangup */
#define SIGINT	2	/* interrupt (rubout) */
#define SIGQUIT 3	/* quit (FS) */
#define SIGINS	4	/* illegal instruction */
#define SIGTRC	5	/* trace or breakpoint */
#define SIGIOT	6	/* iot */
#define SIGEMT	7	/* emt */
#define SIGFPT	8	/* floating exception */
#define SIGKIL	9	/* kill, uncatchable termination */
#define SIGBUS	10	/* bus error */
#define SIGSEG	11	/* segmentation violation */
#define SIGSYS	12	/* bad system call */
#define SIGPIPE 13	/* end of pipe */
#define SIGCLK	14	/* alarm clock */
#define SIGTRM	15	/* Catchable termination */
#define	SIGFN	16	/* function key */

/*
 * fundamental constants of the implementation--
 * cannot be changed easily
 */

#define NBPW	sizeof(int)	/* number of bytes in an integer */
#define BSIZE	1024		/* size of secondary block (bytes) */
#define BSLOP	0		/* In case some device needs bigger buffers */
#define NINDIR	(BSIZE/sizeof(daddr_t))
#define BMASK	01777		/* BSIZE-1 */
#define BSHIFT	10		/* LOG2(BSIZE) */
#define NMASK	0377		/* NINDIR-1 */
#define NSHIFT	8		/* LOG2(NINDIR) */
#define	INOPB	(BSIZE/sizeof(struct dinode))	/* # inodes per block */
#define	LINOPB	4		/* LOG2(INOPB) */
#define USIZE	1		/* size of use block is 1 page */
#define UBASE	0xf000		/* abs. addr of user block is top page	*/
#define NULL	0
#define DCMASK  0               /* default mask for file creation */
#define NODEV	(dev_t)(-1)
#define ROOTINO ((ino_t)2)	/* i number of all roots */
#define SUPERB	((daddr_t)1)	/* block number of the super block */
#define DIRSIZ	14		/* max characters per directory */
#define NICINOD 100		/* number of superblock inodes */
#define NICFREE 100		/* number of superblock free blocks */
/* #define INFSIZE 138		   /* size of per-proc info for users */
#define CBSIZE	6		/* number of chars in a clist block */
#define CROUND	07		/* clist rounding: sizeof(int *) + CBSIZE - 1*/

/*
 * memory management parameters.
 */

#define MAXIMUM (NPAGEPS + USIZE)
#define MMPGSZ	2048		/* bytes per logical page in the memory */
#define LMMPGSZ 11		/* log2(MMPGSZ) */
#define NPAGEPS 32		/* There are 32 pages in a segment */
#define NSEG	0		/* max seg / user (see user.h) */

/*
 * Some macros for units conversion
 */

/* page number to segment register */
#define ptosr(x) (((unsigned)(x))<<7)

/* page number to long address */
#define pntol(x) (((long)((unsigned)(x)))<<LMMPGSZ)

/* pages to disk blocks */
#define ptod(x) ((x)*(MMPGSZ/BSIZE))

/* inumber to disk address */
#define itod(x) (daddr_t)(((unsigned)(x)+(INOPB+INOPB-1))>>LINOPB)

/* inumber to disk offset */
#define itoo(x) (int)(((x)+(INOPB+INOPB-1))&(INOPB-1))

/* pages to bytes */
#define ptob(x) ((x)<<LMMPGSZ)

/* bytes to pages */
#define btop(x) (((unsigned)(x)+(MMPGSZ-1))>>LMMPGSZ)

/* bytes to page number */
#define btopn(x) (((unsigned)(x))>>LMMPGSZ)

/* address (long (32 bit)) to page number (int)*/
#define atopn(x) ((int)(((long)(x))>>LMMPGSZ))

/* address (long (32 bit)) to page count (int)*/
#define atop(x)  ((int)(((long)(x)+(MMPGSZ-1))>>LMMPGSZ))

/* address (long (32 bit)) to offset (int) get bits LMMPGSZ-1 - 0 */
#define atoo(x) ((int)((x)&(MMPGSZ-1)))

/* long address to short address (get low 16 bits of long address */
#define atos(x) ((int)( (x) & 0x0000FFFF))

/* long address to short address (get low 16 bits of long address */
#define atoh(x) ((int)( (x) >> 16))

/* page register to long address */
#define ptol(x) ((long)((int)(x)&(MMPGSZ-1))<<LMMPGSZ)

/* major part of a device */
#define major(x)	(int)(((unsigned)(x)>>8))

/* minor part of a device */
#define minor(x)	(int)((x)&0377)

/* make a device number */
#define makedev(x,y)	(dev_t)((x)<<8 | (y))

typedef struct { int r[1]; } *	physadr;
typedef struct { unsigned short off;
		 unsigned short seg; }  segadr;
typedef long		daddr_t;
typedef char *		caddr_t;
typedef unsigned short	ino_t;
typedef long		time_t;
typedef int		label_t[5];	/* return, sp, bp, si, di */
typedef int		dev_t;
typedef long		off_t;

/*
 * Machine-dependent bits and macros
 */

#define	SPL0MASK	0x00		/* 0xC0 ==> SM on On-Board USART */
#define USERMODE(vec)	(((vec)&(SYSMODE)<<8) == 0)
#define BASEPRI(ps)	(((ps)&PS_PRIMASK)!=(SPL0MASK<<8))

/*
 * K1810WM86 simulated "state" definitions.
 */

#define	PS_USER		0x01		/* "user" mode */
#define	PS_SYS		0xFE		/* for mch.c, turns off user mode */
#define	PS_PRIMASK	0xFF00		/* current spl priority (PIC mask) */

/* trap bits and usermode bit in vector */
#define	ZERODIV		1
#define	TRACE		2
#define	PARITRAP	4
#define	OVFLOW		8
#define	BADTRAP		0x10
#define	SOFTINT		0x20
#define	HARDINT		0x40
#define	SYSMODE		0x80

extern	short	mm_pages[];		/* page list */
extern	short	mm_size;		/* # mem pages, initialized to NCOREL */
#define	MAXPAGE	 0x01FF		/* largest valid physical page number */
#define	MM_BSTACK	0x1000	/* top of temp boot stack */
#define	SEGKI	0x1000		/* Kernel CS */
#define	SEGKD	0x0000		/* Kernel DS */
#define	MM_SCALL 0x8800		/* output ==> system call */
#define	MM_SR	0x0080		/* system-call request */
#define	MM_TR	0x0040		/* trap occured */
#define LHISHIFT 16		/* hi  word of a long word */
#define	OFFUSRPG  0xF800
/*
 *  Configuration information
 */
struct sversion
{
	unsigned dl:8,
		 vl:4,
		 vh:4;
};

/* 
 * system shutdown flavors
 */
#define MONITOR  0
#define REBOOT   1
#define POWEROFF 2

/* 
 * ZVE K2771 reset via PPI 8255, port A, bit 5 
 * setting bit 5 = 1 (high) pulls sown /AUX-RESET
 */
#define MCHRESET 0x20         /* PPI port A, bit 5 set = reset */
