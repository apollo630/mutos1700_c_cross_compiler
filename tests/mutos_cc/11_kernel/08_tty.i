






































































































































































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














































































































































struct	proc {
	char    p_stat;         

	char    p_flag;         
	char    p_pri;          
	char    p_time;         
	char    p_cpu;          
	char    p_nice;         
	short   p_sig;          
	short   p_uid;          
	short   p_pgrp;         
	short   p_pid;          
	short   p_ppid;         
	short   p_addr;         

	short   p_size;         
				
	char	p_pnum;		

	char	p_cflags;	
	short	p_dusize;	


	caddr_t p_wchan;        
	struct text *p_textp;   

	struct proc *p_link;    
	int     p_clktim;       
				
};

extern struct proc proc[];	
extern struct proc *Nproca;     
extern int Nproc;               






























struct	xproc {
	char	xp_stat;
	char	xp_flag;
	char	xp_pri;		
	char	xp_time;	
	char	xp_cpu;		
	char	xp_nice;	
	short	xp_sig;		
	short	xp_uid;		
	short	xp_pgrp;	
	short	xp_pid;		
	short	xp_ppid;	
	short	xp_xstat;	
	time_t	xp_utime;	
	time_t	xp_stime;	
};
















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






















































struct esc { char ec;
	     char fl;
	     struct esc *nesc;};

struct esc esc30[] = {{'g',      01|      04|020},
		      {'J',      01|      04|020},
		      {0177,0,0}};

struct esc esc20[] = {{0,      01|      04|      010|020},
		      {0177,0,0}};

struct esc esc21[] = {{1,      01|      02|      010|020},
		      {0177,0,0}};

struct esc esc23[] = {{2,      01|      02|      010},
		      {'0',0,esc30},
		      {'2',0,esc30},
		      {'3',0,esc30},
		      {0177,0,0}};

struct esc esc10[] = {{'H',      01|      04|020},
		      {'E',      01|      02},
		      {'c',      01|      04|020},
		      {'#',0,esc20},
		      {'O',0,esc21},
		      {'[',0,esc23},
		      {0177,0,0}};

char	t0[]	= "<>7";
char	t1[]	= "PQRSpqrstuvw";
char	t2[]	= "ABCDH";

char	*ttbl[]	= {t0,t1,t2};

struct cblock {
	struct cblock *c_next;
	char	c_info[6		];
};










char maptab[] = {
    0,     0200,     0200,     0,     0200,     0,     0,     0200,
    0200,     0,     0,     0200,     0,     0200,     0200,     0,
    0200,     0,     0,     0200,     0,     0200,     0200,     0,
    0,     0200,     0200,     0,     0200,     0,     0,     0200,
    0200,   '|',     0,     0200,     0,     0200,     0200,   '`',
  '{', 0200+'}',     0200,     0,     0200,     0,     0,     0200,
    0,     0200,     0200,     0,     0200,     0,     0,     0200,
    0200,     0,     0,     0200,     0,     0200,     0200,     0,
    0200,     0,     0,     0200,     0,     0200,     0200,     0,
    0,     0200,     0200,     0,     0200,     0,     0,     0200,
    0,     0200,     0200,     0,     0200,     0,     0,     0200,
    0200,     0,     0,     0200,  '\\',     0200, 0200+'~',     0,
    0, 0200+'A', 0200+'B',   'C', 0200+'D',   'E',   'F', 0200+'G',
0200+'H',   'I',   'J', 0200+'K',   'L', 0200+'M', 0200+'N',   'O',
0200+'P',   'Q',   'R', 0200+'S',   'T', 0200+'U', 0200+'V',   'W',
  'X', 0200+'Y', 0200+'Z',     0,     0200,     0,     0,     0200
};






char cchars[] = {
	0177, 0300, 0377, 0377,    0,    0,    0,    0,
	   0,    0,    0,    0,    0,    0,    0, 0200
};





char wchars[] = {
	   0,    0,    0,    0, 0200,    0, 0377,   03,
	0376, 0377, 0377,   07, 0376, 0377, 0377,   07
};










extern struct tty constty[];






