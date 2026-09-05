#include "../h/param.h"
#include "../h/systm.h"
#include "../h/dir.h"
#include "../h/var.h"
#include "../h/filsys.h"
#include "../h/a.out.h"
#include "../h/user.h"
#include "../h/mount.h"
#include "../h/map.h"
#include "../h/proc.h"
#include "../h/inode.h"
#include "../h/conf.h"
#include "../h/buf.h"

/*
 * Initialization code.
 * Called from cold start routine as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *	clear and free user core
 *	turn on clock
 *	hand craft 0th process
 *	call all initialization routines
 *	fork - process 0 to schedule
 *	     - process 1 execute bootstrap
 *
 * loop at low address in user mode -- /etc/init
 *	cannot be executed.
 */

#define DXMF 44

extern int Cmask, Stand, ROroot;
extern char buffers[][BSIZE+BSLOP];
extern char Mmsg[];
extern daddr_t getlong();
int fpp = 0;

main(p)
int p;
{

	if(Stand == 0)
		printf("\nDevice initialization:\n\n");
	if(fpp)
		printf("NDP      found\n");
	initpic();
	startup(p);
	/*
	 * set up system process
	 */
	generic(p);
	if(Stand == 0)
		printf("%s mem = %Dk\n",Mmsg,ptob((long)maxmem)/0x400L);
	mfree(swapmap, nswap, 1);
	swplo--;
#ifdef MMU
	mmucraf(1);
#endif MMU
	proc[0].p_stat = SRUN;
	proc[0].p_flag |= SLOAD|SSYS;
	proc[0].p_nice = NZERO;
	proc[0].p_addr = malloc(coremap,USIZE);
	proc[0].p_size = USIZE;
	proc[0].p_dusize = USIZE;
	u.u_procp = &proc[0];
	u.u_cmask = Cmask;
	Nproca = &proc[Nproc = 2];

	/*
	 * Initialize devices and
	 * set up 'known' i-nodes
	 */

	cinit();
	binit();
	spl7();
	initpic();
	clkstar();
	tasktime(0);
	iinit();
	(*bdevsw[major(swapdev)].d_open)(swapdev,1);
	rootdir = iget(rootdev, (ino_t)ROOTINO);
	rootdir->i_flag &= ~ILOCK;
	u.u_cdir = iget(rootdev, (ino_t)ROOTINO);
	u.u_cdir->i_flag &= ~ILOCK;
	u.u_rdir = NULL;

	update();

	/*
	 * make init process
	 * enter scheduling loop
	 * with system process
	 */
	if(newproc()) {
		expand((int)btop(szicode) + USIZE);
		u.u_tsize = 0;
		u.u_dsize = btop(szicode);
		u.u_ssize = USIZE;
#ifdef MMU
		mmupini();
		mmumap();
		mmuset(RO);
#endif MMU
		copyout((caddr_t)icode, (caddr_t)0, szicode << 1);
		/*
		 * Return goes to loc. 0 of user init
		 * code just copied out.
		 */
		return(ptosr(proc[1].p_addr + USIZE));
	}
	sched();
}

/*
 * iinit is called once (from main)
 * very early in initialization.
 * It reads the root's super block
 * and initializes the current date
 * from the last modified date.
 *
 * panic: iinit -- cannot read the super
 * block. Usually because of an IO error.
 */

time_t bootime;

iinit()
{
	register struct buf *cp, *bp;
	register struct filsys *fp;

FLY1:
	u.u_error = 0;
	(*bdevsw[major(rootdev)].d_open)(rootdev, 1);
	bp = bread(rootdev, SUPERB);
	cp = geteblk();
	if(u.u_error)
	{
		if(Stand)
		{
			rootdev = pipedev = makedev(0,DXMF + 1);
			goto FLY1;
		}
		else
			panic("iinit");
	}
	bcopy(bp->b_un.b_addr, cp->b_un.b_addr, sizeof(struct filsys));
	brelse(bp);
	mount[0].m_bufp = cp;
	mount[0].m_dev = rootdev;
	fp = cp->b_un.b_filsys;
	fp->s_flock = 0;
	fp->s_ilock = 0;
	fp->s_ronly = ROroot;
	if(fp->s_clean != S_CLEAN) {
		icodech();
		fp->s_clean = S_DIRTY;
	} else
		fp->s_clean = 0;
	fp->s_fmod++;
	bootime = time = fp->s_time;
}


/*
 * Initialize the buffer I/O system by freeing
 * all buffers and setting all device buffer lists to empty.
 */
binit()
{
	register struct buf *bp;
	register struct buf *dp;
	register int i;
	struct bdevsw *bdp;

	bfreelist.b_forw = bfreelist.b_back =
	    bfreelist.av_forw = bfreelist.av_back = &bfreelist;
	for (i=0; i<v.v_buf; i++) {
		bp = &buf[i];
		bp->b_dev = NODEV;
		bp->b_un.b_addr = &buffers[i][BSLOP];
		bp->b_back = &bfreelist;
		bp->b_forw = bfreelist.b_forw;
		bfreelist.b_forw->b_back = bp;
		bfreelist.b_forw = bp;
		bp->b_flags = B_BUSY;
		brelse(bp);
	}
	for (bdp = bdevsw; bdp < &bdevsw[nblkdev]; bdp++) {
		dp = bdp->d_tab;
		if(dp) {
			dp->b_forw = dp;
			dp->b_back = dp;
		}
	}
}

copyseg(from, to)
int from, to;
{
	copyblocks(ptod(from), ptod(to), MMPGSZ/BSIZE);
}

