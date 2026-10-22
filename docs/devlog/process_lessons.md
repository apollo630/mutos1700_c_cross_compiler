# Recurring process lessons

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). Lessons that apply to every milestone.

---

These apply to *every* milestone, not just the one where they were first learned:

- **Never trust a memory note (or this file) claiming something is "fixed" without
  re-verifying against the actual current project files first.** Uploaded/synced
  project files have repeatedly lagged behind or failed to persist previous
  sessions' fixes across this project's history (confirmed multiple times for
  `encode.c`/`symtab.c`/`assemble.c`/`objwrite.c`). The mandatory
  rebuild-and-diff-against-goldens-first rule (`CLAUDE.md` Workflow Guideline 6)
  exists specifically because of this.
- **`.o` is ambiguous in this repo** — golden reference object files
  (`tests/mutos_as/*/*.o.golden`) look like build artifacts but are precious,
  irreplaceable hardware-linked data; a blanket `find . -name "*.o" -delete`
  from the repo root will destroy them. `tests/mutos1700_libc/*.o.base64.txt`
  and `tests/mutos1700_crt0/crt0.o.base64.txt` are the same kind of precious
  hardware-linked data, now stored as base64 text (the raw `.o`/`.a` binaries
  were deleted 2026-09-27) specifically so a `.o`-glob sweep can't catch them
  by accident — don't regenerate or re-encode them either. Scope any
  build-artifact cleanup to the specific `src/mutos_<tool>/` directory being built.
- **Real compiler output already in the repo is evidence too.** The
  `tests/mutos_as/kernel_nonopt/*.s` files are genuine non-optimized MUTOS `c1`
  output, thousands of lines of it. Before extrapolating a codegen idiom from
  one or two `tests/mutos_cc/` goldens, grep those files for it: that is how the
  constant-shift threshold (repeat for 1-2, `mov cx,N` / shift by `cl` from 3 up)
  was found, after `c1` had been silently wrong for counts of 3 or more since
  `04_shift`.
- **So is `libc.a`.** Its objects are real compiler, assembler and hand-coded
  runtime output: their symbol tables name a runtime's entry points, their
  disassembly gives its calling convention, and their data segments hold
  values in the target's own formats. `08_float`'s floating-point runtime
  calls, the floating format and the "4 bytes only if exactly a float" rule
  for constants were all read there, not guessed (see that section). Four
  of its objects (`atof.o`, `ecvt.o`, `gcvt.o`, `fltpr.o`) are C compiled by
  the real compiler: before listing a shape as "waiting for a golden",
  check whether their code already has it - most of the refused floating
  shapes were there (see "Floating shapes from libc.a's compiled C"). They
  were compiled with `-O`, so take only what `c2` cannot have changed.
- **"Unknown, so ignored" is only safe for a directive that emits nothing.**
  `mutos_as` skipped `.float` silently, shifting every later data address;
  the first program whose compiler output used it would have been wrong with
  no diagnostic. A directive (or opcode) that reserves or writes bytes must
  be implemented or refused, never dropped.
- **A fix's real shape can come from the kernel corpus even without its C
  source.** The postfix-in-condition shape was recognizable in
  `kernel_nonopt/*.s` by pattern alone (`mov R,X` / `inc|dec X` / a test
  of `R`), five times, with the idiom's variants (truth test, `> 0`,
  `>= 2`) readable from the constants and branches. Scan for the
  instruction pattern a construct must produce before concluding "no
  golden shows it".
- **Assemble what the fuzzer produces - and know what that still misses.**
  Feeding random programs' `mutos_c1` output to `mutos_as` found 43 of 1500
  that emitted instructions the 8086 does not have (`cmp *3.,...`, `cmp
  mem,(di)`); nothing else had noticed. But it cannot catch legal code that
  computes the wrong thing: `a = v[1];` storing `&v[1]` assembled fine and
  was found only by reading a sample of the output. Anything that reorders
  evaluation (see the `05_matmul` plan) wants a semantic check - running
  fuzzed programs in an 8086 emulator against their expected results
  (done for the `05_matmul` change: an interpreter for `c1`'s instruction
  subset, validated on real goldens first, comparing every variable's
  final value - see that section).
- **A semantic check finds bugs the golden corpus structurally cannot.**
  Its first run flagged `mutos_c0` compiling `7 - x` as `x - 7` - wrong
  since the first binary operator, invisible to all 62 goldens because
  none of them has a constant left operand. When such a check flags a
  program, first compare the PREVIOUS binary's output for it: identical
  output means a pre-existing bug, not a regression of the change under
  test (all four first flags here were that `c0` bug; none involved the
  reordering). And keep the generator away from a known-broken construct
  while validating something else, rather than letting it drown the
  signal.
