/*
* mch.c -- Assemblerteil MUTOS 1700/1834
*
*/

#include "../h/param.h"
#include "../h/intr.h"
#include "../h/proc.h"
#include "../h/a.out.h"
#include "../h/user.h"
#include "../h/text.h"
#include "../h/reg.h"
#include "../h/clock.h"
#include "../h/v30ide.h"

	.globl _u

_u=	OFFUSRPG			| 'U' page
U_STACK=/FFFE				| top of stack

	.text

strt:	jmp	start
	.byte	0


	.globl	_ivecbad
ivectab:

#ifdef	M7100

ivec10:	call	t_trap			|  PIC int 0 comes here.
	.byte	HARDINT
#ifdef	BUS
	.word	_busintr
#else	/* BUS */
	.word	_ndpint
#endif	/* BUS */
	.word	0x8
ivec11: call	t_trap			|  PIC int 1 comes here.
	.byte	HARDINT
	.word	_dbgintr
	.word	0x9
ivec12: call	t_trap			|  PIC int 2 comes here.
	.byte	HARDINT
	.word	_clock
	.word	0xa
ivec13: call	t_trap			|  PIC int 3 comes here.
	.byte	HARDINT
	.word	_novec
	.word	0xb
ivec14: call	t_trap			|  PIC int 4 comes here.
	.byte	HARDINT
#ifdef	LP
	.word	_lpintr
#else	/* LP */
	.word	_novec
#endif	/* LP */
	.word	0xc
ivec15: call	t_trap			|  PIC int 5 comes here.
	.byte	HARDINT
	.word	_k170intr
	.word	0xd
ivec16: call	t_trap			|  PIC int 6 comes here.
	.byte	HARDINT
	.word	_consiintr
	.word	0xe
ivec17: call	t_trap			|  PIC int 7 comes here.
	.byte	HARDINT
	.word	_consointr
	.word	0xf
ivec20: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x58
ivec21: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x59
ivec22: call	t_trap
	.byte	HARDINT
#ifdef	IFSP
	.word	_ifspsintr
#else	/* IFSP */
	.word	_novec
#endif	/* IFSP */
	.word	0x5a
ivec23: call	t_trap
	.byte	HARDINT
#ifdef	IFSP
	.word	_ifspdintr
#else	/* IFSP */
	.word	_novec
#endif	/* IFSP */
	.word	0x5b
ivec24: call	t_trap
	.byte	HARDINT
#ifdef	IFSS
	.word	_ifssintr
#else	/* IFSS */
	.word	_novec
#endif	/* IFSS */
	.word	0x5c
ivec25: call	t_trap
	.byte	HARDINT
#ifdef	IFSS
	.word	_ifssintr
#else	/* IFSS */
	.word	_novec
#endif	/* IFSS */
	.word	0x5d
ivec26: call	t_trap
	.byte	HARDINT
#ifdef	V24
	.word	_v24intr
#else	/* V24 */
	.word	_novec
#endif	/* V24 */
	.word	0x5e
ivec27: call	t_trap
	.byte	HARDINT
#ifdef	V24
	.word	_v24intr
#else	/* V24 */
	.word	_novec
#endif	/* V24 */
	.word	0x5f
#endif	/* M7100 */


#ifdef	M7150

ivec10:	call	t_trap			|  PIC int 0 comes here.
	.byte	HARDINT
#ifdef	BUS
	.word	_busintr
#else	/* BUS */
	.word	_ndpint
#endif	/* BUS */
	.word	0x8
ivec11: call	t_trap			|  PIC int 1 comes here.
	.byte	HARDINT
	.word	_consiintr
	.word	0x9
ivec12: call	t_trap			|  PIC int 2 comes here.
	.byte	HARDINT
	.word	_clock
	.word	0xa
ivec13: call	t_trap			|  PIC int 3 comes here.
	.byte	HARDINT
	.word	_novec
	.word	0xb
ivec14: call	t_trap			|  PIC int 4 comes here.
	.byte	HARDINT
#ifdef	LP
	.word	_lpintr
#else	/* LP */
	.word	_novec
#endif	/* LP */
	.word	0xc
ivec15: call	t_trap			|  PIC int 5 comes here.
	.byte	HARDINT
	.word	_k170intr
	.word	0xd
ivec16: call	t_trap			|  PIC int 6 comes here.
	.byte	HARDINT
	.word	_kgsiintr
	.word	0xe
ivec17: call	t_trap			|  PIC int 7 comes here.
	.byte	HARDINT
	.word	_kgsointr
	.word	0xf
ivec20: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x58
ivec21: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x59
ivec22: call	t_trap
	.byte	HARDINT
#ifdef	IFSP
	.word	_ifspsintr
#else	/* IFSP */
	.word	_novec
#endif	/* IFSP */
	.word	0x5a
ivec23: call	t_trap
	.byte	HARDINT
#ifdef	IFSP
	.word	_ifspdintr
#else	/* IFSP */
	.word	_novec
#endif	/* IFSP */
	.word	0x5b
ivec24: call	t_trap
	.byte	HARDINT
#ifdef	IFSS
	.word	_ifssintr
#else	/* IFSS */
	.word	_novec
#endif	/* IFSS */
	.word	0x5c
ivec25: call	t_trap
	.byte	HARDINT
#ifdef	IFSS
	.word	_ifssintr
#else	/* IFSS */
	.word	_novec
#endif	/* IFSS */
	.word	0x5d
ivec26: call	t_trap
	.byte	HARDINT
#ifdef	V24
	.word	_v24intr
#else	/* V24 */
	.word	_novec
#endif	/* V24 */
	.word	0x5e
ivec27: call	t_trap
	.byte	HARDINT
#ifdef	V24
	.word	_v24intr
#else	/* V24 */
	.word	_novec
#endif	/* V24 */
	.word	0x5f
#endif	/* M7150 */


#ifdef	M1834

ivec10:	call	t_trap			|  PIC int 0 comes here.
	.byte	HARDINT
	.word	_clock
	.word	0x8
ivec11: call	t_trap			|  PIC int 1 comes here.
	.byte	HARDINT
	.word	_consiintr
	.word	0x9
ivec12: call	t_trap			|  PIC int 2 comes here.
	.byte	HARDINT
	.word	_novec
	.word	0xa
ivec13: call	t_trap			|  PIC int 3 comes here.
	.byte	HARDINT
#ifdef	ASK
	.word	_askintr
#else	/* ASK */
	.word	_novec
#endif	/* ASK */
	.word	0xb
ivec14: call	t_trap			|  PIC int 4 comes here.
	.byte	HARDINT
#ifdef	ASK
	.word	_askintr
#else	/* ASK */
	.word	_novec
#endif	/* ASK */
	.word	0xc
ivec15: call	t_trap			|  PIC int 5 comes here.
	.byte	HARDINT
	.word	_xtintr
	.word	0xd
ivec16: call	t_trap			|  PIC int 6 comes here.
	.byte	HARDINT
	.word	_novec
	.word	0xe
ivec17: call	t_trap			|  PIC int 7 comes here.
	.byte	HARDINT
#ifdef	LP
	.word	_lpintr
#else	/* LP */
	.word	_novec
#endif	/* LP */
	.word	0xf
ivec20: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x58
ivec21: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x59
ivec22: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x5a
ivec23: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x5b
ivec24: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x5c
ivec25: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x5d
ivec26: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x5e
ivec27: call	t_trap
	.byte	HARDINT
	.word	_novec
	.word	0x5f
#endif	/* M1834 */


iv0div:	call	t_trap			| #0: zero div
	.byte	ZERODIV
	.word	0
	.word	0
#ifdef	M7100
ivsstep:jmp	dsstep			| #1: single step
#else	/* M7100 */
ivsstep:call	t_trap			| #1: single step
#endif	/* M7100 */
	.byte	TRACE
	.word	0
	.word	1
