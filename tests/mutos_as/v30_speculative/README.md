# 80186/V30 speculative-opcode test files

Seven new MUTOS-assembly test files, covering all twelve "implemented, no real
corpus sample" rows in `STATUS.md`'s 80186/V30 table. Paired by natural
instruction family, using the same pairing convention this project already
used for INSW/OUTSW (`tests/mutos_as/kernel_opt/mch_insw_outsw.s`):

| File                              | STATUS.md row(s) covered                                   |
|------------------------------------|-------------------------------------------------------------|
| `pusha_popa_test.s`                | `pusha` (0x60), `popa` (0x61)                                |
| `push_imm_test.s`                  | `push #imm16` (0x68), `push *imm8` (0x6A)                    |
| `insb_outsb_test.s`                | `insb` (0x6C), `outsb` (0x6E)                                 |
| `shift_rotate_imm_count_test.s`    | shift/rotate, immediate count != 1/CL (`C0`/`C1 /digit ib`)  |
| `enter_leave_test.s`               | `enter` (0xC8 iw ib), `leave` (0xC9)                          |
| `bound_test.s`                     | `bound reg,mem` (0x62 /r)                                     |
| `imul_imm_test.s`                  | `imul dst,imm` (2-op) and `imul dst,src,imm` (3-op), both 0x69/0x6B /r |

`nasm_crosscheck.asm` is a supporting file, not a mutos_as test file - see
"Independent-encoder cross-check" below.

**Since 2026-10-25 (`docs/TOOLCHAIN_PLAN.md` section 4, plan step 5)** these
instructions are behind `mutos_as`'s switch `-1`: the seven files are
assembled with `mutos_as -1 file.s`, and give the same bytes as before the
switch (checked against the build of the day before). Without `-1`
`mutos_as` does what the original `as` does with them - the mnemonics are
words referring to undefined externals, the 80186 forms of `push`, `imul`
and the shifts are refused - and `tests/realbin/ascmp.py goldens` runs
these seven files through the original (under the emulator) and `mutos_as`
without `-1`, which treat them alike.

With `-1` the assembler reports, on one line of the standard error, the
80186/V30 instructions of the object by kind (`docs/TOOLCHAIN_PLAN.md`,
decision 16, since 2026-11-08); `v30_with_1.report` pins the seven
files' lines (`../check_switches.sh`), their counts checked against
`objdump -D -b binary -m i8086` on the objects' text, opcode by opcode.

## Verification already performed on these seven files

- All seven assemble cleanly with the current `mutos_as` (zero errors).
- All seven are clean under a `-fsanitize=address,undefined -O0 -g` rebuild
  (zero ASan/UBSan errors).
- The pre-existing 67/67 golden-file regression (`kernel_opt`+`kernel_nonopt`)
  was re-verified unaffected before and after adding these files (they are
  new files only - no source in `src/mutos_as/` was touched).
- Every instruction in every file was independently decoded with
  `objdump -D -b binary -m i8086` and manually cross-checked against the
  intended mnemonic/operands/addressing-mode - see "Findings" below.
- A representative subset (all seven opcode families) was independently
  re-encoded with NASM (`CPU 186`, `BITS 16`) and compared byte-for-byte
  against the corresponding mutos_as output. Every case that has one
  unambiguous correct encoding matched exactly.
