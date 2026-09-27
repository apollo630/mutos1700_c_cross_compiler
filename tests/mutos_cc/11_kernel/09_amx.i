











































struct firmAMX {
	char  cmd;		
        char  status;		
        char  cmdsem;           
        char  stsem;            
        char  cunit;            
        char  sunit;            
        char  oper;             
        char  deverr;           
        int   firstl;           
        int   lsize;            
        char  nlines;           
        char  intenb;           
        int   vers;             
        char  diagn;            
        char  diage;            
        int   joaddr;           
        char  portno;           
        char  portv;            
        struct {
		char  enb;      
		char  parm;     
		char  state;    
		char  error;    
		int   ibaud;    
		int   obaud;    
		int   iba;      
		int   ibs;      
		int   ibp;      
		int   ibc;      
		int   ibn;      
		int   oba;      
		int   obs;      
		int   obp;      
		int   obc;      
		int   obn;      
		int   obl;      
	}line[4];
};






struct amxcfg{
	long    c_base;
};






struct ibuf {
	long	ibuf_addr;
        int	ibuf_size;
};

struct obuf {
        long	obuf_addr;
        int	obuf_size;
};





struct tout {
	int	index;
	int	size;
	char	outchar;
};






struct amxoff {
	int o_cmd;
	int o_status;
	int o_cunit;
	int o_sunit;
	struct {
		int o_enb;
		int o_parm;
		int o_ibaud;
		int o_ibp;
		int o_ibc;
		int o_ibn;
		int o_obp;
		int o_obc;
		int o_obn;
	}o_line[4];
};



































































 



































 






































































































































































































































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














































































































































































int	amxdebug = 0;			    






extern int		namx;		
extern struct	tty	amxtty[];	
extern struct   firmAMX amxfirm[];     
extern struct	amxcfg	amxcfg[];	
extern struct   amxoff amxoff[];	
extern struct   ibuf    amxibuf[];     
extern struct   obuf    amxobuf[];     
extern struct 	tout	amxtout[];	
extern char		amxcmd[];	
extern char		amxscd[];	
extern char		amxi_buf[];  	
extern char     	*iobuffer[];  	
extern int		amxalive[];	
extern long             amxladdr[];    
extern int		amxlevel;	
int			amxsleep;
int			amxwakeup;	



















amxinit()
{
	register int  board;


	if(amxdebug >= 3)
		printf("amx-init ");


	amxprobe();				
	for(board=0; board<namx; board++) {	
		if (amxalive[board])		
			amxcal(board);		
	}
}



















amxprobe()
{
	register struct amxcfg *cf;
	register struct	firmAMX *Fbase;
	int		alive, board, s;


	if(amxdebug >= 3)
		printf("amx-probe ");




	 for(board=0; board<namx; board++){		 
		cf = &amxcfg[board];			 
		if(cf->c_base == (long)0x0)
			continue;			 
        	Fbase = &amxfirm[board]; 		  
		s = spl7();
		Fbase->status =    0;			 
                Fbase->cmd =    0x01;			 
                amxpoke(0,cf->c_base,2,&Fbase->cmd);    
		splx(s);
	}

        delay(500);					 



	for(board=0; board<namx; board++){		 
		cf = &amxcfg[board];
		if(cf->c_base == (long)0x0)
			continue;			 
		alive = 0;				 
        	Fbase = &amxfirm[board]; 		  
                amxpeek(1,cf->c_base,1,&Fbase->status); 
                if (Fbase->status ==   0x01) 		 
			alive = 1;            		 

		printf("AMX      Based %x level %d %s\n",(unsigned int)(cf->c_base/(long)16),amxlevel,alive ? "found" : "NOT found" );

		amxalive[board] = alive;		
	}
}





















