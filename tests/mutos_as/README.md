# `tests/mutos_as/` — the "-L" real-hardware experiment (concluded)

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
`STATUS.md`'s open item 6 named this outright: *"Whether the kernel
goldens were assembled with `-L`... is not known."* This directory's two
`Makefile.mutos` files exist to settle it with real hardware evidence
instead of another guess — the same standard the rest of this project
holds itself to.

## Result (2026-09-28)

**The experiment concluded before a single golden was needed.** On real
MUTOS 1700 hardware, the very first `-L` invocation attempted —
`as -L -o v30ide.oL v30ide.s` — printed:

```
Unknown option L ignored.
```

...and proceeded to assemble `v30ide.s` anyway, with `-L` simply dropped.
**The real MUTOS 1700 `as` binary does not implement `-L` at all**, despite
`docs/MUTOS1700_Assembler_as.pdf` sect. 3.1 documenting it as a real
switch — a manual/binary mismatch this project cannot resolve (it has no
way to know whether a different `as` build once supported it, or the
manual was always aspirational).

Since the flag is silently ignored rather than merely defaulted, `as` and
`as -L` are necessarily byte-identical on this hardware for any input —
there is nothing to diff, so **no `.oL` goldens were generated**, and
`kernel_opt/Makefile.mutos`'s/`kernel_nonopt/Makefile.mutos`'s `L:` targets
were not run to completion (the `noL:` targets were not run either, since
the question they existed to help answer was already settled).

This rules out **"the kernel build invoked `as -L`"** as the explanation
for the `L`-label discrepancy — the flag cannot have been the difference,
because it has exactly one behavior on this hardware regardless of how
it's invoked. **It does not resolve the discrepancy itself**: every kernel
golden still carries `L`-number labels while none of `libc.a`'s 167
objects do. See `STATUS.md`'s open item 6 for where this leaves things —
the next angle is source-level (whether the *compiler* emits `L`-labels
only under conditions the lost `libc.a` sources never met), not
flag-level. `CLAUDE.md`'s "Known, deliberate exception" for `mutos_as`'s
own `-L` has been updated with this finding too; `mutos_as`'s
always-emit-`L`-labels behavior is unaffected and remains the right choice
for golden parity.

## What the two `Makefile.mutos` files do (for the record)

`kernel_opt/Makefile.mutos` and `kernel_nonopt/Makefile.mutos` were written
to assemble every real kernel `*.s` already in that directory with the
**real** `as`, twice:

- `make -f Makefile.mutos noL` → `<name>.o`, assembled *without* `-L`.
- `make -f Makefile.mutos L` → `<name>.oL`, assembled *with* `-L`.

Both use a genuine two-suffix rule (`.s.o:` / `.s.oL:`, confirmed real-
`make(1)`-compatible — see `tests/mutos_cc/Makefile.mutos`'s own header)
instead of one explicit target per file, so the file itself stays tiny no
matter how many `*.s` files exist; `kernel_opt`'s 61 automated files (62
minus the `mch_insw_outsw.s` exception below) would otherwise risk the
same `Make: out of memory` failure `tests/mutos_cc`'s original all-in-one
`Makefile.mutos` hit on real hardware (`docs/DEVLOG.md`'s Milestone 4
host-tooling findings) — that failure came from ~124 explicit per-file
recipe blocks exhausting a fixed-size internal table, not from file count
by itself, which a shared suffix rule sidesteps. Both files remain in the
repo, unmodified, as working infrastructure — should a future question
ever need a full with/without-`-L` object comparison for some other reason
(unlikely, given the result above, but they cost nothing to keep) — but
neither their reliability at 61 explicit-list files nor the `L:` suffix
rule's real behavior was exercised, since the manpage-vs-binary mismatch
made a full run unnecessary.

### `mch_insw_outsw.s` — the one exception

Its stem is already 14 characters, so even its plain `.s` name (16
characters) already exceeds the real MUTOS 1700 `DIRSIZ=14` filename
limit (`CLAUDE.md`'s "Identifier length limits") — a pre-existing fact
about this one file, unrelated to this experiment. It was deliberately
left out of both `Makefile.mutos noL`/`L` lists. Had the experiment
proceeded to a full run, it would have been assembled by hand under a
short on-device alias:

```sh
# on real MUTOS 1700 hardware, inside kernel_opt/:
cp mch_insw_outsw.s mio14.s
as        -o mio14.o  mio14.s
as -L -o  mio14.oL mio14.s
# copy mio14.o / mio14.oL back to the modern host, THEN:
mv mio14.o  mch_insw_outsw.o
mv mio14.oL mch_insw_outsw.oL
```

This was never actually needed, since `v30ide.s` alone already answered
the `-L` question — kept here only so the reasoning isn't lost.

## `./Makefile` (Linux-side, for the record)

A separate, GNU-Make-only `./Makefile` (same split as `tests/mutos_cc/Makefile`
vs. `Makefile.mutos` — the real MUTOS 1700 `make(1)` cannot run it) exists to
turn any `<name>.o`/`<name>.oL` pair into `<name>.o_base64.txt`/
`<name>.oL_base64.txt` companions (an underscore, not a dot, right before
`base64` — deliberately not `*.o.golden`-shaped, per `CLAUDE.md`'s Golden
Master Integrity rule, since these were always "candidates" pending a
diff, never confirmed goldens). It was smoke-tested locally and never
exercised for real, since no `.o`/`.oL` pair was ever produced. It is kept
for the same reason as the `Makefile.mutos` files above.
