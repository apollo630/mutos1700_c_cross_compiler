/*      Definition of x.out header
 *
 */

struct  exec {  /* x.out header */
	short           x_magic;        /* magic number */
	char            x_mach;         /* machine type */
	char            x_form;         /* rel format */
	long            x_text;         /* size of text segment */
	long            x_data;         /* size of initialized data */
	long            x_bss;          /* size of unitialized data */
	long            x_syms;         /* size of symbol table */
	long            x_entry;        /* entry point */
	long            x_reloc;        /* relocation table length */
	long            x_aux;          /* auxilliary table length */
};

/*      Definitions for exec.x_form
 *
 *      fff             format
 *         ss           size  00 - small data and code
 *                            01 - small data, large code
 *                            10 - large data, small code
 *                            11 - large data, large code
 *           s          seperate address spaces for I & D
 *            p         pure
 *             f        set if fixed stack
 */

#define XF_F86  0040                    /*  */
#define XF_FXO  0100                    /* standard x.out format */
#define XF_FXX  0340                    /* mask for field */

#define XF_S00  0000                    /* small data and code    */
#define XF_S01  0010                    /* small data, large code */
#define XF_S02  0020                    /* large data, small code */
#define XF_S03  0030                    /* large data, large code */
#define XF_SXX  0030                    /* mask for field */

#define XF_SEP  0004                    /* code is in seperate addr space */
#define XF_PUR  0002                    /* code is pure */
#define XF_FS	0001			/* fixed, low memory stack */




struct  oexec {  /* a.out header, obsolete */
	int             oa_magic;       /* magic number */
	unsigned        oa_text;        /* size of text segment */
	unsigned        oa_data;        /* size of initialized data */
	unsigned        oa_bss;         /* size of unitialized data */
	unsigned        oa_syms;        /* size of symbol table */
	unsigned        oa_entry;       /* entry point */
	unsigned        oa_unused;      /* not used */
	unsigned        oa_flag;        /* relocation info stripped */
};

#define	A_MAGIC1	0407       	/* normal */
#define A_MAGIC2        0410            /* pure text */
#define	A_MAGIC3	0411       	/* separated I&D */
#define	A_MAGIC4	0405       	/* overlay */

/*      definitions for x_magic
 *              numbers from 0500 to 0577 are magic numbers,
 *              even are linkable, odd are runable
 */

#define A_MAGMSK        0177700         /* magic number mask */
#define A_MAGVAL        0000500         /* magic value */
#define A_MAG86         0500            /* K1810WM86 */
#define A_MAGZ8K        0502            /*  */
#define A_MAG68K        0504            /*  */
#define A_MAGICL        0000            /* add for linkable */
#define A_MAGICR        0001            /* add for runable */


struct	nlist {	/* symbol table entry */
	char    	n_name[8];	/* symbol name */
	int     	n_type;    	/* type flag */
	unsigned	n_value;	/* value */
};

		/* values for type flag */
#define	N_UNDF	0	/* undefined */
#define	N_ABS	01	/* absolute */
#define	N_TEXT	02	/* text symbol */
#define	N_DATA	03	/* data symbol */
#define	N_BSS	04	/* bss symbol */
#define	N_TYPE	037
#define	N_REG	024	/* register name */
#define	N_FN	037	/* file name symbol */
#define	N_EXT	040	/* external bit, or'ed in */
#define XOUTFM  "%06o"  /* symbol value format */
#define XOUTWI  6       /* symbol value width  */
#define FORMAT  "%04o"  /* symbol value format */
#define FWIDTH  4       /* symbol value width  */


#define XM_RVS   0x00           /* variable sized stack */
#define XM_OVL   0x01           /* is overlaid */
