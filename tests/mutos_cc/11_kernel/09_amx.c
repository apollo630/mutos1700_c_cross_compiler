/*
 * AMX device driver.
 * This is the set of procedures that make up the AMX device driver.
 *
 *	Debug switches are: DEBUG for AMX support.
 *		amxdebug: output control
 *			0 == no output except spurious intrs
 *			1 == special currently same as 0
 *			2 == little but useful output
 *			3 == all output with monitor call
 *
 */

#include "amx.h"			    /* hardware structure and local commands */
#include "param.h"
#include "systm.h"			    /* system */
#include "conf.h"			    /* system configuration */
#include "dir.h"			    /* system directory structures */
#include "a.out.h"         	    /* needed for user.h */
#include "user.h"			    /* user structures (system)	*/
#include "tty.h"			    /* device structures (system)	*/
#include "intr.h"			    /* some pic commands from system */


#define DEBUG

#ifdef DEBUG
int	amxdebug = 0;			    /* debug output control */
#endif

/*
 * These variables are defined in ../cfg/camx.c
 */

extern int		namx;		/* number of AMXs, configurable */
extern struct	tty	amxtty[];	/* 4 lines per AMX */
extern struct   firmAMX amxfirm[];     /* board firmware structure */
extern struct	amxcfg	amxcfg[];	/* board software addresses von conf*/
extern struct   amxoff amxoff[];	/* firmware offset structure */
extern struct   ibuf    amxibuf[];     /* input buffer address */
extern struct   obuf    amxobuf[];     /* output buffer address */
extern struct 	tout	amxtout[];	/* timeout structure for each line */
extern char		amxcmd[];	/* command state for each board */
extern char		amxscd[];	/* status state for each line */
extern char		amxi_buf[];  	/* input buffer for each line */
extern char     	*iobuffer[];  	/* output buffer for each line */
extern int		amxalive[];	/* does it live ?? */
extern long             amxladdr[];    /* line structures address */
extern int		amxlevel;	/* interrupt level */
int			amxsleep;
int			amxwakeup;	/* wakeup variable for modems */

/*
 * This procedure initializes the AMX when the call to dinit is
 * made. This procedure is done ONCE ONLY in the following sequence:
 * 	initialize the AMX structures to point at the board,
 *      calculate the addresses of the line(unit) structures, input buffer
 *	address, output buffer address, and the byte offset of several
 *	commonly used bytes of the firmware.
 *
 * TITLE:	amxinit
 *
 * CALL:	amxinit();
 *
 * INTERFACES:	dinit
 *
 * CALLS:	amxprobe, amxcal
 *
 */

amxinit()
{
	register int  board;

#ifdef DEBUG
	if(amxdebug >= 3)
		printf("amx-init ");
#endif

	amxprobe();				/* check for valid board    */
	for(board=0; board<namx; board++) {	/* for each board in system */
		if (amxalive[board])		/* if board is valid	    */
			amxcal(board);		/* do the calculation	    */
	}
}


/*
 * This procedure verifies that a AMX board is presently
 * configured by sending a reset command to the board, if the
 * board is reset successfully (status byte has a value of 1,
 * indicating the command is accepted), the board is there; 
 * otherwise, the board is considered not there. 
 *
 * TITLE:	amxprobe
 *
 * CALL:	amxprobe();
 *
 * INTERFACES:	amxinit
 *
 * CALLS:	amxpeek, amxpoke
 *
 */

amxprobe()
{
	register struct amxcfg *cf;
	register struct	firmAMX *Fbase;
	int		alive, board, s;

#ifdef DEBUG
	if(amxdebug >= 3)
		printf("amx-probe ");
#endif

/* Reset the board(s) by sending the RESET command */

	 for(board=0; board<namx; board++){		 /* for each board configured */
		cf = &amxcfg[board];			 /* set up ptr. to the board address */
		if(cf->c_base == (long)0x0)
			continue;			 /* skip the following if the board not configured */
        	Fbase = &amxfirm[board]; 		 /* set up ptr. to the board firmware struct */ 
		s = spl7();
		Fbase->status = CLEAR;			 /* clear status byte */
                Fbase->cmd = RESET;			 /* set reset command */
                amxpoke(0,cf->c_base,2,&Fbase->cmd);    /* send to the board */
		splx(s);
	}

        delay(500);					 /* wait for the reset */

/* Read in from the board(s) to confirm the board(s) are there */

	for(board=0; board<namx; board++){		 /* for each board configured */
		cf = &amxcfg[board];
		if(cf->c_base == (long)0x0)
			continue;			 /* skip the following if the board not configured */
		alive = 0;				 /* asume board not there */
        	Fbase = &amxfirm[board]; 		 /* set up ptr. to the board firmware struct */ 
                amxpeek(1,cf->c_base,1,&Fbase->status); /* read status from the board */
                if (Fbase->status == CMDACP) 		 /* check if the reset take place */
			alive = 1;            		 /* board reset, board alives */

		printf("AMX      Based %x level %d %s\n",(unsigned int)(cf->c_base/(long)16),amxlevel,alive ? "found" : "NOT found" );

		amxalive[board] = alive;		/* set board alive indicator */
	}
}


/*
 * This procedure has three sections:
 *	1. Calculate the firmware line structure address of the board, the 
 *	   input buffer address of each line on the board, and the output
 *	   buffer address of each line on the board.  It also get the size of
 *	   each input buffer and output buffer. 
 *	2. Calculate the commonly used firmware byte offset.
 *	3. Set interrupt enable byte on.
 *
 * TITLE:	amxcal
 *
 * CALL:	amxcal(board);
 *
 * INTERFACES:	amxinit
 *
 * CALLS:	amxpeek, amxpoke
 *
 */

