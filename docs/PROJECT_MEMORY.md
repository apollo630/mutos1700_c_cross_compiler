# Project memory — working agreements and context for AI sessions

Until 2026-11-07 this was kept in Claude's conversation memory of the claude.ai
project "MUTOS 1700 Cross-Compiler Toolchain"; the project owner had it moved
into the repository so that every session - on any surface - reads the same.
`CLAUDE.md`'s Session-Start Protocol names this file. It holds what the other
documents do not: how the owner wants the work delivered, his standing rules,
his local setup, and where each decision is recorded. The repository is the
authority; where this file and `STATUS.md` or `docs/TOOLCHAIN_PLAN.md` disagree,
those win, and this file is corrected.

Keep it short and current: a new standing rule or decision of the owner's goes
here (dated) in the same change that acts on it, or into the document it belongs
to with a pointer here. Claude's conversation memory is still kept; since
2026-11-07 everything in it is in this file too (`CLAUDE.md`, Workflow
Guideline 11).

## Working agreements

| Since | Agreement | Where it is enforced |
|---|---|---|
| — | Claude has no write access to the repository. Changes are delivered as a git patch against the owner's current `HEAD`, checked with `git apply --check` on a fresh clone; the owner applies, commits and pushes. Never push. | `CLAUDE.md`, Workflow Guideline 7 |
| 2026-10-14 | Every bug found in an original MUTOS 1700 binary goes into `docs/MUTOS_ORIGINAL_BUGS.md` in the change that finds it, so that it can be fixed later. | Workflow Guideline 9 |
| — | A tracked document with a `Last updated: YYYY-MM-DD` stamp (today `STATUS.md` and `docs/MUTOS_ORIGINAL_BUGS.md`) gets the stamp bumped to the commit's date in the same edit; `scripts/check_docs.py` (check 5) fails otherwise. | `make check-docs`, CI, the pre-commit hook |
| 2026-10-20 | Manual pages (`man/`) are written at the end of the project, in English, without development details, with references to the other tools and one wording and structure throughout; the existing ones stay as they are until then. | Workflow Guideline 10; `docs/TOOLCHAIN_PLAN.md`, decision 12 |
| — | Everything in the repository is English (code comments, documents, manual pages). The owner writes to Claude in German; answers to him are German, what goes into the repository English. | `CLAUDE.md`, "AI Collaboration Persona & Rules" |
| — | Decisions on direction are the owner's: Claude measures, recommends and waits. Open ones are listed below. | — |
| 2026-11-07 | Claude's conversation memory of the claude.ai project is synced into this file: what Claude files there goes into this file in the same session's patch (a patch of this file alone if the session delivers no other); what the automatic memory pass files after a turn is caught at the next session start, when Claude compares its memory with this file. Where the two disagree, the repository wins and the memory is corrected. | `CLAUDE.md`, Workflow Guideline 11 and the Session-Start Protocol |

## The owner's decisions, and where they are recorded

