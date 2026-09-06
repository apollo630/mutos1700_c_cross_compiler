| ============================================================
| pusha_popa_test.s
|
| Speculative-opcode test file for MUTOS mutos_as.
| Covers TWO STATUS.md table rows together (same pairing
| convention already used for INSW/OUTSW in
| tests/mutos_as/kernel_opt/mch_insw_outsw.s):
|
|   pusha   0x60   implemented, no real corpus sample
|   popa    0x61   implemented, no real corpus sample
|
| Both are fixed, zero-operand, single-byte opcodes with NO
| addressing-mode/marker/register-field axis to vary - unlike
| almost everything else in this assembler, there is only one
| possible encoding for each ("60" / "61", always). This file's
| coverage is therefore about CONTEXT, not operand shape:
| several independent (label, address) placements, so a
| disassembly/hand-verification pass has more than one location
| to check, and so any (currently believed nonexistent, but
| unproven) location-counter or dispatch-order interaction bug
| would have a chance to surface.
|
| Expected encoding: pusha -> 60 ; popa -> 61, everywhere below.
| ============================================================

.globl	_pptest

.text
.even

| Case 1: PUSHA as the very FIRST statement in the .text segment
| (location counter 0) - an edge case worth its own check, since
| off-by-one/LC-init bugs most often show up at address 0.
_pptest:
pp_case1:
	pusha
	mov	ax,#_pptest
	popa
	ret

| Case 2: classic ISR-style full-register-save/restore prologue
| and epilogue, wrapped around real work - the realistic intended
| use case for these two instructions on 80186-class hardware
| (a full interrupt handler saving/restoring all eight general
| registers in one instruction instead of eight PUSH/POP pairs).
_isr_full_save:
pp_case2:
	push	bp
	mov	bp,sp
	pusha
	cld
	mov	ax,ds
	push	ax
	mov	ax,#_pptest
	mov	ds,ax
	mov	al,*0.
	mov	dx,#/3F8
	out				| implicit DX/AL, matching real mch.s usage
	pop	ax
	mov	ds,ax
	popa
	pop	bp
	iret

| Case 3: PUSHA/POPA back-to-back with nothing in between - the
| minimal-LC-delta degenerate case (2 bytes total between the two
| labels below).
pp_case3a:
	pusha
pp_case3b:
	popa

| Case 4: a SECOND pusha/popa pair later in the same routine,
| after other instructions have already advanced the location
| counter well past a round/aligned address - rules out any
| stale-state bug that only a first-use-in-file case would miss.
_second_use:
	mov	cx,#100.
	mov	si,#_pptest
	mov	di,#_isr_full_save
pp_case4:
	pusha
	rep
	movsb
	popa
	ret
