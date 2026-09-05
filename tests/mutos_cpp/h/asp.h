#define NASP 1				/* Number of ASP boards */
#define SPL spl5			/* raised interrupt priority */

#define VEC_CTC		0x60		/* Vector for init CTC, channel 0+3 */
#define VEC_PI_STAT	0x68		/* Vector for init PIO, status */
#define VEC_PI_DATA	0x6C		/* Vector for init PIO, data */
#define VEC_SIO		0x70		/* Vector for init SIO */
/*
 *	Device Structures
 */
struct	aspcfg {
		int p_level;		/* interrupt level */
		int p_ien;		/* port for IENABLE */
		int p_reti;		/* port for RETI */
		int p_rgp;		/* control signal register */
		int p_a_dat;		/* pio data port A */
		int p_b_dat;		/* pio data port B */
		int p_a_cntl;		/* pio control port A */
		int p_b_cntl;		/* pio control port B */
		int p_zk0;		/* port time channel 0 (free) */
		int p_zk1;		/* port time channel 1 (S2/V24) */
		int p_zk2;		/* port time channel 2 (IFSS) */
		int p_zk3;		/* port time channel 3 (free) */
		int p_dka;		/* data port S2 */
		int p_dkb;		/* data port IFSS */
		int p_ska;		/* status port S2 */
		int p_skb;		/* status port S2, IFSS */
};