amxcal(board)
int	board;
{
	register struct firmAMX *Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	int		bnum, i, j, s, temp_off;


	if(amxdebug >= 3)
		printf("amx-cal ");


	Fbase = &amxfirm[board];			
	cf = &amxcfg[board];				



	temp_off = (int)((char *)&Fbase->firstl - (char *)&Fbase->cmd);
	amxpeek(temp_off,cf->c_base,4,&Fbase->firstl);










	bnum = board * 4;
	for (i=bnum, j=0; i<bnum+4; i++, j++) {
		amxladdr[i] = cf->c_base + (long)(Fbase->firstl + Fbase->lsize * j);
		temp_off = (int)((char *)&Fbase->line[j].iba - (char *)&Fbase->line[j].enb);
		amxpeek(temp_off,amxladdr[i],14,&Fbase->line[j].iba);
		amxibuf[i].ibuf_addr = cf->c_base + (long)Fbase->line[j].iba;
		amxibuf[i].ibuf_size = Fbase->line[j].ibs;
		amxobuf[i].obuf_addr = cf->c_base + (long)Fbase->line[j].oba;
		amxobuf[i].obuf_size = Fbase->line[j].obs;
	}
        




	of = &amxoff[board];			

	

	of->o_cmd = (char *)&Fbase->cmd - (char *)&Fbase->cmd;
	of->o_status = (char *)&Fbase->status - (char *)&Fbase->cmd;
	of->o_cunit = (char *)&Fbase->cunit - (char *)&Fbase->cmd;
	of->o_sunit = (char *)&Fbase->sunit - (char *)&Fbase->cmd;

	

	for(j=0; j<4; j++) {
		of->o_line[j].o_enb = (char *)&Fbase->line[j].enb - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_parm = (char *)&Fbase->line[j].parm - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_ibaud = (char *)&Fbase->line[j].ibaud - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_ibp = (char *)&Fbase->line[j].ibp - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_ibc = (char *)&Fbase->line[j].ibc - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_ibn = (char *)&Fbase->line[j].ibn - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_obp = (char *)&Fbase->line[j].obp - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_obc = (char *)&Fbase->line[j].obc - (char *)&Fbase->line[j].enb;
		of->o_line[j].o_obn = (char *)&Fbase->line[j].obn - (char *)&Fbase->line[j].enb;
	}



	s = spl7();
	Fbase->status =    0;
	amxpoke(1,cf->c_base,1,&Fbase->status);
	Fbase->intenb =  0x01;
	temp_off = (int)((char *)&Fbase->intenb - (char *)&Fbase->cmd);
	amxpoke(temp_off,cf->c_base,1,&Fbase->intenb);
	splx(s);
}



















int amxbaud[] = {
	0 ,	0,	0,		110,	0,
		150,	0,		300,		600,	1200,
	0,	2400,	4800,	9600,	0,
	0
};

amxparam(dev)
dev_t dev;
{
	register struct tty *tp;
	register struct	firmAMX	*Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	int		board, l_num, s, speed, unit, x;
	int modem;

	unit = 	(int)((dev)&0377) &        0x1F	;			
	modem = 	(int)((dev)&0377) &        0xC0    ;


	if (amxdebug >= 2)
		printf("amx-param unit %d ",unit);


        board = unit >> 2;				
	tp = &amxtty[unit];				
        Fbase = &amxfirm[board];			
	l_num = unit & 03;				
	cf = &amxcfg[board];				
	of = &amxoff[board];				
	s = (int)tp->t_ospeed;

	if(s==0) {					

		










		while (amxcmd[board] & 01	) {	

			if(amxdebug >= 2)
				printf("param wait on hangup unit %d ",unit);

			amxscd[unit] |= 04;	
			amxsleep++;
			sleep((caddr_t)&amxscd[unit],28);
		}

			if(amxdebug >= 2)
				printf("amx-param hangup signal to unit %d ",unit);

		x = spl7();
		Fbase->cunit = l_num;
		Fbase->cmd =   0x09;
		amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
		amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		Fbase->line[l_num].parm = (Fbase->line[l_num].parm & ~  0x01) |  0x10;
		Fbase->cmd =    0x04;
		amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
		amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
		amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		amxcmd[board] |= 01	;
		splx(x);
		return;
	}

	speed = amxbaud[s];				
	if ((s > 15	) || ((s != 0) && (speed == 0))) {
		u.u_error = 22;			
		return;
	}








	if(Fbase->line[l_num].ibaud != speed) {
        	while (amxcmd[board] & 01	) {		

			if(amxdebug >= 2)
				printf("param wait on baud change unit %d ",unit);

			amxscd[unit] |= 04;		
			amxsleep++;
			sleep((caddr_t)&amxscd[unit],28);
		}


			if(amxdebug >= 2)
				printf("amx-param baud rate change unit %d ",unit);

		x = spl7();
		Fbase->cunit = l_num;
		Fbase->cmd =   0x09;
        	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
        	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		amxcmd[board] |= 01	;
		splx(x);
	}
		

        while (amxcmd[board] & 01	) {		

		if(amxdebug >= 2)
			printf("param wait on unit %d ",unit);

		amxscd[unit] |= 04;		
		amxsleep++;
		sleep((caddr_t)&amxscd[unit],28);
	}













	Fbase->line[l_num].parm &= (~   0x02&~  0x04);
	if(((tp->t_flags&0200) && (tp->t_flags&0100)) || (((tp->t_flags&0200) == 0) && ((tp->t_flags&0100) == 0)))
		;
	else
		(tp->t_flags&0200) ? (Fbase->line[l_num].parm |=   0x04) : (Fbase->line[l_num].parm |=    0x02);

	if(tp->t_flags&(040|020000))
		Fbase->line[l_num].parm |= 040000;
	else
		Fbase->line[l_num].parm &= ~040000;
	x = spl7();
        Fbase->line[l_num].ibaud = speed;
	Fbase->line[l_num].obaud = 0;
	Fbase->line[l_num].parm |=   0x01;
	if(modem == 0)
		Fbase->line[l_num].parm |= 0x08;
	else
		Fbase->line[l_num].parm &= ~0x08;
	Fbase->line[l_num].parm &= ~ 0x10;
	Fbase->cmd =    0x04;
	Fbase->cunit = l_num;
        amxpoke(of->o_line[l_num].o_ibaud,amxladdr[unit],4,&Fbase->line[l_num].ibaud);
        amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
        amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
        amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	amxcmd[board] |= 01	;
	splx(x);
}



















