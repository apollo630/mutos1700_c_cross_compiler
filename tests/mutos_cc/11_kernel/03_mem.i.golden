


































































































































































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







































mmread(dev)
{
	register y,x;
	char *a;
	int b,c;
	int ds;
	y = 	(int)((dev)&0377);
	if(y == 2)
		return;
	if(y == 3)
	{
		x = (int)u.u_offset;
		while(passc(y = inb(x)) >= 0)
			;
		u.u_offset = (long)x;
		return;
	}
	else if(y == 4)
	{
		x = (int)u.u_offset;
		do
		    passc(	(int)(((unsigned)(y = in(x))>>8)));
		while(passc(y) >= 0)
			;
		u.u_offset = (long)x;
		return;
	}
	ds = 0;
	if(u.u_offset&0xffff0000)
	{
		x = u.u_offset % (long)2048		;
		ds = u.u_offset / (long)2048		;
		ds =  (((unsigned)(ds))<<7);
		u.u_offset = x;
	}
	do
	    {

		if(ds)
		{
			c = spl7();
			y = getbyte(ds,(int)u.u_offset);
			splx(c);
		}
		else
		{
			a = (int)(u.u_offset);
			y = (int)*a;
		}

		if(u.u_error)
			return;
	}
	while(passc(y) >= 0);
}


mmwrite(dev)
{
	register y,x;
	char *a;
	int b,c,d;
	int ds;
	y = 	(int)((dev)&0377);
	if(y == 2)
	{
		u.u_count = 0;
		return;
	}
	if(y == 3)
	{
		x = (int)u.u_offset;
		while((y = cpass()) >= 0)
			if(u.u_error == 0)
				outb(x,y);
			else
				break;
		u.u_offset = (long)x;
		return;
	}
	else if(y == 4)
	{
		x = (int)u.u_offset;
		while((y = cpass()) >= 0)
		{
			if(u.u_error ==0)
				if((d = cpass()) >= 0)
					if(u.u_error == 0)
						out(x,	(dev_t)((y)<<8 | (	(int)((d)&0377))));
					else
						break;
				else
					break;
			else
				break;
		}
		u.u_offset = (long)x;
		return;
	}
	ds = 0;
	if(u.u_offset&0xffff0000)
	{
		x = u.u_offset % (long)2048		;
		ds = u.u_offset / (long)2048		;
		ds =  (((unsigned)(ds))<<7);
		u.u_offset = x;
	}
	while((y = cpass()) >= 0)
	{
		if(u.u_error == 0)
		{

		if(ds)
		{
			c = spl7();
			setbyte(ds,(int)u.u_offset-1,(char)y);
			splx(c);
		}
		else
		{
			a = (int)(u.u_offset) - 1;
			*a = (char)y;
		}

		}
		else
			return;
	}
}
