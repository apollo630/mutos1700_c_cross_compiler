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

None of this is a substitute for a real hardware-linked golden file (see
the main chat response for why) - it only establishes that these seven
files are well-formed, that mutos_as's output for them decodes back to
what was intended by an independent, actively-maintained disassembler, and
that it matches an independent, actively-maintained assembler wherever the
encoding is unambiguous.

## Findings worth your attention

1. **`imul` immediate-marker truncation is a real, deliberate, and
   non-obvious behavior.** `encode_imul_imm()` picks `0x6B` (byte immediate)
   purely because of the `*` marker, not because the value fits in a signed
   byte - so `imul cx,ax,*200.` truncates 200 to `0xC8` (i.e. multiplies by
   -56, not 200). NASM, even with its optimizer fully disabled (`-O0`),
   refuses to do this: it always promotes an out-of-range immediate to the
   safe `0x69` word form instead of silently corrupting the value. This
   isn't a bug in either tool - it's a genuine design choice unique to this
   codebase's "the marker always wins" convention, matching how `push
   *imm8` already behaves the same way. It's worth deciding, consciously,
   whether that is really what you want here before this goes in front of
   any real MUTOS toolchain comparison, since it's exactly the kind of
   place a real vendor assembler's behavior (once you have a sample) could
   turn out to disagree with.

2. **The literal command you proposed
   (`x86dis -L -e 16 -s intel -f file.o`) silently truncates its output**
   the moment the byte stream contains a `ret` (0xC3) before the end of the
   segment - it stops decoding right there, with no error or warning, and
   simply omits everything after it. Demonstrated directly against
   `pusha_popa_test.o`: it printed only the first 4 instructions (5 of 48
   text bytes) with that exact invocation. `x86dis -L -r 0 <len> -s intel`
   (explicit range instead of `-e`/forward-trace mode) decodes the full
   buffer correctly instead. See the main chat response for why this
   matters for a golden-reference workflow specifically.

3. **`x86dis`'s 16-bit-mode `rep movs` decode prints 32-bit register names**
   (`es:[edi], ds:[esi]`) even with `-L` (legacy/16-bit mode) set, where
   `objdump -m i8086` correctly prints `%es:(%di),%ds:(%si)`. One concrete,
   reproducible data point (not an exhaustive audit) that `x86dis`/
   `libdisasm`'s 16-bit-mode support has real gaps.

## Independent-encoder cross-check

`nasm_crosscheck.asm` re-encodes a representative instruction from each of
the seven families using NASM (`nasm -O0 -f bin -l crosscheck.lst
nasm_crosscheck.asm`), for direct comparison against the corresponding
mutos_as-produced bytes. This is the methodology recommended in the main
chat response for anything that has one unambiguous correct encoding under
the plain 80186 ISA (PUSHA/POPA/INSB/OUTSB/LEAVE, ENTER, BOUND, and the
register/register or fits-the-marker forms of the shift-count and IMUL-imm
families). It cannot, by itself, validate design choices unique to this
codebase's own conventions (see finding 1 above) - only whether a chosen
encoding is a real, valid 80186 instruction that means what you think it
means.
