/*
 * k5170.h
 *	KES K5170 Driver declarations.
 *
 */

#define	N5170		1		/* number of boards */
#define	SPL		spl5		/* for driver mutex */
#define	K5170RETRY	0		/* retry count, use only automatic */
					/* hardware retries */

#define	NUMSPINDLE	8		/* # spindles per board */
#define	FIRSTREMOV	4		/* first removable unit-number */
#define	FIXEDMASK	(FIRSTREMOV-1)	/* mask for fixed-unit given unit */

#define SRTMASK		016
#define SRT4		2       /* 4ms step rate time */
#define SRT6		4       /* 6ms step rate time */
#define HLT8    16      /* 8ms head load time */

/*
 * Encoding of "minor" device number (8-bits).
 *
 * Units (for K5170).  Note: 8 different spindles.
 *	0-3	Harddisk
 *	4-7	Floppy
 *
 * Drtab's are selected per unit.  See k170cfg, below.
 * Partitions are selected per drtab entry.  See k170cdrt, below.
 *
 * Major device number selects which K5170 controller.  All such
 * controllers must be contiguous in the bdevsw/cdevsw tables.  The
 * boards must live in the SAME indicies in these tables, due to the
 * dev -> board mapping in the driver.  k170fmaj holds this index.
 *
 * Note that <drtab,unit> number selects actual drtab entry.
 */

/* Floppy */

struct	k170flminor {
	unsigned m_flunit: 3;		/* unit number */
	unsigned m_fldrtab:3;		/* alternate drtab's */
	unsigned m_flpart: 2;		/* partition number */
};

#define	FLDRTAB(dev)	((minor(dev)>>3)&0x07)	/* dev -> drtab-index map */
#define	FLPARTITION(dev) ((minor(dev)>>6)&0x03)	/* dev -> partition-index map */
/* Disk */

struct	k170hdminor {
	unsigned m_hdunit: 3;		/* unit number */
	unsigned m_hddrtab:2;		/* alternate drtab's */
	unsigned m_hdpart: 3;		/* partition number */
};

#define HDDRTAB(dev) ((minor(dev)>>3)&0x2)
#define HDPARTITION(dev) ((minor(dev)>>5)&0x7)

#define	UNIT(dev)	((minor(dev)>>0)&0x07)	/* dev -> unit# map */

#define	BOARD(dev)	(major(dev)-k170fmaj)	/* dev -> board-index map */

#define	LOW(x)		((x)&0xFF)		/* "low" byte */
#define	HIGH(x)		(((x)>>8)&0xFF)		/* "high" byte */

/*
 * Partition structure.  One per drtab[] entry.
 */

struct	k170part {
	daddr_t	p_fsec;			/* first sector */
	daddr_t	p_nsec;			/* number sectors */
};

/*
 * Per-board configuration.
 * c_devcod indicates what kind of device/floppies are there.
 * This should be:
 *	DEV8FLPY	For 8" floppies on a KES
 *	DEV5FLPY	For 5.25" floppies on a KES
 *
 * The c_drtab field is a pointer to a list of drtab entries per-unit.  A zero
 * value implies non-existant unit (ie, must have a drtab entry for a unit to
 * use the unit).
 */

struct	k170cfg	{
	unsigned	c_wua;			/* Physical Wake-Up Address */
	char		c_devcod;		/* what flavor of KES */
	char		c_level;		/* what interrupt level */
	struct k170cdrt	*c_drtab[NUMSPINDLE];	/* per-spindle pointer to drive-characteristics table */
};

/*
 * Per-board driver "dynamic" data.
 */

