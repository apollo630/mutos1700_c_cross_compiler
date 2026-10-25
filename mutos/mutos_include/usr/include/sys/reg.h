/*
 * Index of the users' stored
 * registers relative to AX.
 * Usage is u.u_aAX[XX].
 * EVERYTHING HERE IS DEPENDENT ON trap: in mch.s and trap.c
 * AND THE HARDWARE
 */

#define URBC    14      /* # of bytes of user stack used */
#define URFLAG  12      /* these guys are in the user data space,       */
#define URCS    10      /* and are read by fuword(u.u_aAX[RSP] + URxx)  */
#define URIP     8
#define URVEC    6
#define URBP     4
#define URES     2
#define URDX     0

#define RSP     8
#define RSS     7
#define RDS     5
#define RDI     4
#define RSI     3
#define RCX     2
#define RBX     1
#define RAX     0
#define Rstate  -1              /* user/system mode, trap # */
#define Rilev   -2              /* interrupted procedure level */


#define CBIT    0x001           /* K1810WM86 'C' bit in flags     */
#define ZBIT    0x040           /* K1810WM86 'Z' bit in flags     */
#define TBIT    0x100           /* K1810WM86 trace bit in 'flags' */
#define IBIT    0x200           /* K1810WM86 interrupt enable bit */
