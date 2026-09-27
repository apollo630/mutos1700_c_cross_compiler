


































































































































































typedef struct { int r[1]; } *	physadr;
typedef struct { unsigned short off;
		 unsigned short seg; }  segadr;
typedef long		daddr_t;
typedef char *		caddr_t;
typedef unsigned short	ino_t;
typedef long		time_t;
typedef int		label_t[5];	
typedef int		dev_t;
typedef long		off_t;



























extern	short	mm_pages[];		
extern	short	mm_size;		












struct sversion
{
	unsigned dl:8,
		 vl:4,
		 vh:4;
};














struct var {
	int v_buf;
	int v_call;
	int v_inode;
	char * ve_inode;
	int v_file;
	char * ve_file;
	int v_mount;
	char * ve_mount;
	int v_proc;
	char * ve_proc;
	int v_text;
	char * ve_text;
	int v_clist;
	int v_maxup;
	int v_lock;
};
extern struct var v;









struct clist {
	int	c_cc;		
	char	*c_cf;		
	char	*c_cl;		
};






struct tc {
	char	t_intrc;	
	char	t_quitc;	
	char	t_startc;	
	char	t_stopc;	
	char	t_eofc;		
	char	t_brkc;		
};











struct tty {
	struct clist t_rawq;	
	struct clist t_outq;	
	int	(* t_oproc)();	
	int	(* t_iproc)();	
	short	t_pgrp;		
	caddr_t	t_addr;		
	dev_t	t_dev;		
	char	t_ispeed;	
	char	t_ospeed;	
	char	t_erase;	
	char	t_kill;		
	short	t_flags;	
	char	t_nldly;	
	char	t_crdly;	
	char	t_htdly;	
	char	t_vtdly;	
	char	t_width;	
	char	t_length;	
	union {
		struct tc;
		struct clist t_ctlq;
	} t_un;
	short	t_state;	
	short	t_xstate;	
	char	t_lnum;		
	char	t_col;		
	char	t_delct;	
	char	t_char;		
	struct chan *t_chan;	
	caddr_t	t_linep;	
	char	t_line;		
	struct	esc  *t_nesc;
	struct	clist  t_escq;
	struct	clist  t_inpq;
};






struct	ttiocb {
	char	ioc_ispeed;
	char	ioc_ospeed;
	char	ioc_erase;
	char	ioc_kill;
	short	ioc_flags;
	char	ioc_nldly;
	char	ioc_crdly;
	char	ioc_htdly;
	char	ioc_vtdly;
	char	ioc_width;
	char	ioc_length;
};











































































































































char	canonb[256		];	
struct inode *rootdir;		
struct proc *runq;		
struct proc *Hogproc;           
struct	inode *inode86;		
unsigned dbreak;			
unsigned brkseg, brkoff;	
int     cputype;                
int     lbolt;                  
time_t	time;			









int	nblkdev;





int	nchrdev;

int	mpid;			
char	runin;			
char	runout;			
char	runrun;			
char	runtxt;			
char	curpri;			
int	maxmem;			
physadr	lks;			
daddr_t	swplo;			
int	nswap;			
int	updlock;		
daddr_t	rablock;		
extern	char	regloc[];	
extern  int     reglocc;        
char	msgbuf[1024		];	
dev_t	rootdev;		
dev_t	swapdev;		
dev_t	pipedev;		


struct inode *acctp;
struct proc *Nproca;    
int Nproc;              



int	Timezone;		
int	Dstflag;		


extern	char	icode[];	
extern	int	szicode;	

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







































int	dk_busy;
long	dk_time[32];
long	dk_numb[3];
long	dk_wds[3];
long	tk_nin;
long	tk_nout;




extern struct sysent {
	char	sy_narg;		
	int	(*sy_call)();		
} sysent[];

unsigned version;
unsigned systype;