int amxstart();

amxopen(dev)
dev_t	dev;
{
	register struct tty *tp;
	register struct	firmAMX *Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	register int 	unit;
	int		board, i, l_num,  modem, s, temp_off;

	unit = 	(int)((dev)&0377) &        0x1F	;


	if (amxdebug >= 2)
		printf("amx-open unit %d ",unit);


	if (unit >= (namx*4)) {			
		u.u_error = 6;
		return;
	}
	if (amxalive[unit/4] == 0) {		
		u.u_error = 6;
		return;
	}

	tp = &amxtty[unit];			
	board = unit >> 2;			
        Fbase = &amxfirm[board];		
	Fbase->cunit = l_num = unit & 03;	
	cf = &amxcfg[board];			
	of = &amxoff[board];			
	modem = 	(int)((dev)&0377) &        0xC0    ;		
        tp->t_addr = (caddr_t)unit;             
	tp->t_oproc = amxstart;

	if ((tp->t_state & 04		) == 0 ) {
		ttychars(tp);
		if(tp->t_ispeed == 0)
		{
			tp->t_ispeed =  tp->t_ospeed = 	13	;		
			tp->t_flags = 0100 | 0200 | 010 | 020;	
		}
		amxparam(dev);			     



		s = spl7();
		Fbase->line[l_num].obl = 256;
		Fbase->line[l_num].enb =  0x01| 0x02;
		temp_off = (int)((char *)&Fbase->line[l_num].obl - (char *)&Fbase->line[l_num].enb);
		amxpoke(temp_off,amxladdr[unit],2,&Fbase->line[l_num].obl);
		amxpoke(of->o_line[l_num].o_enb,amxladdr[unit],1,&Fbase->line[l_num].enb);
		splx(s);

		if (modem &       0x40    ) {
			temp_off = (int)((char *)&Fbase->line[l_num].state - (char *)&Fbase->line[l_num].enb);
                        i = 0;
			while (i == 0) {
				amxpeek(temp_off,amxladdr[unit],2,&Fbase->line[l_num].state);
                                if (Fbase->line[l_num].state &   0x02 == 0)
					sleep((caddr_t)&amxwakeup,28);
                                else
                                        i = 1;
                        }
		}
	}

	if (tp->t_state & 0200		 && u.u_uid != 0) {
		u.u_error = 16;
		return;
	}

	tp->t_state |= 020		;				
	(*linesw[tp->t_line].l_open)(dev, tp);	
}




