ivnmi:	call	t_trap			| #2: parity trap
	.byte	PARITRAP
	.word	0
	.word	2
#ifdef	M7100
int3vec:jmp	dint3			| #3: soft break
#else	/* M7100 */
int3vec:call	t_trap			| #3: soft break
#endif	/* M7100 */
	.byte	TRACE
	.word	0
	.word	3
ivovflo:call	t_trap			| #4: overflow int
	.byte	OVFLOW
	.word	0
	.word	4
syscall:call	t_trap			| #0x20: system call
	.byte	SOFTINT
	.word	_scall
	.word	0x20

#ifdef	MSDOS
dosint:	call	t_trap			| #0x21: msdos interrupt
	.byte	SOFTINT
	.word	_dosint
	.word	0x21
#endif	/* MSDOS */

ivecbad:
_ivecbad:call	t_trap			| #6: random interrupt
	.byte	BADTRAP
	.word	0
	.word	-1
nestloc:

start:
	cli				| clear interrupts
	mov	ax, #SEGKD		| Kernels DS value
	mov	ds, ax			| which is just right for DS,
	mov	es, ax			| ES, and
	mov	ss, ax			| SS.
	mov	sp, #MM_BSTACK		| temp boot stack
#ifdef	M7100
	mov	cx,#_etext		| move text
	shr	cx, #1
	mov	ax, #SEGKI
	mov	es, ax
	mov	di, #_etext
	sub	di, #2
	mov	si, #_etext
	sub	si, #2
	mov	bx, cs
	mov	ds, bx
	std
	rep
	movs
	mov	ax, #SEGKD
	mov	ds, ax
#endif	/* M7100 */
	mov	ax, #SEGKI
	mov	cs, ax

	mov	bx, #_etext
	mov	cx, *11
	shr 	bx, cx
	add	bx, #33
	mov	ffreepage, bx

	mov	di, #_edata		| start at end of data
	sub	cx, cx
	sub	cx, di			| (cx) = bytes to zero
	sub	ax, ax			| zap to zeros
	mov	es, ax
	cld				| ES set above = DS.
	rep				| multiple...
	stob				|	smashem (into es:di)

|***	 NDP test

	.globl	_fpp

	.byte /d9
	.byte /e8
	mov	cx,#/1e
	loop	.
	.byte	/df
	.byte	/1e
	.word	_fpp
	mov	cx,#/1e
	loop	.
	cmp	_fpp,*1
	jne	L0005
	.byte	/db
	.byte	/e3
	wait
	.byte	/d9
	.byte	/2e
	.word	Lfcw
L0005:
|***
|	Save interrupt vectors
|***

#ifdef	M7100
	mov	ax, /01*4+2		| save monitors
	mov	monssvec+2, ax		| single-step vector
	mov	ax, /01*4+0		| save monitors
	mov	monssvec+0, ax		| single-step vector
	mov	ax, /03*4+2		| save monitors
	mov	mon3vec+2, ax		| int-3 vector
	mov	ax, /03*4+0		| save monitors
	mov	mon3vec+0, ax		| int-3 vector
#else	/* M7100 */
	mov	ax,0x40			|  INT 10 nach INT FD
	mov	0x3f4,ax		|      Bildschirm
	mov	ax,0x42
	mov	0x3f6,ax

	mov	ax,0x24			|  INT 9 nach INT FE
	mov	0x3f8,ax		|      Tastatur
	mov	ax,0x26
	mov	0x3fa,ax
#ifdef	M7150
	mov	ax,0x28			|  INT A nach INT FF
	mov	0x3fc,ax		|      Uhr
	mov	ax,0x2a
	mov	0x3fe,ax

	mov	ax,0x20			|  INT 8 nach INT FC
	mov	0x3f0,ax		|      BIOS-Uhr/NDP
	mov	ax,0x22
	mov	0x3f2,ax
#endif	/* M7150 */
#ifdef	M1834
	mov	ax,0x20			|  INT 8 nach INT FF
	mov	0x3fc,ax		|      Uhr
	mov	ax,0x22
	mov	0x3fe,ax

	mov	ax,0x34			|  INT D nach INT FC
	mov	0x3f0,ax		|      harddisk
	mov	ax,0x36
	mov	0x3f2,ax
#endif	/* M1834 */
#endif	/* M7100 */
	mov	ax,cs
	mov	bp,#sstack
	mov	bx,es
	mov	cx,#0x100
L0006:	mov	dx,*0(bp)		| modifizieren Interrupttab.
	cmp	dx,*0			|  0 in Modifizierungstabelle
	je	L0007			|  laesst die Modifizierung 
					|  dieses Interrupts aus    
	mov	(bx),dx
	mov	*2(bx),ax
L0007:	add	bx,*4
	add	bp,*2
	loop	L0006

#ifdef	M7100
	test	_dbreak, #/FFFF		| kernel debug ?
	bne	dbr1
	seg	cs
	movb	al, ivnmi
	seg	cs
	movb	ivsstep, al
	seg	cs
	movb	int3vec, al
	seg	cs
	mov	ivsstep+1, #t_trap - ivsstep - 3
	seg	cs
	mov	int3vec+1, #t_trap - int3vec -3
dbr1:
#endif	/* M7100 */
#ifdef	M1834
	mov	0x1e * 4 + 2, #0	| flp_base in data !

/*
	mov	ax, #/80
	mov	dx, #/321
	out				| reset HDC
*/
#endif	/* M1834 */

|***
|	Set stack to top of u_ page
|***
	mov	sp, #U_STACK
|***
|	Call main
|***
	mov	bx, ffreepage		| store 1st free page
	push	bx			| arg is 1st free page
	call	_main			| complete the initialization
|***
|	When we return here we want to pass control to a user program
|***


	cli				| no interrupts until start user process.
	seg	cs
	movb	state, #PS_USER		| set (soft) user mode

| no static data references until iret


	mov	ds, ax			| DS,
	mov	es, ax			|    ES, and
	mov	ss, ax			|       SS.
	mov	sp, #-4			| setup user stack
	mov	ax, #IBIT		| initial flags...
	push	ax			|	has interupts enabled

	push	ds

	sub	ax, ax			| set up...
	push	ax			|	initial ip = 0
	iret				| return to user state at loc 0

	.data

|***
|	Masks per interrupt-level
|***

#ifdef	M7100
intmask:	.byte	/FF,/FF,/FF,/08,/10,/F8,/C0,/C0		| PIC1
		.byte	/FF,/FF,/08,/08,/08,/08,/08,/08		| PIC2(ASP)

	.globl	_dbreak
_dbreak:.word	0
monssvec:	.=.+4
mon3vec:	.=.+4
#endif	/* M7100 */

#ifdef	M7150
intmask:	.byte	/FF,/C2,/FF,/08,/12,/FC,/C2,/C2		| PIC1
		.byte	/FF,/FF,/08,/08,/08,/08,/08,/08		| PIC2(ASP)
#endif	/* M7150 */

#ifdef	M1834
intmask:	.byte	/FB,/02,/FB,/18,/18,/FB,/FB,/80		| PIC1
		.byte	/FF,/FF,/FF,/FF,/FF,/FF,/FF,/FF		| PIC2
#endif	/* M1834 */

	.globl  _Idlef, _clkflg, _ffreepage
_Idlef:		.word	0
_clkflg:
clkflg: 	.word	1		| ==0 if not to initialize
_ffreepage:
ffreepage:	.word	0
Lfcw:		.word	0xfbf
sstack:		.word	iv0div		| 0
		.word	ivsstep		| 1
		.word	ivnmi		| 2
		.word	int3vec		| 3
		.word	ivovflo		| 4
#ifdef	M7100
		.word	ivecbad		| 5
#else	/* M7100 */
		.word	0		| 5 print screen
#endif	/* M7100 */
		.word	ivecbad
		.word	ivecbad
		.word	ivec10		| 8
		.word	ivec11		| 9
		.word	ivec12
		.word	ivec13
		.word	ivec14
		.word	ivec15
