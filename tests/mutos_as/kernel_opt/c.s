.comm	_io_info,222
.comm	_mpxip,2
.globl	_Mmsg
.data
_Mmsg:.byte	/a,/4d,/55,/54,/4f,/53,/20,/31,/37
.byte	/30,/30,/20,/56,/31
.byte	/2e,/31,/20,/44,/4c,/30,/31,/20,/28
.byte	/47,/72,/75,/6e,/64,/6d
.byte	/6f,/64,/75,/73,/29,/0
.even
.globl	_version
.data
_version:/1101
.comm	_aspalig,2
.globl	_bdevsw
.data
_bdevsw:	_k170ope
	_k170clo
	_k170str
	_k170tab
.even
	_nodev
	_nodev
	_nodev
/0
.even
	_nodev
	_nodev
	_nodev
/0
.even
	_hdopen
	_hdclose
	_hdstrat
	_hdtab
.even
.globl	_cdevsw
.data
_cdevsw:	_k170ope
	_k170clo
	_k170rea
	_k170wri
	_k170ioc
	_nulldev
/0
.even
	_syopen
	_nulldev
	_syread
	_sywrite
	_syioctl
	_nulldev
/0
.even
	_nulldev
	_nulldev
	_mmread
	_mmwrite
	_nodev
	_nulldev
/0
.even
	_consope
	_consclo
	_consrea
	_conswri
	_consioc
	_nulldev
/0
.even
	_ifssope
	_ifssclo
	_ifssrea
	_ifsswri
	_ifssioc
	_nulldev
/0
.even
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
/0
.even
	_v24open
	_v24clos
	_v24read
	_v24writ
	_v24ioct
	_nulldev
/0
.even
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
/0
.even
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
/0
.even
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
/0
.even
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
	_nodev
/0
.even
	_hdopen
	_hdclose
	_hdread
	_hdwrite
	_nodev
	_nulldev
/0
.even
.globl	_nblkdev
.data
_nblkdev:/4
.globl	_nchrdev
.data
_nchrdev:/c
.globl	_rootdev
.data
_rootdev:/301
.globl	_pipedev
.data
_pipedev:/301
.globl	_swapdev
.data
_swapdev:/303
.globl	_swplo
.data
_swplo:	.word	/0,/0
.globl	_nswap
.data
_nswap:/6000
.globl	_dinitsw
.data
_dinitsw:	_k170ini
	_consini
	_ifssini
	_v24init
	_hdinit
/0
.globl	_linesw
.data
_linesw:	_ttyopen
	_nulldev
	_ttread
	_ttwrite
	_nodev
	_ttyinpu
	_nulldev
	_nulldev
	_ttstart
	_nulldev
.even
.globl	_nldisp
.data
_nldisp:/1
.globl	_Timezon
.data
_Timezon:/ffc4
.globl	_Dstflag
.data
_Dstflag:/1
.globl	_Genboot
.data
_Genboot:/0
.globl	_Cmask
.data
_Cmask:/0
.globl	_Stand
.data
_Stand:/0
.globl	_ROroot
.data
_ROroot:/0
.globl	_Bigswap
.data
_Bigswap:/1
.globl	_systype
.data
_systype:/7100
.comm	_buf,520
.comm	_buffers,20480
.comm	_file,1200
.comm	_inode,9120
.comm	_locklis,1400
.comm	_proc,4800
.comm	_text,480
.comm	_swapmap,300
.comm	_callout,150
.comm	_mm_page,1024
.globl	_mm_size
.data
_mm_size:/200
.comm	_cfree,1200
.comm	_mount,48
.globl	_v
.data
_v:/14
/19
/78
	9120.+_inode
/78
	1200.+_file
/8
	48.+_mount
/96
	4800.+_proc
/28
	480.+_text
/96
/1e
/64
.even
.comm	_coremap,300
.data