amxcal(board)
int	board;
{
	register struct firmAMX *Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	int		bnum, i, j, s, temp_off;

#ifdef DEBUG
	if(amxdebug >= 3)
		printf("amx-cal ");
#endif

	Fbase = &amxfirm[board];			/* set up ptr. of the board firmware struct*/
	cf = &amxcfg[board];				/* set up ptr. of the board address */

/* Read in the address offset of the first line structure */

	temp_off = (int)((char *)&Fbase->firstl - (char *)&Fbase->cmd);
	amxpeek(temp_off,cf->c_base,4,&Fbase->firstl);

/*
 * For each line of the board, do the following:
 *	Calculate the firmware line structure address
 *	Calculate the input buffer address
 *	Read in the input buffer size
 *	Calculate the output buffer address
 *	Read in the output buffer size
 */

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
        
/*
 * Calculation of the firmware bytes offset 
 */

	of = &amxoff[board];			/* set up ptr. of the firmware offset struct */

	/* Calculation of the command structure bytes offset */

	of->o_cmd = (char *)&Fbase->cmd - (char *)&Fbase->cmd;
	of->o_status = (char *)&Fbase->status - (char *)&Fbase->cmd;
	of->o_cunit = (char *)&Fbase->cunit - (char *)&Fbase->cmd;
	of->o_sunit = (char *)&Fbase->sunit - (char *)&Fbase->cmd;

	/* Calculation of the line structures bytes offset */

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

/* Enable board interrupt */

	s = spl7();
	Fbase->status = CLEAR;
	amxpoke(1,cf->c_base,1,&Fbase->status);
	Fbase->intenb = ENBINTR;
	temp_off = (int)((char *)&Fbase->intenb - (char *)&Fbase->cmd);
	amxpoke(temp_off,cf->c_base,1,&Fbase->intenb);
	splx(s);
}

/*
 * This procedure sets up the baud rate and the DTR condition of the device.
 * The code depends on having the tty structure filled out before a call is made
 * to amxparam. This is the sequence of events;
 *	check for valid speed
 *	set up the baud rate and the DTR condition
 *
 * TITLE:	amxparam
 *
 * CALL:	amxparam(dev);
 *
 * INTERFACES:	amxioctl, amxopen
 *
 * CALLS:	amxpeek, amxpoke
 *
 */

#define MAXBAUDS 15	/* maximum indexes into amxbaud[] */
int amxbaud[] = {
	0 ,	0,	0,	US_B110,	0,
	US_B150,	0,	US_B300,	US_B600,	US_B1200,
	0,	US_B2400,	US_B4800,	US_B9600,	0,
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

	unit = minor(dev) & MINORMSK;			/* get the unit number */
	modem = minor(dev) & MODEMMSK;

#ifdef DEBUG
	if (amxdebug >= 2)
		printf("amx-param unit %d ",unit);
#endif

        board = unit >> 2;				/* calculate the board number */
	tp = &amxtty[unit];				/* set up ptr. of the tty struct */
        Fbase = &amxfirm[board];			/* set up ptr. of the board firmware struct */
	l_num = unit & 03;				/* get the line no. */
	cf = &amxcfg[board];				/* set up ptr. of the board address */
	of = &amxoff[board];				/* set up ptr. of the offset struct */
	s = (int)tp->t_ospeed;

	if(s==0) {					/* hangup signal via stty */

		/*
		 * Send the hangup signal to the board by:
		 *	Flush the output buffer
		 *	Set Data Terminal Ready parameter bit off  
		 *      Set mask interrupt parameter bit on
		 *	Send the parameter bits to the board
		 *	Send the line number to the board
		 *	Send the command out
		 *	Set command busy flag
		 */

		while (amxcmd[board] & CMD_FLAG) {	/* check if the board is busy */
#ifdef DEBUG
			if(amxdebug >= 2)
				printf("param wait on hangup unit %d ",unit);
#endif
			amxscd[unit] |= PARAM_WAIT;	/* sleep the pocess if it is */
			amxsleep++;
			sleep((caddr_t)&amxscd[unit],TTIPRI);
		}
#ifdef DEBUG
			if(amxdebug >= 2)
				printf("amx-param hangup signal to unit %d ",unit);
#endif
		x = spl7();
		Fbase->cunit = l_num;
		Fbase->cmd = OFLUSH;
		amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
		amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		Fbase->line[l_num].parm = (Fbase->line[l_num].parm & ~DTRDY) | RECINT;
		Fbase->cmd = PARAM;
		amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
		amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
		amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		amxcmd[board] |= CMD_FLAG;
		splx(x);
		return;
	}

	speed = amxbaud[s];				/* get the baud rate */
	if ((s > MAXBAUDS) || ((s != 0) && (speed == 0))) {
		u.u_error = EINVAL;			/* invalid baud rate */
		return;
	}

/* 
 * Check to see if baud rate is really change, if baud rate change,
 * do the following:
 * 	Wait until the board is not busy in handling commands
 * 	Flush the line output buffer.
 */

	if(Fbase->line[l_num].ibaud != speed) {
        	while (amxcmd[board] & CMD_FLAG) {		/* check if the board is busy */
#ifdef DEBUG
			if(amxdebug >= 2)
				printf("param wait on baud change unit %d ",unit);
#endif
			amxscd[unit] |= PARAM_WAIT;		/* sleep the process if it is */
			amxsleep++;
			sleep((caddr_t)&amxscd[unit],TTIPRI);
		}

#ifdef DEBUG
			if(amxdebug >= 2)
				printf("amx-param baud rate change unit %d ",unit);
#endif
		x = spl7();
		Fbase->cunit = l_num;
		Fbase->cmd = OFLUSH;
        	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
        	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		amxcmd[board] |= CMD_FLAG;
		splx(x);
	}
		

        while (amxcmd[board] & CMD_FLAG) {		/* check if the board is busy */
#ifdef DEBUG
		if(amxdebug >= 2)
			printf("param wait on unit %d ",unit);
#endif
		amxscd[unit] |= PARAM_WAIT;		/* sleep the process if it is */
		amxsleep++;
		sleep((caddr_t)&amxscd[unit],TTIPRI);
	}

/*
 * Set up the baud rate of the board by:
 *	Set the input and output baud rate
 *	Set parameter to Data Terminal Ready, no parity, 
 *	and mask interrupt bit off
 *	Send the baud rate to the board
 *	Send the parameter to the board
 *	Set the line number on the board
 *	Send the command to the board
 *	Set command busy flag
 */

	Fbase->line[l_num].parm &= (~PODD&~PEVEN);
	if(((tp->t_flags&EVENP) && (tp->t_flags&ODDP)) || (((tp->t_flags&EVENP) == 0) && ((tp->t_flags&ODDP) == 0)))
		;
	else
		(tp->t_flags&EVENP) ? (Fbase->line[l_num].parm |= PEVEN) : (Fbase->line[l_num].parm |= PODD);

	if(tp->t_flags&(RAW|DW8B))
		Fbase->line[l_num].parm |= BIT8;
	else
		Fbase->line[l_num].parm &= ~BIT8;
	x = spl7();
        Fbase->line[l_num].ibaud = speed;
	Fbase->line[l_num].obaud = 0;
	Fbase->line[l_num].parm |= DTRDY;
	if(modem == 0)
		Fbase->line[l_num].parm |= NOMODEM;
	else
		Fbase->line[l_num].parm &= ~NOMODEM;
	Fbase->line[l_num].parm &= ~RECINT;
	Fbase->cmd = PARAM;
	Fbase->cunit = l_num;
        amxpoke(of->o_line[l_num].o_ibaud,amxladdr[unit],4,&Fbase->line[l_num].ibaud);
        amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
        amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
        amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	amxcmd[board] |= CMD_FLAG;
	splx(x);
}


