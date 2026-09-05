#define NASK 2				/* Number of ASK boards */
#define NCHPASK 2			/* Number of channels per ASK board */
#define SPL spl5			/* raised interrupt priority */

/*
 *	Device Structures
 */
struct	askcfg {
		int p_level;		/* interrupt level */
		int p_ien;		/* port for IENABLE */
		int p_reti;		/* port for RETI */
		int p_a;		/* PPI port A */
		int p_b;		/* PPI port B */
		int p_c;		/* PPI port C */
		int p_cntl;		/* PPI control port */
		int p_zk0;		/* PIT port time channel 0 (SIO-A) */
		int p_zk1;		/* PIT port time channel 1 (SIO-B) */
		int p_zk2;		/* PIT port time channel 2 (Time out) */
		int p_zkc;		/* PIT control port */
		int p_dka;		/* data port A */
		int p_dkb;		/* data port B */
		int p_ska;		/* status port A */
		int p_skb;		/* status port B */
};