#ifdef	M1834
		.word	0
#else	/* M1834 */
		.word	ivec16
#endif	/* M1834 */
		.word	ivec17
#ifndef	M7100
		.word	video		| 10
		.word	0
		.word	0
		.word	0
		.word	ivecbad
		.word	0
		.word	0
		.word	0
		.word	ivecbad
		.word	0
		.word	0
		.word	rti		| 1b
		.word	rti		| 1c
		.word	0
#ifdef	M1834
		.word	_flp	| 1e
#else	/* M1834 */
		.word	0	| 1e
#endif	/* M1834 */
		.word	0
#else	/* M7100 */
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad
#endif	/* M7100 */
		.word	syscall	| 20
#ifdef	MSDOS
		.word	dosint	| 21
#else	/* MSDOS */
		.word	ivecbad	| 21
#endif	/* MSDOS */
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 22
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 30
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad
#ifndef	M7100
		.word	0,0	| 40
#else	/* M7100 */
		.word	ivecbad,ivecbad	| 40
#endif	/* M7100 */
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 42
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 50
#ifdef	M1834
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad
#else	/* M1834 */
		.word	ivec20		| 58
		.word	ivec21		| 59
		.word	ivec22		| 5a
		.word	ivec23		| 5b
		.word	ivec24		| 5c
		.word	ivec25		| 5d
		.word	ivec26		| 5e
		.word	ivec27		| 5f
#endif	/* M1834 */
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 60
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 70
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 80
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | 90
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | a0
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | b0
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | c0
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | d0
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad | e0
		.word	ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad,ivecbad

		.word	ivecbad		| f0
		.word	ivecbad		| f1
		.word	ivecbad		| f2
		.word	ivecbad		| f3
L0F4:		.word	ivecbad		| f4
		.word	ivecbad		| f5
		.word	ivecbad		| f6
		.word	ivecbad		| f7
		.word	ivecbad
		.word	ivecbad
		.word	ivecbad
		.word	ivecbad
#ifndef	M7100
		.word	0		| fc
		.word	0		| fd
		.word	0		| fe
		.word	0		| ff
#else	/* M7100 */
		.word	ivecbad,ivecbad,ivecbad,ivecbad
#endif	/* M7100 */

	.text

| NOTE: lives in CS!

state:		.byte	PS_SYS

#ifdef	M1834

.data
	.globl _flp
_flp:	.byte	0xef,0x02,0x32,0x02,0x09,0x2a
	.byte	0xff,0x50,0xf6,0x12,0x07,0x4f
	.byte	0x80,0x03,0x01,0x48,0x50,0x00
	.byte	0x00,0x00,0x00
.text
#endif	/* M1834 */



#ifndef	M7100

video:	push	di
	push	si
	mov	si,ss
	mov	di,cs
	add	di,#0			|   #0  wird evtl. modifiziert 
	mov	ss,di
	seg	cs
	testb	state, #PS_USER
	je	Lvid00
	mov	di,sp
	mov	sp,#U_STACK
	seg	cs
	andb	state, #PS_SYS
	int	#0xfd			|  alter BIOS-INT 10 
	seg	cs
	andb	state, #PS_USER
	mov	ss,si
	mov	sp,di
	pop	si
	pop	di
	iret
Lvid00:	mov	ss,si
	pop	si
	pop	di
	int	#0xfd		|  alter BIOS-INT 10 

rti:	iret


.globl	_prt_intr
_prt_intr:
	push	bp
	mov	bp, sp
	int	#0x05		|  make print screen interrupt
	pop	bp
	ret



.globl	_disp1

_disp1:	push	bp
	mov	bp, sp
	movb	ah, *1
	movb	ch, *4(bp)
	movb	cl, *6(bp)
	j	disp00

.globl	_disp2

_disp2:	push	bp
	mov	bp,sp
	movb	ah,*2
	movb	bh,*4(bp)
	movb	dh,*6(bp)
	movb	dl,*8(bp)
disp00:
	int	#0xfd
	pop	bp
	ret

	
.globl	_disp5

_disp5:	push	bp
	mov	bp,sp
	movb	ah,*5
	movb	al,*4(bp)
	j	disp00


.globl	_disp7

_disp7:	push	bp
	mov	bp,sp
	movb	ah,*7
disp01:	movb	al,*4(bp)
	movb	bh,*6(bp)
	mov	cx,*8(bp)
	mov	dx,*10(bp)
	j	disp00


.globl	_disp9

_disp9:	push	bp
	mov	bp,sp
	movb	ah,*9
	movb	al,*4(bp)
	movb	bh,*6(bp)
	movb	bl,*8(bp)
	mov	cx,*10(bp)
	j	disp00
	

.globl	_disp14

_disp14:	push	bp
	mov	bp,sp
	movb	ah,*0xe
	movb	al,*4(bp)
	movb	bh,*6(bp)
	subb	bl,bl
	j	disp00
	

.globl	_cn_scroll
	
_cn_scroll:
	push	bp
	mov	bp, sp
	push	di
	push	si
	push	ds
	push	es
	cld
	mov	ax, *4(bp)		| segment
	mov	si, *6(bp)		| from
	mov	di, *8(bp)		| to
	mov	cx, *10(bp)		| count
	mov	es, ax
	mov	ds, ax
	test	*12(bp), #/FFFF		| direction
	beq	scr1
	std
scr1:	rep	
	movs
	pop	es
	pop	ds
	cld
	jmp	cret

	.globl	_cn_clr

_cn_clr:
	push	bp
	mov	bp, sp
	push	di
	push	si
	push	es
	mov	ax, *4(bp)		| segment
	mov	di, *6(bp)		| offset
	mov	cx, *8(bp)		| count
	mov	es, ax
	mov	ax, #/720		| attr./space
	cld
	rep
	stow
	pop	es
	jmp	cret

	.globl	_disp3

_disp3:	push	bp
	mov	bp,sp
	movb	bh, *4(bp)
	movb	ah,*0x3
	int	#0xfd
	pop	bp
	mov	ax, dx
	ret

.globl	_disp15

_disp15: push	bp
	mov	bp,sp
	movb	ah,*0xf
	int	#0xfd
	pop	bp
	movb	cl, *4
	shlb	bh, cl
	orb	ax, bh
	ret

	.globl	_exchscrn, _cnv_page, _vbwsaddr

_exchscrn:
	push	bp
	mov	bp, sp
	push	di
	push	si
	push	es
	push	ds
	xor	si, si
	mov	di, si
	cld
	mov	cx, #2048
	mov	ax, *4(bp)
	mov	es, ax
	mov	dx, #/B000
	mov	ds, dx
	rep
	movs
	mov	bx, *6(bp)
	mov	ds, bx
	mov	es, dx
	xor	si, si
	mov	di, si
	mov	cx, #2048
	rep
	movs
	pop	ds
	pop	es
	jmp	cret

#ifdef	M1834

.globl	_flpio

_flpio:	push	bp
	mov	bp,sp
	movb	ah,*4(bp)
	movb	al,*6(bp)
	movb	ch,*8(bp)
	movb	cl,*10(bp)
	movb	dh,*12(bp)
	movb	dl,*14(bp)
	mov	bx,*16(bp)
	mov	es,*18(bp)
	int	#0x13
	movb	al,ah			|  Status 
	pop	bp
	ret
	

/*
.globl	_getds

_getds:	mov	ax,ds
	ret
*/
#endif	/* M1834 */

#else	/* M7100 */

dsstep:	push	monssvec+2
	push	monssvec
	reti

dint3:	push	mon3vec+2
	push	mon3vec
	reti


|***
|	monitor -- trap to monitor
|***
	.globl	_monitor
_monitor:
	pop	ax
	pushf
	push	cs
	push	ax
	jmp	dint3