ttyopen(dev, tp)
dev_t dev;
register struct tty *tp;
{
	register struct proc *pp;

	pp = u.u_procp;
	tp->t_dev = dev;
	if(pp->p_pgrp == 0){
		u.u_ttyp = tp;
		u.u_ttyd = dev;
		if(tp->t_pgrp == 0)
			tp->t_pgrp = pp->p_pid;
		pp->p_pgrp = tp->t_pgrp;
	}
	tp->t_state &= ~02		;
	tp->t_state |= 04		;
}





ttychars(tp)
register struct tty *tp;
{
	tp->t_un.t_intrc = 03	;
	tp->t_un.t_quitc = 034	;
	tp->t_un.t_startc = 021	;
	tp->t_un.t_stopc = 023	;
	tp->t_un.t_eofc = 032	;
	tp->t_un.t_brkc = 0377	;
	tp->t_erase = 0177	;
	tp->t_kill = 025	;
	tp->t_width = 0;
	tp->t_length = 0;
}




ttyclose(tp)
register struct tty *tp;
{
	tp->t_pgrp = 0;
	wflushtty(tp);
	tp->t_state &= 0400		 ;
	tp->t_xstate = 0;
}





stty()
{
	u.u_arg[2] = u.u_arg[1];
	u.u_arg[1] = (('t'<<8)|9);
	ioctl();
}

gtty()
{
	u.u_arg[2] = u.u_arg[1];
	u.u_arg[1] = (('t'<<8)|8);
	ioctl();
}







ioctl()
{
	register struct file *fp;
	register struct inode *ip;
	register struct a {
		int	fdes;
		int	cmd;
		caddr_t	cmarg;
	} *uap;
	register dev_t dev;
	register fmt;

	uap = (struct a *)u.u_ap;
	if((fp = getf(uap->fdes)) == 0)
		return;
	if(uap->cmd == 	(('f'<<8)|1)){
		u.u_pofile[uap->fdes] |= 01;
		return;
	}
	if(uap->cmd == (('f'<<8)|2)){
		u.u_pofile[uap->fdes] &= ~01;
		return;
	}
	ip = fp->f_inode;
	fmt = ip->i_mode&0170000		;
	if(fmt != 0020000	 && fmt != 0030000	){
		u.u_error = 25;
		return;
	}
	dev = (dev_t)ip->i_un.i_rdev;
	(*cdevsw[	(int)(((unsigned)(dev)>>8))].d_ioctl)(dev, uap->cmd, uap->cmarg, fp->f_flag);
}