| Date | Decision | Record |
|---|---|---|
| 2026-10-20 | Every remaining tool is a port of the original MUTOS 1700 binary, as `c0` and `c1` were: the binary the specification, v7's source the scaffold, the emulated original the oracle. | `docs/TOOLCHAIN_PLAN.md`, decision 1 |
| 2026-10-20 | The MUTOS root ("variant A"): absolute MUTOS paths are looked up under `$MUTOS_ROOT`, by default the build tree's `mutos_root/` compiled in at build time; the passes are symbolic links in it; `make install` later. | decisions 2 and 3 |
| 2026-10-20 | `cpp` and `ld` ported; `ld` keeps `-r`/`-d` as a documented extension. | decisions 4 and 5 |
| 2026-10-20 | `mutos_as`: `-g`/`-C` accepted, no 8087 support, a differential fuzzer against the original before deciding on a port; the 80186/V30 instructions behind the switch `-1` (name confirmed that day). | decision 9; section 4 |
| 2026-10-20 | `c2` ported for the 8086 only; `c1` and `c2` stay without 80186/V30 code generation until the 8086 toolchain works completely - then it is decided. | decisions 7 and 8 |
| 2026-10-20 | Order of work: infrastructure, `cpp`, `ld`, `cc`, `c2`, then the measurement of `as`. | section 3 |
| 2026-10-22 | Path rule: paths of the MUTOS file system (absolute names a tool has compiled in, absolute `#include` names) are looked up under `$MUTOS_ROOT`; names from the command line are the host's. With the `ld` port: `tmp/` in the root; `-r` follows MUTOS's object convention; the output is renamed where the host file system has no hard links. | decisions 4 and 5; `src/mutos_ld/README.md` |
| 2026-10-25 | The original headers (`mutos/mutos_include/usr/include/`) are copied into the root's `usr/include/`. | decision 3 |
| 2026-10-25 | MUTOS makefiles are supported by porting `make` (not a wrapper around the host's `make`), and with it every other binary brought from the hardware: `ar`, `ranlib`, `nm`, `size`, `strip`, `tsort`, `lorder` (a script), `ed`, `em`. | decisions 10 and 11 |
| 2026-10-25 | All original MUTOS files live under `mutos/` (`mutos_bin/` binaries as base64 text, `mutos_include/`, `mutos_man/`), reference data never altered. | `mutos/README.md`; Workflow Guideline 2 |
| 2026-11-07 | The switch for 80186/V30 code generation in `cc`/`c1`/`c2`, should it come, is `-1` as well, the name `mutos_as` has; the older working name was replaced in every Markdown document. | `docs/TOOLCHAIN_PLAN.md`, section 4 |
| 2026-11-08 | After weighing the correction of the originals' bugs against 80186/V30 code generation - his first concern: no wrong code, no existing code broken little by little by special cases, a clean and testable code base -: both come, in the order of the plan's section 7.5 (shared infrastructure first, then the corrections that matter, then the V30 rules). | `docs/TOOLCHAIN_PLAN.md`, section 7 |
| 2026-11-08 | The corrected mode becomes the default at the end; until then a switch chooses it, and afterwards the original's behaviour stays selectable (switch and environment variable; names to be proposed). | decision 13 |
| 2026-11-08 | First: `libc.a` stays the original. Revised the same day, after Claude's recommendation: the original archive stays untouched and is the original mode's library; the corrected mode links a corrected copy in which only measured runtime bugs are corrected (L-04, L-05 and what step F3 finds), each member with its interface kept, so that the copy also works with the original `cc`/`ld` on the hardware. | decision 14; steps F3, F3b |
| 2026-11-08 | `c1`/`c2` generate 80186/V30 code behind `-1`, for faster code on his V30; a closed list of three rules. | decision 15; section 7.4 |
| 2026-11-08 | A run with `-1` prints briefly which and how many 80186/V30 instructions were generated (counted by `mutos_as`, one line per assembly). | decision 16 |
| 2026-11-08 | The names, chosen from Claude's proposals with step F0: `-F` the corrected mode ("fixed"), `-H` the original's behaviour ("historic"), in `cc`, `as` and `ld`; the environment variable `MUTOS_MODE` with `corrected` or `original`. | `docs/TOOLCHAIN_PLAN.md`, section 6; `docs/RULES.md` |

Still the owner's to decide (`docs/TOOLCHAIN_PLAN.md`, sections 3, 6 and 7,
and `STATUS.md`, "Next up"): a port of `as` (recommendation in `STATUS.md`,
Milestone 2, "Step 5 of the plan"); whether the kernel's `arith.a` gets the
corrections of the long helpers too; which of step F4's corrections (C1-12 and
C1-13, found with F0, are among the candidates); `make install`; the manual
pages.

## The owner's setup

- An Ubuntu machine; the repository cloned at `~/mutos1700_c_cross_compiler`.
- The MUTOS 1700 kernel source tree at `~/sys` - `conf/`, `h/`, `io/`, `mdep/`,
  `sys/`, `cfg/` directly under it (confirmed 2026-11-07); builds in a copy,
  `~/sys_cross`. A second tree with the original build's products, the kernel
  included, at `~/mutossys/sys`. None of them is in the repository; the kernel's
  makefile is (`tests/mutos_ed/kernel/conf.Makefile`, without the dependency
  lines).
- The real MUTOS 1700 hardware (A7100) for golden runs: the goldens in `tests/`
  come from it, in rounds (the round-14 `fltprobe` goldens since 2026-10-12). Its
  8086 has been replaced by a NEC V30, and an IDE interface added (said
  2026-11-08): the machine for the hardware checks of the 80186/V30 code.
- The claude.ai project also holds short German summaries of steps 5 to 9, of
  the kernel build, of the plan of 2026-11-08 and of step F0 (`claude/step*_*.md`,
  `claude/kernelbuild_pruefung_2026-11-07.md`, `claude/plan_korrekturen_v30_2026-11-08.md`);
  their content is in `STATUS.md` and the development log, which are
  authoritative.

## Milestones of note

- 2026-11-02: steps 0 to 9 of `docs/TOOLCHAIN_PLAN.md` done - every tool but `as`
  is a port of its original.
- 2026-11-07: the MUTOS kernel, built on the owner's machine with its original,
  unchanged makefile through the cross toolchain, is the original kernel byte for
  byte (`docs/KERNEL_BUILD.md`; `STATUS.md`, "The MUTOS kernel, built with the
  cross toolchain").
- 2026-11-08: step F0 of section 7 - the switches, the rule log and register, the
  accounting, the diagnostic hooks; no output changed (`STATUS.md`, "Step F0";
  `docs/RULES.md`).