|
| co, ci routines, to use firmware monitor.
|
| Does NOT save/restore register var's.
|
co:
	pop	dx			| convert to long return.
	push	cs
	push	dx
	.byte	*/ea,*/20,*/00,*/fb,*/ff	| (long)jmp	fffb:0020
	|No Return

ci:
	pop	dx			| convert to long return
	push	cs
	push	dx
	.byte	*/ea,*/28,*/00,*/fb,*/ff	| (long)jmp	fffb:0028


|	co, ci -- C-Callable.
|
| Just C-callable interfaces to the monitor -- no line-edit, and ci doesn't
| echo.
|

	.globl  _co
_co:
	push	bp
	mov     bp,sp
	push	di
	push	si
	push	*4.(bp)		| push argument
	call	co		| call co(c)
	jmp	cret

	.globl	_ci
_ci:
	push	bp
	mov	bp,sp
	push	di
	push	si
	call	ci		| al = input char.
	jmp	cret

#endif	/* M7100 */

	.globl	_haltcpu

_haltcpu:
	cli
#ifdef	M7100
	mov	cx,mon3vec+2
	mov	/03*4+2,cx
	mov	cx,mon3vec+0
	mov	/03*4+0,cx
	mov	cx,monssvec+2
	mov	/01*4+2,cx
	mov	cx,monssvec+0
	mov	/01*4+0,cx
	sti				| allow interrupts
	mov	ax,_brkseg
	or	ax,_brkoff
	jnz	xbreak

#ifdef  V30IDE
| Switch CPU back to slow clock speed before reboot or exit to monitor.
| Switching of CPU clock is done via unused bit 4 of PPI 8255 port A:
| Bit 4 = 0 --> fast clock speed
| Bit 4 = 1 --> slow clock speed
  push  dx
  mov   dx, #PPI_PA         | PPI 8255 port A
  in                        | port A is input by default; read its value into al
  or    al, *NECSLOW        | set bit 4
  movb  ah, al              | save value in ah
  mov   dx, #PPI_CTL        | PPI 8255 control register 
  movb  al, *0x84
  out                       | switch port A to output, see p.83 of K2771 documentation
  mov   dx, #PPI_PA         | PPI 8255 port A
  movb  al, ah              | restore saved value in al
  out                       | switch CPU to slow clock speed
  mov   dx, #PPI_CTL        | PPI 8255 control register 
  movb  al, *CTL_DEF        | PPI 8255 control word default value
  out                       | switch port A to default (=input) again  
  pop   dx
#endif  V30IDE

| process _haltcpu parameter
| MONITOR  -> regular system shutdown and exit to monitor
| REBOOT   -> reboot system
| POWEROFF -> poweroff system into standby for remote poweron

  mov   bx, sp              | bx is frame pointer
  mov   ax, #2(bx)          | ax = halt action
  cmp   ax, #MONITOR
  je    exitmon
  cmp   ax, #REBOOT
  je    reboot
  cmp   ax, #POWEROFF
  je    pwroff
  j     exitmon             | default is exit to monitor

reboot:
  cli
  mov   dx, #PPI_CTL        | PPI 8255 control register 
  movb  al, *0x84
  out                       | switch port A to output, see p.83 of K2771 documentation
  mov   al, *0
  or    al, *NECSLOW        | set bit 4  
  or    al, *MCHRESET       | set bit 5
  mov   dx, #PPI_PA         | PPI 8255 port A
  out                       | switch CPU to slow clock speed and perform reset
  .byte /f4                 | hlt 

pwroff:
  cli                       | disable maskable interrupts
  mov   dx, #0x00cc         | PPI 8255 port C
  movb  al, *0x20           | set Bit 5: SET-DC-OFF
  out                       | DC power off
  .byte /f4                 | hlt 

exitmon:

	call	_monitor
	j	.
xbreak:	pushf				|flags
	push	_brkseg			|segment
	push	_brkoff			|offset
	jmp	dint3
#else	/* M7100 */
#ifdef	M7150
	mov	ax,0x3fc		|  INT FF nach INT A
	mov	0x28,ax			|      Uhr
	mov	ax,0x3fe
	mov	0x2a,ax

	mov	ax,0x3f0		|  INT FC nach INT 8
	mov	0x20,ax			|      BIOS-Uhr/NDP
	mov	ax,0x3f2
	mov	0x22,ax	
#else	/* M7150 */

	mov	ax, 0x3f4		| restore INT8
	mov	0x40, ax		|    Uhr
	mov	ax, 0x3f6
	mov	0x42, ax

	mov	ax,0x3f0		| restore INTD
	mov	0x34,ax			|    Harddisk
	mov	ax,0x3f2
	mov	0x36,ax
#endif	/* M7150 */

haltcpu:
	mov	ax,0x3f8		|  INT FE nach INT 9
	mov	0x24,ax			|      Tastatur
	mov	ax,0x3fa
	mov	0x26,ax
	movb	al, #/FC
	out	P1_MASK
	sti				| allow interrupts
#ifdef	KDEBUG
haltc1:	mov	ax,_brkseg
	or	ax,_brkoff
	jnz	haltm
	call	_montestc
	cmpb	al,#0x1b
	jnz	haltc1
haltm:	call	_monitor
#endif	/* KDEBUG */
	j	haltcpu
#ifdef	KDEBUG
	.globl	_montestc
_montestc:
	push	bp
	mov	bp,sp
	push	si
	push	di
	movb	ah,#1
	int	#0x16
	mov	ax,#0
	jz	monnoc
	int	#0x16
monnoc:	and	ax,#0xff
	pop	di
	pop	si
	pop	bp
	ret

	.globl	_monitor
_monitor:
	pushf
	cli
	mov	ax,0x3f8		|  INT FE nach INT 9
	mov	0x24,ax			|      Tastatur
	mov	ax,0x3fa
	mov	0x26,ax
	popf
	call	_kdebug
	pushf
	cli
	mov	0x24,cs
	mov	bx,sstack+18
	mov	0x26,bx
	popf
	ret

	.globl	_mon_go, _mon_jp
_mon_go:
_mon_jp:pop	ax		| return address
	pop	di		| segment
	pop	si		| offset
	push	si
	push	di
	push	ax
	push	cs		| far return expected
	push	di		| segment
	push	si		| offset
	reti
#else	/* KDEBUG */
	.globl	_monitor
_monitor:
	ret
#endif	/* KDEBUG */

#endif	/* M7100 */
       .globl   _spl0,_spl2,_spl3,_spl5,_spl6,_spl7,_splx
	.globl  _tasktime

_tasktime:                              | same as spl0 for here
_spl0:					| Everything ON
	movb	ah, #SPL0MASK		| PIC mask for all enabled
	j	spl			| finish.
#ifdef	M7100
_spl2:					| Disable 6-7
	movb	ah, #/C0		| PIC mask for 6-7 disabled
	j	spl			| finish
_spl3:					| Disable only  3, 6, 7
	movb	ah, #/C8		| PIC mask for 3, 6, 7 disabled
	j	spl			| finish.
_spl5:					| Disable 3-7
	movb	ah, #/F8		| PIC mask for 3-7 disabled
	j	spl			| finish.
_spl6:					| Disable 2-7
	movb	ah, #/FC		| PIC mask for 2-7 disabled
	j	spl			| finish.
_spl7:					| ALL OFF!
	movb	ah, #/FD		| Disable 0,2-7 leave monitor break on
#endif	/* M7100 */

#ifdef	M7150
_spl2:					| Disable 1,6,7
	movb	ah, #/C2		| PIC mask for 1,6,7 disabled
	j	spl			| finish
_spl3:					| Disable only  1,3,6,7 
	movb	ah, #/CA		| PIC mask for 1,3,6,7 disabled
	j	spl			| finish.
_spl5:					| Disable 1,3-7
	movb	ah, #/FA		| PIC mask for 1,3-7 disabled
	j	spl			| finish.
