.comm	_mpxip,2
.comm	_canonb,256
.comm	_rootdir,2
.comm	_runq,2
.comm	_Hogproc,2
.comm	_inode86,2
.comm	_dbreak,2
.comm	_brkseg,2
.comm	_brkoff,2
.comm	_cputype,2
.comm	_lbolt,2
.comm	_time,4
.comm	_nblkdev,2
.comm	_nchrdev,2
.comm	_mpid,2
.comm	_runin,2
.comm	_runout,2
.comm	_runrun,2
.comm	_runtxt,2
.comm	_curpri,2
.comm	_maxmem,2
.comm	_lks,2
.comm	_swplo,4
.comm	_nswap,2
.comm	_updlock,2
.comm	_rablock,4
.comm	_msgbuf,1024
.comm	_rootdev,2
.comm	_swapdev,2
.comm	_pipedev,2
.comm	_acctp,2
.comm	_Nproca,2
.comm	_Nproc,2
.comm	_Timezon,2
.comm	_Dstflag,2
.comm	_dk_busy,2
.comm	_dk_time,128
.comm	_dk_numb,12
.comm	_dk_wds,12
.comm	_tk_nin,4
.comm	_tk_nout,4
.comm	_version,2
.comm	_systype,2
.comm	_io_info,222
.globl	___Rombi
.data
___Rombi:.byte /0
.blkb	4095.
.globl	___sysop
.data
___sysop:/0
.globl	___CCBof
.data
___CCBof:/0
.globl	___CCBba
.data
___CCBba:/0
.globl	___empty
.data
___empty:.byte /0
.blkb	7.
.globl	_pvers
.data
_pvers:	_version
.globl	_symt
.data
_symt:	_k170cfg
	_inode
	_mount
	_proc
	_swapdev
	_swplo
	_v
	_text
	_file
	_constty
	_ifsstty
	_v24tty
	_nifss
	_nv24
	_dk_busy
	_io_info
	_rootdev
	_pipedev
	_nswap
	_Genboot
/0
/0
	_ROroot
	_Bigswap
/0
/0
/0
	_systype
	_version
.even
.data
