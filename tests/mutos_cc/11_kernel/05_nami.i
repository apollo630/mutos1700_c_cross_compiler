


































































































































































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









struct	mount
{
	dev_t	m_dev;		
	struct buf *m_bufp;	
	struct inode *m_inodp;	
};

extern struct mount mount[];


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







struct	msexec	{	
	short		ms_magic;	
	unsigned	ms_bytes;	
	unsigned	ms_pages;	
	unsigned	ms_nrel;	
	unsigned	ms_hsize;	
	unsigned	ms_mina;	
	unsigned	ms_maxa;	
	unsigned	ms_ss;		
	unsigned	ms_sp;		
	unsigned	ms_chks;	
	unsigned	ms_ip;		
	unsigned	ms_cs;		
	unsigned	ms_prel;	
	unsigned	ms_ovnum;	
	unsigned	ms_res;		
};










struct	psp	{
	unsigned	ps_i20;		
	unsigned	ps_break;	
	char		ps_res1;	
	char		ps_lc[5];	
	long		ps_iv22;	
	long		ps_iv23;	
	long		ps_iv24;	
	unsigned	ps_ppid;	
	char		ps_filx[0x14];	
	unsigned	ps_envp;	
	long		ps_sstack;	
	unsigned	ps_maxop;	
	long		ps_fxpoi;	
	char		ps_res2[0x18];	
	char		ps_i21[3];	
	char		ps_res3[9];	
	char		ps_fcb1[0x10];	
	char		ps_fcb2[0x10];	
	char		ps_res4[4];	
	char		ps_dta[0x80];	
};








typedef	int	(*dos_ent)();
extern	dos_ent	dosent[];
















extern	int	msdebug;
extern	int	ms_proc;	







struct	exec86 {
	char	e86_magic;		
	char	e86_slen;		
	char	e86_zero;		
	char	e86_nlen;		
	char	e86_rest[14		 + 1];	
};	












struct inode *
namei(func, flag)
int (*func)();
{
	register struct inode *dp;
	register c;
	register char *cp;
	struct buf *bp;
	int i;
	dev_t d;
	off_t eo;

	




	dp = u.u_cdir;
	if((c=(*func)()) == '/')
		if ((dp = u.u_rdir) == 0)
			dp = rootdir;
	iget(dp->i_dev, dp->i_number);
	while(c == '/')
		c = (*func)();
	if(c == '\0' && flag != 0)
		u.u_error = 2;

cloop:
	




	if(u.u_error)
		goto out;
	if(c == '\0')
		return(dp);

	





	cp = &u.u_dbuf[0];
	while (c != '/' && c != '\0' && u.u_error == 0 ) {

		if(cp < &u.u_dbuf[14		])
			*cp++ = c;
		c = (*func)();
	}
	while(cp < &u.u_dbuf[14		])
		*cp++ = '\0';
	while(c == '/')
		c = (*func)();


seloop:
	




	if((dp->i_mode&0170000		) != 0040000	)
		u.u_error = 20;
	if(access(dp, 0100))
		goto out;

	


	u.u_offset = 0;
	u.u_segflg = 1;
	eo = 0;
	bp = 0;

eloop:

	





	if(u.u_offset >= dp->i_size) {
		if(bp != 0)
			brelse(bp);
		if(flag==1 && c=='\0') {
			if(access(dp, 0200))
				goto out;
			u.u_pdir = dp;
			if(eo)
				u.u_offset = eo-sizeof(struct direct);
			else
				dp->i_flag |= 02		|0100		;
			return(0);
		}
		u.u_error = 2;
		goto out;
	}

	





	if((u.u_offset&01777		) == 0) {
		if(bp != 0)
			brelse(bp);
		bp = bread(dp->i_dev,
			bmap(dp, (daddr_t)(u.u_offset>>10		), 01	));
		if (bp->b_flags & 04	) {
			brelse(bp);
			goto out;
		}
	}

	







	bcopy(bp->b_un.b_addr+(u.u_offset&01777		), (caddr_t)&u.u_dent,
		sizeof(struct direct));
	u.u_offset += sizeof(struct direct);
	if(u.u_dent.d_ino == 0) {
		if(eo == 0)
			eo = u.u_offset;
		goto eloop;
	}
	for(i=0; i<14		; i++)
		if(u.u_dbuf[i] != u.u_dent.d_name[i])
			goto eloop;

	





	if(bp != 0)
		brelse(bp);
	if(flag==2 && c=='\0') {
		if(access(dp, 0200))
			goto out;
		return(dp);
	}
	d = dp->i_dev;
	if(u.u_dent.d_ino == ((ino_t)2)	)
	if(dp->i_number == ((ino_t)2)	)
	if(u.u_dent.d_name[1] == '.')
		for(i=1; i<v.v_mount; i++)
			if(mount[i].m_bufp != 0)
			if(mount[i].m_dev == d) {
				iput(dp);
				dp = mount[i].m_inodp;
				dp->i_count++;
				plock(dp);
				goto seloop;
			}
	iput(dp);
	dp = iget(d, u.u_dent.d_ino);
	if(dp == 0)
		return(0);
	goto cloop;

out:
	iput(dp);
	return(0);
}





schar()
{

	return(*u.u_dirp++ & 0377);
}





uchar()
{
	register c;


		c = fubyte(u.u_dirp++);

	if(c == -1)
		u.u_error = 14;

	return(c);
}
