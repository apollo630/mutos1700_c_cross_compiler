/*
 * The user structure.
 * One allocated per process.
 * Contains all per process data
 * that doesn't need to be referenced
 * while the process is swapped.
 * The user block is USIZE pages
 * long; resides at virtual kernel
 * loc 'u'; contains the system
 * stack per user; is cross referenced
 * with the proc structure for the
 * same process.
 *
 */

#define	EXCLOSE	01

struct	user
{
	label_t	u_rsav;			/* save info when exchanging stacks */
	int	u_fper;			/* FP error register */
	int	u_fpsaved;		/* FP regs saved for this proc */
	struct {
		char    u_fpsav[96];    /* FPP save status */
	} u_fps;
	char    u_segflg;               /* IO flag: 0:user D
						    1:system
						    2:user I 1st page
						    n:user I n-2 page   */
	char    u_error;                /* return error code */
	short   u_uid;                  /* effective user id */
	short   u_gid;                  /* effective group id */
	short   u_ruid;                 /* real user id */
	short   u_rgid;                 /* real group id */
	struct proc *u_procp;           /* pointer to proc structure */
#define	U_PROCP	0x78			/* for mch.c */
	int	*u_ap;			/* pointer to arglist */
	union {				/* syscall return values */
		struct	{
			int	r_val1;
			int	r_val2;
		} r_val;
		off_t	r_off;
		time_t	r_time;
	} u_r;
	caddr_t u_base;                 /* base address for IO */
	unsigned int u_count;           /* bytes remaining for IO */
	off_t   u_offset;               /* offset in file for IO */
	struct inode *u_cdir;           /* pointer to inode of current directory */
	struct inode *u_rdir;           /* root directory of current process */
	char    u_dbuf[DIRSIZ];         /* current pathname component */
	caddr_t u_dirp;                 /* pathname pointer */
	struct direct u_dent;           /* current directory entry */
	struct inode *u_pdir;           /* inode of parent directory of dirp */
	struct file *u_ofile[NOFILE];   /* pointers to file structures of open files */
	char    u_pofile[NOFILE];       /* per-process flags of open files */
	int     u_arg[5];               /* arguments to current system call */

	short   u_tsize;                /* text size (pages) */
	short   u_dsize;                /* data size (pages) */
	short   u_ssize;                /* stack size (pages) */
	short   u_usegs[NSEG];          /* segments table for memory management */
	short   u_cedata;               /* core address of last data page */

	label_t u_qsav;                 /* label variable for quits and interrupts */
	label_t u_ssav;                 /* label variable for swapping */
	int     u_signal[NSIG];         /* disposition of signals */
	time_t  u_utime;                /* this process user time */
	time_t  u_stime;                /* this process system time */
	time_t  u_cutime;               /* sum of childs' utimes */
	time_t  u_cstime;               /* sum of childs' stimes */
	int     *u_aAX;                 /* address of users saved AX */
#define U_AAX 0x142			/* for mch.c */
	struct {                        /* profile arguments */
		short   *pr_base;       /* buffer base */
		unsigned pr_size;       /* buffer size */
		unsigned pr_off;        /* pc offset */
		unsigned pr_scale;      /* pc scaling */
	} u_prof;
	char    u_intflg;               /* catch intr from sys */
	char    u_sep;                  /* flag for I and D separation */
	short   u_lxrw;
	struct tty *u_ttyp;             /* controlling tty pointer */
	dev_t   u_ttyd;                 /* controlling tty dev */
	struct exec u_exdata;           /* header of executable file */
#ifdef OLDAOUT
	int	u_hdrsiz;		/* size of exe header: see a.out.h */
#endif
	char    u_comm[DIRSIZ];
	time_t  u_start;
	char    u_acflag;
	short   u_fpflag;               /* unused now, will be later */
	short   u_cmask;                /* mask for file creation */
	int     u_stack[1];
	short	u_textlength;
	int	u_sel;
	int	u_cn;
	unsigned u_baseseg;		/* base segment for i/o */
	unsigned u_dirpseg;		/* segment address of dirp */
	char	u_msdos;		/* process is an msdos program */
					/* kernel stack per user
					 * extends from u + USIZE*64
					 * backward not to reach here
					 */
};

extern struct user u;

/* u_error codes */
#define	EPERM	1
#define	ENOENT	2
#define	ESRCH	3
#define	EINTR	4
#define	EIO	5
#define	ENXIO	6
#define	E2BIG	7
#define	ENOEXEC	8
#define	EBADF	9
#define	ECHILD	10
#define	EAGAIN	11
#define	ENOMEM	12
#define	EACCES	13
#define	EFAULT	14
#define	ENOTBLK	15
#define	EBUSY	16
#define	EEXIST	17
#define	EXDEV	18
#define	ENODEV	19
#define	ENOTDIR	20
#define	EISDIR	21
#define	EINVAL	22
#define	ENFILE	23
#define	EMFILE	24
#define	ENOTTY	25
#define	ETXTBSY	26
#define	EFBIG	27
#define	ENOSPC	28
#define	ESPIPE	29
#define	EROFS	30
#define	EMLINK	31
#define	EPIPE	32
#define	EDOM	33
#define	ERANGE	34
#define	EUCLEAN	35
#define EDEADLOCK 36
