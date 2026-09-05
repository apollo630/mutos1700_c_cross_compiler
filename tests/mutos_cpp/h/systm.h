/*
 * Random set of variables
 * used by more than one
 * routine.
 */
char	canonb[CANBSIZ];	/* buffer for erase and kill (#@) */
struct inode *rootdir;		/* pointer to inode of root directory */
struct proc *runq;		/* head of linked list of running processes */
struct proc *Hogproc;           /* if non-NULL, schedule only this process */
struct	inode *inode86;		/* ip of .86 - file to be loaded */
unsigned dbreak;			/* flag for kernel debugging */
unsigned brkseg, brkoff;	/* seg & offset for break on haltsys */
int     cputype;                /* type of cpu */
int     lbolt;                  /* time of day in 1/HZ sec, not in time */
time_t	time;			/* time in sec from 1970 */

/*
 * Nblkdev is the number of entries
 * (rows) in the block switch. It is
 * set in binit/bio.c by making
 * a pass over the switch.
 * Used in bounds checking on major
 * device numbers.
 */
int	nblkdev;

/*
 * Number of character switch entries.
 * Set by cinit/tty.c
 */
int	nchrdev;

int	mpid;			/* generic for unique process id's */
char	runin;			/* scheduling flag */
char	runout;			/* scheduling flag */
char	runrun;			/* scheduling flag */
char	runtxt;			/* scheduling flag */
char	curpri;			/* more scheduling */
int	maxmem;			/* actual max memory per process */
physadr	lks;			/* pointer to clock device */
daddr_t	swplo;			/* block number of swap space */
int	nswap;			/* size of swap space */
int	updlock;		/* lock for sync */
daddr_t	rablock;		/* block to be read ahead */
extern	char	regloc[];	/* locs. of saved user registers (trap.c) */
extern  int     reglocc;        /* # of entrys in regloc */
char	msgbuf[MSGBUFS];	/* saved "printf" characters */
dev_t	rootdev;		/* device of the root */
dev_t	swapdev;		/* swapping device */
dev_t	pipedev;		/* pipe device */

/* variables removed from c.c */
struct inode *acctp;
struct proc *Nproca;    /* variable end of proc tables--a speedup */
int Nproc;              /* index for Nproca pointer */


/* The following are for configurable parameters */
int	Timezone;		/* holds timezone */
int	Dstflag;		/* holds DST flag  */
/*int	Hz;			/* holds HZ value */

extern	char	icode[];	/* user init code */
extern	int	szicode;	/* its size */

dev_t getmdev();
daddr_t	bmap();
struct inode *ialloc();
struct inode *iget();
struct inode *owner();
struct inode *maknode();
struct inode *namei();
struct buf *alloc();
struct buf *getblk();
struct buf *geteblk();
struct buf *bread();
struct buf *breada();
struct filsys *getfs();
struct file *getf();
struct file *falloc();
int	uchar();

/*
 * Instrumentation
 *
 *      The following cells are used by
 *              iostat.c
 *              systat.c
 *              disk device drivers (optional)
 *
 *      This instrumentation is very rudimentary and inflexible.
 *      It currently allows the monitoring of up to 3 independent units:
 *      there needs to be magic code in the device drivers to divvy up
 *      those three into three different disks devices, three units
 *      of one kind, or whatever.
 *
 *      Whenever each unit is busy, its corresponding bit (01, 02, or 04)
 *      is set in dk_busy.  The driver also should accumulate the
 *      number of transfers performed in dk_numb[unit], and
 *      the number of words transfered in dk_wds[unit].
 *
 *      clock.c increments one cell in dk_time[] depending upon which
 *      of the three units are active (8 possibilities) and upon
 *      whether we're in user or system mode, niced or idle (4 choices)
 *
 *      The structure of dk_time is:
 *
 *      {  long usermode[8],            /* in user mode
 *         long usernice[8],            /* in user mode, nice priority
 *         long sysmode [8],            /* in system mode
 *         long sysidle [8]; }          /* in the idle loop
 *
 *      Programs like systat and iostat can sum these numbers and
 *      extract both disk performance data and the amount of time spent
 *      with the cpu assigned to user, nice, or system, as well as
 *      idle time.  Note that dk_time[24+0] counts system totally idle
 *      time, dk_time[24+1] to dk_time[24+7] count "wasted" time:
 *      time where the cpu was idle waiting for I/O to complete.
 */

int	dk_busy;
long	dk_time[32];
long	dk_numb[3];
long	dk_wds[3];
long	tk_nin;
long	tk_nout;

/*
 * Structure of the system-entry table
 */
extern struct sysent {
	char	sy_narg;		/* total number of arguments */
	int	(*sy_call)();		/* handler */
} sysent[];

unsigned version;
unsigned systype;
