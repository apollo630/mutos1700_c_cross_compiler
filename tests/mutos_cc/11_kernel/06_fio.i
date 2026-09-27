


































































































































































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



















struct	mount
{
	dev_t	m_dev;		
	struct buf *m_bufp;	
	struct inode *m_inodp;	
};

extern struct mount mount[];






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


struct	direct
{
	ino_t	d_ino;
	char	d_name[14		];
};





struct  exec {  
	short           x_magic;        
	char            x_mach;         
	char            x_form;         
	long            x_text;         
	long            x_data;         
	long            x_bss;          
	long            x_syms;         
	long            x_entry;        
	long            x_reloc;        
	long            x_aux;          
};






























struct  oexec {  
	int             oa_magic;       
	unsigned        oa_text;        
	unsigned        oa_data;        
	unsigned        oa_bss;         
	unsigned        oa_syms;        
	unsigned        oa_entry;       
	unsigned        oa_unused;      
	unsigned        oa_flag;        
};




















struct	nlist {	
	char    	n_name[8];	
	int     	n_type;    	
	unsigned	n_value;	
};

		



































struct	user
{
	label_t	u_rsav;			
	int	u_fper;			
	int	u_fpsaved;		
	struct {
		char    u_fpsav[96];    
	} u_fps;
	char    u_segflg;               



	char    u_error;                
	short   u_uid;                  
	short   u_gid;                  
	short   u_ruid;                 
	short   u_rgid;                 
	struct proc *u_procp;           

	int	*u_ap;			
	union {				
		struct	{
			int	r_val1;
			int	r_val2;
		} r_val;
		off_t	r_off;
		time_t	r_time;
	} u_r;
	caddr_t u_base;                 
	unsigned int u_count;           
	off_t   u_offset;               
	struct inode *u_cdir;           
	struct inode *u_rdir;           
	char    u_dbuf[14		];         
	caddr_t u_dirp;                 
	struct direct u_dent;           
	struct inode *u_pdir;           
	struct file *u_ofile[20		];   
	char    u_pofile[20		];       
	int     u_arg[5];               

	short   u_tsize;                
	short   u_dsize;                
	short   u_ssize;                
	short   u_usegs[0		];          
	short   u_cedata;               

	label_t u_qsav;                 
	label_t u_ssav;                 
	int     u_signal[17];         
	time_t  u_utime;                
	time_t  u_stime;                
	time_t  u_cutime;               
	time_t  u_cstime;               
	int     *u_aAX;                 

	struct {                        
		short   *pr_base;       
		unsigned pr_size;       
		unsigned pr_off;        
		unsigned pr_scale;      
	} u_prof;
	char    u_intflg;               
	char    u_sep;                  
	short   u_lxrw;
	struct tty *u_ttyp;             
	dev_t   u_ttyd;                 
	struct exec u_exdata;           

	int	u_hdrsiz;		

	char    u_comm[14		];
	time_t  u_start;
	char    u_acflag;
	short   u_fpflag;               
	short   u_cmask;                
	int     u_stack[1];
	short	u_textlength;
	int	u_sel;
	int	u_cn;
	unsigned u_baseseg;		
	unsigned u_dirpseg;		
	char	u_msdos;		
					



};

extern struct user u;










































struct	filsys {
	unsigned short s_isize;	
	daddr_t	s_fsize;   	
	short  	s_nfree;   	
	daddr_t	s_free[100		];
	short  	s_ninode;  	
	ino_t  	s_inode[100		];
	char   	s_flock;   	
	char   	s_ilock;   	
	char   	s_fmod;    	
	char   	s_ronly;   	
	time_t 	s_time;    	
	daddr_t	s_tfree;   	
	ino_t  	s_tinode;  	
	
	short  	s_m;       	
	short  	s_n;       	
	char   	s_fname[6];	
	char   	s_fpack[6];	
	
	char   	s_clean;   	
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








struct	file
{
	char	f_flag;
	unsigned short f_count;         
	struct inode  *f_inode;         
	union {
		off_t	f_offset;	
		struct chan *f_chan;	
	} f_un;
};

extern struct file file[];	



















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
















struct group {
	short	g_state;
	char	g_index;
	char	g_rot;
	struct	group	*g_group;
	struct	inode	*g_inode;
	struct	file	*g_file;
	short	g_rotmask;
	short	g_datq;
	struct	chan *g_chans[15];
};
struct	inode
{
	char	i_flag;
	char	i_count;	
	dev_t	i_dev;		
	ino_t	i_number;	
	unsigned short	i_mode;
	short	i_nlink;	
	short	i_uid;		
	short	i_gid;		
	off_t	i_size;		
	struct  locklist *i_locklist;           
	union {
		struct {
			daddr_t i_addr[13];	
			daddr_t	i_lastr;	
		} i_i;
		struct	{
			daddr_t	i_rdev;			
			struct	group	i_group;	
		} i_m;
	} i_un;
};

extern struct inode inode[];            
struct inode *mpxip;                    


struct  locklist
{
					