amxclose(dev)
dev_t	dev;
{
	register struct tty *tp;
	register struct	firmAMX  *Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	register int	unit;
	int		board, i, l_num, s;

	unit = 	(int)((dev)&0377) &        0x1F	;			


	if(amxdebug >= 2)
		printf("amx-close unit %d ",unit);


	tp = &amxtty[unit];				
	board = unit >> 2;				
	Fbase = &amxfirm[board];			
	l_num = unit & 03;				
	cf = &amxcfg[board];				
	of = &amxoff[board];				

	if (unit < namx*4) {

		






		i = 0;
		while(i == 0) {
			amxpeek(of->o_line[l_num].o_obc,amxladdr[unit],2,&Fbase->line[l_num].obc);
			if((Fbase->line[l_num].obc != (amxobuf[unit].obuf_size -1)) || (amxcmd[board] & 01	)) {		

				if(amxdebug >= 2)
					printf("close wait on unit %d ",unit);

				amxscd[unit] |= 010;	
				amxsleep++;
				sleep((caddr_t)&amxscd[unit], 28);
			}else
				i = 1;
		}

		

		if(tp->t_state & 01000		)
			tp->t_state &= ~020		;


		

		s = spl7();
                Fbase->line[l_num].parm = (Fbase->line[l_num].parm & ~  0x01) |  0x10;
                Fbase->cmd =    0x04;
		Fbase->cunit = l_num;
                amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
                amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		amxcmd[board] |= 01	;
		splx(s);

		(*linesw[tp->t_line].l_close)(tp);		
		ttyclose(tp);
	}
}















amxread(dev)
dev_t	dev;
{
	register struct tty *tp;			
	register int unit;

	unit = 	(int)((dev)&0377) &        0x1F	;			


	if(amxdebug >= 3)
		printf("amx-read on unit %d ",unit);


	tp = &amxtty[unit];				
	(*linesw[tp->t_line].l_read)(tp);		
}

















amxwrite(dev)
dev_t	dev;
{
	register struct tty *tp;			
	register int unit;

	unit = 	(int)((dev)&0377) &        0x1F	;


	if(amxdebug >= 3)
		printf("amx-write on unit %d ",unit);


	tp = &amxtty[unit];			
	(*linesw[tp->t_line].l_write)(tp);		
}
 

















st_inp(tp)
struct tty *tp;
{
	register struct firmAMX *Fbase;
	register struct amxcfg *cf;
	register struct amxoff *of;
	register unsigned board;
	unsigned	l_num;
	int		s, unit;

	unit = (int)tp->t_addr;		
	board = unit >> 2;		
	l_num = unit % 3;		
	Fbase = &amxfirm[board];	
	cf = &amxcfg[board];		
	of = &amxoff[board];		

	do {				
		amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	}while(Fbase->cmd != 0);

	




	s = spl7();
	Fbase->line[l_num].ibn = 0;
	Fbase->cunit = l_num;
	Fbase->cmd =    0x02;
	amxpoke(of->o_line[l_num].o_ibn,amxladdr[unit],2,&Fbase->line[l_num].ibn);
	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	splx(s);
	amxcmd[board] |= 01	;
}















int	wakeup();
int 	ttrstrt();