struct	k170state {
	char		s_exists;		/* flag that board exists */
	char		s_state;		/* what just finished (for interrupt) */
	char		s_opunit;		/* current unit being programmed */
	char		s_level;		/* what interrupt level (for k170io) */
	unsigned	s_wua;			/* copy of k170cfg.c_wua */
	char		s_flags[NUMSPINDLE];	/* flags per spindle; see below */
	char		s_popen[NUMSPINDLE];	/* bit[i] ==> partition[i] open */
	char		s_devcod[NUMSPINDLE];	/* device-code for iopb */
	char		s_unit[NUMSPINDLE];	/* "unit" code for iopb */
	char		s_init[NUMSPINDLE];	/* status from init op */
	struct k170cdrt	**s_cdrtab;		/* -> k170cfg.c_drtab.c_drtab */
	struct buf	*s_bufh;		/* -> buffer header */
	unsigned	s_hcyl;			/* hold cylinder # during restore */
};

/*
 * Per-Unit State Flags.
 */

#define	SF_OPEN		0x01			/* unit is open */
#define	SF_READY	0x02			/* unit is ready; reset by media-change */

/*
 * Macros to make things easier to read/code/maintain/etc...
 */

#define	IO_OP(bp)	((bp->b_flags&B_READ) ? READ_OP : ((bp->b_flags&B_FORMAT) ? FORMAT_OP : WRITE_OP))

/*
 * KES Wake-Up Block.  Lives at wakeup-address, points at CCB.
 */

struct	k170wub {
	char		w_sysop;	/* Must == 0x01 */
	char		w_rsvd;		/* reserved */
	struct k170ccb	*w_ccb;		/* "offset" of CCB pointer */
	unsigned	w_ccb_b;	/* "base" == Kernel DS == 0 */
};

/*
 * CCB (Channel-Control-Block).  See KES manual.
 */

struct	k170ccb {
	char		c_ccw1;		/* 1 ==> Use KES Firmware */
	char		c_busy1;	/* 0x00 ==> Idle, 0xFF ==> busy */
	struct k170cib	*c_cib;		/* "offset" of CIB pointer */
	unsigned	c_cib_b;	/* "base" == Kernel DS == 0 */
	unsigned	c_rsvd0;	/* reserved */
	char		c_ccw2;		/* Must == 0x01 */
	char		c_busy2;	/* Not useful to Host */
	unsigned	*c_cpp;		/* -> k170ccb.c_cp */
	unsigned	c_cpp_b;	/* "base" == Kernel DS == 0 */
	unsigned	c_cp;		/* Control Pointer == 0x04 */
};

/*
 * CIB (Channel Invocation Block).  See KES manual.
 */

struct	k170cib {
	char		c_cmd;		/* reserved */
	char		c_stat;		/* Operation Status (see below) */
	char		c_cmdsem;	/* Not used by KES */
	char		c_statsem;	/* 0xFF ==> new status avail */
	unsigned	c_csa[2];	/* KES Firmware; MUST == 0 */
	struct k170iopb	*c_iopb;	/* IOPB pointer */
	unsigned	c_iopb_b;	/* "base" == Kernel DS == 0 */
	unsigned	c_rsvd1[2];	/* reserved */
};

/*
 * IOPB (I/O Parameter Block).  See KES manual.
 */

struct	k170iopb {
	unsigned	i_rsvd[2];	/* reserved */
	unsigned	i_actual;	/* actual transfer count */
	unsigned	i_actfill;	/* fill actual to 32-bits; Unused */
	unsigned	i_device;	/* Device Code (see below) */
	char		i_unit;		/* Unit: <4> == fixed/rem, <1,0> == unit # */
	char		i_funct;	/* Function Code (see below) */
	unsigned	i_modifier;	/* Modifier.  0 ==> normal, interrupt */
	unsigned	i_cylinder;	/* starting cylinder # */
	char		i_head;		/* starting head # */
	char		i_sector;	/* starting sector # */
	char		*i_buffp;	/* physical offset of buffer */
	unsigned	i_buffp_b;	/* physical base of buffer */
	unsigned	i_xfrcnt;	/* Requested Xfr Count */
	unsigned	i_cntfill;	/* count fill.  Unused */
};