	struct  locklist *ll_link;      
	int     ll_flags;               
	struct  proc    *ll_proc;       
	off_t   ll_start;               
	off_t   ll_end;                 
};

extern  struct  locklist  locklist[];   































































extern int ROroot;









struct file *
getf(f)
register int f;
{
	register struct file *fp;

	if(0 <= f && f < 20		) {
		fp = u.u_ofile[f];
		if(fp != 0)
			return(fp);
	}
	u.u_error = 9;
	return(0);
}












closef(fp)
register struct file *fp;
{
	register struct inode *ip;
	int flag, mode;
	dev_t dev;
	register int (*cfunc)();
	union {
		struct inode *a;
		struct mount *b;
	}c;

	if(fp == 0)
		return;
	unlckf(fp->f_inode);
	if (fp->f_count > 1) {
		fp->f_count--;
		return;
	}
	ip = fp->f_inode;
	plock(ip);
	flag = fp->f_flag;
	dev = (dev_t)ip->i_un.i_rdev;
	mode = ip->i_mode&0170000		;

	fp->f_count = 0;
	if(flag & 04) {
	ip->i_mode &= ~(0400		|0200);
		wakeup((caddr_t)ip+1);
		wakeup((caddr_t)ip+2);
	}

	switch(mode) {

	case 0020000	:
		cfunc = cdevsw[	(int)(((unsigned)(dev)>>8))].d_close;
		break;

	case 0060000	:
		cfunc = bdevsw[	(int)(((unsigned)(dev)>>8))].d_close;
		break;
	default:
		iput(ip);
		return;
	}

	for(fp=file;fp < v.ve_file; fp++)
		if(fp->f_count && (dev_t)(c.a = fp->f_inode)->i_un.i_rdev == dev && mode == (c.a->i_mode&0170000		))
		{
			iput(ip);
			return;
		}
	if(mode == 0060000	)
	{
		for(c.b=mount;c.b < v.ve_mount;c.b++)
			if(c.b->m_bufp != 0 && c.b->m_dev == dev)
			{
				iput(ip);
				return;
			}
		bpurge(dev);
	}
	(*cfunc)(dev ,flag);
	iput(ip);
	return;
}






openi(ip, rw)
register struct inode *ip;
{
	dev_t dev;
	register unsigned int maj;

	dev = (dev_t)ip->i_un.i_rdev;
	maj = 	(int)(((unsigned)(dev)>>8));
	switch(ip->i_mode&0170000		) {

	case 0020000	:
	case 0030000	:
		if(maj >= nchrdev)
			goto bad;
		(*cdevsw[maj].d_open)(dev, rw);
		break;

	case 0060000	:
	case 0070000	:
		if(maj >= nblkdev)
			goto bad;
		(*bdevsw[maj].d_open)(dev, rw);
	}
	return;

bad:
	u.u_error = 6;
}














access(ip, mode)
register struct inode *ip;
{
	register m;

	m = mode;
	if(m == 0200) {
		if(getfs(ip->i_dev)->s_ronly != 0) {
			if(((ip->i_mode&0170000		) == 0040000	) || ((ip->i_mode&0170000		) == 0100000	) || (ROroot == 0))
			{
				u.u_error = 30;
				return(1);
			}
		}
		if (ip->i_flag&040		)		
			xrele(ip);
		if(ip->i_flag & 040		) {
			u.u_error = 26;
			return(1);
		}
	}
	if(u.u_uid == 0)
		return(0);
	if(u.u_uid != ip->i_uid) {
		m >>= 3;
		if(u.u_gid != ip->i_gid)
			m >>= 3;
	}
	if((ip->i_mode&m) != 0)
		return(0);

	u.u_error = 13;
	return(1);
}









struct inode *
owner()
{
	register struct inode *ip;

	ip = namei(uchar, 0);
	if(ip == 0)
		return(0);
	if(u.u_uid == ip->i_uid)
		return(ip);
	if(suser())
		return(ip);
	iput(ip);
	return(0);
}





suser()
{

	if(u.u_uid == 0) {

		return(1);
	}
	u.u_error = 1;
	return(0);
}




ufalloc()
{
	register i;

	for(i=0; i<20		; i++)
		if(u.u_ofile[i] == 0) {
			u.u_r.r_val1 = i;
			u.u_pofile[i] = 0;
			return(i);
		}
	u.u_error = 24;
	return(-1);
}










struct file *
falloc()
{
	register struct file *fp;
	register i;

	i = ufalloc();
	if(i < 0)
		return(0);
	for(fp = &file[0]; fp < v.ve_file; fp++)
		if(fp->f_count == 0) {
			u.u_ofile[i] = fp;
			fp->f_count++;
			fp->f_un.f_offset = 0;
			return(fp);
		}
	printf("no file\n");
	u.u_error = 23;
	return(0);
}
