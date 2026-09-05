
/*
 * clock.h
 *	Definitions for on-board KR580WI53 (timer).
 *
 */

#ifndef	M1834
#define	PIT_CTR0_PORT	0xD0		/* PIT counter 0 port */	
#define	PIT_CTR1_PORT	0xD2		/* PIT counter 1 port */	
#define	PIT_CTR2_PORT	0xD4		/* PIT counter 2 port */	
#define	PIT_CTRL_PORT	0xD6		/* PIT control port */
#else
#define	PIT_CTR0_PORT	0x40		/* PIT counter 0 port */	
#define	PIT_CTR1_PORT	0x41		/* PIT counter 1 port */	
#define	PIT_CTR2_PORT	0x42		/* PIT counter 2 port */	
#define	PIT_CTRL_PORT	0x43		/* PIT control port */
#endif

/*
 * Control commands for KR580WI53
 */

#define PIT_S0          0x00            /* select timer 0 */
#define PIT_S1          0x40            /* select timer 1 */
#define PIT_S2          0x80            /* select timer 2 */
#define	PIT_CNTR_LATCH	0x00		/* counter latch operation */
#define	PIT_READ_LOAD	0x30		/* read/load least signf/most signf */
#define	PIT_SQWAVE_MODE	0x06		/* square-wave mode */
#define	PIT_RATE_MODE	0x06		/* square-wave mode for USART */
#define	PIT_ONE_SHOT	0x00		/* one-shot mode */