- The relocation table of `push_imm_test.o` was manually parsed and checked
  against the object file's own symbol table: the `#_localword` reference
  correctly got an `R_DATA` entry (it's a `.data`-segment symbol), the
  `#_externsym` reference correctly got an `R_EXT` entry with the right
  symbol index, and - most subtly - the `R_EXT` entry correctly has the
  "1-byte-shift" flag (bit 15) set, because that particular immediate word
  happens to start at an ODD byte offset relative to the relocation table's
  even-byte grid. This is a real, working confirmation of one of this
  project's more delicate documented conventions, on brand-new code paths.

None of this is a substitute for a real hardware-linked golden file (no
real MUTOS 1700 tool supports these opcodes - the original `as` has none of
them) - it only establishes that these seven
files are well-formed, that mutos_as's output for them decodes back to
what was intended by an independent, actively-maintained disassembler, and
that it matches an independent, actively-maintained assembler wherever the
encoding is unambiguous.

**Run on the hardware since 2026-11-21** (not these files, the same
encodings): the forms the rules of `-1` write - `push` of a byte and of a
word, `sal`/`sar`/`shr` of a register by 3 to 15, `imul ax,*n.` and `imul
ax,#n` - and, in step V4b, `imul` with three operands ran on the owner's
A7100 with its NEC V30 in the kits of steps V4 and V4b
(`tests/rules/v30kit.py`, `v30time.py`): every result and every defined
flag as the forms' definitions give them (`STATUS.md`, "Step V4", "Step
V4b"; footnote ³ of its 80186/V30 table). That shows what the CPU does
with `mutos_as`'s bytes; it is still no golden. `pusha`/`popa`,
`insb`/`outsb`, `enter`/`leave`, `bound` and the rotates have not run on
the hardware.

## Findings worth your attention

1. **`imul` and `push` with a byte constant outside -128 to 127 are
   refused (since 2026-11-14).** `encode_imul_imm()` picks `0x6B` (byte
   immediate) because of the `*` marker, not because the value fits in a
   signed byte - so `imul cx,ax,*200.` was encoded with the low byte `0xC8`
   and multiplied by -56, not 200; `push *200.` pushed -56 in the same way.
   NASM, even with its optimizer disabled (`-O0`), takes the word form
   `0x69` for such a value. By the owner's decision `mutos_as` now refuses
   both, with a message that names `#n` - the marker still chooses the
   form, but a value the form cannot give is an error (in pass 2, where
   the value is known). `imul_imm_test.s` has `imul cx,ax,*-128.` in the
   place of its `*200.` case, `push_imm_test.s` lost its `push */80`;
   `tests/mutos_as/check_switches.sh` checks the refusals and the ends of
   the range. A real vendor assembler's behaviour, should a sample ever
   surface, is still unknown.

2. **The command first proposed for decoding
   (`x86dis -L -e 16 -s intel -f file.o`) silently truncates its output**
   the moment the byte stream contains a `ret` (0xC3) before the end of the
   segment - it stops decoding right there, with no error or warning, and
   simply omits everything after it. Demonstrated directly against
   `pusha_popa_test.o`: it printed only the first 4 instructions (5 of 48
   text bytes) with that exact invocation. `x86dis -L -r 0 <len> -s intel`
   (explicit range instead of `-e`/forward-trace mode) decodes the full
   buffer correctly instead. For a golden-reference workflow that matters:
   a silently truncated listing looks complete.

3. **`x86dis`'s 16-bit-mode `rep movs` decode prints 32-bit register names**
   (`es:[edi], ds:[esi]`) even with `-L` (legacy/16-bit mode) set, where
   `objdump -m i8086` correctly prints `%es:(%di),%ds:(%si)`. One concrete,
   reproducible data point (not an exhaustive audit) that `x86dis`/
   `libdisasm`'s 16-bit-mode support has real gaps.

4. **The length of a shift by a constant was wrong in pass 1 (found and
   corrected 2026-11-14, with step V2).** `encode_group2()` took every
   count for 1 while sizing, since pass 1 does not evaluate expressions -
   so `sal ax,*3.`, three bytes in pass 2, counted as two, and every label
   after one was a byte low: a branch to it went one byte short of its
   target. None of the sources here has a label behind such a shift, and
   a disassembly of the bytes shows nothing wrong, so neither check could
   see it; the compiler's first use of the form did (the linked triggers
   of `tests/rules/check_v30.py`). A count made of numbers is now
   evaluated in pass 1 too; one with a symbol in it is taken for 1 there,
   and refused in pass 2 if it is not 1. `tests/mutos_as/check_switches.sh`
   assembles a branch back over such a shift and compares the bytes.

## Independent-encoder cross-check

`nasm_crosscheck.asm` re-encodes a representative instruction from each of
the seven families using NASM (`nasm -O0 -f bin -l crosscheck.lst
nasm_crosscheck.asm`), for direct comparison against the corresponding
mutos_as-produced bytes. This is the methodology for anything that has one unambiguous correct encoding under
the plain 80186 ISA (PUSHA/POPA/INSB/OUTSB/LEAVE, ENTER, BOUND, and the
register/register or fits-the-marker forms of the shift-count and IMUL-imm
families). It cannot, by itself, validate design choices unique to this
codebase's own conventions (see finding 1 above) - only whether a chosen
encoding is a real, valid 80186 instruction that means what you think it
means.
