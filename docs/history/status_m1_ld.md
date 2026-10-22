# `STATUS.md` history — Milestone 1 (`mutos_ld`), the port of v7's `ld.c`

Section moved here from `STATUS.md` on 2026-10-22, unchanged: the record of `mutos_ld.c` (deleted 2026-10-22, the last version in commit `36e69a5`), a port of v7's `ld.c` with the MUTOS deltas as they were known then, which was replaced by a port of the original binary. It was true when written ("this session" means that session); the current state is in [`STATUS.md`](../../STATUS.md), the narrative in [`docs/devlog/`](../devlog/). Where it describes the original `ld`, the port's reading of the binary has the final word (`src/mutos_ld/README.md`).

---

**Status: COMPLETE as a port of v7's `ld.c`** *(carried from prior session records -
its goldens are missing, see below)*; **to be ported from the original binary**
(`docs/TOOLCHAIN_PLAN.md`, decision 5 and step 2).

`mutos_ld.c` (`src/mutos_ld/`, ~1180 lines) reimplements the V7 `ld` linker with the
MUTOS-specific `a.out` deviations (see "Implementation summary" below), including
full archive (`.a` / `-l`) support.

### Verified 2026-10-20
- `mutos_ld.c` compiles cleanly (`cc -std=c11 -Wall -Wextra -Wpedantic -O2`), zero
  warnings.
- The reference archive is present and intact as `tests/mutos1700_libc/libc.a.base64.txt`
  (decodes to 72,178 bytes, 168 members: `__.SYMDEF` and the 167 objects).
- Linking assembled compiler goldens (`08_float`, `10_integ`, `04_funcs`) with the
  real `crt0.o` and `libc.a` works; with the archive's modification time newer
  than its `__.SYMDEF` time (any freshly decoded copy) `mutos_ld` warns
  `ld:libc.a(__.SYMDEF): out of date (warning)` and scans linearly - the output was
  the same as with the table on all 14 programs tried.
- **Against the original `ld`**, run under `tests/realbin/mutosemu.py` (first time):
  - `-lX` searches `/lib/libX.a`, then `/usr/lib/libX.a`, never the current
    directory. `mutos_ld` (`resolve_dash_l()`) searches `./libX.a`, then
    `/usr/lib/libX.a` - a deviation, removed by the port (`-l` then under
    `$MUTOS_ROOT`).
  - The original refuses `-r` and `-d` (`ld: -r and -d flags not supported`, exit
    status 4); `mutos_ld` implements both - kept as a documented extension.
  - **Its output, compared for the first time** (step 0 of the plan: the emulator now
    serves the `fstat`, `link`, `chmod` and `umask` it asks for, so it leaves its
    `a.out`): fourteen compiled goldens (`10_integ`'s five, `08_float/01_floatbas`
    and `02_dblconv`, `04_funcs`'s seven), assembled by `mutos_as`, linked `-X` with
    `crt0.o` and `-lc` by both: **5 byte-identical, 9 differing in one thing - the
    padding after the text.** The original appends 6 bytes of `eb fe` (three `jmp $`)
    whatever the text's length; `mutos_ld` rounds it up to a multiple of 8. All else
    follows from it: every data/BSS address, in the text and in the symbol table,
    shifted by the difference (checked word by word). `mutos_ld`'s rule came from the
    `myhello` golden (3,498 bytes of text, 3,504 in its header), which fits both;
    `idhello`'s 6 bytes of `eb fe` before its zero fill fit only the original's. For
    the port (step 2); `mutos_ld` is unchanged.
  - **`libc.a`'s time**: the original warns `ld:/lib/libc.a(__.SYMDEF): out of date
    (warning)` for an archive newer than its `__.SYMDEF` time and is silent at that
    time (602282477); `mutos_root/lib/libc.a` carries it, and `mutos_ld` and the
    original both link through the root without the warning, both warn with a newer
    copy (`tests/mutos_root/check_root.py`, in `make test`; the original with
    `--realbin`, in `make check-realbin`).

### NOT verified this session
- **The golden reference binaries the "byte-for-byte identical" claim rests on
  (`myhello`/`idhello`, default-FMAGIC and `-i`/IMAGIC links) are not in this
  checkout**, nor is a `tests/mutos_ld/` directory. The
  linker's output has not been compared with real linked binaries since. Step 2 of
  the plan replaces those goldens with the emulated original `ld` as the oracle.

### Implementation summary (prior session records)
- Three archive code paths in `getfile()`: (1) plain archive, no `__.SYMDEF` → full
  linear scan; (2) up-to-date `__.SYMDEF` → fast `ldrand()`-driven symbol pull to a
  fixed point; (3) stale `__.SYMDEF` (archive mtime newer than the embedded ranlib
  timestamp — the common case for any freshly-uploaded archive) → warn + linear-scan
  fallback.
- MUTOS-specific `a.out`/relocation deviations from stock V7, applied throughout:
  1. Relocation word bit 15 = 1-byte target-shift flag (x86 is byte-oriented, unlike
     the word-aligned PDP-11 the original V7 `ld` targeted).
  2. DATA/BSS-relative values are pure segment-local offsets, not V7's "baked-in
     preceding segment size" convention.
  3. `R_EXT` relocation symbol-index field is 11 bits (bits 4–14), not 12.
  4. `nflag`/`iflag` 64-byte rounding is a separate zero-filled padding stage.
  5. `ar` archive header `long` fields use PDP-11 middle-endian encoding (high
     16-bit word first, each word itself little-endian).
