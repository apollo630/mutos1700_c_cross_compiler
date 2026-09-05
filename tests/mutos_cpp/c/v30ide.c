/*
 * V30 IDE device driver for MUTOS 1700 
 *
 * Some parts of this code were taken from V7/x86 source code. 
 * See www.nordier.com/v7x86, /usr/sys/dev/hd.c
 * Copyright (c) 2006 Robert Nordier. All rights reserved.
 *
 * This is a set of procedures that make up a simple IDE device driver
 * for a ZVE K2771 hardware expansion board containing a very basic IDE
 * adapter along with some clock switching circuitry for using a NEC V30 CPU.
 * The board is using unused Bit 4 of PPI 8255, port A to switch the CPU
 * to a higher clock rate (a low clock rate is needed during ACT for passing
 * the oscillator test).
 *
 * The IDE adapter is using PIO CPU polling only, there are no interrupts
 * and there is no support for DMA.
 *
 * Unit 0 0 is expected to be an non-removable IDE DOM flash.
 * Unit 1 is a removable CF card and the driver is checking if the card has
 * been replaced on each call to hdopen().
 *
 * Since both units are flash based there are basically no seek times
 * and even in PIO mode they are faster than the CPU so that using blocking 
 * polling I/O routines without interrupt is not expected being a disadvantage 
 * in this setup. Not using interrupts might even reduce latency quite a bit.
 *
 * The driver is using V20/V30 in/out multiple opcodes to increase throughput.
 * There is MBR partition table support for up to 4 primary partitions.
 *
 */

#include "../h/param.h"
#include "../h/systm.h"
#include "../h/buf.h"
#include "../h/dir.h"
#include "../h/a.out.h"
#include "../h/user.h"
#include "../h/v30ide.h"

static struct identDev identDev;
static struct devinfo devinfo[2];
static struct hd hd;
static struct part part[2][1+NPTE]; /* 2 units, 1+4 partitions per unit */
struct buf rhdbuf;     				/* buffer header for raw block-device I/O */
struct buf hdtab;							/* buffer header for buffer cache I/O */

/* static marker variables */
static int currUnit = -1;     /* currently used unit */
static int haveV30IDE = 0;    /* V30 IDE expansion present */ 

static int hdrw(), smplCmd(), await();
static ident(), swtchDev(), delay(), bpEnxio();

/* defintion in conf/c.c */
extern int Stand;

/* definitions in mdep/mch.c */
extern int inb();
extern outb();
extern int hdio();            /* NEC V20/V30 version */
extern int isNEC();           /* returns true in case we have a NEC V20/V30 */


hdinit()
{

  int valPA;                  /* value of 8255 port A */

  /* At this point we don't know yet if the IDE adapter is present or not.
   * Still we (try) to perform a software reset of both drives to make sure
   * they are in a defined state prior to checking if drive 0 is actually
   * present.
   */
  outb(DC, RSTON);            /* set SRST */
  delay();
  outb(DC, RSTOFF);           /* clear SRST */
  delay();  

  /* 
   * The master (drive 0) is non-removable. 
   * We assume the IDE interface to be present in case there is drive 0 present.
   * The slave (drive 1) is a removable CF-card.
   * We need to re-check if a slave is present on each call to hdopen().
   *
   */ 
  hd.unit = 0;                /* select master */

  if (isNEC()) {
    ident(QUIET);
    haveV30IDE = devinfo[hd.unit].present;
  }

  if (Stand == 0) {
    printf("V30 IDE  Based %x %s.\n", BASE,
           haveV30IDE ? "found" : "NOT found"); 
    if (haveV30IDE) {
      for (hd.unit = 0; hd.unit < 2; hd.unit++) {
        printf(" %s ", hd.unit ? "Slave: " : "Master:");
        ident(VERBOSE);
      }
      hd.unit = 0;
    }
  }

  /* 
   * We assume having our special V30 IDE board with the ability to
   * switch the CPU from a slow clock (for passing ACT) to a faster clock.
   * Switching of CPU clock is done via unused bit 4 of PPI 2255 port A:
   * Bit 4 = 0 --> fast clock speed
   * Bit 4 = 1 --> slow clock speed
   *
   */
  if (haveV30IDE) {
    valPA = inb(PPI_PA);      /* port A is input by default; read its value */
    valPA = valPA & NECFAST;  /* clear bit 4 */
    outb(PPI_CTL, 0x84);      /* switch port A to output, p.83 of K2771 doc */
    outb(PPI_PA, valPA);      /* switch CPU to fast clock speed */
    outb(PPI_CTL, CTL_DEF);   /* switch port A to default (=input) again */
  }
}