amxintr(level)
int	level;
{
	register struct tty *tp;
	register struct	firmAMX *Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	register char	c;
	int	 	board, count, gotone, i, l_num, libc,
			libp, nch, s, scd, status, unit;


	if(amxdebug >= 2)
		printf("amx-intr ");


	do {
		gotone=0;				
		for(board=0; board<namx; board++) {	
			if(amxalive[board] == 0)	
				continue;
			Fbase = &amxfirm[board];	
			cf = &amxcfg[board];		
			of = &amxoff[board];		






                        amxpeek(of->o_status,cf->c_base,6,&Fbase->status);
			if (Fbase->status ==    0)		 	
				continue;
			status = Fbase->status;

			unit = board * 4 + Fbase->sunit;        
			tp = &amxtty[unit];			
			l_num = Fbase->sunit;		  	

	 		gotone++;			 	

			if(amxdebug >= 2)
				printf("status %d on unit %d\n",Fbase->status,unit);



			



			s = spl7();
                        Fbase->status =    0;
			amxpoke(of->o_status,cf->c_base,1,&Fbase->status);
			splx(s);

                        switch(status) {
			






                        case   0x01 : 
					if (amxscd[unit] & 	01){
						amxscd[unit] &= ~	01;
						c = amxi_buf[unit];
						(*linesw[tp->t_line].l_rint)(c,tp);
					}
					amxcmd[board] &= ~01	;
                                       	break;
			


                        case   0x02 :
					amxcmd[board] &=~01	;
                                       	break;
			














                        case    0x03  :
					if(tp->t_rawq.c_cc >= (256- 1)) {
						timeout(st_inp,tp,	2	);
					 	wakeup((caddr_t)&tp->t_rawq);
						break;
					}
					amxpeek(of->o_line[l_num].o_ibp,amxladdr[unit],4,&Fbase->line[l_num].ibp);
					libp = Fbase->line[l_num].ibp;
					libc = Fbase->line[l_num].ibc;
					



					amxscd[unit] |= 	01;
					amxpeek(libp,amxibuf[unit].ibuf_addr,1,&amxi_buf[unit]);
					





					do {
						amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					}while(Fbase->cmd != 0);
					s = spl7();
                                       	Fbase->cunit = l_num;
					Fbase->line[l_num].ibn = 1;
                                       	Fbase->cmd =    0x02;
                                      	amxpoke(of->o_line[l_num].o_ibn,amxladdr[unit],2,&Fbase->line[l_num].ibn);
                                      	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                                       	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					splx(s);
					amxcmd[board] |= 01	;
				      	break; 
			







			case   0x04 :
					if(tp->t_state & 01		) {
						if (Fbase->line[l_num].obc != (amxobuf[unit].obuf_size - 1))
							break;
						else
							timeout(ttrstrt, (caddr_t)tp, amxtout[unit].outchar);
					}
					tp->t_state &= ~040		;
					(*linesw[tp->t_line].l_start)(tp);
					if((tp->t_state & 0100		) && (tp->t_outq.c_cc <= 30) && (!(tp->t_state & 01		))) {
						tp->t_state &= ~0100		;
						wakeup((caddr_t)&tp->t_outq);
                                        }
					break;
			






			case     0x05   :
                                        wakeup((caddr_t)&amxwakeup);
					s = spl7();
                                        Fbase->line[l_num].parm |=   0x01;
					Fbase->line[l_num].parm &= ~ 0x10;
                                        Fbase->cunit = l_num;
                                        Fbase->cmd =    0x04;
					amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
                                        amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                                        amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					splx(s);
					amxcmd[board] |= 01	;
					break;
			







			case   0x06 : 
                                      	if ((tp->t_state & (020		|04		)) == (020		|04		))
                                      		signal(tp->t_pgrp,1	);
                                        tp->t_state &= ~020		;
					s = spl7();
                                        Fbase->line[l_num].parm = (Fbase->line[l_num].parm & ~  0x01) |  0x10;
                                        Fbase->cunit = l_num;
                                        Fbase->cmd =    0x04;
					amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
                                        amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                                        amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					splx(s);
					amxcmd[board] |= 01	;
                                        break;
			



			case   0x07:
					break;
			} 
		}
	} while(gotone);



	if(amxsleep > 0) {
		for(unit=0; unit<namx*4; unit ++) {
			if(amxscd[unit] & (010|04)) {
				amxsleep--;
				scd = amxscd[unit];
				if(scd & 010)
					scd &= ~010;
				else
					scd &= ~04;
				amxscd[unit] = scd;
				wakeup((caddr_t)&amxscd[unit]);
			}
		}
	}
}



















char partab[] =
{
       	0x8101	,0x181	,0x181	,0x8101	,0x482	,0x8103	,0x8605	,0x181,
	0x181	,0x8101	,0x8101	,0x181	,0x8101	,0x181	,0x181	,0x8101,
	0x80	,0x8000	,0x8000	,0x80	,0x8000	,0x80	,0x80	,0x8000,
	0x8000	,0x80	,0x80	,0x8000	,0x80	,0x8000	,0x8000	,0x80,
	0x80	,0x8000	,0x8000	,0x80	,0x8000	,0x80	,0x80	,0x8000,
	0x8000	,0x80	,0x80	,0x8000	,0x80	,0x8000	,0x8000	,0x80,
	0x8000	,0x80	,0x80	,0x8000	,0x80	,0x8000	,0x8000	,0x80,
	0x80	,0x8000	,0x8000	,0x80	,0x8000	,0x80	,0x80	,0x8100,
};

amxstart(tp)
struct tty *tp;
{
	register char   *bufp;
	register struct firmAMX	*Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	register char	*buff;
        int		bnum, cntl, i, io_count, l_num, limit, s, unit;





	s = spl5();
	if(tp->t_state & (01		|040		)) {
		splx(s);
		return;
	}
	tp->t_state |= 040		;
	splx(s);