_spl6:					| Disable 1-7
	movb	ah, #/FE		| PIC mask for 1-7 disabled
	j	spl			| finish.
_spl7:					| ALL OFF!
	movb	ah, #/FF		| Disable all
#endif	/* M7150 */

#ifdef	M1834
_spl2:					| Disable 1
	movb	ah, #/02		| PIC mask for 1 disabled
	j	spl			| finish
_spl3:					| Disable  1, 3, 4
	movb	ah, #/1A		| PIC mask for 1, 3, 4 disabled
	j	spl			| finish.
_spl5:					| Disable 1, 3-7
	movb	ah, #/FA		| PIC mask for 1, 3-7 disabled
	j	spl			| finish.
_spl6:					| Disable 0, 1, 3-7
	movb	ah, #/FB		| PIC mask for 0, 1, 3-7 disabled
	j	spl			| finish.
_spl7:					| Disable 0, 1, 3-7
	movb	ah, #/FB		| PIC mask for 0, 1, 3-7 disabled
#endif	/* M1834 */

spl:					| Common finish.
	cli				| zap ints while doing this
	in	P1_MASK			| get old value
	xchgb	ah, al			| swap old/new mask
	out	P1_MASK			| output new mask
	sti
	ret				| ah = old mask, al = new mask
_splx:					| Restore old "state"
	cli				| turn off
	mov	bx, sp			| frame pointer
	movb	al, #3(bx)		| al = old mask
	out	P1_MASK			| set in PIC
	sti				| turn on obvious
	ret				|


.globl	_clkintr
_clkintr:
#ifdef	M1834
	push	bp
	mov	bp, sp
	int	#0xff			|  alter BIOS-INT8  
	pop	bp
#endif	/* M1834 */
	ret


	
t_trap:	nop
	push	bp
	push	es
	push	dx
	mov	es,ax			|ES saves old AX for now
	mov	dx,#0			|initialize compressed vector
vloop:
	mov	bp,sp
	mov	ax,#6(bp)		|vector info
	xchg	ax,bp
	seg	cs
	orb	dh,#0(bp)		|or in status byte
	seg	cs
	testb	#0(bp),#HARDINT+SOFTINT	|must remember vector ?
	xchg	ax,bp			|ax = vector info
	jz	istrap			|don't load vector
	sub	ax,#ivectab		|make offset
	shr	ax,#1
	shr	ax,#1
	shr	ax,#1			|ax = # of table entry
	movb	dl,al
istrap:	cmp	#10(bp),#SEGKI		|previous mode = user mode ?
	jne	vlexit			|end of loop
	mov	ax,#8(bp)		|saved ip
	cmp	ax,#nestloc		|nested traps ?
	ja	vlexit
	test	ax,#3
	jnz	vlexit			|not really nested
	or	ax,#3
	mov	#12(bp),ax		|new vector info
	mov	ax,#4(bp)		|saved bp
	mov	#10(bp),ax		|overwrites flags
	mov	ax,#2(bp)		|saved es
	mov	#8(bp),ax		|overwrites cs
	mov	ax,#0(bp)		|saved dx
	mov	#6(bp),ax		|overwrites ip
	lea	sp,#6(bp)		|clean up stack
	j	vloop
vlexit:	cmp	#10(bp),#SEGKI		|previous mode = system mode ?
	je	sysmode
	seg	cs
	cmpb	state,#PS_SYS
	jne	nosys
sysmode:orb	dh,#SYSMODE		|set system mode bit in vector
nosys:	mov	#6(bp),dx		|replace old vector by compressed one
	mov	ax,ss			|get stack segment
	mov	bp, sp			| save user SP
	testb	dh,#SYSMODE		| must switch stacks ?
	jnz	sysstk			|no
	mov	sp, #SEGKD		| kernel DS
	mov	ss, sp			| set system segment
	mov	sp, #U_STACK		| get real kernel SP
sysstk:	push	bp			| push user SP
	push	ax			| push user SS
	push	dx			| duplicate vector indicator address
					| on the system per-user stack
|***
|	we are now safe in our system stack.  Push all the registers
|	and call _trap
|***
	push	ds
	push	di
	push	si
	push	cx
	push	bx
	push	es			|old ax
|***
|	setup segment registers.
|***
	mov	ax, ss			| SS already has kernel DS value
	mov	ds, ax			| DS, and
	mov	es, ax			|    ES want it too.
	mov	bp, sp			| want our stack (did enuf work for it!)
|***
|	It is now safe to address system data.
|	Push old "state" and set new state.
|***
	cli				| shut off interrupt
	in	P1_MASK			| interrupted mask
	movb	ah, al			| in high-byte of pushed state
	seg	cs
	movb	al,state		|soft state in low byte
	push	ax			| save previous state
	seg	cs
	movb	state,#PS_SYS		|set system mode
	in	P1_ISR			|al = ISR
	mov	ax, #12(bp)		| vector info
	and	ax,#/ff			| interrupt vector
	mov	bx,ax
	shl	bx,#1
	shl	bx,#1
	shl	bx,#1
	seg	cs
	mov	si,#ivectab+4(bx)	| address of int routine
	testb	#13(bp),#HARDINT	|high byte
	jz	xtraps

/*		now the stack looks like that:
 *
 *	user / system stack:		system stack:
 *	(depending on
 *	previous mode)
 *				20	|---------------|
 *			     -----------|    user SP	|
 *			    |	18	|---------------|	16(bp)
 *			    |		|    user SS	|
 *			    |	16	|---------------|	14(bp)
 *			    |		| flags | intno |
 *	|---------------|   |	14	|---------------|	12(bp)
 *	|     flags	|   |		|      ds	|
 *	|---------------|   |	12	|---------------|	10(bp)
 *	|      cs	|   |		|      di	|
 *	|---------------|   |	10	|---------------|	8(bp)
 *	|      ip	|   |		|      si	|
 *	|---------------|   |	8	|---------------|	6(bp)
 *	|  vector info	|   |		|      cx	|
 *	|---------------|   |	6	|---------------|	4(bp)
 *	|      bp	|   |		|      bx	|
 *	|---------------|   |	4	|---------------|	2(bp)
 *	|      es	|   |		|      ax	|
 *	|---------------|   |	2	|---------------|<---current bp
 *	|      dx	|   |		|old spl| state |
 *	|---------------|<--	0	|---------------|<---current SP
 */

|***
|	Handle I/O interrupt
|***
|	cmp	ax, #8			|still contains # of table entry
|	jb	doioin1
|	mov	ax, #3
doioin1:mov	bx, ax			|
	movb	bl,#intmask(bx)		| new PIC mask for this interrupt
	in	P1_MASK			| old PIC mask
	or	al, bl			| set additional bits
	out	P1_MASK			| mask the new level and current levels
	xchgb	ah,al			| save old mask for return
	movb	al, #PIC_EOI		| now do EOI
	out	P1_CMD1			| mask doesn't allow more at this level
	sti				| safe for these again.
	push	si			| 1st. arg is isr address
	call	_ioint			| do an I/O interrupt.
	pop	cx			| clean up stack
	j	xtraps
xtraps:	sti
	testb	#13(bp),#ZERODIV+TRACE+PARITRAP+OVFLOW+BADTRAP
	jz	xsoft		|no traps pending
	call	_trap
xsoft:	testb	#13(bp),#SOFTINT
	jz	nosint
	call	@si			| execute any kind of soft int
nosint:
|***
|	return from trap.  Registers are as described above
|***
	cli				| off interrupts, in case back on again
	pop	ax			| pop "soft" state
	seg	cs
	movb	state,al		|restore it
	xchgb	al, ah			| old PIC mask
	out	P1_MASK		| restore