hdopen(dev)
dev_t dev;
{
  register int unit;
  struct buf *bp;
  struct ptent *peptr;        /* ptr to mbr partition table entry */
  int i;

  if (!haveV30IDE) {
    u.u_error = ENXIO;
    return;
  }

  /* re-check if a slave is present */
  unit = UNIT(dev);
  if (unit == 1) {
    hd.unit = unit;
		/* 
     * force calling swtchDev() to re-select CF card
		 * since it might have been removed in the meantime
     *
     */
		currUnit = -1;
    ident(QUIET);									
  }

  /* check if unit is present */
  if(!devinfo[unit].present) {
    u.u_error = ENXIO;
    return;
  }

  /* remainder of function is to read mbr and fill partition table */
  if ((unit == 0) && part[unit][0].size)
    return;                   /* omit re-reading mbr for unit 0 */

  /* clear (old) partition table in case new cf card doesn't have an mbr */
  for (i = 0; i <= NPTE; i++) {
    part[unit][i].base = 0;
    part[unit][i].size = 0; 
  }
  /* first entry in partition table is for whole disk */
  part[unit][0].size = devinfo[unit].sectors;

  /* read first bdev block of whole disk */
  bp = bread((BDEVMAJ << 8) | (UNIT(dev) << 3), (daddr_t)0 );
  if ( ((bp->b_flags & B_ERROR) == 0) &&
       (*(unsigned short *)(bp->b_un.b_addr + 510) == PTMAGIC) ) {
    peptr = (struct ptent *)(bp->b_un.b_addr + PTOFF);
    for (i = 1; i <= NPTE; peptr++, i++) {
#ifdef MIDDLE_ENDIAN
      *(USHORT *)&part[unit][i].base = *((USHORT *)&peptr->bas + 1);
      *((USHORT *)&part[unit][i].base + 1) = *(USHORT *)&peptr->bas;
      *(USHORT *)&part[unit][i].size = *((USHORT *)&peptr->siz + 1);
      *((USHORT *)&part[unit][i].size + 1) = *(USHORT *)&peptr->siz;
#else
      part[unit][i].base = peptr->bas;
      part[unit][i].size = peptr->siz;
#endif
    }
  }
  brelse(bp);                 /* release buffer */
}


hdclose(dev, flag)
dev_t dev;
int flag;
{
  if (( dev == rootdev ) || ( dev == swapdev ))
    return;

  hd.unit = UNIT(dev);
  if (smplCmd(FLSHCCH))       /* flush cache */
    /* don't throw an error in case the command has been aborted
       since this indicates that 'flush cache' is not supported */
    if ( !(inb(ERROR) & ABRT) )
      u.u_error = EIO;
}


hdstrategy(bp)
register struct buf *bp;
{
  int unit, partnum;         
  int bcnt, scnt;							/* block and sector count */
  daddr_t snum;								/* start sector number */
  int err;
  unsigned addr;

  /* case 1: raw I/O */
  if (bp->b_flags & B_PHYS) {
    /* bp->b_blkno is a byte-offset
       make sure byte-offset and byte-count are dividable by 512 */
    if ( (bp->b_blkno % 512) || (bp->b_bcount % 512) ) {
      bp->b_flags |= B_ERROR;
      bp->b_error = EINVAL;
      iodone(bp);
      return;
    }
    /* sector number: divide byte-offset by sector size of 512 byte */
    snum = bp->b_blkno >> (daddr_t) 9;
		/* sector count: divide byte-count by sector size */
		scnt = bp->b_bcount >> 9;
  } 
  /* case 2: buffered I/O */
  else {
    /* bp->b_blkno is a block number */
    snum = bp->b_blkno << (daddr_t) LOG2(SPB);
    /* byte-count -> block-count */
    bcnt = (bp->b_bcount + BMASK) >> BSHIFT;
    /* block-count -> sector-count */
    scnt = bcnt << LOG2(SPB); 
	}

  unit = UNIT(bp->b_dev);
  partnum = PART(bp->b_dev);

  /* printf("unit: %d, partition: %d\n", unit, partnum);
  printf("bp->b_blkno: %D, snum: %D\n", bp->b_blkno, snum);
  printf("bp->b_bcount: %d, bcnt: %d, scnt: %d\n", 
					bp->b_bcount, bcnt, scnt); */

  if (snum + scnt > part[unit][partnum].size) {
    bpEnxio(bp);
    return;
  }

  if (scnt == 0) {
    bp->b_resid = 0;
    iodone(bp);
    return;
  }

  hd.unit = unit;
  hd.scnt = scnt;
  hd.read = bp->b_flags & B_READ;
  hd.snum = part[unit][partnum].base + snum;

  /* 
   * In some cases (e.g. swapping) it may happen that we have a
   * big lower-order memory address and a big sector count too.
   * This may cause a wrap-around of the lower-order memory address
   * which must be prevented. The solution is to move bits 8-15
   * of the lower-order memory address to the segment part.
   * Example: bp->b_xmem = 0x0001, bp->b_un.b_addr = 0xd000
   *
   */
  
  /* segment = high-order memory address shifted by 12 bits,
     remaining 4 bits will be done by segmentation hardware */
  hd.segm = ((int)bp->b_xmem) << 12;                 /* 0x1000 */ 
  addr    = ((unsigned)bp->b_un.b_addr) >> 4;        /* 0x0d00 */
  hd.segm += addr;                                   /* 0x1d00 */
  hd.addr = (caddr_t)(((int)bp->b_un.b_addr) & 0xf); /* 0x0000 */

  err = hdrw();
  if (err) {
    deverror(bp, hd.read ? RD_SECT : WR_SECT, err);
    bp->b_flags |= B_ERROR;
    bp->b_error = EIO;
  }
  
  iodone(bp);  
}


