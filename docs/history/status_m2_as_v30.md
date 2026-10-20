# `STATUS.md` history — the 80186/V30 speculative-opcode test files

Section moved here from `STATUS.md` on 2026-10-20, unchanged (written in the session that added `tests/mutos_as/v30_speculative/`; "this session" means that session). The current state is in [`STATUS.md`](../../STATUS.md).

---

## 80186/V30 speculative-opcode test files (this session)

Added `tests/mutos_as/v30_speculative/` — seven hand-written test files (one per
natural instruction family, same pairing convention as `mch_insw_outsw.s`) covering
all twelve "no real corpus sample" rows in the table above, plus `nasm_crosscheck.asm`
and a `README.md` documenting the full methodology, exact commands, and results.

Verified this session:
- All seven files assemble cleanly with the current `mutos_as` (zero errors) and
  remain clean under `-fsanitize=address,undefined -O0 -g`.
- The pre-existing 67/67 golden regression is unaffected (new files only, no `src/`
  changes).
- Every instruction in every file was independently decoded with `objdump -D -b
  binary -m i8086` and manually checked against the intended mnemonic/operand/
  addressing-mode; a representative subset covering all seven families was
  independently re-encoded with NASM and matched byte-for-byte wherever the encoding
  is unambiguous (see footnote ¹ above for the one case where it is not).
- The real kernel corpus (`kernel_opt`+`kernel_nonopt`) was re-searched for any
  further hand-encoded `.byte` opcode workarounds of the kind that made INSW/OUTSW
  "confirmed real" (see Milestone 2's "Earlier fixes", now in `status_m2_as.md`) — **none found** for any
  of the other twelve opcodes; INSW/OUTSW remain the only such case in the corpus.
- `x86dis` (`libdisasm` 0.23, no real code change since Dec 2013) was evaluated as an
  alternative to `objdump` and rejected for this purpose: its `-e <offset>`
  (forward-trace) mode silently stops decoding at the first `ret` byte with no error
  or warning (reproduced directly: it decoded only 5 of 48 real text bytes from
  `pusha_popa_test.o`), and its 16-bit (`-L`) mode prints 32-bit register names for
  `rep movs`. `objdump` showed neither issue. Full detail in the README above.
