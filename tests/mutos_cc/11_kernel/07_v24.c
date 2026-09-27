#
/*
 *   ASP-V.24 driver
 */
#include "param.h"
#include "conf.h"
#include "asp.h"
#include "dir.h"
#include "a.out.h"
#include "user.h"
#include "tty.h"
#include "chars.h"
#include "systm.h"
#include "intr.h"

#define	SR1		01
#define	SR2		02
#define	SR3		03
#define	SR4		04
#define	SR5		05
#define	LR1		01
#define	LR2		02

#define	RESTRI		050	/* reset transmitter interrupt */
#define RESCHAN		030	/* reset channel */
#define RESERR		060	/* reset error */
#define	ENRINT		01	/* enable receiver interrupt */
#define INTREC		030	/* receiver interrupt mode */
#define	INTTRA		02	/* enable transmitter interrupt */
#define SMV		04	/* status modifies vector */
#define	RECCHAR		01	/* receive character available */
#define	TRBE		04	/* transmitter buffer empty */
#define	RF		0100	/* frame error */
#define	UF		040	/* receiver overrun */
#define	PF		020	/* parity error */

#define	XON		021
#define	XOFF		023
#define B9600		13
#define B4800		12
#define B2400		11
#define B1200		9
#define	B600		8
#define B300		7
#define	B200		6
#define B150		5
#define	B75		2
#define B50		1


#define	DLDELAY	4

struct	tty v24tty[NASP];
extern	struct	aspcfg	aspcfg[];

int	ttrstrt();
int	v24intr();
int	v24start();
extern	int	aspaligned;
extern int Stand;
int	nv24 = NASP;
int	v24end = 1;

char	maptab[];