/* raw block-device I/O functions */
hdread(dev)
dev_t dev;
{
  physio(hdstrategy, &rhdbuf, dev, B_READ);
}

hdwrite(dev)
dev_t dev;
{
  physio(hdstrategy, &rhdbuf, dev, B_WRITE);
}


/*
 * call identify device for a given hd.unit
 * check if device is present and in proper state
 * fill global struct devinfo for given device
 * print some device information to console if verbose
 * 
 */
static ident(verbose)
int verbose;
{
  int i;
  char model[41];             /* ASCII model name */ 
  char firmware[9];           /* ASCII firmware revision */
  daddr_t sectors;            /* total number of sectors */

  devinfo[hd.unit].present = 0;
  devinfo[hd.unit].sectors = 0;

  if (!smplCmd(IDENTDV) && !hdio(SEGKD, &identDev, 1, 1)) {

    for (i = 0; i < sizeof(identDev.model); i+=2) {
      model[i] = identDev.model[i+1];
      model[i+1] = identDev.model[i];
    }
    model[40] = '\0';
    for (i = 0; i < sizeof(identDev.firmware); i+=2) {
      firmware[i] = identDev.firmware[i+1];
      firmware[i+1] = identDev.firmware[i];
    }
    firmware[8] = '\0';

/*
 * Number: 0x11223344 
 * --> increasing memory address -->
 * LSB             MSB 
 * 0x44 0x33 0x22 0x11  regular little endian format
 * 0x22 0x11 0x44 0x33  PDP-11 middle endian format
 *
 */
#ifdef MIDDLE_ENDIAN
    *(USHORT *)&sectors = *((USHORT *)&identDev.lbasecs + 1);
    *((USHORT *)&sectors + 1) = *(USHORT *)&identDev.lbasecs;
#else
    sectors = identDev.sectors;
#endif

    if (identDev.capab & LBACAP) {
      devinfo[hd.unit].present = 1;
      devinfo[hd.unit].sectors = sectors;
    }
  }

  if (verbose) {
    if (devinfo[hd.unit].present) {
      printf("%s FW: %s\n", model, firmware);
      printf("  %D 512-byte sectors, %D %d-byte blocks (%D MiB)\n", 
                         sectors, sectors/SPB, BSIZE, sectors/2/1024);
    } else
      printf("not found\n");
  }
}


/* 
 * read/write sectors according to struct hd
 * blocking polling version w/o interrupts
 * return 0 on success or ERR1/ERR2 otherwise
 * in case of ERR2 the low-byte contains ERR2
 * and the high-byte the content of the status register
 *
 */
static int hdrw()
{
  register int status;

  if (await(STAT, BSY | DRQ)) /* make sure BSY and DRQ are cleared */
    return ERR1;

  /* changing the drive requires a 400ns delay allowing the new drive to settle
     avoid that extra delay() in case the drive has not been changed */
  if (currUnit != hd.unit) {
    swtchDev();
    if (await(STAT, BSY | DRQ))
    return ERR1;
  }

  outb(DC, IRQOFF);           /* stop current device from sending interrupts */
  outb(FR, 0);
  outb(SC, hd.scnt);
  outb(SN, (int)hd.snum);
  outb(CL, (int)(hd.snum >> 8));
  outb(CH, (int)(hd.snum >> 16));
  outb(DH, LBA | (hd.unit ? DEV1 : DEV0) | ( ((int)(hd.snum >> 24)) & 0xf));
  outb(CMD, hd.read ? RD_SECT : WR_SECT);
  /* do sector-looping in assembly for performance reasons */
  return( hdio(hd.segm, hd.addr, hd.scnt, hd.read) );
}


static int smplCmd(cmd)
int cmd;
{
	register int status;

  if (await(STAT, BSY | DRQ)) /* make sure BSY and DRQ are cleared */  
    return ERR1;              /* timeout */

  if (currUnit != hd.unit) {
    swtchDev();
    if (await(STAT, BSY | DRQ))
      return ERR1;
  }
 
  outb(CMD, cmd);
  if (await(STAT, BSY))
    return ERR1;

	status = inb(STAT);
  if (status & (BSY | DF | ERR))
    /* shift content of status register to upper 8 bit */
    return((status << 8) | ERR2);

  return 0;       
}


static swtchDev()
{
  outb(DH, hd.unit ? DEV1 : DEV0);
  currUnit = hd.unit;
  delay();
}


static delay()
{
  inb(STAT);
  inb(STAT);
  inb(STAT);
  inb(STAT);
}


/* 
 *
 * wait on port until all mask bits are cleared or a timeout occurs
 *
 */  
static int await(port, mask)
int port;
int mask;
{
  unsigned int i;
  
  for (i = 0; i < TIMEOUT; i++)
    if ((inb(port) & mask) == 0)
      return 0;
  return -1;
}


static bpEnxio(bp)
struct buf *bp;
{
  bp->b_flags |= B_ERROR;
  bp->b_error = ENXIO;
  iodone(bp);
}
