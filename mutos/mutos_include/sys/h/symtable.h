#define SYMTSTART (long)0x1010		/* Start of symbol table in mutos kernel */
#ifndef M1834
extern struct k170cfg k170cfg[];
extern struct tty ifsstty, v24tty,amxtty;
extern int nifss, nv24,namx;
#else
extern struct wfcfg wfcfg[];
extern struct tty asktty;
extern int nask;
#endif M1834
extern struct tty constty;
extern struct proc proc[];
extern struct file file[];
extern struct text text[];
extern int Genboot,ROroot,Bigswap,Systyp,Version;
struct	symtable {
#ifndef M1834
	struct	k170cfg	*s_k170cfg;
#else
	struct  wfcfg *s_wfcfg;
#endif M1834
	struct	inode	*s_inode;
	struct	mount	*s_mount;
	struct	proc	*s_proc;
	int	s_swapdev;
	int	s_swplo;
	struct	var	*s_v;
	struct	text	*s_text;
	struct	file	*s_file;
	struct	tty	*s_cons;
#ifndef M1834
	struct	tty	*s_ifss;
	struct	tty	*s_v24;
	int	s_nifss;
	int	s_nv24;
#else
	struct	tty	*s_ask;
	int	s_empty1;
	int	s_nask;
	int	s_empty2;
#endif M1834
	int	s_dk_busy;
	int     s_io_info;
	int	s_rootdev;
	int	s_pipedev;
	int	s_nswap;
	int	s_Genboot;
#ifndef M1834
	struct	tty	*s_amx;
	int	s_namx;
#else
	int	s_empty3;
	int	s_empty4;
#endif M1834
	int	s_ROroot;
	int	s_Bigswap;
	int	s_Nsec;
	int	s_Nhead;
	int	s_Ncyl;
	int	s_Systyp;
	int	s_Version
	}; 