	if(amxdebug>=2)
		printf("amxstart: called on unit %d\n",tp->t_addr);


	unit = (int)tp->t_addr;			
        bnum = unit >> 2;               	
	l_num = unit & 03;			
	Fbase = &amxfirm[bnum];		
	of = &amxoff[bnum];			
	cf = &amxcfg[bnum];			
	buff = iobuffer[unit];			







	amxpeek(of->o_line[l_num].o_obp,amxladdr[unit],4,&Fbase->line[l_num].obp);
	if (Fbase->line[l_num].obc > 256)
		limit = 256;
	else
		limit = Fbase->line[l_num].obc;



	








	if((amxscd[unit] & 02	) != 02	) {


		if(amxdebug >= 3)
			printf("amx-start, no timeout pending\n");

		io_count = q_to_b(&tp->t_outq, buff, limit);
		if(io_count == 0) {
			tp->t_state &= ~040		;
			return;
		}
		if (tp->t_flags & 040)  {

			if(amxdebug >= 3)
				printf("amx-start, RAW mode\n");

	       		amxbpoke(unit,0,io_count,Fbase->line[l_num].obp,buff);
			do {				
				amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
			}while(Fbase->cmd != 0);
			if(amxcmd[bnum] & 01	)
				amxintr(3);
			s = spl7();
        		Fbase->line[l_num].obn = io_count;
			Fbase->cunit = l_num;
        		Fbase->cmd =   0x03;
        		amxpoke(of->o_line[l_num].o_obn,amxladdr[unit],2,&Fbase->line[l_num].obn);
        		amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
        		amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
			amxcmd[bnum] |= 01	;
			splx(s);
			return;
		}	
		amxtout[unit].index = 0;
		amxtout[unit].size = io_count;
	}

	





	if(amxtout[unit].index == 256) {
		io_count = q_to_b(&tp->t_outq, buff, limit);
		if(io_count == 0) {
			tp->t_state &= ~040		;
			return;
		}
		amxtout[unit].index = 0;
		amxtout[unit].size = io_count;
	}

	







	if(amxdebug >= 3)
		printf("amxstart: scan timeout\n");

	bufp = &buff[amxtout[unit].index];
	for(i=amxtout[unit].index;i<amxtout[unit].size;i++) {
		if(*bufp <= 0x7f)

			*bufp++;
		else {
			amxtout[unit].index = i;
			break;
		}
	}

	





	amxbpoke(unit,amxtout[unit].index,i,Fbase->line[l_num].obp,buff);
	do {				
		amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	}while(Fbase->cmd != 0);
	if(amxcmd[bnum] & 01	)
		amxintr(3);
	s = spl7();
       	Fbase->line[l_num].obn = i;
	Fbase->cunit = l_num;
       	Fbase->cmd =   0x03;
       	amxpoke(of->o_line[l_num].o_obn,amxladdr[unit],2,&Fbase->line[l_num].obn);
       	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
       	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	amxcmd[bnum] |= 01	;
	splx(s);

	




	if(i == amxtout[unit].size)
		amxscd[unit] &= ~02	;
	else{

	



		amxscd[unit] |= 02	;
		tp->t_state |=01		;
		tp->t_state &= ~040		;
		amxtout[unit].outchar = *buff & 0x7f;
	}
}
















amxioctl(dev, comd, addr, flag)
caddr_t addr;
{
	struct tty *tp;
	struct firmAMX *Fbase;
	struct amxoff *of;
	int	board, l_num, s, unit;

	unit = 	(int)((dev)&0377) &        0x1F	;				


	if(amxdebug >= 2)
		printf("amx-ioctl unit %d ",unit);


	tp = &amxtty[unit];					

	if (ttioccomm(comd, tp, addr, dev)) {
		board = unit >> 2;
		l_num = unit & 03;
		Fbase = &amxfirm[board];
		of = &amxoff[board];
	
		if (comd==(('t'<<8)|11) || comd ==(('t'<<8)|17))
			amxparam(dev);
		if(tp->t_flags & 040) {				
			s = spl7();
        		Fbase->line[l_num].enb =  0x01;
        		amxpoke(of->o_line[l_num].o_enb,amxladdr[unit],1,&Fbase->line[l_num].enb);
			splx(s);
		}else {						
			s = spl7();
        		Fbase->line[l_num].enb =  0x01 |  0x02;
        		amxpoke(of->o_line[l_num].o_enb,amxladdr[unit],1,&Fbase->line[l_num].enb);
			splx(s);
		}
	}else
		u.u_error = 25;
}
 























