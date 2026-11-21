# CPU reference: the 8086, the 80186 and the NEC V30

What the project uses from two manufacturers' documents. Until 2026-11-21 both were
in `docs/` as PDFs; they are their publishers' copyright and were removed at the
owner's decision. This file keeps the facts the tools and documents rely on, in the
project's words, with the documents named so that they can be found:

- **AP-186**: Intel, *Introduction to the 80186 Microprocessor*, Application Note
  AP-186, order number 210973-001, March 1983 - the 80186's architecture, its
  differences from the 8086, the algorithms of its new instructions (no clock
  tables).
- **The NEC manual**: NEC Electronics Inc., *µPD70108 (V20) 8/16-Bit CMOS
  Microprocessor, µPD70116 (V30) 16-Bit CMOS Microprocessor - User's Manual*,
  October 1986, stock no. 500350 - the V20/V30's instruction set with the clocks,
  bytes, transfers and flags of every form.

Where the owner's machine measured otherwise, it says so; where the project found a
fact, the document of record is named.

## The chips

- The V30 is the µPD70116: pin-compatible with the 8086, a 16-bit data bus, a
  prefetch queue of 6 bytes. The V20 (µPD70108) is the 8088's counterpart: an 8-bit
  bus, a queue of 4 bytes. MUTOS 1700's machines had the 8086 (K1810WM86); the
  owner's A7100 has a V30 in its place (at 15 MHz, the memory with 3 or 4 wait
  states - `docs/PROJECT_MEMORY.md`).
- The V30 runs 8086 code, and the instructions the 80186 added, with the 80186's
  encodings. Besides it has an 8080 emulation mode, selected by the flag MD (bit 15
  of the flag word; 1 = native mode), and instructions of its own (bit fields, BCD
  strings) - none of which the project uses. The routines of `tests/rules/v30kit.py`
  that set the flags keep bit 15 as `pushf` gave it.
- The 80188 is the 80186 with an 8-bit bus and a 4-byte queue; the instruction set is
  the same.

### NEC's names

The NEC manual uses its own mnemonics; searching it for Intel's finds nothing.

| Intel | NEC | Intel | NEC |
|---|---|---|---|
| `imul` (signed) | `MUL` | `mul` (unsigned) | `MULU` |
| `idiv` (signed) | `DIV` | `div` (unsigned) | `DIVU` |
| `enter` | `PREPARE` | `leave` | `DISPOSE` |
| `pusha` / `popa` | `PUSHR` / `POPR` | `bound` | `CHKIND` |
| `ins` / `outs` | `INM` / `OUTM` | `pushf` / `popf` | `PUSH PSW` / `POP PSW` |
| `aam` / `aad` | `CVTBD` / `CVTDB` | `lea` | `LDEA` |
| `jmp` | `BR` | `jcc` | `Bcc` (`BLT`, `BNE`, ...) |
| `loop` | `DBNZ` | `lock` | `BUSLOCK` |

## Where the chips compute differently

These change what a program computes; the emulator of `tests/realbin/` runs on
Unicorn's 80386, so `mutosemu.py` does the 8086's behaviour itself (its
`_hook_8086()`; `docs/devlog/originals_and_ports.md`, "The port of `c0`").

- **The count of a shift or rotate.** The 8086 and the V30 shift by the whole count -
  the NEC manual describes every multi-bit shift as "temp <- count, while temp != 0"
  -, by CL and, on the V30, by an immediate alike. The 80186 (AP-186) and the 80386
  take the count modulo 32.
- **The most negative quotient.** A signed division whose quotient is -32768 (8000h;
  -128, 80h, for a byte) is a divide error on the 8086 and on the V30 (the V30's
  most negative quotient is -32767, 8001h); the 80186 (AP-186) and the 80386 deliver
  it. `docs/MUTOS_ORIGINAL_BUGS.md` has where `libc.a` meets it.
- **`pushf` is not a difference.** In real mode the 8086 and the 80186 both push
  bits 12 to 15 of the flag word as 1 (AP-186). A "reference" claiming that the
  80186 pushes them as 0 has been pasted into sessions repeatedly; it is wrong
  (`CLAUDE.md`, the callout at the top; `docs/devlog/m2_as.md`, "CPU reference").
- **The 80186's new opcodes on an 8086.** 60h-6Fh act as the conditional jumps
  70h-7Fh, and C0h/C1h as `ret n`/`ret` (C2h/C3h): code assembled with `-1` runs on
  an 8086 without a fault - and wrongly (`docs/devlog/m5_c2_v30.md`).
- Other 80186 differences, none relevant to code generation (AP-186): `pusha` pushes
  AX, CX, DX, BX, SP (its value before the instruction), BP, SI, DI, and `popa`
  restores them in reverse, skipping SP; the 80186's integrated peripherals sit in a
  256-byte peripheral control block (by default at FF00h, accessed by words only);
  segment wrap-around, an interrupted `rep` and the timing of `lock` differ. The V30
  has no integrated peripherals.

## The forms `-1` writes

| Form | Encoding | Flags after it |
|---|---|---|
| `push *n.` (a byte, -128 to 127) | `6A ib` - the byte sign-extended to a word | unchanged |
| `push #n` (a word, an address) | `68 iw` | unchanged |
| `sal`/`sar`/`shr r,*n.` (n from 3 to 15) | `C1 /r ib` (`C0` for a byte register) | CY the last bit shifted out; P, Z, S of the result; AC and V undefined |
| `imul r,x,*n.` (n from -128 to 127) | `6B /r ib` - the byte sign-extended | CY and V set when the product does not fit 16 bits (signed), cleared otherwise; P, AC, Z, S undefined |
| `imul r,x,#n` | `69 /r iw` | as `6B` |

