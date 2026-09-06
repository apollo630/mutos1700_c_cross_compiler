| ============================================================
| push_imm_test.s
|
| Speculative-opcode test file for MUTOS mutos_as. Covers TWO
| STATUS.md table rows together (same family, same mnemonic
| "push", distinguished only by the operand's own size marker):
|
|   push #imm16   0x68 iw   implemented, no real corpus sample
|   push *imm8    0x6A ib   implemented, no real corpus sample
|
| IMPORTANT SCOPE NOTE: every operand below carries an explicit
| '#' or '*' marker so it classifies as ADDR_IMMEDIATE. A BARE,
| unmarked "push <expr>" is a DIFFERENT, ALREADY CONFIRMED REAL
| code path (FF /6, "push the word stored at that address" -
| see encode_pushpop's doc comment and real mch.o's
| "push monssvec+2") and is deliberately NOT exercised here, to
| keep this file's coverage cleanly isolated to the two untested
| opcodes above.
| ============================================================

.globl	_pushimmtest

.data
.even
_localword:
.word	0

.text
.even

_pushimmtest:
	| ---- push *imm8 (0x6A ib, sign-extended byte) ----
	push	*0.			| zero
	push	*1.			| smallest positive
	push	*127.			| largest positive signed byte
	push	*-1.			| all-ones byte (FF) - CPU sign-extends
					| to FFFF at runtime; the STATIC byte
					| the assembler must emit is just FF
	push	*-128.			| smallest (most negative) signed byte
	push	*/7F			| same as *127. via hex literal
	push	*/80			| same bit pattern as *-128. via hex

	| ---- push #imm16 (0x68 iw, full word) ----
	push	#0.
	push	#1.
	push	#32767.
	push	#-1.			| FFFF
	push	#-32768.		| most negative signed word
	push	#/8000			| high bit set, via hex literal
	push	#65535.			| max unsigned word value

	| ---- push #imm16 of a SYMBOL address - exercises this
	| opcode's own relocation-emission site (classify_word_reloc
	| called from encode_pushpop's #imm16 branch), independent of
	| every other word-immediate site already confirmed elsewhere
	| (mov reg,#imm / mov mem,#imm / etc. each call the same
	| classifier but from a different call site, so a bug can be
	| purely local to this one). ----
	push	#_localword		| local TEXT-segment symbol -> R_TEXT
	push	#_externsym		| never defined in this file -> R_EXT

	| ---- restore the stack exactly as deep as it was pushed,
	| so this routine is at least internally consistent if ever
	| linked and run, not just assembled ----
	add	sp,#32.
	ret