/*
 * This procedure opens one of the 4 lines on the AMX board for
 * exclusive use by a user.  The file structure is initialized
 * and control is passed to ttyread which does the actual open.
 * Not supported is the fifth device which is the parallel port.
 *
 *
 * TITLE:	amxopen
 *
 * CALL:	amxopen(dev, flag);
 *
 * INTERFACES:	mutos
 *
 * CALLS:	amxparam, amxpeek, amxpoke, ttyopen
 *
 */

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

	unit = minor(dev) & MINORMSK;

#ifdef DEBUG
	if (amxdebug >= 2)
		printf("amx-open unit %d ",unit);
#endif

	if (unit >= (namx*4)) {			/* not enough tp's */
		u.u_error = ENXIO;
		return;
	}
	if (amxalive[unit/4] == 0) {		/* Board not there! */
		u.u_error = ENXIO;
		return;
	}

	tp = &amxtty[unit];			/* set up ptr. to the tty struct */
	board = unit >> 2;			/* get the board number */
        Fbase = &amxfirm[board];		/* set up ptr. to the board firmware struct */
	Fbase->cunit = l_num = unit & 03;	/* get the line number */
	cf = &amxcfg[board];			/* set up ptr. to the board address */
	of = &amxoff[board];			/* set up ptr. to the offset struct */
	modem = minor(dev) & MODEMMSK;		/* get the modem number */
        tp->t_addr = (caddr_t)unit;             /* save the unit number */
	tp->t_oproc = amxstart;

	if ((tp->t_state & ISOPEN) == 0 ) {
		ttychars(tp);
		if(tp->t_ispeed == 0)
		{
			tp->t_ispeed =  tp->t_ospeed = ISPEED;		/* channel speed */
			tp->t_flags = ODDP | EVENP | ECHO | CRMOD;	/* tty mode */
		}
		amxparam(dev);			     /* load baud rate and parameter */

/* Set the output byte limit and enable output on the board */

		s = spl7();
		Fbase->line[l_num].obl = TTYHOG;
		Fbase->line[l_num].enb = ENBOUT|XOFFON;
		temp_off = (int)((char *)&Fbase->line[l_num].obl - (char *)&Fbase->line[l_num].enb);
		amxpoke(temp_off,amxladdr[unit],2,&Fbase->line[l_num].obl);
		amxpoke(of->o_line[l_num].o_enb,amxladdr[unit],1,&Fbase->line[l_num].enb);
		splx(s);

		if (modem & MODEMWAIT) {
			temp_off = (int)((char *)&Fbase->line[l_num].state - (char *)&Fbase->line[l_num].enb);
                        i = 0;
			while (i == 0) {
				amxpeek(temp_off,amxladdr[unit],2,&Fbase->line[l_num].state);
                                if (Fbase->line[l_num].state & DSRDY == 0)
					sleep((caddr_t)&amxwakeup,TTIPRI);
                                else
                                        i = 1;
                        }
		}
	}

	if (tp->t_state & XCLUDE && u.u_uid != 0) {
		u.u_error = EBUSY;
		return;
	}

	tp->t_state |= CARR_ON;				/* open the line */
	(*linesw[tp->t_line].l_open)(dev, tp);	
}


