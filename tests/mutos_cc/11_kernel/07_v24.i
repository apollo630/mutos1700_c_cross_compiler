






































































































































































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











struct	aspcfg {
		int p_level;		
		int p_ien;		
		int p_reti;		
		int p_rgp;		
		int p_a_dat;		
		int p_b_dat;		
		int p_a_cntl;		
		int p_b_cntl;		
		int p_zk0;		
		int p_zk1;		
		int p_zk2;		
		int p_zk3;		
		int p_dka;		
		int p_dkb;		
		int p_ska;		
		int p_skb;		
};


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










































































struct	tty v24tty[1				];
extern	struct	aspcfg	aspcfg[];

int	ttrstrt();
int	v24intr();
int	v24start();
extern	int	aspaligned;
extern int Stand;
int	nv24 = 1				;
int	v24end = 1;

char	maptab[];

char	v24dev[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


static unsigned wr5;

v24init()
{
	register struct aspcfg *pasp;
	int	a;
	char	b;

	if(aspaligned == 0) {
		pasp = &aspcfg;
		a = 2;
		outb(pasp->p_b_cntl, 0xf);
		outb(pasp->p_b_dat, 0x55);
		b = inb(pasp->p_b_dat);
		outb(pasp->p_b_dat, 0);
		if(b != 0x55)
			a = 1;
		if(Stand == 0)
			printf("ASP      Based %x level %d %s.\n", pasp->p_ien, pasp->p_level, (a == 2)?"found":"NOT found");
		aspaligned = a;
		if(a == 2)
			outb(pasp->p_ien, 0);			
	}
}

v24open(dev, flag)
dev_t	dev;
{
	register struct tty *tp;
	register struct aspcfg *pasp;
	register int	d;
	int	s;

	d = 	(int)((dev)&0377);
	if((d >= 1				) || (aspaligned < 2)) {
		u.u_error = 6;
		return;
	}
	tp = &v24tty[d];
	pasp = &aspcfg[d];
	tp->t_oproc = v24start;
	if ((tp->t_state&04		) == 0) {
		if(tp->t_ispeed == 0) {
			tp->t_ispeed = tp->t_ospeed = 	13;
			tp->t_flags = 02000|020|0100;
			vchanini(dev,tp);
		}
		tp->t_state |= 04		|020		;
		ttychars(tp);
	}
	else if (tp->t_state&0200		 && u.u_uid != 0){
		u.u_error = 16;
		return;
	}
	s = spl3();
	inb(pasp->p_ska);		
	outb(pasp->p_ska, (	01 | 	060	)) ;
	outb(pasp->p_ska, (	030	 | 	04	 | 	02	));
	inb(pasp->p_skb);
	outb(pasp->p_skb, 	01);
	outb(pasp->p_skb, (	030	 | 	04	 | 	02	)) ;
	outb(pasp->p_reti, 0100) ;
	splx(s);
	ttyopen(dev,tp);
}

v24close(dev,flag)
dev_t	dev;
int	flag;
{
	register struct tty *tp;
	register struct aspcfg *pasp;
	register int	d;
	int	s;

	d = 	(int)((dev)&0377);
	tp = &v24tty[d];
	pasp = &aspcfg[d];
	if ((tp->t_state&04		) == 0)
		return;
	tp = &v24tty[	(int)((dev)&0377)];
	ttyclose(tp);
	s = spl3();
	inb(pasp->p_ska);		
	outb(pasp->p_ska, (	01 | 	060	)) ;
	outb(pasp->p_ska, 0);		
	splx(s);
}

v24read(dev)
dev_t	dev;
{
	ttread(&v24tty[	(int)((dev)&0377)]);
}

v24write(dev)
dev_t	dev;
{
	ttwrite(&v24tty[	(int)((dev)&0377)]);
}

v24intr(level)
int	level;
{
	register struct tty *tp;
	register struct aspcfg *pasp;
	register	int	c,d;
	int	temp;

	
	d = v24dev[level];
	tp = &v24tty[d];
	pasp = &aspcfg[d];
	if ((struct	device	*)tp->t_addr == 0) {
		tp->t_oproc = v24start;
		}
	inb(pasp->p_ska); 	

loop1:
	if ((temp = inb(pasp->p_ska))&	01	) {

    
    outb(pasp->p_ska, 	05);         
    outb(pasp->p_ska, wr5 & 0xfd);  

		v24end = 1;
		c = inb(pasp->p_dka);
		outb(pasp->p_ska, 	01) ;
		if ((inb(pasp->p_ska))&(	0100	|	040	|	020	)) {
			outb(pasp->p_ska, 	060	);
		}
		if((tp->t_flags&04000) && ((c&0177) == 0X14)) {
			tp->t_xstate |= 040	;
			tp->t_state |= 0400		;
			}
		else {
			putc(c, &tp->t_inpq);
			v24end++;
			goto loop1;
		}
	}
	v24end = 0;
	if (temp & 	04	 ) {   
		outb(pasp->p_ska, 	050	) ;
		if((tp->t_state&(04		|020		)) == (04		|020		)) {
			ttstart(tp);
			if (tp->t_outq.c_cc == 0 || tp->t_outq.c_cc == 30)
				wakeup((caddr_t)&tp->t_outq);
		}
	}
	if(v24end)
		goto loop1;
reti:
	while((c = getc(&tp->t_inpq)) >= 0) {
		ttyinput(c, tp);
		if(inb(pasp->p_ska)&	01	)
			goto loop1;
	}
	if(inb(pasp->p_ska)&	01	)
		goto loop1;

  
  outb(pasp->p_ska, 	05);         
  outb(pasp->p_ska, wr5 | 0x02);  

	outb(pasp->p_reti, 0100);
	if(tp->t_xstate&040	){
		while (getc(&tp->t_outq) >= 0);
		wakeup((caddr_t)&tp->t_outq);
	}
}


v24ioctl(dev,cmd,addr,flag)
caddr_t	addr;
dev_t	dev;
{
	register struct tty *tp;
	int speed;
	unsigned flags;

	tp = &v24tty[	(int)((dev)&0377)];
	speed = tp->t_ispeed;
	flags = tp->t_flags;
	if (ttioccom(cmd,&v24tty[	(int)((dev)&0377)],addr,dev) == 0) {
		u.u_error = 25;
		return;
	}
	if((speed != tp->t_ispeed) || ((flags&(020000|0100|0200)) != (tp->t_flags&(020000|0100|0200))))
		vchanini(dev,tp);
}

v24start(tp)
register struct tty *tp;
{
	register struct aspcfg *pasp;
	register c,d;

	d = 	(int)((tp->t_dev)&0377);
	pasp = &aspcfg[d];
	inb(pasp->p_ska);		
	if(((inb(pasp->p_ska)&	04	) == 0)
	|| (tp->t_state&0400		)
	|| (tp->t_xstate&02	))
		goto	ret;
	if((c = getc(&tp->t_outq)) >= 0){
		v24end++;
		if(tp->t_flags&040)
			outb(pasp->p_dka, c) ;
		else if(c <= 0177)
			outb(pasp->p_dka, c) ;
		else if(c == 0200)
			tp->t_xstate |= 02	;
		else {
			timeout(ttrstrt, (caddr_t)tp, (c&0177) + 4);
			tp->t_state |= 01		;
		}
	}
	else if(tp->t_state&0100		){
		tp->t_state &= ~0100		;
		wakeup((caddr_t)&tp->t_outq);
	}
ret:
	outb(pasp->p_reti, 0100);
	return;
}

vchanini(dev,tp)
dev_t	dev;
register	struct	tty 	*tp;

{
	register struct aspcfg *pasp;
	register	d,s,o,zk;

	s = spl3();
	d = 	(int)((dev)&0377);

	pasp = &aspcfg[d];
	switch(tp->t_ispeed) {
		case 	13:
		case 	9:
		case 	5:
		case 	1:
			o = 0100;	
			break;
		case 	12:
		case 	8:
		case 	2:
			o = 0200;
			break;
		case 	11:
		case 	7:
		case 	6:
			o = 0300;
			break;
		default:
			u.u_error = 22;
			return;
	}
	switch(tp->t_ispeed) {
		default:
		case 	13:
		case 	12:
		case 	11:
				zk = 1;
				break;
		case 	9:
		case 	8:
		case 	7:
				zk = 8;
				break;
		case 	6:
				zk = 12;
				break;
		case 	5:
		case 	2:
				zk = 64;
				break;
		case 	1:
				zk = 196;
				break;
	}
	inb(pasp->p_ska);	
	outb(pasp->p_ska, 	030	) ;
	outb(pasp->p_ska, 	04) ;
	outb(pasp->p_zk1, 3); 	
	outb(pasp->p_zk1, 7);	
	outb(pasp->p_zk1, zk);
	if(tp->t_flags&(0100|0200) && ((tp->t_flags&(0100|0200)) != (0100|0200)))
		o |= (tp->t_flags&0100) ? 01 : 03;
	o  |= 04;
	outb(pasp->p_ska, o) ;

	outb(pasp->p_ska, 	05) ;
	o = 050;			
	if(tp->t_flags&020000)
		o |= 0100;	
	o |= 0202;		
	outb(pasp->p_ska, o) ;
  wr5 = o;      

	o = 0100;
	outb(pasp->p_ska, 	03) ;
	if(tp->t_flags&020000) 
		o |= 0200;		
	outb(pasp->p_ska, o);
	outb(pasp->p_ska, 	03) ;
	outb(pasp->p_ska, (o|	01	)) ;

	inb(pasp->p_skb);

	outb(pasp->p_skb, 	02) ;
	outb(pasp->p_skb, 	0x70		) ; 
	outb(pasp->p_ska, 	01) ;
	outb(pasp->p_ska, (	030	 | 	04	 | 	02	)) ; 
	outb(pasp->p_skb, 	01) ;
	outb(pasp->p_skb, (	030	 | 	04	 | 	02	)) ;
	outb(pasp->p_reti, 0100) ;
	splx(s);
}

