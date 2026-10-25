#define ISPEED		13	/* initial baud rate of 9600 == (13);300 == (7) */
#define MINORMSK        0x1F	/* reserve bit 7; bit 6 for modem */
#define MODEMMSK        0xC0    /* bit 6 of the minor number sets modem op */
#define MODEMWAIT       0x40    /* wait for carrier modem op */
#define ECHOMSK		0x20	/* bit 5 of the minor number turns echo off */
#define NCLAMX		2	/* ticks to clear the raw queue */
#define OBLIMIT		1024	/* size of output line buffer on AMX */

/* command type */

#define CMD_FLAG	01	/* command in progess indicator */

/* line state information */

#define IN_STAT		01
#define TIME_STAT	02	/* timeout request on the line */
#define PARAM_WAIT	04
#define	CLOSE_WAIT	010


/*
 * Structures for the AMX/Firmware
 *------------------------------------
 *
 * The structure consists of a command/status/information structure
 * and 4 line (units) structures.
 *       command/status/information [1]
 *       unit structure             [4]
 */

struct firmAMX {
	char  cmd;		/* command from host */
        char  status;		/* status to host */
        char  cmdsem;           /* command semaphore */
        char  stsem;            /* status semaphore */
        char  cunit;            /* unit number associate with command */
        char  sunit;            /* unit number associate with status */
        char  oper;             /* operational status */
        char  deverr;           /* error reporting */
        int   firstl;           /* addr of first line structure */
        int   lsize;            /* size of each line structure */
        char  nlines;           /* number of lines on board */
        char  intenb;           /* interrupt enable */
        int   vers;             /* firmware version number */
        char  diagn;            /* firmware diagnostic number */
        char  diage;            /* failed diagnostic information */
        int   joaddr;           /* jump out command address */
        char  portno;           /* Port I/O command port number */
        char  portv;            /* Port I/O command data */
        struct {
		char  enb;      /* enable bits */
		char  parm;     /* parameter bits */
		char  state;    /* state of line */
		char  error;    /* error bits */
		int   ibaud;    /* input baud rate */
		int   obaud;    /* output baud rate */
		int   iba;      /* input buffer address */
		int   ibs;      /* input buffer size */
		int   ibp;      /* input buffer pointer */
		int   ibc;      /* input buffer count */
		int   ibn;      /* number of bytes input */
		int   oba;      /* output buffer address */
		int   obs;      /* output buffer size */
		int   obp;      /* output buffer pointer */
		int   obc;      /* output buffer count */
		int   obn;      /* number of bytes output */
		int   obl;      /* output byte limit */
	}line[4];
};

/* 
 * Structure for base assignments of boards in the configuration
 * This is used to associate the board to the base address
 */

struct amxcfg{
	long    c_base;
};

/*
 * Structure of the LINE input/output buffer
 *
 */

struct ibuf {
	long	ibuf_addr;
        int	ibuf_size;
};

struct obuf {
        long	obuf_addr;
        int	obuf_size;
};

/* 
 * Structure of the line timeout
 */

struct tout {
	int	index;
	int	size;
	char	outchar;
};

/*
 * Structure of the offset from the beginning of the board,
 * this only contain the most used byte offset
 */

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

/*
 * Command for the command/status/information structure
 * ----------------------------------------------------
 * 
 */

/*
 * Command from the host
 *
 *   RESET   resets the entire oard and runs micro diagnostics
 *
 *   INPUT   signals that the host has transferred bytes from the
 *           input buffer
 *   
 *   OUTPUT  signals that the host has placed bytes in the output
 *           buffer for output
 *
 *   PARAM   signals that the host has changed parameters
 *
 *   JPOUT   causes the AMX to call the subroutine indicated by
 *           'joaddr'
 *
 *   CONTI   causes the reset to continue, ignoring the error
 *
 *   PRTOUT  for diagnostic use only
 *
 *   PRTIN   for diagnostic use only
 *
 *   OFLUSH  flush the output buffer	
 */

#define   RESET    0x01
#define   INPUT    0x02
#define   OUTPUT   0x03
#define   PARAM    0x04
#define   JPOUT    0x05            /* for diagnostic only */
#define   CONTI    0x06
#define   PRTOUT   0x07            /* for diagnostic only */
#define   PRTIN    0x08            /* for diagnostic only */
#define	  OFLUSH   0x09

/*
 * Status to the host
 *
 *   CMDACP  acknowledges the execution of the last command
 *
 *   INVCMD  indicates that the last command was in error
 *
 *   INRDY   indicates that the input is ready
 *
 *   OUTRDY  indicates that the output is ready
 *
 *   RING    indicates that a line is ringing
 *
 *   CARIER  indicates that there has been a carrier loss on line
 *
 *   ABAUDR  indicates that the baud rate has been recognized on line
 */

#define   CMDACP   0x01
#define   INVCMD   0x02
#define   INRDY    0x03
#define   OUTRDY   0x04
#define   RING     0x05
#define   CARIER   0x06
#define   ABAUDR   0x07
 
/*
 * Diagnostic information
 *
 *   ROMERR  ROM checksum error
 *
 *   RAMFIL  RAM failure
 *   
 *   PARINT  parallel port initialization failure
 *
 *   PAROPR  parallel port operating state failure
 * 
 *   ROMSUM  sum of ROM bytes
 *   
 *   RAMVAL  value that should have been in RAM location
 *
 *   PARLV1, PARLV2  value from port A of parallel interface
 *
 *   DIAGRUN running diagnostic
 *
 *   DIAGSUC diagnostics succeeded
 *
 *   DIAGERR diagnostic error
 */

#define   ROMERR   0
#define   RAMFIL   0x01
#define   PARINT   0x02
#define   PAROPR   0x03
#define   ROMSUM   0
#define   RAMVAL   0x01
#define   PARLV1   0x02
#define   PARLV2   0x03
#define   DIAGRUN  0x01
#define   DIAGSUC  0x02
#define   DIAGERR  0x03
 
/* Mis. information for the command/status/information structure */

#define   CLEAR    0
#define   ENBINTR  0x01

/* 
 * Line (Unit) structure information
 * ---------------------------------
 *
 *   ENBOUT  enable output
 *
 *   XOFFON  enable Control-S/Control-Q
 *
 *   DTRDY   Data Terminal Ready
 *
 *   PNO     no parity
 *
 *   PODD    odd parity
 *
 *   PEVEN   even parity
 *
 *   CARRIER Carrier state set
 *
 *   DSRDY   data set ready
 *   
 *   IBUFOV  input buffer overflowed
 *
 *   BIT_8	8 Bit
 */

#define   ENBOUT  0x01
#define   XOFFON  0x02
#define   DTRDY   0x01
#define   PNO     0x00
#define	  RECINT  0x10
#define   PODD    0x02
#define   PEVEN   0x04
#define   CARRIER 0x01
#define   DSRDY   0x02
#define   NOMODEM 0x08
#define   IBUFOV  0x01
#define   BIT_8	  0x80

/*
 * Supported baud rate of the AMX device driver
 *
 *   US_B50       50 BAUD
 *   US_B110     110 BAUD
 *   US_B150     150 BAUD
 *   US_B300     300 BAUD
 *   US_B600     600 BAUD
 *   US_B1200   1200 BAUD
 *   US_B2400   2400 BAUD
 *   US_B4800   4800 BAUD
 *   US_B9600   9600 BAUD
 *   
 */

#define US_B50		50
#define US_B110		110
#define US_B150		150
#define US_B300		300
#define US_B600		600
#define US_B1200	1200
#define US_B2400	2400
#define US_B4800	4800
#define US_B9600	9600
