/*
 * The I node is the focus of all
 * file activity in mutos. There is a unique
 * inode allocated for each active file,
 * each current directory, each mounted-on
 * file, text file, and the root. An inode is 'named'
 * by its dev/inumber pair. (iget/iget.c)
 * Data, from mode on, is read in
 * from permanent inode on volume.
 *
 */

#define	NADDR	13
#define	NINDEX	15

struct group {
	short	g_state;
	char	g_index;
	char	g_rot;
	struct	group	*g_group;
	struct	inode	*g_inode;
	struct	file	*g_file;
	short	g_rotmask;
	short	g_datq;
	struct	chan *g_chans[NINDEX];
};
struct	inode
{
	char	i_flag;
	char	i_count;	/* reference count */
	dev_t	i_dev;		/* device where inode resides */
	ino_t	i_number;	/* i number, 1-to-1 with device address */
	unsigned short	i_mode;
	short	i_nlink;	/* directory entries */
	short	i_uid;		/* owner */
	short	i_gid;		/* group of owner */
	off_t	i_size;		/* size of file */
	struct  locklist *i_locklist;           /* locked region list */
	union {
		struct {
			daddr_t i_addr[NADDR];	/* if normal file/directory */
			daddr_t	i_lastr;	/* last logical block read (for read-ahead) */
		} i_i;
		struct	{
			daddr_t	i_rdev;			/* i_addr[0] */
			struct	group	i_group;	/*  multiplexor group file */
		} i_m;
	} i_un;
};

extern struct inode inode[];            /* The inode table itself */
struct inode *mpxip;                    /* mpx virtual inode */


struct  locklist
{
					/* NOTE link must be first in struct */
	struct  locklist *ll_link;      /* link to next lock region */
	int     ll_flags;               /* misc flags ** sleeping */
	struct  proc    *ll_proc;       /* process which owns region */
	off_t   ll_start;               /* starting offset */
	off_t   ll_end;                 /* ending offset, zero is eof */
};

extern  struct  locklist  locklist[];   /* the lock table itself */

/* ll_flags */
#define LROLOCK 01              /* record is locked and is read only */

/* flags */
#define	ILOCK	01		/* inode is locked */
#define	IUPD	02		/* file has been modified */
#define	IACC	04		/* inode access time to be updated */
#define	IMOUNT	010		/* inode is mounted on */
#define	IWANT	020		/* some process waiting on lock */
#define	ITEXT	040		/* inode is pure text prototype */
#define	ICHG	0100		/* inode has been changed */

/* modes */
#define	IFMT	0170000		/* type of file */
#define		IFDIR	0040000	/* directory */
#define		IFCHR	0020000	/* character special */
#define		IFBLK	0060000	/* block special */
#define		IFREG	0100000	/* regular */
#define		IFMPC	0030000	/* multiplexed char special */
#define		IFMPB	0070000	/* multiplexed block special */
#define	ISUID	04000		/* set user id on execution */
#define	ISGID	02000		/* set group id on execution */
#define ISVTX	01000		/* save swapped text even after use */
#define	IREAD	0400		/* read, write, execute permissions */
#define	IWRITE	0200
#define	IEXEC	0100