/*
 * This procedure performs the close operation on one of the devices of the
 * AMX. A close masks the device on board; reinstalls the flags that
 * state the device is closed; calls ttyclose to do the operation.
 * Not implimented yet is device 4 which is the parallel port; it is
 * unknown device at this minute.
 *
 * TITLE:	amxclose
 *
 * CALL:	amxclose(dev, flag);
 *
 * INTERFACES:	mutos
 *
 * CALLS:	amxpoke, ttyclose
 *
 */


amxclose(dev)
dev_t	dev;
{
	register struct tty *tp;
	register struct	firmAMX  *Fbase;
	register struct	amxcfg	*cf;
	register struct	amxoff	*of;
	register int	unit;
	int		board, i, l_num, s;

	unit = minor(dev) & MINORMSK;			/* get the unit number */

#ifdef DEBUG
	if(amxdebug >= 2)
		printf("amx-close unit %d ",unit);
#endif

	tp = &amxtty[unit];				/* set up ptr. to the tty struct */
	board = unit >> 2;				/* get the board number */
	Fbase = &amxfirm[board];			/* set up ptr. to board firmware struct */
	l_num = unit & 03;				/* get the line number */
	cf = &amxcfg[board];				/* set up ptr. to the board address */
	of = &amxoff[board];				/* set up ptr. to the offset struct */

	if (unit < namx*4) {

		/*
		 * Check the output buffer of the closing line.  If the output
		 * buffer is not empty, the line is not ready to be closed.
		 * Sleep the process until the output buffer is empty, then
		 * continue to close the line.
		 */

		i = 0;
		while(i == 0) {
			amxpeek(of->o_line[l_num].o_obc,amxladdr[unit],2,&Fbase->line[l_num].obc);
			if((Fbase->line[l_num].obc != (amxobuf[unit].obuf_size -1)) || (amxcmd[board] & CMD_FLAG)) {		/* is the board busy */
#ifdef DEBUG
				if(amxdebug >= 2)
					printf("close wait on unit %d ",unit);
#endif
				amxscd[unit] |= CLOSE_WAIT;	/* sleep the process if it is */
				amxsleep++;
				sleep((caddr_t)&amxscd[unit], TTIPRI);
			}else
				i = 1;
		}

		/* Line is ready to be closed */

		if(tp->t_state & HUPCLS)
			tp->t_state &= ~CARR_ON;


		/* Set DTR off and set the mask interrupt bit on */

		s = spl7();
                Fbase->line[l_num].parm = (Fbase->line[l_num].parm & ~DTRDY) | RECINT;
                Fbase->cmd = PARAM;
		Fbase->cunit = l_num;
                amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
                amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
		amxcmd[board] |= CMD_FLAG;
		splx(s);

		(*linesw[tp->t_line].l_close)(tp);		/* close the line */
		ttyclose(tp);
	}
}


/*
 * This procedure interfaces the read request with the system read operation.
 *
 * TITLE:	amxread
 *
 * CALL:	amxread(dev)
 *
 * INTERFACES:	mutos
 *
 * CALLS:	ttread
 *
 */

amxread(dev)
dev_t	dev;
{
	register struct tty *tp;			
	register int unit;

	unit = minor(dev) & MINORMSK;			/* get the unit number */

#ifdef DEBUG
	if(amxdebug >= 3)
		printf("amx-read on unit %d ",unit);
#endif

	tp = &amxtty[unit];				/* set up ptr. to tty struct */
	(*linesw[tp->t_line].l_read)(tp);		/* request system read */
}


/*
 * This procedure is the compliment of the amxread routine. A call is
 * made to ttwrite which watches the output queue for characters and
 * gets the characters in the queue out to the device.
 *
 * TITLE:	amxwrite
 *
 * CALL:	amxwrite(dev);
 *
 * INTERFACES:	mutos
 *
 * CALLS:	ttwrite
 *
 */

amxwrite(dev)
dev_t	dev;
{
	register struct tty *tp;			
	register int unit;

	unit = minor(dev) & MINORMSK;

#ifdef DEBUG
	if(amxdebug >= 3)
		printf("amx-write on unit %d ",unit);
#endif

	tp = &amxtty[unit];			
	(*linesw[tp->t_line].l_write)(tp);		
}
 

/*
 * This routine is used to restart the input sequence of the AMX firmware.
 * It tells the AMX firmware that 0 chars had been read from its input
 * buffer and that causes an input ready status (and interrupt) to be 
 * generated.
 *
 * TITLE:	st_inp
 *
 * CALL:	st_inp(tp)
 *
 * INTERFACES:	amxintr
 *
 * CALLS:	amxpeek, amxpoke
 *
 */

st_inp(tp)
struct tty *tp;
{
	register struct firmAMX *Fbase;
	register struct amxcfg *cf;
	register struct amxoff *of;
	register unsigned board;
	unsigned	l_num;
	int		s, unit;

	unit = (int)tp->t_addr;		/* get the unit number */
	board = unit >> 2;		/* get the board number */
	l_num = unit % 3;		/* get the line number */
	Fbase = &amxfirm[board];	/* set up ptr. to board firmware addr */
	cf = &amxcfg[board];		/* set up ptr. to board addr */
	of = &amxoff[board];		/* set up ptr. to offset struct */

