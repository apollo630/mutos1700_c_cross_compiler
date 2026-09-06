| ============================================================
| imul_imm_test.s
|
| Speculative-opcode test file for MUTOS mutos_as. Covers TWO
| STATUS.md table rows together (same mnemonic "imul", same
| underlying encode_imul_imm(), distinguished only by operand
| count - dispatches purely on nops, so the classic single-
| operand 8086 "imul"/"imulb" form, F6/F7 /5, is a completely
| separate, ALREADY-CONFIRMED-REAL code path (encode_group3) and
| is deliberately not touched by this file):
|
|   imul dst,imm       (2-op)   0x69/0x6B /r   implemented, no
|                                              real corpus sample
|   imul dst,src,imm   (3-op)   0x69/0x6B /r   implemented, no
|                                              real corpus sample
|
| Not in Assembler_as.pdf (K1810WM86-only, no 80186/V30 coverage)
| and not exercised anywhere in the real corpus - v30opt.s's 18
| real "imul" uses are all confirmed single-operand F6/F7 forms
| (see encode_group3's doc comment), so this 2/3-operand syntax
| is purely speculative.
|
| Immediate width follows this codebase's documented, MARKER-
| DRIVEN (not magnitude-driven) convention: '*' forces the short
| ib form (0x6B) REGARDLESS of whether the value actually fits in
| a signed byte, exactly like PUSH imm's own marker convention -
| this is DIFFERENT from Group1 arithmetic's automatic sign-
| extension logic (which picks 0x83 based on the value fitting,
| not a marker). One test case below deliberately exploits this
| to document the resulting (arguably surprising) truncation
| behavior explicitly, rather than leaving it as an unverified
| assumption.
| ============================================================

.globl	_mulconst
.globl	_multab

.text
.even

_imul_imm_test:
	| ---- 2-operand form: "imul dst,imm" - dst doubles as both
	| ModRM reg AND r/m (register-register, mod=11) ----
	imul	ax,*10.			| byte imm -> 6B, mod=11 rm=reg=ax(0)
	imul	cx,#1000.		| word imm ('#' forces 69) -> exceeds
					| signed-byte range anyway, so this
					| pairing is also the "natural" choice
	imul	dx,*-1.			| byte imm, negative (sign-extends to
					| -1 at runtime; static byte is FF)
	imul	bx,#-30000.		| word imm, large negative
	imul	si,*0.
	imul	di,#0.
	imul	bp,*127.
	imul	sp,#32767.

	| ---- 3-operand form: "imul dst,src,imm" - src is a SEPARATE
	| register (reg=dst, r/m=src, mod=11) ----
	imul	ax,cx,*4.
	imul	dx,bx,#2000.
	imul	cx,ax,*-8.
	imul	bx,dx,#-1.

	| ---- 3-operand form, src is an INDIRECT memory operand
	| (reg=dst, r/m=mem, mod per the usual displacement rules -
	| same emit_modrm_indirect() addressing-mode axis already
	| exercised in this project's bound_test.s/shift test, so kept
	| brief here to isolate this file's OWN new ground: the
	| opcode-selection and reg/r-m assignment for a 3-operand
	| memory source, not addressing-mode coverage in general) ----
	imul	ax,(bx),*6.
	imul	cx,4.(si),#500.
	imul	dx,_multab(bp),*3.

	| ---- 3-operand form, src is a DIRECT (bare absolute-address)
	| memory operand - a mode BOUND/LDS/LES/LEA in this codebase
	| do NOT support but encode_imul_imm() explicitly does (its
	| own separate rm->mode==ADDR_DIRECT branch, calling
	| emit_modrm_direct rather than emit_modrm_indirect) - worth
	| its own explicit case since it is genuinely new ground, not
	| shared with any already-confirmed instruction family ----
	imul	bx,_mulconst,*7.
	imul	si,_multab,#100.

	| ---- the marker-forces-form-regardless-of-fit case: *200.
	| does NOT fit in a signed byte (max +127), but the '*' marker
	| still forces the ib (0x6B) form per this codebase's
	| documented convention - the assembler must truncate/wrap to
	| a single byte (200 & 0xFF = 0xC8) rather than promote to the
	| word form. This is the single most important case in this
	| file: if a future real-hardware sample ever shows the real
	| vendor assembler instead REJECTS this or auto-promotes to
	| iw, that is exactly the kind of divergence a golden reference
	| would catch that a hand-derived smoke test cannot. ----
	imul	cx,ax,*200.

	| ---- word-immediate SYMBOL reference - exercises this
	| opcode's OWN relocation-emission site (independent of the
	| push_imm/enter/shift-count sites already covered by this
	| project's sibling test files) ----
	imul	di,#_mulconst

	ret

.data
.even
_mulconst:
.word	42.
_multab:
.word	1.,2.,3.,4.
