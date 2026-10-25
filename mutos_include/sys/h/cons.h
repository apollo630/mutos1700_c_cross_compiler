#define	SPL		spl5		/* for driver mutex */

#define ISPEED          13              /* initial baud rate of 9600==(13);300==(7)*/
#define DTRON           0x80            /* bit 8 of usart status byte; 1 if present */

#define	VEC_KBD	0x20		/* Vector for keyboard */

struct kbdcfg {
	int	kb_data;	/* data port */
	int	kb_ctl;		/* control port */
};

/*
 * Structure for base assignments of boards in the configuration
 * This is used to associate the board to the base address when the 
 * interrupt level is not known.
 */
struct conscfg{
	int	c_data;
	int	c_control;
	int	c_odata;
	int	c_ocontrol;
	int	c_taddr;
	int	c_tcntrl;
	int	c_tunit;
};


/*
 * KR580WI53 PIT commands
 *
 *	RATEMD0:	read/load timer0(or 4) for mode 3 (baud rate generator)
 *
 */

#define	RATEMD0	0x36

/*
 * KR580WW51A USART command instructions
 *
 * SRESET :	disable		transmitter
 *		disable		reciever
 *		enable		return to mode instruction format
 *
 * SANSWER :	enable		transmitter
 *		enable		DTR
 *		enable		reciever
 *		enable		a reset of error flags
 *		enable		RTS
 *
 * STXD :	disable		transmitter
 *		enable		receiver
 *		enable		a reset of error flags
 *
 * SHANGUP :	disable		transmitter
 *		disable		reciever
 *		enable		a reset of error flags
 *
 *
 */

#define	SRESET	0x40
#define	SANSWER	0x37
#define STXD	0x14
#define	SHANGUP	0x10


/*
 * RXRDY: Reciever has data
 * TXRDY: Transmitter empty
 */

#define RXRDY	0x02
#define TXRDY	0x01

/*
 * KR580WW51A USART mode instructions
 *
 * SMODE : usart mode of operaton as follows:
 *	 	16X clock
 *	 	8 data bits
 *	 	No parity
 *		2 stop bits
 *
 */

#define	SMODE	0xCE
