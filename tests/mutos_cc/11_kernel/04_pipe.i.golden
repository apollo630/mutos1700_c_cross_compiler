


































































































































































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




























































pipe()
{
	register struct inode *ip;
	register struct file *rf, *wf;
	int r;

	ip = ialloc(pipedev);
	if(ip == 0)
		return;
	rf = falloc();
	if(rf == 0) {
		iput(ip);
		return;
	}
	r = u.u_r.r_val1;
	wf = falloc();
	if(wf == 0) {
		rf->f_count = 0;
		u.u_ofile[r] = 0;
		iput(ip);
		return;
	}
	u.u_r.r_val2 = u.u_r.r_val1;
	u.u_r.r_val1 = r;
	wf->f_flag = 02|04;
	wf->f_inode = ip;
	rf->f_flag = 01|04;
	rf->f_inode = ip;
	ip->i_count = 2;
	ip->i_mode = 0100000	;
	ip->i_flag = 04		|02		|0100		;
}




readp(fp)
register struct file *fp;
{
	register struct inode *ip;

	ip = fp->f_inode;

loop:
	



	plock(ip);
	


	if (ip->i_size == 0) {
		




		prele(ip);
		if(ip->i_count < 2)
			return;
		ip->i_mode |= 0400		;
		sleep((caddr_t)ip+2, 26);
		goto loop;
	}

	



	u.u_offset = fp->f_un.f_offset;
	readi(ip);
	fp->f_un.f_offset = u.u_offset;
	



	if (fp->f_un.f_offset == ip->i_size) {
		fp->f_un.f_offset = 0;
		ip->i_size = 0;
		if(ip->i_mode & 0200) {
			ip->i_mode &= ~0200;
			wakeup((caddr_t)ip+1);
		}
	}
	prele(ip);
}




writep(fp)
register struct file *fp;
{
	register c;
	register struct inode *ip;

	ip = fp->f_inode;
	c = u.u_count;

loop:

	



	plock(ip);
	if(c == 0) {
		prele(ip);
		u.u_count = 0;
		return;
	}

	





	if(ip->i_count < 2) {
		prele(ip);
		u.u_error = 32;
		psignal(u.u_procp, 13	);
		return;
	}

	





	if(ip->i_size >= 4096) {
		ip->i_mode |= 0200;
		prele(ip);
		sleep((caddr_t)ip+1, 26);
		goto loop;
	}

	







	u.u_offset = ip->i_size;
	u.u_count = min((unsigned)c, (unsigned)4096);
	c -= u.u_count;
	writei(ip);
	prele(ip);
	if(ip->i_mode&0400		) {
		ip->i_mode &= ~0400		;
		wakeup((caddr_t)ip+2);
	}
	goto loop;
}






plock(ip)
register struct inode *ip;
{

	while(ip->i_flag&01		) {
		ip->i_flag |= 020		;
		sleep((caddr_t)ip, 10);
	}
	ip->i_flag |= 01		;
}








prele(ip)
register struct inode *ip;
{

	ip->i_flag &= ~01		;
	if(ip->i_flag&020		) {
		ip->i_flag &= ~020		;
		wakeup((caddr_t)ip);
	}
}