ttioccomm(com, tp, addr, dev)
register struct tty *tp;
caddr_t addr;
{
	unsigned t;
	register n;
	extern int nldisp;
	struct	sttiocb {
		char	ioc_ispeed;
		char	ioc_ospeed;
		char	ioc_erase;
		char	ioc_kill;
		short	ioc_flags;
	};

	switch(com){
	


	case (('t'<<8)|0):
		t = tp->t_line;
		if(copyout((caddr_t)&t, addr, sizeof(t)))
			u.u_error = 14;
		break;
	


	case (('t'<<8)|1):
		if(copyin(addr, (caddr_t)&t, sizeof(t))){
			u.u_error = 14;
			break;
		}
		if(t >= nldisp){
			u.u_error = 6;
			break;
		}
		if(tp->t_line)
			(*linesw[tp->t_line].l_close)(tp);
		if(t)
			(*linesw[t].l_open)(dev, tp, addr);
		if(u.u_error == 0)
			tp->t_line = t;
		break;
	


	case (('t'<<8)|13):
		tp->t_state |= 0200		;
		break;
	case (('t'<<8)|14):
		tp->t_state &= ~0200		;
		break;
	


	case (('t'<<8)|9):
		wflushtty(tp);
	case (('t'<<8)|10):
		if(copyin(addr, (caddr_t)&tp->t_ispeed, sizeof(struct sttiocb))){
			u.u_error = 14;
			return(1);
		}
		break;
	


	case (('t'<<8)|8):
		if(copyout((caddr_t)&tp->t_ispeed, addr, sizeof(struct sttiocb)))
			u.u_error = 14;
		break;
	


	case (('t'<<8)|11):
		wflushtty(tp);
		n = tp->t_length;
		if(copyin(addr, (caddr_t)&tp->t_ispeed, sizeof(struct ttiocb))){
			u.u_error = 14;
			return(1);
		}
		if((tp->t_length &= 0177) != n){
			if(tp->t_length)
				tp->t_xstate |= 01	;
			else
				tp->t_xstate &= ~01	;
			tp->t_lnum = 0;
		}
		break;
	


	case (('t'<<8)|12):
		if(copyout((caddr_t)&tp->t_ispeed, addr, sizeof(struct ttiocb)))
			u.u_error = 14;
		break;
	


	case (('t'<<8)|2):
		tp->t_state |= 01000		;
		break;
	case 	(('t'<<8)|15):
		t = 0;
		if(tp->t_flags&(040|02))
			t = tp->t_rawq.c_cc;
		else if(tp->t_delct) {
			register char *cp;

			cp = tp->t_rawq.c_cf;
			while(	((*cp++)&0377) != 0377)
				t++;
		}
		if(copyout((caddr_t)&t, addr, sizeof(t)))
			u.u_error = 14;
		break;
	case (('t'<<8)|16):
		flushtty(tp);
		break;
	


	case (('d'<<8)|9):
	case (('d'<<8)|8):
		(*linesw[tp->t_line].l_ioctl)(com, tp, addr);
		break;
	


	case (('t'<<8)|17):
		if(copyin(addr, (caddr_t)&tp->t_un, sizeof(struct tc)))
			u.u_error = 14;
		break;
	case (('t'<<8)|18):
		if(copyout((caddr_t)&tp->t_un, addr, sizeof(struct tc)))
			u.u_error = 14;
		break;
	case        (('f'<<8)|3):
		if((tp->t_state&020		) == 0)
			break;
		if(tp->t_rawq.c_cc) {
			u.u_r.r_val.r_val1 = 1;
			break;
		}
		if(tp->t_delct > 0)
			u.u_r.r_val.r_val1 = 1;
		break;
	default:
		return(0);
	}
	return(1);
}




wflushtty(tp)
register struct tty *tp;
{
	spl3();
	while (tp->t_outq.c_cc && tp->t_state&020		){
		(*tp->t_oproc)(tp);
		tp->t_state |= 0100		;
		sleep((caddr_t)&tp->t_outq, 29);
	}
	flushtty(tp);
	spl0();
}




flushtty(tp)
register struct tty *tp;
{
	register s;

	wakeup((caddr_t)&tp->t_rawq);
	wakeup((caddr_t)&tp->t_outq);
	s = spl6();
	tp->t_xstate &= ~(02	|04	|010	|020	);
	(*cdevsw[	(int)(((unsigned)(tp->t_dev)>>8))].d_stop)(tp);
	while(getc(&tp->t_outq) >= 0);
	while(getc(&tp->t_rawq) >= 0);
	tp->t_delct = 0;
	tp->t_lnum = 0;
	splx(s);
}































