| ============================================================
| bound_test.s
|
| Speculative-opcode test file for MUTOS mutos_as. Covers ONE
| STATUS.md table row:
|
|   bound reg,mem   0x62 /r   implemented (indirect-memory
|                             operand only) - no real corpus
|                             sample
|
| encode_ldx() reuse: BOUND shares its EXACT ModRM shape with
| this codebase's already-confirmed-real LEA/LDS/LES family -
| dst=REGISTER (the index being range-checked), src=ADDR_INDIRECT
| ONLY (this codebase's established restriction for this whole
| instruction family - no DIRECT/absolute-address support, same
| as LEA/LDS/LES). Not in Assembler_as.pdf (K1810WM86-only, no
| 80186/V30 coverage) and not exercised anywhere in the real
| corpus, not even v30ide.s.
|
| Runtime semantics (INT 5 if the index in `reg` falls outside
| the signed-word pair [mem], [mem+2]) are irrelevant to the
| static byte encoding under test here - only the ModRM/reg-field/
| addressing-mode axis matters, exactly as for LEA/LDS/LES.
| ============================================================

.globl	_boundtab

.text
.even

_bound_test:
	| ---- reg field coverage: one sample per general-purpose
	| register as the index operand, same base indirect operand
	| (bx) each time so only the ModRM reg field (bits 3-5)
	| varies ----
	bound	ax,(bx)
	bound	cx,(bx)
	bound	dx,(bx)
	bound	bx,(bx)
	bound	sp,(bx)
	bound	bp,(bx)
	bound	si,(bx)
	bound	di,(bx)

	| ---- addressing-mode coverage on the mem operand, reg fixed
	| to ax so only the rm/mod bits and displacement field vary ----
	bound	ax,(bx)			| mod=00, no displacement
	bound	ax,(bp)			| mod=00 rm=110 special case - "(bp)"
					| with NO displacement still forces
					| mod=01 disp8=0 (rm==6 quirk, same
					| as every other indirect operand in
					| this codebase)
	bound	ax,4.(si)		| mod=01, small positive disp8
	bound	ax,-4.(di)		| mod=01, small negative disp8
	bound	ax,127.(bx)		| mod=01, largest disp8 that still fits
	bound	ax,128.(bx)		| mod=10 forced - one past the signed-
					| byte boundary, pure-constant path
	bound	ax,1000.(bp)		| mod=10, disp16, pure-constant path
	bound	ax,-1000.(si)		| mod=10, negative disp16

	| ---- mod=10 FORCED by a symbol in the displacement,
	| regardless of the symbol's eventual resolved value (same
	| rule already confirmed for every other indirect-addressing
	| instruction in this codebase - LEA/LDS/LES/group1/group2/
	| mov/etc. all share this rule; BOUND reuses the identical
	| emit_modrm_indirect() code path) ----
	bound	cx,_boundtab(bx)
	bound	dx,4.+_boundtab(si)

	ret

.data
.even
_boundtab:
.word	0,100.				| lower bound, upper bound