char	v24dev[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* saved value of write register 5 to implement receive flow control */
static unsigned wr5;

v24init()
{
	register struct aspcfg *pasp;
	int	a;
	char	b;

	if(aspaligned == 0) {
		pasp = &aspcfg;
		a = 2;
		outb(pasp->p_b_cntl, 0xf);
		outb(pasp->p_b_dat, 0x55);
		b = inb(pasp->p_b_dat);
		outb(pasp->p_b_dat, 0);
		if(b != 0x55)
			a = 1;
		if(Stand == 0)
			printf("ASP      Based %x level %d %s.\n", pasp->p_ien, pasp->p_level, (a == 2)?"found":"NOT found");
		aspaligned = a;
		if(a == 2)
			outb(pasp->p_ien, 0);		/* Enable interrupt */	
	}
}

v24open(dev, flag)
dev_t	dev;
{
	register struct tty *tp;
	register struct aspcfg *pasp;
	register int	d;
	int	s;

	d = minor(dev);
	if((d >= NASP) || (aspaligned < 2)) {
		u.u_error = ENXIO;
		return;
	}
	tp = &v24tty[d];
	pasp = &aspcfg[d];
	tp->t_oproc = v24start;
	if ((tp->t_state&ISOPEN) == 0) {
		if(tp->t_ispeed == 0) {
			tp->t_ispeed = tp->t_ospeed = B9600;
			tp->t_flags = XTABS|CRMOD|ODDP;
			vchanini(dev,tp);
		}
		tp->t_state |= ISOPEN|CARR_ON;
		ttychars(tp);
	}
	else if (tp->t_state&XCLUDE && u.u_uid != 0){
		u.u_error = EBUSY;
		return;
	}
	s = spl3();
	inb(pasp->p_ska);		/* reset pointer */
	outb(pasp->p_ska, (SR1 | RESERR)) ;
	outb(pasp->p_ska, (INTREC | SMV | INTTRA));
	inb(pasp->p_skb);
	outb(pasp->p_skb, SR1);
	outb(pasp->p_skb, (INTREC | SMV | INTTRA)) ;
	outb(pasp->p_reti, IENABLE) ;
	splx(s);
	ttyopen(dev,tp);
}

v24close(dev,flag)
dev_t	dev;
int	flag;
{
	register struct tty *tp;
	register struct aspcfg *pasp;
	register int	d;
	int	s;

	d = minor(dev);
	tp = &v24tty[d];
	pasp = &aspcfg[d];
	if ((tp->t_state&ISOPEN) == 0)
		return;
	tp = &v24tty[minor(dev)];
	ttyclose(tp);
	s = spl3();
	inb(pasp->p_ska);		/* reset pointer */
	outb(pasp->p_ska, (SR1 | RESERR)) ;
	outb(pasp->p_ska, 0);		/* disable interrupts */
	splx(s);
}

v24read(dev)
dev_t	dev;
{
	ttread(&v24tty[minor(dev)]);
}

v24write(dev)
dev_t	dev;
{
	ttwrite(&v24tty[minor(dev)]);
}

v24intr(level)
int	level;
{
	register struct tty *tp;
	register struct aspcfg *pasp;
	register	int	c,d;
	int	temp;

	
	d = v24dev[level];
	tp = &v24tty[d];
	pasp = &aspcfg[d];
	if ((struct	device	*)tp->t_addr == 0) {
		tp->t_oproc = v24start;
		}
	inb(pasp->p_ska); 	/* reset pointer */

loop1:
	if ((temp = inb(pasp->p_ska))&RECCHAR) {

    /* clear RTS */
    outb(pasp->p_ska, SR5);         /* select channel A, write register 5 */
    outb(pasp->p_ska, wr5 & 0xfd);  /* clear bit 1 (RTS) */

		v24end = 1;
		c = inb(pasp->p_dka);
		outb(pasp->p_ska, LR1) ;
		if ((inb(pasp->p_ska))&(RF|UF|PF)) {
			outb(pasp->p_ska, RESERR);
		}
		if((tp->t_flags&SD) && ((c&0177) == DC4)) {
			tp->t_xstate |= DC4ERROR;
			tp->t_state |= TTSTOP;
			}
		else {
			putc(c, &tp->t_inpq);
			v24end++;
			goto loop1;
		}
	}
	v24end = 0;
	if (temp & TRBE ) {   /* transmit buffer empty */
		outb(pasp->p_ska, RESTRI) ;
		if((tp->t_state&(ISOPEN|CARR_ON)) == (ISOPEN|CARR_ON)) {
			ttstart(tp);
			if (tp->t_outq.c_cc == 0 || tp->t_outq.c_cc == TTLOWAT)
				wakeup((caddr_t)&tp->t_outq);
		}
	}
	if(v24end)
		goto loop1;
reti:
	while((c = getc(&tp->t_inpq)) >= 0) {
		ttyinput(c, tp);
		if(inb(pasp->p_ska)&RECCHAR)
			goto loop1;
	}
	if(inb(pasp->p_ska)&RECCHAR)
		goto loop1;

  /* set RTS */
  outb(pasp->p_ska, SR5);         /* select channel A, write register 5 */
  outb(pasp->p_ska, wr5 | 0x02);  /* set bit 1 (RTS) */

	outb(pasp->p_reti, IENABLE);
	if(tp->t_xstate&DC4ERROR){
		while (getc(&tp->t_outq) >= 0);
		wakeup((caddr_t)&tp->t_outq);
	}
}


v24ioctl(dev,cmd,addr,flag)
caddr_t	addr;
dev_t	dev;
{
	register struct tty *tp;
	int speed;
	unsigned flags;

	tp = &v24tty[minor(dev)];
	speed = tp->t_ispeed;
	flags = tp->t_flags;
	if (ttioccom(cmd,&v24tty[minor(dev)],addr,dev) == 0) {
		u.u_error = ENOTTY;
		return;
	}
	if((speed != tp->t_ispeed) || ((flags&(DW8B|ODDP|EVENP)) != (tp->t_flags&(DW8B|ODDP|EVENP))))
		vchanini(dev,tp);
}

v24start(tp)
register struct tty *tp;
{
	register struct aspcfg *pasp;
	register c,d;

	d = minor(tp->t_dev);
	pasp = &aspcfg[d];
	inb(pasp->p_ska);		/* reset pointer */
	if(((inb(pasp->p_ska)&TRBE) == 0)
	|| (tp->t_state&TTSTOP)
	|| (tp->t_xstate&XPAGE1))
		goto	ret;
	if((c = getc(&tp->t_outq)) >= 0){
		v24end++;
		if(tp->t_flags&RAW)
			outb(pasp->p_dka, c) ;
		else if(c <= 0177)
			outb(pasp->p_dka, c) ;
		else if(c == 0200)
			tp->t_xstate |= XPAGE1;
		else {
			timeout(ttrstrt, (caddr_t)tp, (c&0177) + DLDELAY);
			tp->t_state |= TIMEOUT;
		}
	}
	else if(tp->t_state&ASLEEP){
		tp->t_state &= ~ASLEEP;
		wakeup((caddr_t)&tp->t_outq);
	}
ret:
	outb(pasp->p_reti, IENABLE);
	return;
}

vchanini(dev,tp)
dev_t	dev;
register	struct	tty 	*tp;

{
	register struct aspcfg *pasp;
	register	d,s,o,zk;

	s = spl3();
	d = minor(dev);

	pasp = &aspcfg[d];
	switch(tp->t_ispeed) {
		case B9600:
		case B1200:
		case B150:
		case B50:
			o = 0100;	/* Takt */
			break;
		case B4800:
		case B600:
		case B75:
			o = 0200;
			break;
		case B2400:
		case B300:
		case B200:
			o = 0300;
			break;
		default:
			u.u_error = EINVAL;
			return;
	}
	switch(tp->t_ispeed) {
		default:
		case B9600:
		case B4800:
		case B2400:
				zk = 1;
				break;
		case B1200:
		case B600:
		case B300:
				zk = 8;
				break;
		case B200:
				zk = 12;
				break;
		case B150:
		case B75:
				zk = 64;
				break;
		case B50:
				zk = 196;
				break;
	}
	inb(pasp->p_ska);	/* reset pointer */
	outb(pasp->p_ska, RESCHAN) ;
	outb(pasp->p_ska, SR4) ;
	outb(pasp->p_zk1, 3); 	/* reset time channel */
	outb(pasp->p_zk1, 7);	/* TC0 TC1 TC2 */
	outb(pasp->p_zk1, zk);
	if(tp->t_flags&(ODDP|EVENP) && ((tp->t_flags&(ODDP|EVENP)) != (ODDP|EVENP)))
		o |= (tp->t_flags&ODDP) ? 01 : 03;
	o  |= 04;
	outb(pasp->p_ska, o) ;

	outb(pasp->p_ska, SR5) ;
	o = 050;			/* BDE: Sendez.-laenge/Freigabe */
	if(tp->t_flags&DW8B)
		o |= 0100;	/* 8 Datenbit */
	o |= 0202;		/* 108 (DTR) + 105 (RTS) ein*/
	outb(pasp->p_ska, o) ;
  wr5 = o;      /* save value of write register 5 to static variable */

	o = 0100;
	outb(pasp->p_ska, SR3) ;
	if(tp->t_flags&DW8B) 
		o |= 0200;		/* 8 Datenbit */
	outb(pasp->p_ska, o);
	outb(pasp->p_ska, SR3) ;
	outb(pasp->p_ska, (o|ENRINT)) ;

	inb(pasp->p_skb);

	outb(pasp->p_skb, SR2) ;
	outb(pasp->p_skb, VEC_SIO) ; /* vector */
	outb(pasp->p_ska, SR1) ;
	outb(pasp->p_ska, (INTREC | SMV | INTTRA)) ; 
	outb(pasp->p_skb, SR1) ;
	outb(pasp->p_skb, (INTREC | SMV | INTTRA)) ;
	outb(pasp->p_reti, IENABLE) ;
	splx(s);
}