ttyinput(c, tp)
register c;
register struct tty *tp;
{
	int ret;
	register int t_flags;


	c &= 0377;
	if((tp->t_flags&(040|020000)) == 0)
		c &= 0177;
	t_flags = tp->t_flags;
	if(t_flags&01)
		ttyblock(tp);
	if((t_flags&040) == 0) {
		if(tp->t_state&0400		){
			if((c == tp->t_un.t_startc) || (c == tp->t_un.t_quitc) ||
				(c == tp->t_un.t_intrc)) {
				tp->t_state &= ~0400		;
				tp->t_xstate &= ~040	;
				if(c != tp->t_un.t_quitc && c != tp->t_un.t_intrc)
					goto out3;
			}
			if(c == tp->t_un.t_stopc)
				return;
		} else {
			if(c == tp->t_un.t_stopc){
				tp->t_state |= 0400		;
				(*cdevsw[	(int)(((unsigned)(tp->t_dev)>>8))].d_stop)(tp);
				return;
			}
			if(c == tp->t_un.t_startc)
				return;
		}
		if((t_flags&02) == 0){
			if(tp->t_xstate& 0100	) {
				if(ttyesc(c,tp))
					goto out3;
				else
					return;
			}
			else if(c == 0X1B) {
				tp->t_xstate |=  0100	;
				tp->t_nesc = &esc10[0];
				putc(c,&tp->t_escq);
				return;
			}
		}
		if(c == 0X14)
			return;
		if(tp->t_xstate&010	){
			tp->t_xstate &= ~010	;
			if((t_flags&04) && (maptab[c]&0177))
				c = maptab[c]&0177;
			else if(!	(ttyspcl(c, tp))){
				putc('\\', &tp->t_rawq);
				goto contin;
			}
			putc(c, &tp->t_rawq);
			goto out2;
		contin:;
		}
		if((c == 020	) && tp->t_length){
			tp->t_xstate ^= 01	;
			if((tp->t_xstate&02	) == 0)
				goto out2;
		}
		if(tp->t_xstate&02	){
			tp->t_xstate &= ~(02	|04	);
			if(c == '\r' || c == '\n')
				tp->t_lnum = tp->t_length-1;
			else
				tp->t_lnum = 0;
			if(c != tp->t_un.t_quitc && c != tp->t_un.t_intrc)
				goto out3;
		}
		if(c == tp->t_un.t_quitc || c == tp->t_un.t_intrc){
			flushtty(tp);
			c = (c == tp->t_un.t_intrc) ? 2	:3	;
				signal(tp->t_pgrp, c);
				ttyecho('\n', tp);
				ttstart(tp);
			return;
		}
		if(t_flags&02)
			goto out0;
		if(c == tp->t_erase){
			if(	(ttybit(cchars, c))){
				ttywipe(zapc(&tp->t_rawq), tp);
				goto out3;
			}
			else {
				zapc(&tp->t_rawq);
				goto out2;
			}
		}
		else if(c == tp->t_kill){
			if(((t_flags&0400) == 0) || (!	(ttybit(cchars, c)))){
				while(zapc(&tp->t_rawq) >= 0);
				ttyecho(c, tp);
				ttyecho('\n', tp);
			}
			else {
				while((c = zapc(&tp->t_rawq)) >= 0){
					if(	((tp->t_col)&0377) > 0)
						ttywipe(c, tp);
				}
			}
			goto out3;
		}
		else if(c == tp->t_un.t_eofc){
			ttyecho('\n', tp);
			goto out1;
		}
		else if(c == '\\'){
			tp->t_xstate |= 010	;
			ttyecho(c, tp);
			goto out3;
		}
		else if(c == 022	){
			if(tp->t_outq.c_cc)
				goto out3;

			ttyecho('\n', tp);
			ttyretype(tp, 1);

			goto out3;
		}
		else if(c == 027	){
			ttywipe((c = zapc(&tp->t_rawq)), tp);
			if(	(ttybit(wchars, c))){
				while(	(ttybit(wchars, c = zapc(&tp->t_rawq))))
					ttywipe(c, tp);
				if(c >= 0)
					putc(c, &tp->t_rawq);
			}
			goto out3;
		}
	}
out0:
	if((c == 0x80) && (tp >= &constty[0]) && (tp <= &constty[3]))
		c = 0x15;
	if(c == '\r' && (t_flags&020))
		c = '\n';
	if((t_flags&04) && 	('A' <= c && c <= 'Z'))
		c += 'a'-'A';
	if(((t_flags&(040|04)) == 0) && (t_flags&010000)) {
		if(	('A' <= c && c <= 'Z'))
			c += 'a' - 'A';
		else if (	('a' <= c && c <= 'z'))
			c -= 'a' - 'A';
		}
	if(tp->t_rawq.c_cc > 256){
		flushtty(tp);
		return;
	}
	putc(c, &tp->t_rawq);
out1:
	if((t_flags&(040|02)) || (c == '\n' || c == tp->t_un.t_eofc || c == tp->t_un.t_brkc)){
		if((t_flags&(040|02)) == 0 && putc(0377, &tp->t_rawq) == 0)
			tp->t_delct++;

		wakeup((caddr_t)&tp->t_rawq);
	}
out2:
	if(ttyecho(c, tp) == 0)
		return;
out3:
	ttstart(tp);
}