	do {				/* wait until board is not busy */
		amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	}while(Fbase->cmd != 0);

	/*
	 * send an input command of 0 byte to the AMX board.  This is to
	 * get interrupts started again.
	 */

	s = spl7();
	Fbase->line[l_num].ibn = 0;
	Fbase->cunit = l_num;
	Fbase->cmd = INPUT;
	amxpoke(of->o_line[l_num].o_ibn,amxladdr[unit],2,&Fbase->line[l_num].ibn);
	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	splx(s);
	amxcmd[board] |= CMD_FLAG;
}

/*
 * This procedure is called by mutos with interrupts off (spl5) when the
 * AMX interrupts.
 *
 * TITLE:	amxintr
 *
 * CALL:	amxintr(level);
 *
 * INTERFACES:	mutos
 *
 * CALLS:	amxpeek, amxpoke, ttyinput, ttstart
 *
 */

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

#ifdef DEBUG
	if(amxdebug >= 2)
		printf("amx-intr ");
#endif

	do {
		gotone=0;				/* reset the interrupt indicator */
		for(board=0; board<namx; board++) {	/* check interrupt for each board */
			if(amxalive[board] == 0)	/* skip the following if board not there */
				continue;
			Fbase = &amxfirm[board];	/* set up ptr. to board firmware struct */
			cf = &amxcfg[board];		/* set up ptr. to board address */
			of = &amxoff[board];		/* set up ptr. to offset struct */

/*
 * Read in the board status to determine what kind of interrupt has happened,
 * also read in the unit(line) number associated with the status
 */

                        amxpeek(of->o_status,cf->c_base,6,&Fbase->status);
			if (Fbase->status == CLEAR)		/* skip the following if no interrupt */ 	
				continue;
			status = Fbase->status;

			unit = board * 4 + Fbase->sunit;        /* get the unit  number */
			tp = &amxtty[unit];			/* set up ptr. to tty struct*/
			l_num = Fbase->sunit;		  	/* get the line number */

	 		gotone++;			 	/* incr. interrupt indicator */
#ifdef DEBUG
			if(amxdebug >= 2)
				printf("status %d on unit %d\n",Fbase->status,unit);
#endif

/* Interrupt handling section */
			/*
			 * Clear status byte
			 */

			s = spl7();
                        Fbase->status = CLEAR;
			amxpoke(of->o_status,cf->c_base,1,&Fbase->status);
			splx(s);

                        switch(status) {
			/*
			 * Command accepted:						
			 *			Do the echo if this interrupt is cause 
			 *			by input interrupt of the same line and	
			 *			reset the input request indicator	
			 *			Reset the command busy flag
			 */
                        case CMDACP : 
					if (amxscd[unit] & IN_STAT){
						amxscd[unit] &= ~IN_STAT;
						c = amxi_buf[unit];
						(*linesw[tp->t_line].l_rint)(c,tp);
					}
					amxcmd[board] &= ~CMD_FLAG;
                                       	break;
			/*
			 * Invalid command:					
			 */
                        case INVCMD :
					amxcmd[board] &=~CMD_FLAG;
                                       	break;
			/*
			 * Input ready:	
			 *		Read in the input buffer pointer and the
			 *		input byte count
		         *		If the input byte count plus the number
			 *		of bytes in the raw queue is greater 
			 *		than 256, suspend the process for 5 
			 *		ticks, and skip the following steps
			 *		Read in chars from the input buffer
			 *		Check to make sure the board is not
			 *		busy processing another command
			 *		Send the number of bytes input, line
			 *		number, and INPUT command to the board
			 *		Set command busy flag
			 */
                        case INRDY  :
					if(tp->t_rawq.c_cc >= (TTYHOG- 1)) {
						timeout(st_inp,tp,NCLAMX);
					 	wakeup((caddr_t)&tp->t_rawq);
						break;
					}
					amxpeek(of->o_line[l_num].o_ibp,amxladdr[unit],4,&Fbase->line[l_num].ibp);
					libp = Fbase->line[l_num].ibp;
					libc = Fbase->line[l_num].ibc;
					/*libc = (libc>TTHIWAT?TTHIWAT:libc);
					 *nch = 0;
					 *while(libc-- && (tp->t_rawq.c_cc<TTYHOG)) {
					 */
					amxscd[unit] |= IN_STAT;
					amxpeek(libp,amxibuf[unit].ibuf_addr,1,&amxi_buf[unit]);
					/*	c = amxi_buf[unit];
					 *	(*linesw[tp->t_line].l_rint)(c, tp);
					 *	nch++;
					 *	libp=(++libp==amxibuf[unit].ibuf_size?0:libp);
					 *}
					 */
					do {
						amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					}while(Fbase->cmd != 0);
					s = spl7();
                                       	Fbase->cunit = l_num;
					Fbase->line[l_num].ibn = 1;
                                       	Fbase->cmd = INPUT;
                                      	amxpoke(of->o_line[l_num].o_ibn,amxladdr[unit],2,&Fbase->line[l_num].ibn);
                                      	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                                       	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					splx(s);
					amxcmd[board] |= CMD_FLAG;
				      	break; 
			/*
			 * Output:
			 *		Check for timeout state, if timeout and output buffer is empty,
			 *		then do the timeout procedure              
			 *		If not timeout state, reset the terminal busy flag, and start the
			 *		next output procedure
			 *		If the process is asleep, wakeup the process
			 */
			case OUTRDY :
					if(tp->t_state & TIMEOUT) {
						if (Fbase->line[l_num].obc != (amxobuf[unit].obuf_size - 1))
							break;
						else
							timeout(ttrstrt, (caddr_t)tp, amxtout[unit].outchar);
					}
					tp->t_state &= ~BUSY;
					(*linesw[tp->t_line].l_start)(tp);
					if((tp->t_state & ASLEEP) && (tp->t_outq.c_cc <= TTLOWAT) && (!(tp->t_state & TIMEOUT))) {
						tp->t_state &= ~ASLEEP;
						wakeup((caddr_t)&tp->t_outq);
                                        }
					break;
			/*
			 * Ring:
			 *		Wakeup the process
			 * 		Set the parameter to Data Terminal Ready
			 * 		Send the parameter, line number, and PARAM command to the board
			 *		Set command busy flag
			 */
			case RING   :
                                        wakeup((caddr_t)&amxwakeup);
					s = spl7();
                                        Fbase->line[l_num].parm |= DTRDY;
					Fbase->line[l_num].parm &= ~RECINT;
                                        Fbase->cunit = l_num;
                                        Fbase->cmd = PARAM;
					amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
                                        amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                                        amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					splx(s);
					amxcmd[board] |= CMD_FLAG;
					break;
			/*
			 * Carrier:
			 *		Signal the process
			 *		Reset the CARR_ON state of the line
			 *		Reset the Data Terminal Ready bit
			 *		Send the parameter, line number, and command to the board
			 *		Set command busy flag
			 */
			case CARIER : 
                                      	if ((tp->t_state & (CARR_ON|ISOPEN)) == (CARR_ON|ISOPEN))
                                      		signal(tp->t_pgrp,SIGHUP);
                                        tp->t_state &= ~CARR_ON;
					s = spl7();
                                        Fbase->line[l_num].parm = (Fbase->line[l_num].parm & ~DTRDY) | RECINT;
                                        Fbase->cunit = l_num;
                                        Fbase->cmd = PARAM;
					amxpoke(of->o_line[l_num].o_parm,amxladdr[unit],1,&Fbase->line[l_num].parm);
                                        amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
                                        amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
					splx(s);
					amxcmd[board] |= CMD_FLAG;
                                        break;
			/*
			 * Auto baud rate recognized:
			 *		not supported by the driver
			 */
			case ABAUDR:
					break;
			} 
		}
	} while(gotone);

