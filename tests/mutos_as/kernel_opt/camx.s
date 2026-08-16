.globl	_namx
.data
_namx:/1
.globl	_amxleve
.data
.data
_amxleve:/3
.globl	_amxcfg
.data
.data
_amxcfg:.word	/e,/0
.even
.word	/e,/4000
.even
.word	/e,/8000
.even
.word	/e,/c000
.even
.comm	_amxtty,272
.comm	_amxfirm,142
.comm	_amxoff,80
.comm	_amxibuf,24
.comm	_amxobuf,24
.comm	_amxtout,24
.comm	_amxcmd,2
.comm	_amxscd,4
.comm	_amxi_bu,4
.comm	_amxaliv,2
.comm	_amxladd,16
.comm	_amxbufs,1024
.globl	_iobuffe
.data
.data
_iobuffe:_amxbufs
256.+_amxbufs
512.+_amxbufs
768.+_amxbufs
1024.+_amxbufs
1280.+_amxbufs
1536.+_amxbufs
1792.+_amxbufs
2048.+_amxbufs
2304.+_amxbufs
2560.+_amxbufs
2816.+_amxbufs
3072.+_amxbufs
3328.+_amxbufs
3584.+_amxbufs
3840.+_amxbufs
.data