ttyretype(tp, flag)
register struct tty *tp;
{
	register char *cp;
	register int c, n;
	register int width, col;

	cp = tp->t_rawq.c_cf;
	n = tp->t_delct;
	width = tp->t_width ? 	((tp->t_width)&0377): 0377;
	col = 0;
	for(c = tp->t_rawq.c_cc; c--; ){
		if(n){
			if(	((*cp++)&0377) == 0377)
				n--;
		}
		else {
			if((tp->t_flags&04) && 	('A' <= *cp && *cp <= 'Z') || 	(ttyspcl(*cp, tp))){
				if(flag)
					ttyecho('\\', tp);
				else
					col++;
			}
			if(flag)
				ttyecho(*cp++, tp);
			else {
				if(*cp++ == '\t')
					col += 8 - (col&07);
				else
					col++;
				if(col >= width)
					col -= width;
			}
		}
		if(((int)cp & 07		) == 0)
			cp = (((struct cblock *)cp) - 1)->c_next->c_info;
	}
	return(col);
}






ttyecho(c, tp)
register c;
register struct tty *tp;
{
	if(tp->t_flags&020000 == 0)
		c &= 0177;
	if((tp->t_flags&010) && c == 0x15)
		goto ntest;
	if((tp->t_flags&010) == 0 || (	(ttybit(cchars, c)) && (tp->t_flags&01000) == 0))
		return(0);
ntest:
	if(tp->t_xstate&020	){
		tp->t_xstate &= ~020	;
		ttyoutput(']', tp);
	}
	if((tp->t_flags&04) && 	('A' <= c && c <= 'Z'))
		c += 'a'-'A';
	


	if((c == '\n') && (tp->t_xstate&01	) && ((tp->t_flags&040) == 0)){
		if(tp->t_lnum > 0)
			tp->t_lnum--;
	}
	ttyoutput(c, tp);
	return(1);
}





ttywipe(c, tp)
register c;
register struct tty *tp;
{
	if((tp->t_flags&010) == 0)
		return;
	if(c < 0){
		ttyoutput(07	, tp);
		return;
	}
	if(tp->t_flags&0400){
		register n = 1;

		if(	(ttybit(cchars, c))){
			n = 0;
			if(tp->t_flags&01000)
				n = 2;
			if(	(ttyspcl(c, tp)))
				n++;
		}
		else if(c == '\t'){

			if(n = ttyretype(tp, 0))
				n = 	((tp->t_col)&0377) - 	((n) > (	((tp->t_htdly)&0377)) ? (n): (	((tp->t_htdly)&0377)));
			else if((n = 	((tp->t_col)&0377) - 	((tp->t_htdly)&0377)) == 0)
				n = 8;
			if(	((tp->t_htdly)&0377) >= (	((tp->t_col)&0377) - n))
				tp->t_htdly = 0;

		}
		else if(tp->t_flags&04){
			if(	('A' <= c && c <= 'Z') || c == '`' || c >= '{')
				n++;
		}
		if((n < 0) || (n > 	((tp->t_col)&0377))) {
			
				ttyoutput('\n',tp);
			ttyretype(tp,1);
		}
		else
		while(n--){
			ttyoutput('\b', tp);
			ttyoutput(' ', tp);
			ttyoutput('\b', tp);
		}
		return;
	}
	if((tp->t_xstate&020	) == 0){
		tp->t_xstate |= 020	;
		ttyoutput('[', tp);
	}
	ttyoutput(c, tp);
}





zapc(q)
register struct clist *q;
{
	extern struct cblock *cfreelist;
	register struct cblock *bp;
	register char *cp;
	register int c, s;

	s = spl6();
	if(((cp = q->c_cl) == 0) || ((c = 	((*--cp)&0377)) == 0377)){
		splx(s);
		return(-1);
	}
	q->c_cl = cp;
	cp -= sizeof(char *);
	if(--q->c_cc <= 0){
		bp = (struct cblock *) ((int) cp & ~07		);
		q->c_cf = q->c_cl = 0;
		bp->c_next = cfreelist;
		cfreelist = bp;
	}
	else if(((int) cp & 07		) == 0){
		bp = (struct cblock *) cp;
		bp->c_next = cfreelist;
		cfreelist = bp;
		bp = (struct cblock *) q->c_cf;
		bp = (struct cblock *) ((int) bp & ~07		);
		while(bp->c_next != (struct cblock *) cp)
			bp = bp->c_next;
		bp->c_next = 0;
		q->c_cl = &bp->c_info[6		];
	}
	splx(s);
	return(c);
}




