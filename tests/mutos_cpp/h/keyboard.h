
/*
 *	User level include file for: 
 *	PC monitor keyboard related defines and variables.
 *
 */

#define NUM_KEYS	256	/* Max number of possible physical keys */
#define NUM_STATES	8	/* Number of key states */
#ifdef	M7150
#define NFKEYS		70
#else
#define	NFKEYS		61
#endif	M7150

#define MAXFK	30

#define	NUM_21_KEYS	111	/* Max number of keys under 2.1.x */

#define MIOC	('k' << 8)

/* GET/SET FKEY in machdep.h are (MIOC|0) and (MIOC|1) */
/* GIO/PIO _SCRNMAP in console.h are (MIOC|2) and (MIOC|3) */

#define GIO_21_STRMAP	(MIOC|4)	/* Get 2.1 function key string table */
#define PIO_21_STRMAP	(MIOC|5)	/* Put 2.1 function key string table */

#define GIO_KEYMAP	(MIOC|6)	/* Get keyboard map table */
#define PIO_KEYMAP	(MIOC|7)	/* Put keyboard map table */

#define GIO_21_KEYMAP	(MIOC|8)	/* Get 2.1 keyboard map table */
#define PIO_21_KEYMAP	(MIOC|9)	/* Put 2.1 keyboard map table */

#define SETLOCKLOCK	(MIOC|10)	/* global cap/num lock on/off */

#define GIO_STRMAP	(MIOC|11)	/* Get function key string table */
#define PIO_STRMAP	(MIOC|12)	/* Put function key string table */

#define GETFKEY	(MIOC)
#define SETFKEY (MIOC | 1)

#define PIO_CHARSET	(MIOC|13)	/* Put new charset */

/*  typedef unsigned char keymap_t[NUM_KEYS+1][NUM_STATES];  */
typedef struct {
	int  n_keys ;
	struct key_t {
		char map[NUM_STATES];
		char spcl;
		char flgs;
	} key[NUM_KEYS+1];
} keymap_t;


/* structure used for SETFKEY and GETFKEY ioctls */
struct fkeyarg {	
	unsigned int	keynum;
	char	 	keydef[MAXFK];
	char		flen;			
};


/* key map table flags */
#define	KMF_CLOCK	0x01		/* affected by caps lock */
#define	KMF_NLOCK	0x02		/* affected by num lock */
#define	KMF_RILOCK	0x04		/* affected by caps lock (right) */
#define KMF_LELOCK	0x05		/* affected by caps lock (left) */
#define KMF_ALTLOCK	0x08		/* character set right <---> left */

#define	L_O	0
#define	L_C	1
#define	L_N	2
#define	L_B	3

#define NORMAL		0
#define SHIFT		1
#define CTRL		2
#define	SHFCTL		3
#define ALT		4
#define	ALTSHF		5
#define	ALTCTL		6
#define	ALTSHFCTL	7

/***************** Special range macros and keys *****************/

#define	IS_SPECIAL(c,i)	( cnkeymap.key[(c)].spcl & (0x80>>(i)) )

#define	K_NOP	0	/* Keys with no function */
#define K_LSH	2	/* Left shift */
#define K_RSH	3	/* Right shift */
#define K_CLK	4	/* Caps lock */
#define K_NLK	5	/* Num lock */
#define K_SLK	6	/* Scroll lock */
#define	K_BTAB	8	/* Back tab */
#define K_ALT	7	/* Alt */
#define K_CTL	9	/* Control */
#define K_NXSC	10	/* Switch to next screen */
#define K_SCRF	11	/* Switch to first screen */
#define	K_SCRL	(K_SCRF+MAXSCREEN)	/* Switch to last screen */
#define	K_FUNF	27	/* First function key */
#define	K_FUNL	122	/* Last function key */

/***************** Expanded function and screen key macros ***************/

#define	NSTRKEYS	(K_FUNL-K_FUNF+1)
#define STR_21_TABLN	256	/* max length of the sum of all strings */
#define STRTABLN	512	/* max length of the sum of all strings */

#define	FKEYSTART	0	/* The F1 key is string key #0 */

#define	IS_SCRKEY(x)	( ((x) >= K_SCRF) && ((x) <= K_SCRL) )
#define	IS_FUNKEY(x)	( ((x) >= K_FUNF) && ((x) <= K_FUNL) )

typedef char strmap_t[STRTABLN];

/***************** Special system scan codes ****************************/

#define	KBD_OVERRUN	-1	/* keyboard imput data queue has been overrun */
#define	KBD_BREAK	0x4000	/* key make/break flag (make=0/break=1) */
#define	KBD_SCMASK	0xff	/* scan code mask */
