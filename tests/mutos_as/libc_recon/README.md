# `tests/mutos_as/libc_recon/` — real `libc.a` objects as `mutos_as` goldens

The golden objects in `kernel_opt/` and `kernel_nonopt/` are real MUTOS 1700
assembler output for real kernel sources. `tests/mutos1700_libc/` holds a second
body of real assembler output — every object of the real `libc.a` — but without
its sources. This directory turns parts of it into `mutos_as` regression tests
for features the kernel corpus never uses.

**The sources here are reconstructed, not original.** Each `.s` is written from a
disassembly of the real object so that `mutos_as` reproduces that object; the
golden is the real object itself (a verbatim copy, with the usual
`*.o.golden_base64.txt` companion). A byte-for-byte match therefore shows that
`mutos_as` encodes this source exactly as the real assembler encoded the
original — the original's own spelling (comments, the order of `.globl` lines,
a marker the real assembler ignored) is unknown and does not matter. The symbol
table is part of the comparison, and its order is the order of first mention,
so a reconstruction must mention its symbols in the original's order.

| File | Real object | What it covers |
|---|---|---|
| `ldexp.s` | `tests/mutos1700_libc/ldexp.o.base64.txt` (hand-written assembler) | `lea <reg>,<label>` — an external label at even and odd offsets (`lea di,fac`, `lea ax,fac`: `R_EXT`) and a local data label at an odd offset (`lea si,huge`: `R_DATA`); `mov` with a byte register and a memory operand (`mov al,fac+7` → `A0`, `mov fac+7,al` → `A2`, `mov al,*10.(bp)` → `8A`) |
| `floatdat.s` | `atof.o`, `ecvt.o` (data bytes only) | `.float`/`.double`: six constants the real toolchain wrote into those objects' data segments — zero among them (compared with all six of its occurrences) and `ecvt.o`'s inexact 8-byte `0.03` — spelled `%.17e` as `mutos_c1` (and the real compiler) writes them |

`ldexp.s` runs through `../run_goldens.sh` like the kernel directories. `floatdat.s`
is not a whole object — no real object is made of `.float` constants alone — so
`check_floatdat.sh` compares each assembled value's 4 or 8 bytes with the bytes at the
named data offset of the real `atof.o`/`ecvt.o`, read from `tests/mutos1700_libc/`
at test time (`run_goldens.sh` lists it as "no golden"). Both run from the
top-level `make test`.

## Why only these

The obvious candidates for `.float` — the compiled-C objects `atof.o` and `ecvt.o`,
which hold their constants in `.data` and address them with `lea ax,<label>` —
cannot be reconstructed whole yet — since 2026-09-28 only because of the
second point below.

- **Floating constants: solved.** Both hold `0.0` as `bc a2 31 00`:
  exponent byte 0 (zero), but the mantissa bits of 5\*\*17 (= 10\*\*17 /
  2\*\*17). That points at a conversion of a text with 17 fraction digits
  (`%.17e` of zero) that scales by 5\*\*17 and leaves 5\*\*17's mantissa in a
  zero result — but `libc.a`'s own `atof` returns a clean zero for that text
  (its `fl /= flexp` reaches `dmath.o`'s `ddiv`, which clears `fac` for a zero
  dividend), so the real assembler's conversion is not this `atof`. Real
  hardware settled the bytes on 2026-09-28: `.float 0.00000000000000000e+00`
  assembles to exactly `bc a2 31 00` (`../float_coverage/fltzero.o.golden`),
  and `mutos_as` writes the same (with `../float_coverage/fltopen.o.golden`,
  also other zero spellings - see `src/mutos_as/fltconst.h`);
  `floatdat.s` compares it with all six zero constants in `atof.o`/`ecvt.o`.
  `ecvt.o` also holds an 8-byte constant, `.03`, which is not exact. Since
  `mutos_as` re-enacts the real conversion (v7 `atof()` on the 56-bit double,
  see `src/mutos_as/fltconst.h`, from `../float_coverage/fltopen.o.golden`),
  `.double 3.00000000000000000e-02` — and `.03` — give exactly `ecvt.o`'s
  `c3 f5 28 5c 8f c2 75 7b` under every rounding still possible;
  `floatdat.s` checks all 8 bytes.
- **No `L` labels.** Their symbol tables contain no compiler-generated `L`
  labels (nor does any other of `libc.a`'s 167 objects) — the real `as`'s
  documented default without `-L`
  (`docs/MUTOS1700_Assembler_as.pdf` sect. 3.1: identifiers beginning with `L`
  are local labels unless `-L` is given) — while `mutos_as` always writes them,
  because the kernel goldens compiled from C have them (`CLAUDE.md`'s "Known,
  deliberate exception"). The real binary turned out to ignore `-L` entirely
  (`Unknown option L ignored`, 2026-09-28 - see `STATUS.md`'s open item 6), so
  why the kernel goldens have `L` labels and `libc.a` has none is still open.
  A compiled-C `libc.a` object can only be reproduced once that difference is
  resolved - for both `atof.o` and `ecvt.o` it is now the only blocker.

`ldexp.o` has neither problem: it is hand-written (no `L` labels) and its one
floating constant, `huge`, is emitted as `.word`s here — its original spelling
is unknown and irrelevant to what the test checks.
