| ============================================================
| enter_leave_test.s
|
| Speculative-opcode test file for MUTOS mutos_as. Covers TWO
| STATUS.md table rows together (natural stack-frame
| setup/destroy pair, same pairing convention already used for
| INSW/OUTSW and PUSHA/POPA):
|
|   enter framesize,nestlevel   0xC8 iw ib   implemented, no
|                                            real corpus sample
|   leave                       0xC9         implemented, no
|                                            real corpus sample
|
| Neither is in Assembler_as.pdf (K1810WM86-only manual, no
| 80186/V30 coverage) nor exercised anywhere in the real kernel
| corpus (nested Pascal-style stack frames are not something a
| K&R C compiler backend emits) - both are speculative additions.
|
| encode_enter() is a FIXED 4-byte encoding (C8 + iw + ib) no
| matter what nestlevel holds - the interesting runtime behavior
| for nestlevel>0 (copying a chain of outer-frame pointers) is a
| CPU EXECUTION-time semantic, NOT something this assembler
| computes, so there is no special-casing to test there. What
| genuinely varies at the ENCODING level is just: the two
| immediate VALUES, their NOTATION (decimal/hex/marker), and
| whether framesize is a plain constant or a symbol reference
| (own relocation-emission site, independent of every other
| word-immediate site already covered by this project's other
| speculative-opcode test files).
| ============================================================

.globl	_stackspace

.text
.even

_enter_leave_test:
	| ---- nestlevel==0 (no frame-pointer chain) - the simplest,
	| most realistic case if a C-style backend ever emitted this
	| pair as a function prologue/epilogue instead of the classic
	| "push bp / mov bp,sp ... mov sp,bp / pop bp" idiom this
	| project's own crt0/kernel code always uses instead ----
	enter	16.,0.
	leave
	ret

	| ---- nestlevel==1 and nestlevel==3 - exercises the ib field
	| taking small nonzero values; the assembler's job is just to
	| emit the literal byte, so no special dispatch differs, but
	| worth covering explicitly since 0/1/3 are the only three
	| nestlevel values with well-defined runtime semantics anyone
	| would realistically write ----
	enter	32.,1.
	leave
	enter	64.,3.
	leave

	| ---- framesize notation variety: decimal-with-dot, hex via
	| '/', and an explicit '#'-marked word - ENCODE_ENTER accepts
	| ADDR_DIRECT or ADDR_IMMEDIATE for both operands and does NOT
	| branch on the marker at all (always full iw+ib regardless),
	| so these should all produce IDENTICAL bytes to their
	| unmarked decimal equivalent above/below - itself worth
	| confirming, not just assuming ----
	enter	/40,0.			| /40 hex == 64. decimal
	enter	#256.,0.		| explicit '#' word marker

	| ---- a '*' BYTE marker on framesize - semantically odd for a
	| real programmer to write (framesize is virtually always
	| >255 in practice), but exercises whether encode_enter really
	| does ignore the marker entirely and still emit a FULL iw
	| field as the real 80186 ENTER instruction's fixed encoding
	| requires (there is no ib-sized short form of ENTER at all,
	| unlike PUSH) - if this ever emitted only 1 byte instead of 2
	| for the framesize field, that would be a real encoding bug,
	| not a stylistic quirk. ----
	enter	*16.,0.

	| ---- boundary/edge nestlevel values on the ib field: the
	| documented-useful maximum (31, since the runtime pointer-
	| chain copy only makes sense up to the CPU's addressing
	| limits) and a deliberately out-of-spec value (250) purely to
	| confirm the assembler does not clamp/validate nestlevel
	| itself - clamping (if any is even architecturally correct
	| here, which is not itself confirmed) would be a CPU/runtime
	| concern, not an assembler concern. ----
	enter	48.,31.
	enter	48.,250.

	| ---- framesize as a SYMBOL reference - exercises this
	| opcode's OWN relocation-emission site in encode_enter()
	| (classify_word_reloc called from a call site distinct from
	| every other word-immediate site already covered by this
	| project's other speculative-opcode test files - push_imm,
	| shift-count, imul-imm each have their own independent call
	| site and can each independently harbor a bug) ----
	enter	#_stackspace,0.

	leave
	leave
	ret

.data
.even
_stackspace:
.word	512.