amxbpoke(unit,index,cnt,oset,bufp)
int  unit, index, cnt, oset;
char *bufp;
{

	int	byte_rem, cntl, s, temp_cnt;


	if (amxdebug >= 2)
		printf("amx-bpoke unit %d ",unit);


	byte_rem = amxobuf[unit].obuf_size - oset;
	cntl = 0;
	if (cnt > byte_rem) {
		temp_cnt = byte_rem;
		cntl = 1;
	}else
		temp_cnt = cnt;

	s = spl7();
	amxpoke(oset,amxobuf[unit].obuf_addr,temp_cnt,&bufp[index]);
	if (cntl) {
		byte_rem = cnt - temp_cnt;
		amxpoke(0,amxobuf[unit].obuf_addr,byte_rem,&bufp[index+temp_cnt]);
	}
	splx(s);
}
 























	

amxpoke(offset,selector,count,src_off)
register char	*src_off;
int	offset, count;
long	selector;
{
	register char	*p;
	int	x, ds, c;
	int	i, s;
	long	temp;
        short	pgnum, p_offset;


	if (amxdebug >=2)
		printf("amx poke ");


	temp = selector + (long)offset;

	ds = 0;
	if(temp&0xffff0000)
	{
		x = temp % (long)2048		;
		ds = temp / (long)2048		;
		ds =  (((unsigned)(ds))<<7);
		temp = x;
	}
	if(ds)
	{
		for(i=0; i<count; i++)
		{
			c = spl7();
			setbyte(ds,(int)temp++,*src_off++);

	if(amxdebug >= 3)
		printf("\nds=%x offset=%d wert=%x\n",ds,(int)(temp -1),*(src_off -1));

			splx(c);
		}
	}
	else
	{
		p = (int)temp;
		for (i=0; i<count; i++)
       			*p++ = *src_off++;
	}



	if(amxdebug >= 4)
		monitor();

}






















	


amxpeek(offset,selector,count,dest_off)
register char 	*dest_off;
int	offset, count;
long	selector;
{
        register char	*p;
	int	x, ds, c;
        int	i,s;
	long	temp;	
        short	pgnum,p_offset;


	if(amxdebug >= 2)
		printf("amx peek ");


	temp = selector + (long)offset;

	ds = 0;
	if(temp&0xffff0000)
	{
		x = temp % (long)2048		;
		ds = temp / (long)2048		;
		ds =  (((unsigned)(ds))<<7);
		temp = x;
	}
	if(ds)
	{
		for(i=0; i<count; i++)
		{
			c = spl7();
			x = getbyte(ds,(int)temp++);

	if(amxdebug >= 3)
		printf("\nds=%x offset=%d wert=%x\n",ds,(int)(temp -1),x);


			splx(c);
			*dest_off++ = x;
		}
	}
	else
	{
		p = (int)temp;
		for (i=0; i<count; i++)
       			*dest_off++ = *p++;
	}


	if(amxdebug >= 4)
		monitor();

}


struct cblock {
	struct cblock *c_next;
	char	c_info[6		];
};

extern struct	cblock	cfree[];
struct	cblock	*cfreelist;





q_to_b(q, cp, cc)
register struct clist *q;
register char *cp;
{
	register struct cblock *bp;
	register int s;
	char *acp;

	if (cc <= 0)
		return(0);
	s = spl6();
	if (q->c_cc <= 0) {
		q->c_cc = 0;
		q->c_cf = q->c_cl = 0;
		splx(s);
		return(0);
	}
	acp = cp;
	cc++;

	while (--cc) {
		*cp++ = *q->c_cf++;
		if (--q->c_cc <= 0) {
			bp = (struct cblock *)(q->c_cf-1);
			bp = (struct cblock *)((int)bp & ~07		);
			q->c_cf = q->c_cl = 0;
			bp->c_next = cfreelist;
			cfreelist = bp;
			break;
		}
		if (((int)q->c_cf & 07		) == 0) {
			bp = (struct cblock *)(q->c_cf);
			bp--;
			q->c_cf = bp->c_next->c_info;
			bp->c_next = cfreelist;
			cfreelist = bp;
		}
	}
	splx(s);
	return(cp-acp);
}