- **A baseline that "passes" can be right by luck.** Random programs mask
  wrong values constantly - a comparison or truth test squashes them to
  0/1, a later statement overwrites them. Check intermediate state, not
  just the final one (`fuzz_c.py`'s statement markers), and before calling
  a changed-but-correct output harmless, re-run the program with other
  initial values: that turned 31 of 51 "both correct" differences into
  proven fixes. Conversely, a "regression" from right-by-luck to an
  honest refusal is not one - read each before believing it.
- **A label number off by one is evidence of something that writes
  nothing.** v7 hands out intermediate-code label numbers from one counter
  (`isn`) at places that leave no trace in the stream - the lexer numbers
  every string token, even a char array's initializer, which is written
  without a label (`10_integ/01_wordcount`: `main()`'s labels start at 2).
  When a stream matches a golden except for a constant shift of its
  labels, look for such a consumer in v7's source before the one that
  writes the label.
- **A consistency check must agree with the moment it runs at.**
  `check_docs.py`'s Check 5 compared a "Last updated" stamp with the file's
  last commit in `git log`. Run from the pre-commit hook, that is the
  PREVIOUS commit, so the first commit of a new day with a correctly bumped
  stamp (`STATUS.md`: 2026-09-24, last commit 2026-09-23) was blocked, while a
  commit on a later day that forgot to bump the stamp passed the hook and
  failed only in CI, after the push. For a file
  with uncommitted changes the check now expects today's date (the date the
  pending commit will carry); for an unchanged file - e.g. CI's clean
  checkout - still the last commit's date.
- **Key a refusal on the rule it enforces, not on one operand kind.**
  `OP_ASSIGN` refused `x = y;` by testing for `VK_MEM`, but the rule behind
  it - the 8086 has no memory-to-memory `MOV` - holds for every memory
  operand; two local statics (`VK_STATIC`) slipped through as `mov L4,L5`
  for as long as statics existed, found only when file-scope variables
  joined that kind (`07_scope`). When a new kind of value joins an old one,
  re-read every test of the old kind by name.
- **The library is part of the program.** The original `c1`'s output
  passes through `libc.a` - `atof`, `printf`, `itod`, `aldiv` - and two
  of those are not what C computes (`itod`'s stray bytes, `aldiv`'s
  sixteen steps). A port that calls the host's arithmetic instead is
  wrong exactly where the routine is, and nothing short of running the
  original on inputs at the routine's own path boundaries shows it: the
  general fuzzer met the `aldiv` case once in thousands of programs, a
  generator aimed at the routine's paths in most of 300.
- **The oracle is a model too.** The emulator runs the original binaries,
  but on an 80386's semantics: Unicorn masks a shift count to five bits and
  lets a quotient of -32768 through, where the 8086 and the V30 do neither.
  The first `c0` port refused the shifts on a wrong reading ("the 8086 and
  the V30 differ") instead of asking which machine the emulator is; the
  CPU manuals in `docs/` settled it in minutes. Where an emulated original
  and the hardware could differ, check the instruction against the
  hardware's documentation and fix the oracle, rather than building the
  port - or its refusals - around the oracle's behaviour.
- **A plan that says "done" is still a plan.** `docs/C0_PORT_PLAN.md` was
  written with "Status: done - `mutos_c0` is the port" and an empty
  "Result" section while nothing was built in, committed or checked; the
  old front end was still `mutos_c0`. Read such a status as the intent of
  the session that wrote it, and write a status only after the run that
  shows it.
- **Corpus-driven validation catches real bugs that a "looks correct" review
  wouldn't** — nearly every bug in this log's Bug-fix History sections was found by
  diffing against a real golden file, not by code review.
- **A user-supplied technical spec framed as generic "expert persona priming"
  (e.g. "act as an x86 architecture expert, here are the specs") can bypass the
  mandatory session-start protocol** if that protocol is only triggered by
  requests that self-identify as MUTOS work. The 2026-09 `PUSHF` recurrence (see
  the CPU reference section above) happened exactly this way. `CLAUDE.md` now
  states explicitly that the protocol applies from message 1 of any session in
  this project, regardless of how the opening message is framed.
- **Evidence from a boundary case cannot tell two rules apart.** `mutos_as` padded
  a segment at every `.text`/`.data` switch because `malloc.o` and `mch.o` showed a
  pad there - but both switches were their files' last, where "pad at the end of the
  file" gives the same bytes. The wrong rule stood unnoticed until code switched to
  `.data` between two instructions (`mutos_c1`'s floating constants) and the pad
  landed in the code. When a rule is inferred from a sample, ask which other rule the
  same sample also fits, and look for a sample that separates them (here `atof.o`).
- **A model fitted to samples is only as good as its candidate set - run
  the real thing on the model's whole input space when you have it.**
  `fltmodel.py`'s rounding modes were idealized (truncate, nearest,
  away), and 19 real constants happily picked one. `libc.a`'s own `atof`
  on its own runtime, run under an emulator on thousands of random texts,
  showed that the family's multiplication is none of them for wide
  operands - one partial product loads the wrong word - in a region no
  golden had touched. Fitting would never have proposed that candidate.
- **Optimized real output supports a rule only where it tells the rules
  apart.** Every floating line in `libc.a`'s compiled C fit a constant of
  degree 0 - because none of it compares a float variable with a constant,
  where degree 0 and 1 give different code. The model stood, consistent with
  all of it, until the first probe written for exactly that case. When real
  output confirms a model, list the cases where a rival rule would differ
  and check that the sample contains one; if it does not, the next probe
  should.
- **Run what you build, when you can.** 67/67 byte-exact goldens said nothing about
  a pad byte in code no golden contained; linking the float goldens with the real
  `crt0.o`/`libc.a` and executing them in an emulator is what makes "it assembles"
  mean "it works".
- **A golden set can arrive incomplete - and a harness that skips quietly
  hides it.** Round 2 of `tests/mutos_cc/fltprobe/` was pushed with its `.s`,
  `.1` and `.2` but no `.i`; `run_goldens.sh` skipped every file without an
  `.i.golden`, so all four would have gone unchecked under an unchanged "71
  byte-exact". After new goldens arrive, list what each recipe makes and what
  came back, and make a harness report what it skips (it now checks such a
  file from `mutos_c0` on and lists it apart).
- **An extension without an oracle needs a check of what it is for.** `mutos_ld`'s
  `-r`, put back from v7's source into the port of a binary that has none, produced
  well-formed objects that linked without a message - and programs that returned the
  wrong value or crashed: v7's relocatable output has data addresses after the text,
  MUTOS's objects segment offsets. Only running a program linked through `-r`
  against the same program linked at once showed it. Where the port goes beyond the
  original, build the semantic check before trusting the code.
