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
.globl	_sysent
.data
_sysent:.byte /0
.even
_nosys
.even
.byte /1
.even
_rexit
.even
.byte /0
.even
_fork
.even
.byte /3
.even
_read
.even
.byte /3
.even
_write
.even
.byte /2
.even
_open
.even
.byte /1
.even
_close
.even
.byte /0
.even
_wait
.even
.byte /2
.even
_creat
.even
.byte /2
.even
_link
.even
.byte /1
.even
_unlink
.even
.byte /2
.even
_exec
.even
.byte /1
.even
_chdir
.even
.byte /0
.even
_gtime
.even
.byte /3
.even
_mknod
.even
.byte /2
.even
_chmod
.even
.byte /3
.even
_chown
.even
.byte /1
.even
_sbreak
.even
.byte /2
.even
_stat
.even
.byte /4
.even
_seek
.even
.byte /0
.even
_getpid
.even
.byte /3
.even
_smount
.even
.byte /1
.even
_sumount
.even
.byte /1
.even
_setuid
.even
.byte /0
.even
_getuid
.even
.byte /2
.even
_stime
.even
.byte /4
.even
_ptrace
.even
.byte /1
.even
_alarm
.even
.byte /2
.even
_fstat
.even
.byte /0
.even
_pause
.even
.byte /2
.even
_utime
.even
.byte /2
.even
_stty
.even
.byte /2
.even
_gtty
.even
.byte /2
.even
_saccess
.even
.byte /1
.even
_nice
.even
.byte /1
.even
_ftime
.even
.byte /0
.even
_sync
.even
.byte /2
.even
_kill
.even
.byte /0
.even
_nullsys
.even
.byte /0
.even
_nullsys
.even
.byte /1
.even
_nosys
.even
.byte /2
.even
_dup
.even
.byte /0
.even
_pipe
.even
.byte /1
.even
_times
.even
.byte /4
.even
_profil
.even
.byte /0
.even
_nosys
.even
.byte /1
.even
_setgid
.even
.byte /0
.even
_getgid
.even
.byte /2
.even
_ssig
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /3
.even
_ioctl
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /0
.even
_nosys
.even
.byte /3
.even
_exece
.even
.byte /1
.even
_umask
.even
.byte /1
.even
_chroot
.even
.byte /5
.even
_clocal
.even
.byte /5
.even
_cspec
.even
.globl
.data
.data