/* If any process is asleep, wake it up */

	if(amxsleep > 0) {
		for(unit=0; unit<namx*4; unit ++) {
			if(amxscd[unit] & (CLOSE_WAIT|PARAM_WAIT)) {
				amxsleep--;
				scd = amxscd[unit];
				if(scd & CLOSE_WAIT)
					scd &= ~CLOSE_WAIT;
				else
					scd &= ~PARAM_WAIT;
				amxscd[unit] = scd;
				wakeup((caddr_t)&amxscd[unit]);
			}
		}
	}
}


/*
 * This procedure starts output on a line if needed. amxstart gets 
 * characters from the character queue to th internal buffer, then outputs the 
 * character to the line and sets the BUSY flag.
 * The busy flag gets unset when the characters has been transmitted by 
 * amxintr().
 *
 * TITLE:	amxstart
 *
 * CALL:	amxstart(tp)
 *
 * INTERFACES:	ttystart
 *
 * CALLS:	amxxbuff
 *
 */

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

/*
 * Check for interrupted start
 */

	s = spl5();
	if(tp->t_state & (TIMEOUT|BUSY)) {
		splx(s);
		return;
	}
	tp->t_state |= BUSY;
	splx(s);

#ifdef DEBUG
	if(amxdebug>=2)
		printf("amxstart: called on unit %d\n",tp->t_addr);
#endif

	unit = (int)tp->t_addr;			/* get the unit number */
        bnum = unit >> 2;               	/* get the board number */
	l_num = unit & 03;			/* get the line number */
	Fbase = &amxfirm[bnum];		/* set up ptr. to board firmware struct */
	of = &amxoff[bnum];			/* set up ptr. to offset struct */
	cf = &amxcfg[bnum];			/* set up ptr. to board address */
	buff = iobuffer[unit];			/* ptr. to internal output buffer */

/*
 * Read in the output buffer pointer and the output byte count, then set the
 * transfer limit to the internal buffer size or the output byte count,
 * whichever is smaller
 */

	amxpeek(of->o_line[l_num].o_obp,amxladdr[unit],4,&Fbase->line[l_num].obp);
	if (Fbase->line[l_num].obc > TTYHOG)
		limit = TTYHOG;
	else
		limit = Fbase->line[l_num].obc;

