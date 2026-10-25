/*
 * V30 IDE driver declarations
 *
 * Some parts of this code were taken from V7/x86 source code. 
 * See www.nordier.com/v7x86.
 * Copyright (c) 2006 Robert Nordier. All rights reserved.
 *
 */

/* 
 * The Robotron C-Compiler for MUTOS1700 uses the PDP-11
 * middle endian format for storing 'long' 4-Byte words.
 * 
 */
#define MIDDLE_ENDIAN   1

#define SPB   (BSIZE/512)     /* sectors per block */
/* max. supported blocksize by LOG2(n) is 8 KiB */
#define LOG2(n) ( (n)==16 ? 4 : (n)==8 ? 3 : (n)==4 ? 2 : (n)==2 ? 1 : 0 )

/* IDE base address = 0xexxx */
#define BASE    0xe000

/* 
 * IDE controller registers 
 * we can't use relative offsets to BASE since this header file is included 
 * into the mdep/mch.c assembler file where braces are causing assembly errors
 *
 *  A5     A4    A3  A2  A1  A0   CPU address (A0 unused)
 * /CS1FX /CS3FX DA2 DA1 DA0      IDE interface address
 * ----------------------------------------------------------------------
 *  0      1     0   0   0   x    0x10: data reg
 *  0      1     0   0   1   x    0x12: error reg / feature reg
 *  0      1     0   1   0   x    0x14: sector count
 *  :      :     :   :   :   :
 *  0      1     1   1   1   x    0x1e: primary status / command
 *
 *  1      0     1   1   0   x    0x2c: alternate status / device control
 *
 */

#define DATA    0xe010        /* data reg */
#define ERROR   0xe012        /* error reg */
#define FR      0xe012        /* feature reg */
#define SC      0xe014        /* sector count */
#define SN      0xe016        /* sector number */
#define CL      0xe018        /* cylinder low */
#define CH      0xe01a        /* cylinder high */
#define DH      0xe01c        /* device head */
#define STAT    0xe01e        /* primary status */
#define CMD     0xe01e        /* command */

#define ASTAT   0xe02c        /* alternate status */
#define DC      0xe02c        /* device control */

/* IDE status flags */
#define BSY     0x80          /* busy */
#define DF      0x20          /* device fault */
#define DRQ     0x08          /* data request */
#define ERR     0x01          /* error */

/* IDE error flags */
#define ABRT    0x04          /* command aborted */

/* IDE commands */
#define IDENTDV 0xec          /* identify device */
#define RD_SECT 0x20          /* read sector(s) */
#define WR_SECT 0x30          /* write sector(s) */
#define FLSHCCH 0xe7          /* flush cache */      

/* IDE misc bits */
#define DEV0    0xa0          /* unit 0 */
#define DEV1    0xb0          /* unit 1 */
#define LBA     0x40          /* LBA mode */
#define LBACAP  0x200         /* LBA capab.: IDENTDV word 49, bit 9 */
#define RSTON   0x4           /* device control: set SRST */
#define RSTOFF  0x0           /* device control: clear SRST */
#define IRQOFF  0x2           /* device control: set nIEN */

#define TIMEOUT 65535         /* timeout for waiting on port */
#define ERR1    1             /* error: timeout */
#define ERR2    2             /* error: other */

/* NEC V30 CPU clock switching via PPI 8255, port A, bit 4 */
#define PPI_CTL 0xce          /* PPI 8255 control register */
#define PPI_PA  0xc8          /* PPI 8255 port A */
#define CTL_DEF 0x94          /* PPI 8255 control word default,
                                 see ZVE K2771 documentation, page 83 */ 
#define NECFAST 0xef          /* PPI port A, bit 4 cleared = fast clock speed */
#define NECSLOW 0x10          /* PPI port A, bit 4 set = slow clock speed */

/* flags passed to ident() function */
#define QUIET   0
#define VERBOSE 1

/* mbr definitions */
#define PTMAGIC 0xaa55        /* last two byte of mbr */
#define PTOFF   0x1be         /* partition table offset in mbr */
#define NPTE    4             /* number of partition table entries */

/* major block device number */
#define BDEVMAJ 3             /* has to be in sync with bdevsw in conf/c.c */
                              
/* minor dev mapping to unit number and partition:
   bit 0..2: partition number
   bit 3:    unit number
   bit 4..7: ununsed

   0000.0.000   0   /dev/hd   (whole disk)
   0000.0.001   1   /dev/hd1
   0000.0.010   2   /dev/hd2	
   0000.0.011   3   /dev/hd3	
   0000.0.100   4   /dev/hd4
   0000.1.000   8   /dev/cf   (whole disk)
   0000.1.001   9   /dev/cf1
   0000.1.010  10   /dev/cf2	
   0000.1.011  11   /dev/cf3	
   0000.1.100  12   /dev/cf4
*/
#define UNIT(dev) ((minor(dev) & 0x08) >> 3)
#define PART(dev) (minor(dev) & 0x07)


typedef unsigned short USHORT;
typedef long ULONG;           /* compiler not supporting unsigned long */
typedef char UCHAR;           /* compiler not supporting unsigned char */

struct identDev {             /* 512-byte block holding identify device info */
  USHORT unused1[23];         /* word 0..22 */
  UCHAR  firmware[8];         /* word 23..26 */
  UCHAR  model[40];           /* word 27..46 */
  USHORT unused2[2];          /* word 47..48 */
  USHORT capab;               /* word 49 */
  USHORT unused3[10];         /* word 50..59 */
  ULONG  lbasecs;             /* word 60, 61 */
  USHORT unused4[194];        /* word 62..255 */
};         

struct ptent {                /* 16-byte mbr partition table entry */
  USHORT  unused5[4];         /* boot indicator, start chs, type, end chs */
  daddr_t bas;                /* lba of first sector in the partition */
  daddr_t siz;                /* number of sectors in partition */
};

struct devinfo {              /* basic info about master and slave */
  int present;                /* boolean: 1 if device is present */
  daddr_t sectors;            /* total number of 512-byte sectors */
};

struct hd {                   /* data to perform a physical I/O operation */
  int unit;                   /* unit number: 0=master, 1=slave */
  int scnt;                   /* number of sectors to read/write */
  int read;                   /* read flag */
  daddr_t snum;               /* start sector number */
  caddr_t addr;               /* lower-order memory transfer address */
  int segm;                   /* segment of memory transfer address  */
};

struct part {
  daddr_t base;               /* starting sector of partition */
  daddr_t size;               /* size of partition in sectors */
};
