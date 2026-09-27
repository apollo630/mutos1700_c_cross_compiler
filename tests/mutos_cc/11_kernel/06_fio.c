#include "param.h"
#include "mount.h"
#include "systm.h"
#include "dir.h"
#include "a.out.h"
#include "user.h"
#include "filsys.h"
#include "var.h"
#include "file.h"
#include "conf.h"
#include "inode.h"
#include "reg.h"
#ifdef SYSACCT
#include "acct.h"
#endif

extern int ROroot;

/*
 * Convert a user supplied
 * file descriptor into a pointer
 * to a file structure.
 * Only task is to check range
 * of the descriptor.
 */

struct file *
getf(f)
register int f;
{
	register struct file *fp;

	if(0 <= f && f < NOFILE) {
		fp = u.u_ofile[f];
		if(fp != NULL)
			return(fp);
	}
	u.u_error = EBADF;
	return(NULL);
}

/*
 * Internal form of close.
 * Decrement reference count on
 * file structure.
 * Also make sure the pipe protocol
 * does not constipate.
 *
 * Decrement reference count on the inode following
 * removal to the referencing file structure.
 * Call device handler on last close.
 */
closef(fp)
register struct file *fp;
{
	register struct inode *ip;
	int flag, mode;
	dev_t dev;
	register int (*cfunc)();
	union {
		struct inode *a;
		struct mount *b;
	}c;

	if(fp == NULL)
		return;
	unlckf(fp->f_inode);
	if (fp->f_count > 1) {
		fp->f_count--;
		return;
	}
	ip = fp->f_inode;
	plock(ip);
	flag = fp->f_flag;
	dev = (dev_t)ip->i_un.i_rdev;
	mode = ip->i_mode&IFMT;

	fp->f_count = 0;
	if(flag & FPIPE) {
	ip->i_mode &= ~(IREAD|IWRITE);
		wakeup((caddr_t)ip+1);
		wakeup((caddr_t)ip+2);
	}

	switch(mode) {

	case IFCHR:
		cfunc = cdevsw[major(dev)].d_close;
		break;

	case IFBLK:
		cfunc = bdevsw[major(dev)].d_close;
		break;
	default:
		iput(ip);
		return;
	}

	for(fp=file;fp < v.ve_file; fp++)
		if(fp->f_count && (dev_t)(c.a = fp->f_inode)->i_un.i_rdev == dev && mode == (c.a->i_mode&IFMT))
		{
			iput(ip);
			return;
		}
	if(mode == IFBLK)
	{
		for(c.b=mount;c.b < v.ve_mount;c.b++)
			if(c.b->m_bufp != NULL && c.b->m_dev == dev)
			{
				iput(ip);
				return;
			}
		bpurge(dev);
	}
	(*cfunc)(dev ,flag);
	iput(ip);
	return;
}

/*
 * openi called to allow handler
 * of special files to initialize and
 * validate before actual IO.
 */
openi(ip, rw)
register struct inode *ip;
{
	dev_t dev;
	register unsigned int maj;

	dev = (dev_t)ip->i_un.i_rdev;
	maj = major(dev);
	switch(ip->i_mode&IFMT) {

	case IFCHR:
	case IFMPC:
		if(maj >= nchrdev)
			goto bad;
		(*cdevsw[maj].d_open)(dev, rw);
		break;

	case IFBLK:
	case IFMPB:
		if(maj >= nblkdev)
			goto bad;
		(*bdevsw[maj].d_open)(dev, rw);
	}
	return;

bad:
	u.u_error = ENXIO;
}

/*
 * Check mode permission on inode pointer.
 * Mode is READ, WRITE or EXEC.
 * In the case of WRITE, the
 * read-only status of the file
 * system is checked.
 * Also in WRITE, prototype text
 * segments cannot be written.
 * The mode is shifted to select
 * the owner/group/other fields.
 * The super user is granted all
 * permissions.
 */
access(ip, mode)
register struct inode *ip;
{
	register m;

	m = mode;
	if(m == IWRITE) {
		if(getfs(ip->i_dev)->s_ronly != 0) {
			if(((ip->i_mode&IFMT) == IFDIR) || ((ip->i_mode&IFMT) == IFREG) || (ROroot == 0))
			{
				u.u_error = EROFS;
				return(1);
			}
		}
		if (ip->i_flag&ITEXT)		/* try to free text */
			xrele(ip);
		if(ip->i_flag & ITEXT) {
			u.u_error = ETXTBSY;
			return(1);
		}
	}
	if(u.u_uid == 0)
		return(0);
	if(u.u_uid != ip->i_uid) {
		m >>= 3;
		if(u.u_gid != ip->i_gid)
			m >>= 3;
	}
	if((ip->i_mode&m) != 0)
		return(0);

	u.u_error = EACCES;
	return(1);
}

/*
 * Look up a pathname and test if
 * the resultant inode is owned by the
 * current user.
 * If not, try for super-user.
 * If permission is granted,
 * return inode pointer.
 */
struct inode *
owner()
{
	register struct inode *ip;

	ip = namei(uchar, 0);
	if(ip == NULL)
		return(NULL);
	if(u.u_uid == ip->i_uid)
		return(ip);
	if(suser())
		return(ip);
	iput(ip);
	return(NULL);
}

/*
 * Test if the current user is the
 * super user.
 */
suser()
{

	if(u.u_uid == 0) {
#ifdef SYSACCT
		u.u_acflag |= ASU;
#endif SYSACCT
		return(1);
	}
	u.u_error = EPERM;
	return(0);
}

/*
 * Allocate a user file descriptor.
 */
ufalloc()
{
	register i;

	for(i=0; i<NOFILE; i++)
		if(u.u_ofile[i] == NULL) {
			u.u_r.r_val1 = i;
			u.u_pofile[i] = 0;
			return(i);
		}
	u.u_error = EMFILE;
	return(-1);
}

/*
 * Allocate a user file descriptor
 * and a file structure.
 * Initialize the descriptor
 * to point at the file structure.
 *
 * no file -- if there are no available
 * 	file structures.
 */
struct file *
falloc()
{
	register struct file *fp;
	register i;

	i = ufalloc();
	if(i < 0)
		return(NULL);
	for(fp = &file[0]; fp < v.ve_file; fp++)
		if(fp->f_count == 0) {
			u.u_ofile[i] = fp;
			fp->f_count++;
			fp->f_un.f_offset = 0;
			return(fp);
		}
	printf("no file\n");
	u.u_error = ENFILE;
	return(NULL);
}