|***
|	Must restore registers and return to user map.  Have to pop most
|	registers from kernel stack, then switch to user stack.
|***
	pop	ax			| get reg's off system stack
	pop	bx
	pop	cx
	pop	si
	pop	di
	pop	ds
	inc	sp			| skip vector info
	inc	sp
	pop	dx			| old SS
	pop	sp			| old SP
	mov	ss, dx			| establish old SS.  Now on user stack.

|	Whew!
|***
|	Stack is clean except for pushed dx,es,bp,vector.  Am in system/user mode,
|	depending on how caller wants to return.  Nofault comes here too.
|***

	pop     dx                      | interrupted guy wants...
	pop     es                      |       these as...
	pop     bp                      |               they were.
	inc	sp			| skip...
	inc	sp			|	vector info.
	iret				| return to previous state and mode

|***
|***	routine to wait for interupt
|***
	.globl	_idle, _waitloc
_idle:
	pushf                           | save i flag
	cli				| Int's OFF while fussing mask
	in	P1_MASK		| save callers PIC-mask,
	push	ax			| and do
	movb	al, #/0		| spl0()
	out	P1_MASK		|
	incb    _Idlef                  | set idling flag
	sti
|       hlt                             | wait for the interrupt
	.byte   /f4
_waitloc:				| so "they" know we were idle
	cli				| int's OFF while fussing mask
	pop     ax
	out     P1_MASK                | restore callers PIC-mask
	movb    _Idlef,*0
	popf                            | PIC Mask stable now
	ret

|***					***
|***	routines to switch context	***
|***					***

	.globl	_save, _resume
|***
|	save(u.u_?sav)	- save registers in u structure
|***
_save:
	cli
	mov	ax, ds			|
	mov	es, ax			| es = ds
	pop	ax			| (ax) = return address
	mov	bx, sp			|
	mov	dx, di			| (dx) = saved di
	mov	di, #0(bx)		| (es:di) = target address
	cld				|
	stow				| store return address
	mov	bx, ax			| (bx) = retadr
	mov	ax, sp			|
	stow				| store stack
	mov	ax, bp			|
	stow				| store bp
	mov	ax, si			|
	stow				| store si
	xchg	ax, dx			|
	stow				| store di
	xchg	ax, di			|
	sub	ax, ax			| indicate old process
	sti
	jmp	@bx			|

|***
|	resume(proc.p_addr, u.u_qsav) - resume a process
|
|	NOTE: This routine does an spl0() upon return
|***
_resume:
	cli
	cld
	mov	bx,_u+U_PROCP
	mov	ax,#P_ADDR(bx)
	cmpb	#P_PSTAT(bx), #SZOMB
	je	resum1
	testb	#P_PSTAT+1(bx), #SLOAD
	je	resum1
	mov	cx, #7
	shl	ax, cl
	mov	cx, #MMPGSZ/2
	mov	es, ax
	sub	si, si
	mov	di, si
	mov	ax, #/f80
	mov	ds, ax
	rep
	movs
resum1:
	pop	bx			| discard return address
	pop	ax			| (ax) = new proc address
	pop	bx			| save si
	mov	cx, #7
	shl	ax, cl
	mov	cx, #MMPGSZ/2
	sub	si, si
	mov	di, si
	mov	dx, #/f80
	mov	es, dx
	mov	ds, ax
	rep
	movs

	mov	si, bx
	mov	bx, #SEGKD
	mov	ds, bx
	lodw				|
	mov	bx, ax			| (bx) = new return address
	lodw				|
	mov	sp, ax			| restore stack pointer
	lodw				|
	mov	bp, ax			| restore bp
	lodw				|
	mov	dx, ax			| restore si into dx
	lodw				|
	mov	di, ax			| restore di
	mov	si, dx			| restore si
	mov	ax, ss
	mov	es, ax
	mov	ds, ax
	movb	al, #SPL0MASK		| want spl0()
	out	P1_MASK		| all levels enabled
|	sti				| set interrupts; stack is valid again
	inc	ax			| indicate transfer
	sti
	jmp	@bx			| return

	.globl	_savfp, _restfp
_savfp:
	test	_fpp, #/FFFF
	jz	savf1
	push	bp
	mov	bp, sp
	mov	bx, 4(bp)		| addr of u.u_fps
	pushf
	cli
	.byte	/DD
	.byte	/37
	wait
	popf
	pop	bp
savf1:
	ret

_restfp:
	test	_fpp, #/FFFF
	jz	restf1
	push	bp
	mov	bp, sp
	mov	bx, 4(bp)		| addr of u.u_fps
	pushf
	cli
	.byte	/DD
	.byte	/27
	wait
	popf
	pop	bp
restf1:
	ret


	.globl	_clkstart
_clkstart:
	nop				| replace by RET to not use clock.
	mov	cx, clkflg		| See what to do.
	|cmp	cx, #2			| turn it off?
	|jz	clk2			| yup.
	jcxz	clk1			| already initialized
	dec	clkflg			|
	pushf				| save interrupt flag
	cli				| no interrupts while programming
	movb    al, #PIT_S0+PIT_READ_LOAD+PIT_SQWAVE_MODE
	out	PIT_CTRL_PORT		| control byte
	movb	al, _clknumb+0		| least significant byte
	out	PIT_CTR0_PORT		| goes out first
	movb	al, _clknumb+1		| most significant byte
	out	PIT_CTR0_PORT		| goes out next
	popf				| restore interrupt flag
clk1:	ret				| exit

	.globl	_fubyte,_fuibyte,_subyte,_suibyte
	.globl	_fuword,_fuiword,_suword,_suiword
_fuibyte:
	mov	bx, _u+U_PROCP
	mov	bx, #P_TEXTP(bx)
	test	bx, bx
	jz	_fubyte
	mov	ax, #X_CADDR(bx)
	j	fubyt
_fubyte:				|
	mov	bx, _u+U_PROCP		| u.u_procp
	mov	ax, #P_ADDR(bx)
	add	ax, #USIZE
fubyt:
	mov	cx, #7
	shl	ax, cl
	pushf				| save interrupt state
	cli				| no interrupts while fussing map
	mov	es, ax			| set up to address
	mov	bx, sp
	mov 	bx, #4(bx)
	seg	es			|
	movb	al, #0(bx)		| do the fetch
	subb	ah, ah			|
	j	uxit			| break fetchahead
|***
|	the various routines share this code
|
|	((sp)) = saved flags
|***
uxit:
	popf				| restore interrupt state
	mov	bx, ds			| set up es...
	mov	es, bx			|	just to be safe
	ret

_fuiword:
	mov	bx, _u+U_PROCP
	mov	bx, #P_TEXTP(bx)
	test	bx, bx
	jz	_fuword
	mov	ax, #X_CADDR(bx)
	j	fuwrd			|
_fuword:				|
	mov	bx, _u+U_PROCP		| u.u_procp
	mov	ax, #P_ADDR(bx)
	add	ax, #USIZE
fuwrd:					|
	mov	cx, #7
	shl	ax, cl

	pushf				| save interrupt state
	cli				| no interrupts while fussing map
	mov	es, ax			| set up to address
	mov	bx, sp			| get stack "pointer"
	mov	bx, #4(bx)		| address (4 due to flags)
	seg	es			| fetch from this segment
	mov	ax, #0(bx)		| do the fetch
	j	uxit			| we all share an exit

_suibyte:
	mov	bx, _u+U_PROCP
	mov	bx, #P_TEXTP(bx)
	test	bx, bx
	jz	_subyte
	mov	ax, #X_CADDR(bx)
	j	subyt			|
_subyte:				|
	mov	bx, _u+U_PROCP		| u.u_procp
	mov	ax, #P_ADDR(bx)
	add	ax, #USIZE
subyt:					|
	mov	cx, #7
	shl	ax, cl

	pushf				| save interrupt state
	cli				| no interrupts while fussing map
	mov	es, ax			| set up to address
	mov	bx, sp			| stack "pointer"
	mov	ax, #6(bx)		| (al) = value (6 due to flags)
	mov	bx, #4(bx)		| address (offset += 2, due to flags)
	seg	es			|
	movb	#0(bx), al		| do the store
	subb	ah, ah			| insure >= 0.
	jmp	uxit			| we all share an exit