/*
 * Drive-Data Table (used to initialize drives).  See KES manual.
 * Note allignment problem on secsiz.
 * Note: fields thru dr_nalt are programmed into controler for an init.
 *	 Other fields are for more efficient programming.
 * The k170cdrt structure is for configuring the same data (no allignment
 *	worries).
 */

struct	k170drtab {
	unsigned	dr_ncyl;	/* # cylinders */
	char		dr_nfhead;	/* # fixed heads */
	char		dr_nrhead;	/* # removable heads */
	char		dr_nsec;	/* # sectors per track */
	char		dr_lsecsiz;	/* "low" of sector-size */
	char		dr_hsecsiz;	/* "high" of sector-size */
	char		dr_nalt;	/* # alternate cylinders */
					/* if floppy, 0==FM, 1==MFM */
					/* if floppy, step rate bits 1...3 */
	unsigned	dr_spc;		/* actual sectors/cylinder */
	unsigned	dr_spb;		/* sectors/block */
	unsigned	dr_secsiz;	/* sector-size (bytes) */
	struct k170part	*dr_part;	/* partition table pointer */
};

struct	k170cdrt {
	unsigned	cdr_ncyl;	/* # cylinders */
	char		cdr_nfhead;	/* # fixed heads */
	char		cdr_nrhead;	/* # removable heads */
	char		cdr_nsec;	/* # sectors per track */
	unsigned	cdr_secsiz;	/* sector-size */
	char		cdr_nalt;	/* # alternate cylinders */
	struct k170part	*cdr_part;	/* partition table pointer */
};

/*
 * Error Status-Structure, Returned on status inquiry.  See KES manual.
 * Only 1st 2 fields used.  Note another allignment problem (ignored).
 */

struct	k170err {
	unsigned	e_hard;		/* Hard Error Status (see below) */
	char		e_soft;		/* soft error status */
	unsigned	e_req_cyl;	/* desired cylinder */
	char		e_req_head;	/* desired head and volume */
	char		e_req_sec;	/* desired sector */
	unsigned	e_act_cyl;	/* actual cylinder & flags */
	char		e_act_head;	/* actual head & volume */
	char		e_act_sec;	/* actual sector */
	char		e_retries;	/* # retries attempted */
};

/*
 * Format Structure.  1 per "board", usage mutexed via use of "raw" buffer-
 * header (see k5170.c).
 * k170ftk is the argument structure to the format ioctl.
 */

struct	k170format {
	char	f_trtype;		/* format track-type code */
	char	f_pattern[4];		/* pattern; depends on f_trtype */
	char	f_interleave;		/* interleave-factor */
};

struct	k170ftk	{
	int	f_track;		/* track # */
	int	f_intl;			/* interleave factor */
	int	f_skew;			/* track skew -- ignored by KES */
	char	f_type;			/* format type-code */
	char	f_pat[4];		/* pattern data */
};

/*
 * Volume structure. Get cylinders, haeds, sectors, sector size,
 * first sector and number of sectors for a specified file
 * system device (raw interface) by the system call
 * ioctl(fildes,K5170_VOL,(caddr_t)k170vol)
 */

struct	k170vol {
	struct k170cdrt	v_cdrt;
	struct k170part	v_part;
};


/*
 * KES Per-Board Device-Data.  One per board (declared in driver).
 */

struct	k170dev {
	struct	k170state	d_state;
	struct	k170ccb		d_ccb;
	struct	k170cib		d_cib;
	struct	k170iopb	d_iopb;
	struct	k170drtab	d_drtab[NUMSPINDLE];
	struct	k170err		d_error;
	struct	k170format	d_format;
};

/*
 * Values of buffer-header b_active, used for mutual-exclusion of
 * opens and other IO requests.
 */

#define	IO_IDLE		0		/* idle -- anything goes */
#define	IO_OPEN_WAIT	1		/* open waiting */
#define	IO_BUSY		2		/* something going on */
#define	B_FORMAT	040000		/* "new" buf.h flag: must NOT overlap buf.h! */

