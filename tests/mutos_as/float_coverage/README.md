# `tests/mutos_as/float_coverage/` — closing the open floating-point gaps

Four hand-written, **new** assembler sources (not reconstructed from a
real object, unlike `../libc_recon/`) targeting the specific floating-point
gaps this project's own docs already flag as open, rather than general
"more float coverage":

| File | Tests | Expected on real `as` | Expected on current `mutos_as` |
|---|---|---|---|
| `fltaddr.s` | `lea <reg>,<label>` + `call` addressing a real `.float` constant, in one whole object | succeeds | succeeds (regression golden) |
| `fltmulti.s` | two `.float` constants, `.text`/`.data` switch + `EVEN`-padding interaction, chained runtime calls | succeeds | succeeds (regression golden) |
| `fltzero.s` | `.float 0.0` | succeeds | **refused** (`FLT_ZERO`) |
| `fltdbl.s` | `.double` (8-byte), a value needing >24 mantissa bits | succeeds | **refused** (`.double` not implemented) |

## Why these four, specifically

`tests/mutos_as/libc_recon/floatdat.s` covers `.float` constants but has
no code around them (its own header: "not a whole object — no real
object is made of `.float` constants alone"), and only checks isolated
4-byte ranges via `check_floatdat.sh`, never a whole `.o.golden` diff.
`ldexp.s` covers `lea <reg>,<label>` + code but never combines it with a
`.float` constant (its one float-ish value, `huge`, is emitted as raw
`.word`s). `fltaddr.s` and `fltmulti.s` close that combination gap with
values `mutos_as` can already encode exactly (`src/mutos_as/fltconst.h`
only accepts a value that's exactly representable in 24 mantissa bits —
`2.5`, `3.140625`, `2.71875` are all exact terminating binary fractions,
picked for that reason, not because they're numerically interesting).

`fltzero.s` and `fltdbl.s` are different in kind: they are **probes for
open questions**, not regression goldens. `mutos_as` currently refuses
both outright —

- a zero `.float`, because `fltconst.h` says plainly: *"a zero's real
  bytes are not a plain zero"* — known only from the two real compiled-C
  objects that happen to hold one (`atof.o`/`ecvt.o`, both `bc a2 31 00`),
  whose real assembler-input spelling is unknown (see
  `../libc_recon/README.md`'s "Why only these"), so `mutos_as` refuses to
  guess.
- `.double` at all — `src/mutos_as/assemble.c` errors out before even
  parsing the operand, because the one known real 8-byte constant
  (`ecvt.o`'s `.03`) can't by itself pin down how a value needing more
  than a float's 24 bits is converted.

Both gaps share the same root cause: no real object with a **known
hand-written source** exercises them, only compiled-C objects whose
source is lost. `fltzero.s` and `fltdbl.s` are exactly that missing
known source — real hardware assembling them for the first time
produces ground truth that `atof.o`/`ecvt.o` alone cannot.

## Running this

Two steps, split across two machines — same shape as every other golden
corpus in this project (see `tests/mutos_cc/README.md`):

1. `make -f Makefile.mutos` — **on real MUTOS 1700 hardware / an
   accurate emulator**, from inside this directory. Produces
   `fltaddr.o`, `fltmulti.o`, `fltzero.o`, `fltdbl.o`. All four are
   expected to succeed here — `mutos_as`'s refusal of the latter two is
   a `mutos_as` gap, not a real-`as` one.
2. `make goldens` — **on the modern (Linux) side**, after copying all
   four `*.o` back into this directory. Produces `*.o.golden` (a
   verbatim copy) and `*.o.golden_base64.txt` (base64 text) for each —
   the same naming `../kernel_opt/`, `../kernel_nonopt/` and
   `../libc_recon/` already use (`../mk_goldenbase64.sh`).

**Neither `Makefile.mutos` nor this `Makefile` has produced real
goldens yet** — both are infrastructure only, added this session without
access to real MUTOS 1700 hardware. Treat every claim above about what
"is expected to succeed" as a documented prediction from reading
`fltconst.h`/`assemble.c`, not a verified fact, until a real run confirms
it — the whole point of running `fltzero.s`/`fltdbl.s` for real is that
their outcome isn't actually known yet.

## After the real goldens exist

Run `../run_goldens.sh` from this directory exactly as in `kernel_opt/`/
`kernel_nonopt/`: `fltaddr.s`/`fltmulti.s` should land in its category 1
(clean); `fltzero.s`/`fltdbl.s` are expected to land in its category 3
(assembler error) until `fltconst.c`/`assemble.c` are updated from the
new goldens' real bytes — that is expected, not a regression, exactly
like `../libc_recon/`'s two currently-irreproducible compiled-C objects.
Once `fltzero.o.golden`/`fltdbl.o.golden` exist, use their real data
bytes to implement zero-`.float` and `.double` encoding in
`src/mutos_as/fltconst.c`/`assemble.c`, then update `CLAUDE.md`/
`STATUS.md` per the usual sync rule (Workflow Guideline 6) — including
removing the "is not yet supported" wording once it no longer applies.
