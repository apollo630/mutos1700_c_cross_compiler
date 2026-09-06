| ============================================================
| shift_rotate_imm_count_test.s
|
| Speculative-opcode test file for MUTOS mutos_as. Covers ONE
| STATUS.md table row:
|
|   shift/rotate, immediate count != 1/CL   C0/C1 /digit ib
|   implemented (register + indirect-memory dest) - no real
|   sample (every real sample uses count=1 or CL)
|
| encode_group2()'s three-way dispatch:
|   count via "cl" (any name spelling, matched by REGISTER
|     NUMBER, not text - confirmed real, e.g. mch.s's "shr bx,cx")
|     -> D2/D3, CONFIRMED REAL, not re-tested here.
|   count==1                                -> D0/D1, CONFIRMED
|     REAL (10+ corpus samples), not re-tested here.
|   count==anything else (this file's subject) -> C0/C1 /digit ib,
|     UNCONFIRMED - every real corpus sample happens to be
|     count==1 or count==cl.
|
| /digit assignments (from GROUP2[] in encode.c):
|   rol=0 ror=1 rcl=2 rcr=3 shl/sal=4 shr=5 sar=7
|   (there is no real ISA operation at /digit=6 - matches
|   standard 8086/80186 Group2; not this codebase's omission)
|
| Covers: all seven distinct operations, register AND indirect-
| memory destinations, word AND byte width (word register /
| byte register / explicit -b mnemonic for a byte memory dest),
| several addressing-mode shapes for the memory-destination case
| (mod00/01/10, plus a symbol-forced mod=10), and one deliberately
| out-of-range count to document that the ASSEMBLER does not (and
| per this project's division of concerns should not) perform the
| 80186's runtime "AND 01Fh" count masking itself - that masking
| is a CPU EXECUTION-time behavior, not a static ENCODING rule;
| the assembler's only job is to emit the literal ib byte the
| source line asked for.
| ============================================================

.text
.even

_shift_test:
	| ---- anchor: count==1, CONFIRMED REAL short form (D0/D1) -
	| included purely as a side-by-side contrast so a disassembly
	| makes the D0/D1-vs-C0/C1 boundary visually obvious. NOT
	| itself under test. ----
	sal	bx,*1.			| anchor: D1 /4 (2 bytes)

	| ---- register destination, word size, count in [2..31], one
	| sample per operation, several different registers so the
	| ModRM rm field also varies ----
	rol	ax,*2.
	ror	cx,*3.
	rcl	dx,*4.
	rcr	bx,*5.
	sal	si,*7.
	shr	di,*15.
	sar	bp,*31.			| max useful 5-bit count
	shl	sp,*8.

	| ---- register destination, byte size, byte-ness inferred
	| from the register class itself (no -b suffix needed - same
	| convention as mov/mul/inc/dec elsewhere in this codebase) ----
	rol	al,*2.
	ror	bl,*3.
	shr	cl,*4.			| note: "cl" here is the DESTINATION being
					| shifted, not a shift-count register -
					| unambiguous since the count is the
					| SECOND operand and is a literal here
	sar	dh,*6.

	| ---- indirect-memory destination, mod=00 (no displacement) ----
	shl	(bx),*4.		| word (no -b, no byte reg to infer from
					| defaults to word per the established
					| "byte_mode only from suffix or REG_BYTE
					| dest" rule)
	rolb	(si),*5.		| byte, explicit -b suffix (required -
					| a memory operand has no register class
					| to infer byte-ness from)

	| ---- indirect-memory destination, mod=01 (disp8) ----
	shr	4.(bp),*3.
	sarb	-2.(bx),*6.

	| ---- indirect-memory destination, mod=10 (disp16, value too
	| large for a signed byte) ----
	rcl	1000.(si),*9.
	rcrb	-1000.(di),*12.

	| ---- indirect-memory destination, mod=10 FORCED by a symbol
	| in the displacement (regardless of the symbol's eventual
	| resolved value - same rule already confirmed for other
	| instructions' indirect addressing elsewhere in this file's
	| sibling test files) ----
	shl	_shifttable(bx),*10.
	rorb	_shifttable(si),*4.

	| ---- deliberately out-of-range count (>31): documents that
	| the assembler emits the literal ib byte unmasked. The 80186
	| would mask this to (33 AND 01Fh)=1 at EXECUTION time; the
	| STATIC byte this assembler must emit is the literal value
	| 33 (021h), not the pre-masked runtime-equivalent 1. If this
	| ever disagrees with a real vendor assembler's behavior, that
	| is exactly the kind of thing a golden reference would catch. ----
	sal	ax,*33.

	ret

.data
.even
_shifttable:
.word	0,0,0,0