ttybit(area, bit)
char *area;
register int bit;
{
	if(bit >0177)
		return(0);
	return(bit < 0 ? 0: area[bit >> 3]&(1 << (bit&07)));
}




ttyspcl(c, tp)
register c;
register struct tty *tp;
{
	register char *cp;
	register n;

	if(c < 0)
		return(0);
	for(cp = &tp->t_un.t_intrc, n = 0; n < sizeof(struct tc); n++){
		if(c == *cp++)
			return(1);
	}
	if((c == tp->t_erase) || (c == tp->t_kill))
		return(1);
	if((c == 022	) || (c == 027	) || ((c == 020	) && tp->t_length))
		return(1);
	if(c == 0X1B)
		return(1);
	return(0);
}




ttyblock(tp)
register struct tty *tp;
{
	register x;
	x = tp->t_rawq.c_cc;
	if(x > 256){
		flushtty(tp);
		tp->t_state &= ~02000		;
	}
	if((x >= 256/2) && ((tp->t_state&02000		) == 0)) {
		if(putc(tp->t_un.t_stopc, &tp->t_outq) == 0){
			tp->t_state |= 02000		;
			ttstart(tp);
		}
	}
}








ttyoutput(c, tp)
register c;
register struct tty *tp;
{
	register char *colp;



	if(tp->t_flags&040000)
	{
		putc(c, &tp->t_outq);
		return;
	}
	


	if(c == '\n' && (tp->t_flags&020))
		ttyoutput('\r', tp);
	if((tp->t_flags&040) == 0){
		


		if((tp->t_flags&01000) && 	(ttybit(cchars, c)) && c != 0x15){
			ttyoutput('^', tp);
			if(c == 0177)
				c = '#';
			else
				c |= 0140;
		}
		


		if(tp->t_length && (tp->t_xstate&01	)){
			if((c == '\n' && (	((++(tp->t_lnum))&0377) >= 	((tp->t_length)&0377))) || c == '\014'){
				tp->t_lnum = 0;
				tp->t_xstate |= 04	;
				putc(0200, &tp->t_outq);
			}
		}
	}
	





	if(tp->t_flags&04){
		if('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if(	('A' <= c && c <= 'Z'))
			ttyoutput('\\', tp);
		else {
			colp = "({)}!|^~'`";
			while(*colp++){
				if(c == *colp++){
					ttyoutput('\\', tp);
					c = colp[-2];
					break;
				}
			}
		}
	}
	



	colp = &tp->t_col;
	


	if(c == '\t' && (tp->t_flags&02000)){

		if(tp->t_htdly == 0)
			tp->t_htdly = tp->t_col;

		do
			ttyoutput(' ', tp);
		while((*colp)&07);
		return;
	}
	if((' ' <= c && c <= '~') || c > 0177 || c == 0x15)
	{
		if(tp->t_width && (	((*colp)&0377) >= 	((tp->t_width)&0377)))
			ttyoutput('\n', tp);
		(*colp)++;
		putc(c, &tp->t_outq);
		return;
	}
	putc(c, &tp->t_outq);
	






	switch(c&0177){
	case '\b':
		if(*colp)
			(*colp)--;
	default:
		return;
	case '\t':
		*colp |= 07;
		(*colp)++;
		c = tp->t_htdly;
		break;
	case '\n':

		if(tp->t_flags&02000)
			tp->t_htdly = 0;

		*colp = 0;
		c = tp->t_nldly;
		break;
	case 013:
	case 014:
		c = tp->t_vtdly;
		break;
	case '\r':
		*colp = 0;
		c = tp->t_crdly;
		break;
	}	
	if(c && ((tp->t_flags&040) == 0))
		putc(c|0200, &tp->t_outq);
}







ttrstrt(tp)
register struct tty *tp;
{
	tp->t_state &= ~01		;
	ttstart(tp);
}







ttstart(tp)
register struct tty *tp;
{
	register s;

	s = spl3();
	if((tp->t_state&(01		|0400		|040		)) == 0)
		(*tp->t_oproc)(tp);
	splx(s);
}





ttread(tp)
register struct tty *tp;
{
	register int c;

	if((tp->t_state&020		) == 0)
		return(0);
	spl3();
	while(((tp->t_flags&(040|02)) == 0 && tp->t_delct == 0)
	      || ((tp->t_flags&(040|02)) != 0 && tp->t_rawq.c_cc == 0)){
		if((tp->t_state&020		) == 0)
			return(tp->t_rawq.c_cc);
		sleep((caddr_t)&tp->t_rawq, 28);
	}
	spl0();
	if(tp->t_flags&(040|02)){
		while(tp->t_rawq.c_cc && passc(getc(&tp->t_rawq)) >= 0);
		goto out;
	}
	while((c = getc(&tp->t_rawq)) != 0377 && passc(c) >= 0);
	if((c != 0377) && 	((*(tp->t_rawq.c_cf))&0377) == 0377)
		c = getc(&tp->t_rawq);
	if(c == 0377)
		tp->t_delct--;
	if(tp->t_length){
		tp->t_xstate |= 01	;
		tp->t_xstate &= ~(02	|04	);
		tp->t_lnum = 0;
	}
	else
		tp->t_xstate &= ~01	;
out:
	if((tp->t_state&02000		) && tp->t_rawq.c_cc < 256/5){
		if(putc(tp->t_un.t_startc, &tp->t_outq) == 0){
			tp->t_state &= ~02000		;
			ttstart(tp);
		}
	}
	return(tp->t_rawq.c_cc);
}





caddr_t
ttwrite(tp)
register struct tty *tp;
{
	register c;

	if((tp->t_state&020		) == 0)
		return(0);
	while(u.u_count){
		spl3();
		while(tp->t_outq.c_cc > 60 || (tp->t_xstate&04	)){
			ttstart(tp);
			if((tp->t_outq.c_cc > 30) || (tp->t_xstate&04	)) {
				tp->t_state |= 0100		;
				sleep((caddr_t)&tp->t_outq, 29);
				if(tp->t_xstate&040	) {
					while ( getc(&tp->t_outq) >= 0) ;
					break;
				}
			}
		}
		spl0();
		if((c = cpass()) < 0)
			break;
		ttyoutput(c, tp);
	}
	ttstart(tp);
	return(0);
}







ttyesc(c,tp)
register char	c;
struct	tty *tp;
{
register struct esc *ep;
int j,found;
register char *tx;

	found = 0;
	putc(c,&tp->t_escq);
	ep = tp->t_nesc;
	for( ; ep->ec != 0177 ; ep++) {
		if((ep->fl&      010) == 0) {
			if(ep->ec == c) {
				found++;
				break;
			}
			else
				continue;
		}
		else {
			tx = ttbl[ep->ec];
			for(j = 0; (tx[j] != 0) && (c != tx[j]); j++) ;
			if(tx[j] == 0)
				continue;
			else {
				found++;
				break;
			}
		}
	}
	if(found) {
		if(ep->fl&      01) {
			while((c = getc(&tp->t_escq)) >= 0) {
				if(ep->fl&      04)
					putc(c,&tp->t_outq);
				if(ep->fl&      02)
					putc(c,&tp->t_rawq);
			}
			tp->t_xstate &= ~ 0100	 ;
			tp->t_nesc = 0 ;
			return(ep->fl&020);
		}
		else 
			tp->t_nesc = ep->nesc;
	}
	else {
		tp->t_xstate &= ~ 0100	 ;
		tp->t_nesc = 0 ;
		while((c = getc(&tp->t_escq)) >= 0) {
			putc(c,&tp->t_rawq);
			ttyecho(c,tp);
		}
		ttstart(tp);
	}
	return(0);
}
 
