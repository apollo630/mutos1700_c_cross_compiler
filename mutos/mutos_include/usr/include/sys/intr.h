
/*
 * intr.h
 *      definitions for the interrupt control system
 *
 */

/*
 * PIC Constants
 */

#define P1_VEC_BASE	0x08		/* Base Vector, Master PIC */ 
#define	P1_ICW1		0x15		/* Edge mode, slave(s), ICW4 */
#define	P1_ICW2		P1_VEC_BASE	/* Base of vectors PIC1 */
#define	P1_ICW4		0x0D		/* Nested, Master, Not auto-EOI */
#define	PIC_EOI		0x20		/* Non-specific EOI command */
#define	P1_MASK		P1_CMD2		/* output ==> set mask */
#define	PIC_SISR	0x0B		/* Select ISR for input */
#define	P1_ISR		P1_CMD1		/* input ==> read ISR PIC1 */

#ifdef M1834
#define P1_ICW3		PIC_MOFF(2)	/* Slave on level 2 */
#define	P1_CMD1		0x20		/* 1st command port PIC1 */
#define	P1_CMD2		0x21		/* 2nd command port PIC1 */
#define	P2_ISR		P2_CMD1		/* input ==> read ISR PIC2 */
#define	P2_MASK		P2_CMD2		/* output ==> set mask */
#define	P2_CMD2		0xC1		/* 2nd command port PIC2 */
#define	P2_CMD1		0xC0		/* 1st command port PIC2 */
#define	P2_ICW4		0x09		/* Nested, Slave, Not auto-EOI */
#define P2_ICW3		0x02		/* Slave ICW3 */
#define	P2_ICW2		P2_VEC_BASE	/* Base of vectors PIC2 */
#define	P2_ICW1		0x15		/* Edge mode, slave(s), ICW4 */
#define	P2_VEC_BASE	0x70		/* Base Vector, Slave PIC */
#else
#define	P1_ICW11	0x17		/* Edge mode, no slave(s), ICW4 */
#define P1_ICW3		PIC_MOFF(3)	/* Slave on level 2 */
#define	P1_CMD1		0xC0		/* 1st command port PIC1 */
#define	P1_CMD2		0xC2		/* 2nd command port PIC1 */
#define PIC_V_ASP	0x58		/* Base Vector, slave PIC */ 
#endif

/*
 * Generate masks to turn on/off a given level.
 * PIC_MASK: bit(i) = 1 ==> masked; 0 ==> enabled.
 */

#define	PIC_MON(i)	(0xFF & ~(1<<(i)))
#define	PIC_MOFF(i)	(1<<(i))