_suiword:
	mov	bx, _u+U_PROCP
	mov	bx, #P_TEXTP(bx)
	test	bx, bx
	jz	_suword
	mov	ax, #X_CADDR(bx)
	j	suwrd			|
_suword:				|
	mov	bx, _u+U_PROCP		| u.u_procp
	mov	ax, #P_ADDR(bx)
	add	ax, #USIZE
suwrd:
	mov	cx, #7
	shl	ax, cl
	pushf				| save interrupt state
	cli				| no interrupts while fussing map
	mov	es, ax			| set up to address
	mov	bx, sp			| get stack "pointer"
	mov	ax, #6(bx)		| (ax) = value
	mov	bx, #4(bx)		| address (4 due to flags)
	seg	es			|
	mov	#0(bx), ax		| do the store
	jmp	uxit			| we all share an exit






	.globl  _copy
_copy:					|copy(srcaddr, dstaddr, nbytes)
	mov     bx,sp			|all in kernel's address space
	push    si
	push    di
	cld
	mov     si,*2(bx)
	mov     di,*4(bx)
	mov     cx,*6(bx)
	mov     ax,si
	or      ax,di
	or      ax,cx
	shr     ax,*1
	jb      _copy1          | do it byte at a time
	shr     cx,*1
	rep
	movs
	pop     di
	pop     si
	ret                     | exit

_copy1: rep
	movsb                   | move bytewise
	pop     di
	pop     si
	ret


#ifdef	RAM
	.globl _copymem
_copymem:		|copymem(srcseg, srcaddr, dstseg, dstaddr, nbytes)
	mov	bx, sp
	push	di
	push	si
	push	ds
	push	es
	mov	cx, #10(bx)
	mov	si, #4(bx)
	mov	di, #8(bx)
	mov	es, #6(bx)
	mov	ax, #2(bx)	
	mov	ds, ax
	mov     ax,si
	or      ax,di
	or      ax,cx
	shr     ax,*1
	jb      cmem1
	shr     cx,*1
	rep
	movs
	jmp	cpret
cmem1:	rep
	movsb                   | move bytewise
	jmp	cpret

#endif	/* RAM */


	.globl _copyblocks
_copyblocks:
	mov	bx, sp			| (bx) is our 'frame pointer'
	push	di			|
	push	si			| preserve regs
	push	ds			|
	push	es			|
	sub	si, si			| from addr = (fseg:0000)
	mov	di, si			| to addr =   (tseg:0000)
	cld				|
	mov	ax, #4(bx)		| "to" page
	mov	cx, *6
	shl	ax, cl			| page number to base address
	push	ax        		| in DS
	mov	ax, #2(bx)		| "from" page
	mov	cx, *6
	shl	ax, cl			| page number to base address
	push	ax			| in ES
	mov	ax, #6(bx)
cprw:
	mov	cx, #BSHIFT-1
	shl	ax, cl			| (cx) = word count
	mov	cx, ax
|	No data references beyond this point (ds trashed)

	pop	ds
	pop	es
	rep				|
	movs				| move by words
cpret:
	pop	es			|
	pop	ds			|

|	data references now safe.

	pop	si			|
	pop	di			|
	ret				| exit


	.globl _getbyte
	.globl _getword
_getbyte:					|int getbyte(segment, addr)
	mov	bx,sp
	push	si
	mov	dx,#2(bx)
	mov	si,#4(bx)
	mov	es,dx
	seg	es
	mov	ax,(si)
	mov	cx,ds
	mov	es,cx
	pop	si
	movb	ah,#0
	ret

_getword:					|int getword(segment, addr)
	mov	bx,sp
	push	si
	mov	dx,#2(bx)
	mov	si,#4(bx)
	mov	es,dx
	seg	es
	mov	ax,(si)
	mov	cx,ds
	mov	es,cx
	pop	si
	ret

	.globl _setbyte
_setbyte:					|setbyte(segment, addr, byte)
	mov	bx,sp
	push	si
	mov	dx,#2(bx)
	mov	si,#4(bx)
	mov	cx,#6(bx)
	mov	es,dx
	seg	es
	movb	(si),cl
	mov	cx,ds
	mov	es,cx
	pop	si
	ret

	.globl _setword
_setword:					|setword(segment, addr, word)
	mov	bx,sp
	push	si
	mov	dx,#2(bx)
	mov	si,#4(bx)
	mov	cx,#6(bx)
	mov	es,dx
	seg	es
	mov	(si),cx
	mov	cx,ds
	mov	es,cx
	pop	si
	ret

       .globl _clearseg
_clearseg:				|clearseg(page)
	mov	bx, sp			| bx is stack marker
	push	di			| save registers
	mov	ax, #2(bx)		| (ax) = page number
	mov	cx, *7			|
	shl	ax, cl			| page number to base address
	mov	es, ax			| store into es:di
	mov	cx, #MMPGSZ/2		| (cx) = count
	sub	ax, ax			| want zero
	mov	di, ax			| also, offset = 0
	cld				| store "forward"
	rep				| multiple times...
	stow				| store a word of zeros
	mov     di,ds
	mov     es,di                   | es = ds
	pop	di			| restore register(s) and
	ret				| return.

	.globl	_zeromem
_zeromem:				|zeromem(addr, nbytes)
					| in kernel's address space
	mov	bx, sp			| bx is stack marker
	push    di                      | save
	mov	di, #2(bx)		| (di) = destination
	mov	ax, ds			|
	mov	es, ax			| (es:di)
	mov     cx, #4(bx)              | (cx) = count
	subb	al, al			| store zeroes
	rep                             |
	stob                            | repeat zap
	pop     di                      |
	ret				|

	.globl _copyin,_copyiin,_copyout,_copyiout
	.globl  _copysin,_copysout
|       .globl  _copylin,_copylout


_copyin:				| copy from user data to kernal data
	mov	bx, _u+U_PROCP		| u.u_procp
	mov	ax, #P_ADDR(bx)
	add	ax, #USIZE
	j	copyi1			|
_copyiin:				| copy from user ispace to kernal data
	mov	bx, _u+U_PROCP
	mov	bx, #P_TEXTP(bx)
	mov	ax, #X_CADDR(bx)
copyi1:				|
	mov	cx, #7
	shl	ax, cl
	mov	bx, sp			| (bx) = frame pointer
	push	si			|
	push	di			| preserve regs
	mov	si, #2(bx)		| (si) = source (user)
	mov	di, #4(bx)		| (di) = dest	(sys)
	mov	cx, #6(bx)		| cx = length
	push	ds			| push destination seg = kernal data
	push	ax
	cli				| no interrupts while fussing map
	j	copy			| do the work
_copysin:
	mov	bx,sp
	push	si
	push	di
	mov	ax,#2(bx)		| source segment
	mov	si,#4(bx)		| source offset
	mov	di,#6(bx)		| dest. offset
	mov	cx,#8(bx)		| length
	push	ds
	push	ax
	j	copy
|***
|	Copy-out routines.
|***
_copyout:				| copy from kernal data to user data
	mov	bx, _u+U_PROCP		| u.u_procp
	mov	ax, #P_ADDR(bx)
	add	ax, #USIZE
	j	copyo1
_copyiout:
	mov	bx, _u+U_PROCP
	mov	bx, #P_TEXTP(bx)
	mov	ax, #X_CADDR(bx)
copyo1:					|
	mov	cx, #7
	shl	ax, cl

	mov	bx, sp			| (bx) = frame pointer
	push	si			|
	push	di			| preserve regs
	mov	si, #2(bx)		| (si) = source (system)
	mov	di, #4(bx)		| (di) = dest	(user)
	mov	cx, #6(bx)		| cx = length
	push	ax
	push	ds			| source seg = kernal data
	j	copy			| do the work