extern struct bdevsw
{
	int	(*d_open)();
	int	(*d_close)();
	int	(*d_strategy)();
	struct buf *d_tab;
} bdevsw[];




extern struct cdevsw
{
	int	(*d_open)();
	int	(*d_close)();
	int	(*d_read)();
	int	(*d_write)();
	int	(*d_ioctl)();
	int	(*d_stop)();
	struct tty *d_ttys;
} cdevsw[];




extern struct linesw
{
	int	(*l_open)();
	int	(*l_close)();
	int	(*l_read)();
	char	*(*l_write)();
	int	(*l_ioctl)();
	int	(*l_rint)();
	int	(*l_rend)();
	int	(*l_meta)();
	int	(*l_start)();
	int	(*l_modem)();
} linesw[];





















struct buf
{
	int	b_flags;		
	struct	buf *b_forw;		
	struct	buf *b_back;		
	struct	buf *av_forw;		
	struct	buf *av_back;		
	dev_t	b_dev;			
	unsigned b_bcount;		
	union {                 
	    caddr_t b_addr;             
	    int *b_words;               
	    struct filsys *b_filsys;    
	    struct dinode *b_dino;      
	    daddr_t *b_daddr;           
	} b_un;
	daddr_t	b_blkno;		
	char	b_xmem;			
	char	b_error;		
	unsigned int b_resid;		
	unsigned int b_cylin;		
};

extern struct buf buf[];		
extern struct buf bfreelist;		



































struct iostat {
	int	nbuf;
	long	nread;
	long	nreada;
	long	ncache;
	long	nwrite;
	long    bufcount[50              ];
	long    nswapb;
} io_info;



struct cblock {
	struct cblock *c_next;
	char	c_info[6		];
};

extern struct	cblock	cfree[];
struct	cblock	*cfreelist;
int	cbad;




getc(p)
register struct clist *p;
{
	register struct cblock *bp;
	register int c, s;

	s = spl6();
	if (p->c_cc <= 0) {
		c = -1;
		p->c_cc = 0;
		p->c_cf = p->c_cl = 0;
	} else {
		c = *p->c_cf++ & 0377;
		if (--p->c_cc<=0) {
			bp = (struct cblock *)(p->c_cf-1);
			bp = (struct cblock *) ((int)bp & ~07		);
			p->c_cf = 0;
			p->c_cl = 0;
			bp->c_next = cfreelist;
			cfreelist = bp;
		} else if (((int)p->c_cf & 07		) == 0){
			bp = (struct cblock *)(p->c_cf);
			bp--;
			p->c_cf = bp->c_next->c_info;
			bp->c_next = cfreelist;
			cfreelist = bp;
		}
	}
	splx(s);
	return(c);
}


putc(c, p)
register struct clist *p;
{
	register struct cblock *bp;
	register char *cp;
	register s;

	s = spl6();
	if ((cp = p->c_cl) == 0 || p->c_cc < 0 ) {
		if ((bp = cfreelist) == 0) {
			splx(s);
			return(-1);
		}
		cfreelist = bp->c_next;
		bp->c_next = 0;
		p->c_cf = cp = bp->c_info;
	} else if (((int)cp & 07		) == 0) {
		bp = (struct cblock *)cp - 1;
		if ((bp->c_next = cfreelist) == 0) {
			splx(s);
			return(-1);
		}
		bp = bp->c_next;
		cfreelist = bp->c_next;
		bp->c_next = 0;
		cp = bp->c_info;
	}
	*cp++ = c;
	p->c_cc++;
	p->c_cl = cp;
	splx(s);
	return(0);
}






cinit()
{
	register int ccp;
	register struct cblock *cp;
	register struct cdevsw *cdp;

	ccp = (int)cfree;
	ccp = (ccp+07		) & ~07		;
	for(cp=(struct cblock *)ccp; cp <= &cfree[v.v_clist-1]; cp++) {
		cp->c_next = cfreelist;
		cfreelist = cp;
	}
}


