/*
 * One structure allocated per active
 * process. It contains all data needed
 * about the process while the
 * process may be swapped out.
 * Other per process data (user.h)
 * is swapped with the process.
 */
struct	proc {
	char    p_stat;         /* +0  state */
#define P_PSTAT 0		/* for mch.c */
	char    p_flag;         /* +1  flags */
	char    p_pri;          /* +2  priority, negative is high */
	char    p_time;         /* +3  resident time for scheduling */
	char    p_cpu;          /* +4  cpu usage for scheduling */
	char    p_nice;         /* +5  nice for cpu usage */
	short   p_sig;          /* +6  signals pending to this process */
	short   p_uid;          /* +8  user id, used to direct tty signals */
	short   p_pgrp;         /* +10 name of process group leader */
	short   p_pid;          /* +12 unique process id */
	short   p_ppid;         /* +14 process id of parent */
	short   p_addr;         /* +16 loc of upage or loc on swap */
#define P_ADDR 16		/* for mch.c */
	short   p_size;         /* +18 size of proc in pages */
				/*     = p_dusize + ssize  */
	char	p_pnum;		/* +20 process number */
#define	P_PNUM	20		/* for mch.c */
	char	p_cflags;	/* +21 contiguous memory flags */
	short	p_dusize;	/* +22 data+U size, to find last data page */
#define P_DUSIZE 22		/* for mch.c */

	caddr_t p_wchan;        /* +24 event process is awaiting */
	struct text *p_textp;   /* +26 pointer to text structure */
#define P_TEXTP 26		/* for mch.c */
	struct proc *p_link;    /* +28 linked list of running processes */
	int     p_clktim;       /* +30 time to alarm clock signal */
				/* +32 */
};

extern struct proc proc[];	/* the proc table itself */
extern struct proc *Nproca;     /* address of highest+1 used entry in proc */
extern int Nproc;               /* index of highest+1 used entry in proc */

/* stat codes */
#define	SSLEEP	1		/* awaiting an event */
#define	SWAIT	2		/* (abandoned state) */
#define	SRUN	3		/* running */
#define	SIDL	4		/* intermediate state in process creation */
#define	SZOMB	5		/* intermediate state in process termination */
#define	SSTOP	6		/* process being traced */

/* flag codes */
#define	SLOAD	01		/* in core */
#define	SSYS	02		/* scheduling process */
#define	SLOCK	04		/* process cannot be swapped */
#define	SSWAP	010		/* process is being swapped out */
#define	STRC	020		/* process is being traced */
#define	SWTED	040		/* another tracing flag */
#define	SULOCK	0100		/* user settable lock in core */
#define	SESTAB	0200		/* need to call estabur on restart */

/* contiguity flags */
#define	SCSTACK	01
#define	SCDATA	02
#define SCWANT	04

/*
 * parallel proc structure
 * to replace part with times
 * to be passed to parent process
 * in ZOMBIE state.
 */
struct	xproc {
	char	xp_stat;
	char	xp_flag;
	char	xp_pri;		/* priority, negative is high */
	char	xp_time;	/* resident time for scheduling */
	char	xp_cpu;		/* cpu usage for scheduling */
	char	xp_nice;	/* nice for cpu usage */
	short	xp_sig;		/* signals pending to this process */
	short	xp_uid;		/* user id, used to direct tty signals */
	short	xp_pgrp;	/* name of process group leader */
	short	xp_pid;		/* unique process id */
	short	xp_ppid;	/* process id of parent */
	short	xp_xstat;	/* Exit status for wait */
	time_t	xp_utime;	/* user time, this proc */
	time_t	xp_stime;	/* system time, this proc */
};