- `imul ax,*n.` is the three-operand form with AX as source and target. `mutos_as -1`
  refuses `push *n` and `imul r,x,*n` for an n outside -128 to 127 (the byte would be
  sign-extended; `docs/PROJECT_MEMORY.md`, the decisions of 2026-11-14).
- A shift by 1 (`D1 /r`) defines V as well; a shift by CL or by a larger immediate
  leaves it undefined - which matters for `c1`, whose code branches on the flags of
  a shift (`docs/MUTOS_ORIGINAL_BUGS.md`, C1-01 and C1-15).
- On the owner's V30 (step V4, `tests/rules/v30kit.py`'s `probe.v`) every form
  computed as above, and set the undefined flags exactly as the 8086 sequence it
  replaces does on the same chip.
- The other 80186 instructions, which `-1` does not write: `enter` `C8 iw ib`, `leave`
  `C9`, `pusha` `60`, `popa` `61`, `bound` `62`, `ins` `6C`/`6D`, `outs` `6E`/`6F`
  (`STATUS.md`, "`mutos_as` opcode coverage"; `tests/mutos_as/v30_speculative/`).

## Timing of the V30

From the NEC manual, for the µPD70116 at even addresses and without wait states -
the figures `tests/mutos_cc/fuzz/x86sim.py` (`v30_clocks()`) and
`tests/rules/v30bus.py` use:

- A bus cycle is 4 clocks (T1 to T4), plus the memory's wait states. A word at an odd
  address takes two bus cycles, 4 clocks more (`mov reg,mem`: 11 at an even address,
  15 at an odd one).
- The V30 prefetches a word whenever a word of its 6-byte queue is free. An
  instruction's clocks assume its bytes are in the queue; when fetching is slower
  than executing, the fetch is the time.
- The effective address is computed in 2 clocks for every addressing mode (the 8086
  needs 5 to 12); the figures include it.

| Instruction | Clocks | Instruction | Clocks |
|---|---|---|---|
| `mov` reg,reg / reg,imm | 2 / 4 | `mov` mem,reg / reg,mem / mem,imm | 9 / 11 / 11 |
| `add`, `sub`, `and`, `or`, `xor`, `adc`, `sbb` reg,reg / reg,imm | 2 / 4 | the same reg,mem / mem,reg / mem,imm | 11 / 16 / 18 |
| `cmp` reg,reg / reg,imm / reg,mem / mem,reg / mem,imm | 2 / 4 / 11 / 11 / 13 | `test` reg,reg / reg,imm / with mem / mem,imm | 2 / 4 / 10 / 11 |
| `inc`, `dec`, `not`, `neg` reg / mem | 2 / 16 | `lea` | 4 |
| `push` reg / mem / imm8 / imm16 | 8 / 18 / 7 / 8 | `pop` reg / mem | 8 / 17 |
| `call` near | 16 | `ret` / `ret n` | 15 / 20 |
| `jmp` near or short | 12 | `jcc` taken / not taken | 14 / 4 |
| `loop` taken / not taken | 13 / 5 | `cbw` / `cwd` / `sahf` / `nop` | 2 / 4-5 / 3 / 3 |
| shift reg,1 / mem,1 | 2 / 16 | shift reg by CL or imm8 / mem | 7+n / 19+n |
| `imul` reg16 / mem16 | 41-47 / 47-53 | `imul` reg16,reg16,imm8 / imm16 | 28-34 / 36-42 |
| `imul` reg16,mem16,imm8 / imm16 | 34-40 / 42-48 | `mul` reg8 / reg16 / mem16 | 21-22 / 29-30 / 35-36 |
| `idiv` reg16 / mem16 | 38-43 / 44-49 | `aam` / `aad` | 15 / 7 |
| `enter` imm16,0 / `leave` | 12 / 6 | `pusha` / `popa` | 35 / 43 |

A range is "according to data". At an odd address a word transfer costs 4 clocks
more (`push` reg 12, imm8 11, imm16 12; `ret` 19).

### Measured on the owner's V30

Step V4b (2026-11-21, `tests/rules/v30time.py`, the log
`tests/rules/hwlog_v4b_2026-11-21.txt`; `STATUS.md`, "Step V4b") timed the forms one
by one on the owner's A7100: the V30 at 15 MHz, the memory with 3 wait states - a
bus cycle of 7 clocks, 3.5 clocks a byte of code. Short instructions wait for the
bus, not for the execution unit (`tests/rules/v30bus.py` models that). Where the
execution unit differs from the manual:

| Instruction | Manual | Measured |
|---|---|---|
| `imul` reg16,reg16,imm8 | 28-34 | 40; 44 when the product is negative |
| `imul` reg16,reg16,imm16 | 36-42 | 40; 44 when the product is negative |
| shift reg by CL or imm8 (n) | 7+n | 10+n |
| `mul` reg16 (unsigned) | 29-30 | 32-33 |

The same as the manual: `aam` (15), the one-operand `imul` reg16 (42, 46.5 with a
negative product; 41-47). So on this chip the three-operand `imul` takes as long
with a byte as with a word, and about 2 clocks less than the one-operand form:
`-1`'s product by a constant gains 8 clocks over the 8086 code by a byte, 3.7 by a
word (`mov cx,*n.`/`imul cx` against `imul ax,*n.`). `v30bus.A7100` holds these
figures; `v30bus.py --measured` counts with them.
