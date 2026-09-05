/* defines for execution of msdos object code */

#define	MS_MAGIC	0x5a4d		/* 'MZ' */

struct	msexec	{	/* file header */
	short		ms_magic;	/* magic number */
	unsigned	ms_bytes;	/* bytes in last record */
	unsigned	ms_pages;	/* # of pages, i.e. 512'er blocks */
	unsigned	ms_nrel;	/* # of relocation entries */
	unsigned	ms_hsize;	/* size of header */
	unsigned	ms_mina;	/* min. allocation size */
	unsigned	ms_maxa;	/* max. allocation size */
	unsigned	ms_ss;		/* initial ss */
	unsigned	ms_sp;		/* initial sp */
	unsigned	ms_chks;	/* checksum */
	unsigned	ms_ip;		/* initial ip */
	unsigned	ms_cs;		/* inital cs */
	unsigned	ms_prel;	/* pointer to relocation table */
	unsigned	ms_ovnum;	/* overlay number */
	unsigned	ms_res;		/* reserved */
};
/* memory layout for execution of an msdos program:

-->	null-terminated environment strings (max. 32 kbyte)
	(char) 0
	(char) 1
	null-terminated argv[0]
-->	psp
-->	program segments
*/

struct	psp	{
	unsigned	ps_i20;		/* int 20h */
	unsigned	ps_break;	/* ^ to 1st. free memory segment */
	char		ps_res1;	/* dos reserved */
	char		ps_lc[5];	/* long call for cp/m functions */
	long		ps_iv22;	/* int-vector 22 of parent process */
	long		ps_iv23;	/* int-vector 23 of parent process */
	long		ps_iv24;	/* int-vector 24 of parent process */
	unsigned	ps_ppid;	/* pointer to psp of parent process */
	char		ps_filx[0x14];	/* indices into fd array */
	unsigned	ps_envp;	/* pointer to env. segment */
	long		ps_sstack;	/* stack segment for system calls */
	unsigned	ps_maxop;	/* max. open files (14h) */
	long		ps_fxpoi;	/* long pointer to ps_filx */
	char		ps_res2[0x18];	/* dos reserved */
	char		ps_i21[3];	/* int 21h + far return */
	char		ps_res3[9];	/* dos reserved */
	char		ps_fcb1[0x10];	/* fcb1 */
	char		ps_fcb2[0x10];	/* fcb2 */
	char		ps_res4[4];	/* unused */
	char		ps_dta[0x80];	/* disk transfer area */
};

#define	PSP(x)	((char *)(&((struct psp *)0)->x))
#define	INT3	0xcc
#define	PSPSEG	((unsigned)u.u_textlength)	/* temp ! */
#define PSPSEGL u.u_textlength			/* lvalue */
#define	MS_MEMORY 150	/* in pages --> 300 kbyte */
#define	MSNAMESIZE 80	/* max. length of a pathname */

typedef	int	(*dos_ent)();
extern	dos_ent	dosent[];

/*#define	MSDEBUG		/* if undefined msdebug will only be fun */
/* msdebug may be patched in the running kernel
 * and is defined in trap.c
 * the following values are currently supported:
 *
 * 0 --> be totally silent (except on fatal errors)
 * 1 --> print a message on exec
 * 2 --> print additional info on exec
 * 3 --> print names of files accessed by the MSDOS program
 * 4 --> print the number of each DOS call
 * 5 --> print additional info on various DOS calls
 * 7 --> break on unimplemented DOS calls (but still exit after return)
 * 8 --> break on exec before starting the MSDOS program
 */

extern	int	msdebug;
extern	int	ms_proc;	/* only 1 msdos process may be active */

/* defines for loading of .86 files */

#define	LHEADR	0x82
#define	PEDATA	0x84
#define	MODEND	0x8a

struct	exec86 {
	char	e86_magic;		/* segment type */
	char	e86_slen;		/* segment length */
	char	e86_zero;		/* high byte of slen */
	char	e86_nlen;		/* length of module name in an LHEADR */
	char	e86_rest[DIRSIZ + 1];	/* module name & checksum */
};	