/* Tranfer characters from output queue to internal buffer */

	/* 
	 * If no timeout pending, transfer a block of characters from the 
	 * output queue to the internal buffer, and set the internal buffer
 	 * pointer to zero.  If line is in RAW mode, send the characters to 
	 * line output buffer, send the number of bytes output, line number, 
	 * and output command to the board, set the command busy flag, and
	 * set busy state on
	 */

	if((amxscd[unit] & TIME_STAT) != TIME_STAT) {

#ifdef DEBUG
		if(amxdebug >= 3)
			printf("amx-start, no timeout pending\n");
#endif
		io_count = q_to_b(&tp->t_outq, buff, limit);
		if(io_count == 0) {
			tp->t_state &= ~BUSY;
			return;
		}
		if (tp->t_flags & RAW)  {
#ifdef DEBUG
			if(amxdebug >= 3)
				printf("amx-start, RAW mode\n");
#endif
	       		amxbpoke(unit,0,io_count,Fbase->line[l_num].obp,buff);
			do {				
				amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
			}while(Fbase->cmd != 0);
			if(amxcmd[bnum] & CMD_FLAG)
				amxintr(3);
			s = spl7();
        		Fbase->line[l_num].obn = io_count;
			Fbase->cunit = l_num;
        		Fbase->cmd = OUTPUT;
        		amxpoke(of->o_line[l_num].o_obn,amxladdr[unit],2,&Fbase->line[l_num].obn);
        		amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
        		amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
			amxcmd[bnum] |= CMD_FLAG;
			splx(s);
			return;
		}	
		amxtout[unit].index = 0;
		amxtout[unit].size = io_count;
	}

	/*
	 * If internal buffer pointer points to the end of the buffer, transfer
	 * a block of characters from the output queue to the internal buffer
	 * and set the pointer back to zero
	 */

	if(amxtout[unit].index == TTYHOG) {
		io_count = q_to_b(&tp->t_outq, buff, limit);
		if(io_count == 0) {
			tp->t_state &= ~BUSY;
			return;
		}
		amxtout[unit].index = 0;
		amxtout[unit].size = io_count;
	}

	/*
	 * Scan through the characters in the internal buffer for timeout
	 * characters, if the character is not timeout character, set the 
	 * parity and continue, once a timeout character is scanned this 
	 * process will stop
	 */

#ifdef DEBUG
	if(amxdebug >= 3)
		printf("amxstart: scan timeout\n");
#endif
	bufp = &buff[amxtout[unit].index];
	for(i=amxtout[unit].index;i<amxtout[unit].size;i++) {
		if(*bufp <= 0x7f)
/*			*bufp++ |= partab[*bufp] & 0200; */
			*bufp++;
		else {
			amxtout[unit].index = i;
			break;
		}
	}

	/*
	 * Send the characters out to the line output buffer, send the number of
	 * bytes output, line number, and output command to the board.  Set the
	 * command busy flag
	 */

	amxbpoke(unit,amxtout[unit].index,i,Fbase->line[l_num].obp,buff);
	do {				
		amxpeek(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	}while(Fbase->cmd != 0);
	if(amxcmd[bnum] & CMD_FLAG)
		amxintr(3);
	s = spl7();
       	Fbase->line[l_num].obn = i;
	Fbase->cunit = l_num;
       	Fbase->cmd = OUTPUT;
       	amxpoke(of->o_line[l_num].o_obn,amxladdr[unit],2,&Fbase->line[l_num].obn);
       	amxpoke(of->o_cunit,cf->c_base,1,&Fbase->cunit);
       	amxpoke(of->o_cmd,cf->c_base,1,&Fbase->cmd);
	amxcmd[bnum] |= CMD_FLAG;
	splx(s);

	/*
	 * If the whole internal buffer does not contain any timeout character,
	 * reset the timeout indicator, and set busy state on
	 */

	if(i == amxtout[unit].size)
		amxscd[unit] &= ~TIME_STAT;
	else{

	/* 
	 * If any timeout character appears in the internal buffer, set the
	 * timeout indicator, and the timeout state
	 */
		amxscd[unit] |= TIME_STAT;
		tp->t_state |=TIMEOUT;
		tp->t_state &= ~BUSY;
		amxtout[unit].outchar = *buff & 0x7f;
	}
}

/*
 * This procedure handles the ioctl system calls for such things as baud rate
 * changes and various hardware control changes from the initial set up.
 * Currently only baud rate changes and terminal mode changes are supported.
 *
 * TITLE:	amxioctl
 *
 * CALL:	amxioctl(dev, comd, addr, flag)
 *
 * INTERFACES:	ioctl
 *
 * CALLS:	amxparam, amxpoke, ttioccomm
 *
 */

amxioctl(dev, comd, addr, flag)
caddr_t addr;
{
	struct tty *tp;
	struct firmAMX *Fbase;
	struct amxoff *of;
	int	board, l_num, s, unit;

	unit = minor(dev) & MINORMSK;				/* get the unit number */

#ifdef DEBUG
	if(amxdebug >= 2)
		printf("amx-ioctl unit %d ",unit);
#endif

	tp = &amxtty[unit];					/* set up ptr. to tty struct */

	if (ttioccomm(comd, tp, addr, dev)) {
		board = unit >> 2;
		l_num = unit & 03;
		Fbase = &amxfirm[board];
		of = &amxoff[board];
	
		if (comd==TIOCSETA || comd ==TIOCSETC)
			amxparam(dev);
		if(tp->t_flags & RAW) {				/* disable CNTL-Q/CNTL-S */
			s = spl7();
        		Fbase->line[l_num].enb = ENBOUT;
        		amxpoke(of->o_line[l_num].o_enb,amxladdr[unit],1,&Fbase->line[l_num].enb);
			splx(s);
		}else {						/* enable CNTL-Q/CNTL-S */
			s = spl7();
        		Fbase->line[l_num].enb = ENBOUT | XOFFON;
        		amxpoke(of->o_line[l_num].o_enb,amxladdr[unit],1,&Fbase->line[l_num].enb);
			splx(s);
		}
	}else
		u.u_error = ENOTTY;
}
 