_copysout:
	mov	bx,sp
	push	si
	push	di
	mov	si,#2(bx)		| source offset
	mov	ax,#4(bx)		| dest. segment
	mov	di,#6(bx)		| dest. offset
	mov	cx,#8(bx)		| length
	push	ax
	push	ds
	j	copy



copy:	pop	ds			|
	pop	es			| load source and dest segments
| NO STATIC REFERENCES UNTIL DS IS RESTORED
	mov	ax, cx			| (ax) = (cx) = count
	or	ax, si			|
	or	ax, di			|
	rcr	ax, #1			| see if any are odd
	jb      copyf                   | error!
	shr	cx, #1			| (cx) = word count
	jcxz	copy1			| if move count == 0
	cld				|
	rep				|
	movs				| move the word
copy1:	sub	ax, ax			| no mmu fault

|
|	Note that DS and ES are garbage, but 'pops' are safe because
|	SS is still ok.

copyf:
	pop	di			|
	pop	si			| restore regs
	mov	bx, ss			|
	mov	ds, bx			| restore segment registers
	mov	es, bx			|
	ret				|

	.globl _in, _inb,_out,_outb

_in:
	mov	bx, sp			| bx is frame pointer
	mov	dx, #2(bx)		| dx = port
	inw				| input a word
	ret				|

_inb:
	mov	bx, sp			| bx is frame pointer
	mov	dx, #2(bx)		| dx = port
	in				| input a byte
	subb	ah, ah			|
	ret				|

_out:					| don't use stack, out may change mapping
	mov	bx, sp			| bx is frame pointer
	mov	dx, #2(bx)		| dx = port
	mov	ax, #4(bx)		| ax = value to output
	outw				| output a word
	ret				|

_outb:
	mov	bx, sp			| bx is frame pointer
	mov	dx, #2(bx)		| dx = port
	mov	ax, #4(bx)		| ax = byte to output
	out				| output a byte
	ret				|

#ifndef M1834
#ifdef  V30IDE
  .globl _isNEC, _hdio

| From: Processor Identification by Chris Dragan & Chili
| http://mattst88.com/programming/AssemblyProgrammersJournal/issue/6/
| NEC processors differ from Intel's with respect to the handling of the zero
| flag during a MUL operation. While a NEC V20/V30 does not clear ZF after a
| non-zero multiplication result, but only according to it, an Intel 8086/88 
| will always clear it
_isNEC:
  xor   al, al                          | force ZF to set
  movb  al, */40
  mul   al
  jz    isV20_30                        | check if ZF is clear
  mov   ax, #0                          | is an Intel 808x
  ret
isV20_30:
  mov   ax, #1                          | is a NEC V20 or V30
  ret

| read/write sectors after the RD_SECT/WR_SECT command has been issued
| blocking polling version w/o interrupts
| returns 0 on success or ERR1/ERR2 otherwise
| in case of ERR2 the low-byte contains ERR2
| and the high-byte the content of the status register
| hdio(hd.segm, hd.addr, hd.scnt, hd.read)
_hdio: 
        mov     bx, sp                  | bx is frame pointer
        push    si
        push    di
        push    es

| process parameters
        mov     es, #2(bx)              | es = memory segment
        mov     di, #4(bx)              | di = memory address used for read
        mov     si, di                  | si = memory address used for write
        mov     ax, #6(bx)              | sector count (8-bit value)
        movb    cl, al
        mov     ax, #8(bx)              | read flag (one-bit value)
        movb    ch, al
        mov     bx, cx                  | bh = read flag, bl = sector count 

| read status register and wait until BSY is cleared and DRQ is set
| we consider having DRQ not set as an error, DF and ERR flags not relevant
        mov     dx, #STAT               | use status register as port address
        cld                             | direction for inm/outm: address increment
sectLoop:
        mov     cx, #TIMEOUT
wReady:                                 | wait until BSY is cleared
        in                              | read status ('in' == 'in al, dx')
        test    al, *BSY                | BSY cleared?
        jne     wRetry                  | no -> try again
        test    al, *DRQ                | DRQ set?
        jne     dataReady               | yes -> let's go on
        j       ioERR2                  | no -> other error
wRetry:
        nop                             | add a little extra delay since 16-bit
        nop                             | timeout is too small for slow CF-cards
        loop    wReady 
        mov     ax, #ERR1               | timeout error
        j       ioDone

dataReady:
        mov     dx, #DATA               | use data register as port address
        mov     cx, #256                | transfer count = 1 sector (256 words)
        test    bh, *1                  | read operation?
        jne     readOp                  | yes
                                        | write operation
        rep                             | repeat cx times
        seg es                          | segment override: es:si
        .byte /6f                       | NEC V20/V30 outm - output multiple
        j       decSecCnt
readOp:                                 | read operation
        rep                             | repeat cx times
        .byte /6d                       | NEC V20/V30 inm - input multiple
       
| decrement sector count and check if more sectors are to be processed
decSecCnt:
        mov     dx, #STAT               | use status register as port address
        dec     bl
        test    bl, bl                  | sector count zero?
        jne     sectLoop                | no -> next sector

| in case of a write operation, wait until last sector has been written
        test    bh, *1                  | read operation?
        jne     finChk                  | yes -> no wait needed
        mov     cx, #TIMEOUT
wLast:                                  | in case of a write operation
        in                              | read status
        test    al, *BSY                | BSY cleared?
        je      finChk                  | yes -> do final check
        nop                             | add a little extra delay since 16-bit
        nop                             | timeout is too small for slow CF-cards
        loop    wLast                   | try again otherwise 
        mov     ax, #ERR1               | timeout error
        j       ioDone

| perform a final check for errors before returning
finChk:
        mov     bl, *DF                 | build error mask
        or      bl, *ERR                | all bits have to be zero
        or      bl, *DRQ
        or      bl, *BSY
        in                              | read status
        test    al, bl                  | all test bits zero?
        jne     ioERR2                  | no -> error
        
        mov     ax, #0                  | otherwise: no errors
        j       ioDone

ioERR2:
        movb    ah, al                  | move status register to upper 8 bit
        movb    al, *ERR2               | and mark as "other" error

ioDone:
        pop     es 
        pop     di
        pop     si
        ret
#endif  V30IDE
#endif  M1834

	.globl _memchk
_memchk:
	mov	bx,sp
	mov	dx,#2(bx)
	mov	cx,*7
	shl	dx,cl
	mov	es,dx
	seg	es
	mov	cx,0
	mov	ax,#/4242
	seg	es
	mov	0,ax
	seg	es
	mov	bx,0
	cmp	ax,bx
	jnz	memfault
	mov	ax,#/5244
	seg	es
	mov	0,ax
	seg	es
	mov	bx,0
	cmp	ax,bx
	jnz	memfault
	sub	ax,ax
	j	memret
memfault:
	mov	ax,#-1
memret:
	seg	es
	mov	0,cx
	mov	cx, ds
	mov	es, cx
	ret

|***
|	cret -- exit from C-funciton.
|***
	|.comm	__stkmax, 2
	.globl	cret
cret:
|	cmp	sp,__stkmax
|	jae	foo
|	mov	__stkmax,sp
|foo:
	lea	sp,#-4(bp)
	pop	si
	pop	di
	pop	bp
	ret

	.globl _icode,_szicode
.data
.even
_icode:	mov	ax,#/3b
	mov	bx,#init-_icode
	mov	cx,#argv-_icode
	mov	si,#envp-_icode
	mov	dx,#/8800
	int	#/20
loop:	j	.
argv:	.word /16
envp:	.word 0
init:	.asciz */etc/init* 

_szicode: . - _icode
	.globl _icodech
.text
.even
_icodech:
	mov	init+/8,#/72
	ret

.end	strt