/*
 * Values of k170state.s_state, internal driver state.
 */

#define	NOTHING			0	/* normal situation, RW */
#define	GET_BAD_STATUS		1	/* retrieveing status; last cmd got error */
#define	RESTORING		2	/* seeking to track 0 for retry */
#define	INITIALIZING		3	/* going thru init-sweep */
#define	READING_LABEL		4	/* reading label for device-characteristics */
#define FORMAT0                 5       /* unused */
#define FORMAT1                 6       /* unused */
#define FORMAT2                 7       /* unused */
#define FORMAT3                 8       /* unused */
#define	T0CHANGING		0x80	/* Track 0 is changing */

/*
 * IOPB fields/flags definitions.
 */

#define	UNIT_REMOVABLE		0x10	/* ==> removable unit */

/*
 * KES Wake-up command codes.  These get output to the wakeup-address-port.
 */

#define	WAKEUP_CLEAR_INT	0x0000
#define	WAKEUP_START		0x0001
#define	WAKEUP_RESET		0x0002

/*
 * KES IOPB Command Codes.
 */

#define INIT_OP                 0
#define	STATUS_OP		1
#define	FORMAT_OP		2
#define	READ_ID_OP		3
#define	READ_OP			4
#define	VERIFY_OP		5
#define	WRITE_OP		6
#define	WRITE_BUFFER_OP		7
#define	SEEK_OP			8

/*
 * KES IOPB Modifier Bits.
 */

#define	MOD_NO_INT		0x0001	/* no interrupt */
#define	MOD_NO_RETRY		0x0002	/* no retry attempts */
#define	MOD_DELETED_DATA	0x0004	/* KES deleted-data RW */

/*
 * Device Codes (for iopb.i_device).
 */

#define	DEVWINI		0		/* Wini */
#define	DEV8FLPY	1		/* 8" Floppy */
#define	DEV5FLPY	3		/* 5.25" Floppy */

/*
 * Floppy FM/MFM codes for drtab[*].nalt.
 */

#define	FLPY_FM		0		/* FM -- single density */
#define	FLPY_MFM	1		/* MFM -- double density */

/*
 * Operation Status Bits.  Returned by controller in k170cib.c_stat.
 */		

#define	ST_OP_COMPL		0x01	/* operation complete */
#define	ST_SEEK_COMPL		0x02	/* seek complete */
#define	ST_MEDIA_CHANGE		0x04	/* media changed */
#define	ST_FLOPPY		0x08	/* ==> KES floppy */
#define	ST_UNIT			0x30	/* unit mask */
#define	ST_HARD_ERR		0x40	/* 0 ==> was soft, recovered error */
#define	ST_ERROR		0x80	/* summary error */

/*
 * Error Bits.
 *
 * Errors returned to user in b_error (byte).  Error is either soft-status
 * byte, or high-byte of hard-status byte.  b_error needs to be a word,
 * and can be used as:
 *	Bits	Contents
 *	 6-0	EIO
 *	  7	0 ==> Hard, 1 ==> Soft status
 *	15-8	High-order byte of hard status, or soft status byte.
 */

#define	HARD_WRITE_PROT		0x8000	/* write-protected drive */
#define	HARD_NOT_READY		0x4000	/* went not-ready */
#define	HARD_NO_SECTOR		0x1000	/* couldn't find sector */
#define	SOFT_NO_SECTOR		0x0001	/* set in soft status, "reserved" bit */

/*
 * Misc Format definitions, for k170ftk.f_type.
 */

#define	FORMAT_DATA		0x00	/* format data track */
#define	FORMAT_BAD		0x80	/* format bad track */
#define	FORMAT_ALTERNATE	0x40	/* format alternate track */

/*
 *  KES ioctl mnemonics.
 */

#define	K5170_IOC_FMT		(('W'<<8)|0)
#define K5170_VOL		(('V'<<8)|0)
