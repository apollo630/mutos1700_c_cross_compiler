| ============================================================
| insb_outsb_test.s
|
| Speculative-opcode test file for MUTOS mutos_as. Covers TWO
| STATUS.md table rows together (same natural I/O pair, same
| pairing convention already used for INSW/OUTSW in
| tests/mutos_as/kernel_opt/mch_insw_outsw.s):
|
|   insb    0x6C   implemented, no real corpus sample
|   outsb   0x6E   implemented, no real corpus sample
|
| Fixed, zero-operand, single-byte opcodes - same situation as
| PUSHA/POPA (see pusha_popa_test.s). Modeled directly on the
| REAL, hardware-linked idiom found in kernel_opt/mch.s around
| its readOp:/writeOp: block (the exact source lines that
| motivated adding INSW/OUTSW to this assembler in the first
| place, before its own .byte /6d / .byte /6f workaround was
| replaced by the real mnemonics):
|
|   rep
|   seg es
|   .byte /6f      | NEC V20/V30 outm - output multiple
|   ...
|   rep
|   .byte /6d      | NEC V20/V30 inm - input multiple
|
| NOTE the asymmetry this idiom already documents and which this
| file preserves for the byte forms too: OUTS's SOURCE (DS:SI)
| can take a segment override ("seg es"), INS's DESTINATION
| (ES:DI) cannot (there is no override for it - ES is fixed by
| the CPU for INS), so only the outsb case gets a "seg es" line.
|
| Additionally covers ONE CONTEXT THE CONFIRMED-REAL INSW/OUTSW
| SAMPLE DOES NOT: a bare, single-transfer INSB/OUTSB with no
| "rep" prefix at all. The existing confirmed evidence only ever
| exercises these opcodes AS THE REP-PREFIXED FORM - the
| opcode byte itself is identical either way (REP is a wholly
| separate, independently-emitted prefix byte), but a fully
| independent placement costs nothing and removes one more
| "well, technically only ever seen with rep" caveat.
| ============================================================

.text
.even

_insb_outsb_test:
	mov	dx,#/3F8		| port address, DX-indirect port form
	mov	cx,#512.		| block transfer count

writeOp:				| block write, mirrors real mch.s
	rep
	seg	es
	outsb				| NEC V20/V30 outm (byte) - output multiple
	j	decCnt
readOp:					| block read, mirrors real mch.s
	rep
	insb				| NEC V20/V30 inm (byte) - input multiple
decCnt:
	dec	cx
	jne	readOp

	| bare, non-REP-prefixed single-transfer forms - not covered
	| by the rep-prefixed cases above, nor by the confirmed-real
	| INSW/OUTSW sample (which is always REP-prefixed).
bare_forms:
	insb
	outsb
	ret
