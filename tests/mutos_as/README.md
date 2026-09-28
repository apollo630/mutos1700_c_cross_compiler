# `tests/mutos_as/` — the "-L" real-hardware experiment

`kernel_opt/` and `kernel_nonopt/` each hold a real-hardware-linked
`<name>.o.golden` per `<name>.s` (real, non-optimized and optimized MUTOS
1700 `c1` kernel output — see each `.s`'s own header). Every one of those
already-committed goldens carries compiler-internal `L`-number labels in
its symbol table. `as.1`'s documented default **excludes** those labels
unless `-L` is given (`docs/MUTOS1700_Assembler_as.pdf` sect. 3.1); `mutos_as`
currently always writes them regardless of any flag, a "Known, deliberate
exception" recorded in `CLAUDE.md` because implementing the documented
default broke byte-for-byte golden parity against these very files.

That exception was based on only one side of the evidence. Since
2026-09-27 there is a counterpart: none of `libc.a`'s 167 real linked
objects — compiled C included — has an `L`-number label, which is what
the documented default actually predicts. So either the kernel build
invoked `as -L` and the libc build didn't, or something else is going on.
`STATUS.md`'s open item 6 names this outright: *"Whether the kernel
goldens were assembled with `-L`... is not known."* This directory's two
`Makefile.mutos` files exist to settle it with real hardware evidence
instead of another guess — the same standard the rest of this project
holds itself to.

## What the two `Makefile.mutos` files do

`kernel_opt/Makefile.mutos` and `kernel_nonopt/Makefile.mutos` each
assemble every real kernel `*.s` already in that directory with the
**real** `as`, twice:

- `make -f Makefile.mutos noL` → `<name>.o`, assembled *without* `-L`.
- `make -f Makefile.mutos L` → `<name>.oL`, assembled *with* `-L`.

Run both, on real MUTOS 1700 hardware / an accurate emulator, from inside
each directory. Both use a genuine two-suffix rule (`.s.o:` / `.s.oL:`,
confirmed real-`make(1)`-compatible — see `tests/mutos_cc/Makefile.mutos`'s
own header) instead of one explicit target per file, so the file itself
stays tiny no matter how many `*.s` files exist; `kernel_opt`'s 61
automated files (62 minus the exception below) would otherwise risk the
same `Make: out of memory` failure `tests/mutos_cc`'s original all-in-one
`Makefile.mutos` hit on real hardware (`docs/DEVLOG.md`'s Milestone 4
host-tooling findings) — that failure came from ~124 explicit per-file
recipe blocks exhausting a fixed-size internal table, not from file count
by itself, which a shared suffix rule sidesteps.

**Neither has been run on real MUTOS 1700 hardware yet.** Both are
infrastructure only, following the same constraints and workarounds
`tests/mutos_cc`'s already-verified `Makefile.mutos` files established
(two-suffix rules, one shell line per recipe), but that does not make
them verified — treat them as unverified until a real run confirms they
work as designed.

### `mch_insw_outsw.s` — the one exception

Its stem is already 14 characters, so even its plain `.s` name (16
characters) already exceeds the real MUTOS 1700 `DIRSIZ=14` filename
limit (`CLAUDE.md`'s "Identifier length limits") — a pre-existing fact
about this one file, unrelated to this experiment. It is deliberately
left out of both `Makefile.mutos noL`/`L` lists. Assemble it by hand
under a short on-device alias, and only rename the result to its
descriptive name after copying it back to the modern host (renaming it
to the long name while still on MUTOS 1700 would silently truncate or
fail under `DIRSIZ`):

```sh
# on real MUTOS 1700 hardware, inside kernel_opt/:
cp mch_insw_outsw.s mio14.s
as        -o mio14.o  mio14.s
as -L -o  mio14.oL mio14.s
# copy mio14.o / mio14.oL back to the modern host, THEN:
mv mio14.o  mch_insw_outsw.o
mv mio14.oL mch_insw_outsw.oL
```

## Turning the results into inspectable text

After copying every `*.o`/`*.oL` produced above back into `kernel_opt/`
and `kernel_nonopt/` respectively, run (from this directory, on the
modern/Linux side):

```sh
make base64
```

This is a **separate, GNU-Make-only** `./Makefile` (same split as
`tests/mutos_cc/Makefile` vs. `Makefile.mutos` — the real MUTOS 1700
`make(1)` cannot run it). It turns each `<name>.o`/`<name>.oL` into a
`<name>.o_base64.txt`/`<name>.oL_base64.txt` companion — the same
`<file>_base64.txt` naming `./mk_goldenbase64.sh` already uses for this
directory's existing `*.o.golden` files, so an underscore (not a dot)
right before `base64` marks these apart from a real golden's own
companion at a glance. It deliberately does **not** rename or copy
anything to `*.o.golden` — see the next section.

## Interpreting the result

Diff each fresh `<name>.o` and `<name>.oL` against the already-committed
`<name>.o.golden`. Three outcomes:

- **`<name>.o` matches, `<name>.oL` doesn't** — the kernel build used the
  documented default (no `-L`); the already-committed goldens are
  themselves anomalous, or `mutos_as`'s current always-emit-`L`-labels
  behavior needs a different explanation than "`-L` was used".
- **`<name>.oL` matches, `<name>.o` doesn't** — the kernel build used
  `-L`; `mutos_as` should properly implement `-L` (default excluded,
  `-L` includes) instead of always writing labels unconditionally, and
  the future `mutos_cc` driver needs to invoke `mutos_as -L` for kernel
  builds.
- **Neither matches, or the two golden directories disagree with each
  other** — the discrepancy is not `-L` alone; needs its own
  investigation before touching `mutos_as`'s exception.

Whichever variant matches is real hardware evidence. Promoting it to
replace or corroborate the existing `<name>.o.golden` is a **separate,
deliberate manual step** once that comparison is actually done — never
automated by either Makefile here, per `CLAUDE.md`'s Golden Master
Integrity rule (Workflow Guideline 2): a `.o.golden` is precious
reference data, not a build byproduct to overwrite in a routine `make`
run. Update `STATUS.md`'s open item 6 and `CLAUDE.md`'s `-L` exception
with whatever this settles.
