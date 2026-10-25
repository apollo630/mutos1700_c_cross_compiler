/*
 * Structure of the super-block
 */
struct	filsys {
	unsigned short s_isize;	/* size in blocks of i-list */
	daddr_t	s_fsize;   	/* size in blocks of entire volume */
	short  	s_nfree;   	/* number of addresses in s_free */
	daddr_t	s_free[NICFREE];/* free block list */
	short  	s_ninode;  	/* number of i-nodes in s_inode */
	ino_t  	s_inode[NICINOD];/* free i-node list */
	char   	s_flock;   	/* lock during free list manipulation */
	char   	s_ilock;   	/* lock during i-list manipulation */
	char   	s_fmod;    	/* super block modified flag */
	char   	s_ronly;   	/* mounted read-only flag */
	time_t 	s_time;    	/* last super block update */
	daddr_t	s_tfree;   	/* total free blocks*/
	ino_t  	s_tinode;  	/* total free inodes */
	/* remainder not maintained by this version of the system */
	short  	s_m;       	/* interleave factor */
	short  	s_n;       	/* " " */
	char   	s_fname[6];	/* file system name */
	char   	s_fpack[6];	/* file system pack name */
	/* remainder is maintained for MUTOS */
	char   	s_clean;   	/* S_CLEAN if structure is properly closed */
};

#define	S_CLEAN	0106        	/* arbitrary magic value  */
#define	S_DIRTY (~S_CLEAN)	/* probably corrupted filsys, must be fsck'ed */