/*
 * This procedure is to send the characters from the internal io buffer to the 
 * output buffer on the board.
 *
 * TITLE:	amxbpoke
 *
 * CALL:	amxbpoke(unit, index, cnt, oset)
 *		where:
 *			unit	is the unit which characters should send to
 *			index	is the address of the next available output
 *				character in the internal buffer
 *			cnt	is the number of characters
 *			oset	is the offset of bytes from the beginning of the
 *				output buffer to the position of the next
 *				available output character place
 *
 * INTERFACES:	amxstart
 *
 * CALLS:	amxpoke
 *
 */

amxbpoke(unit,index,cnt,oset,bufp)
int  unit, index, cnt, oset;
char *bufp;
{

	int	byte_rem, cntl, s, temp_cnt;

#ifdef DEBUG
	if (amxdebug >= 2)
		printf("amx-bpoke unit %d ",unit);
#endif

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
 
/*
 * This procedure is to send the information in the memory to the board.
 * This procedure is called with interrupt off (spl7).
 * when enter this procedure.
 *
 * TITLE:	amxpoke
 *
 * CALL:	amxpoke(offset, selector, count, src_off)
 *		where:
 *			offset		is the offset from the beginning of the
 *					command structure of the line structure
 *					of the firmware, of the first available
 *					byte of the input/output buffer
 *			selector	is the address of the selector on the board
 *			count		is the byte count to be 'poked'
 *			src_off		is the location where the 'poked' data 
 *					will be take from
 *
 * INTERFACES:	amxbpoke, amxcal, amxclose, amxintr, amxioctl, amxopen
 *		amxparam, amxprobe, amxstart
 *
 * CALLS:	mapwork
 *
 */	

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

#ifdef DEBUG
	if (amxdebug >=2)
		printf("amx poke ");
#endif

	temp = selector + (long)offset;
#ifdef MMU
	pgnum = (short)atopn(temp);
	p_offset = (short)(temp & (MMPGSZ - 1));
	p = (mapwork(pgnum)) + p_offset;
	for(i=0; i<count; i++)
		*p++ = *src_off++;
#else
	ds = 0;
	if(temp&0xffff0000)
	{
		x = temp % (long)MMPGSZ;
		ds = temp / (long)MMPGSZ;
		ds = ptosr(ds);
		temp = x;
	}
	if(ds)
	{
		for(i=0; i<count; i++)
		{
			c = spl7();
			setbyte(ds,(int)temp++,*src_off++);
#ifdef DEBUG
	if(amxdebug >= 3)
		printf("\nds=%x offset=%d wert=%x\n",ds,(int)(temp -1),*(src_off -1));
#endif
			splx(c);
		}
	}
	else
	{
		p = (int)temp;
		for (i=0; i<count; i++)
       			*p++ = *src_off++;
	}
#endif

#ifdef DEBUG
	if(amxdebug >= 4)
		monitor();
#endif
}

/*
 * This procedure is to read the information from the board to the memory.  Interrupt is mask (spl7)
 * when enter this procedure.
 *
 * TITLE:	amxpeek
 *
 * CALL:	amxpeek(offset, selector, count, dest_off)
 *		where:
 *			offset		is the offset from the beginning of the
 *					command structure of the line structure
 *					of the firmware, of the first available
 *					byte of the input/output buffer
 *			selector	is the address of the selector on the board
 *			count		is the byte count to be 'peeked'
 *			dest_off	is the location where the 'peeked' data 
 *					will be placed
 *
 * INTERFACES:	amxcal, amxopen, amxparam, amxprobe, amxintr amxstart
 *
 * CALLS:	mapwork
 *
 */	


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

#ifdef DEBUG
	if(amxdebug >= 2)
		printf("amx peek ");
#endif

	temp = selector + (long)offset;
#ifdef MMU
	pgnum = (short)atopn(temp);
	p_offset = (short)(temp & (MMPGSZ - 1));
	p = (mapwork(pgnum)) + p_offset;
	for(i=0; i<count; i++)
		*dest_off++ = *p++;
#else
	ds = 0;
	if(temp&0xffff0000)
	{
		x = temp % (long)MMPGSZ;
		ds = temp / (long)MMPGSZ;
		ds = ptosr(ds);
		temp = x;
	}
	if(ds)
	{
		for(i=0; i<count; i++)
		{
			c = spl7();
			x = getbyte(ds,(int)temp++);
#ifdef DEBUG
	if(amxdebug >= 3)
		printf("\nds=%x offset=%d wert=%x\n",ds,(int)(temp -1),x);
#endif

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
#endif
#ifdef DEBUG
	if(amxdebug >= 4)
		monitor();
#endif
}


struct cblock {
	struct cblock *c_next;
	char	c_info[CBSIZE];
};

extern struct	cblock	cfree[];
struct	cblock	*cfreelist;

/*
 * copy clist to buffer.
 * return number of bytes moved.
 */
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
		q->c_cf = q->c_cl = NULL;
		splx(s);
		return(0);
	}
	acp = cp;
	cc++;

	while (--cc) {
		*cp++ = *q->c_cf++;
		if (--q->c_cc <= 0) {
			bp = (struct cblock *)(q->c_cf-1);
			bp = (struct cblock *)((int)bp & ~CROUND);
			q->c_cf = q->c_cl = NULL;
			bp->c_next = cfreelist;
			cfreelist = bp;
			break;
		}
		if (((int)q->c_cf & CROUND) == 0) {
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
