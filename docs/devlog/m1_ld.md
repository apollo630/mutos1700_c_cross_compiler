# Milestone 1 — `mutos_ld` (linker)

Part of the development log; the index of all parts is [`docs/DEVLOG.md`](../DEVLOG.md). The linker: archive support and the MUTOS deviations of the `a.out` and `ld` format from stock V7.

---

**Status:** this part is the record of the first `mutos_ld` - a port of v7's
`ld.c` with the MUTOS deltas as they were known then (`mutos_ld.c`, deleted
2026-10-22) -, verified when the golden reference binaries (`myhello`, `idhello`) were
last present; they are missing from the checkout. Since 2026-10-20 the original `ld`
runs under the emulator (`originals_and_ports.md`, "The other originals as
oracles"), and since 2026-10-22 `mutos_ld` is a port of the original binary, checked
against it (`originals_and_ports.md`, "The port of `ld`"; `src/mutos_ld/README.md`).
See `STATUS.md` for the current status.

Verified byte-for-byte identical against three real hardware-linked binaries:
`myhello` (default FMAGIC), `idhello` (`-i`/IMAGIC, separate I&D), and end-to-end
against the real `libc.a` for archive/`-l` support. Archive/library support (`-l`,
`.a` files) is part of Milestone 1, not a later milestone.

## Archive / `-l` implementation

Three code paths in `getfile()`/archive handling:

1. **Plain archives without `__.SYMDEF`** — full linear scan via `step()`.
2. **Up-to-date ranlib `__.SYMDEF`** table of contents — fast `ldrand()`-driven symbol
   pull, repeated to a fixed point for transitive dependencies.
3. **Stale `__.SYMDEF`** (archive mtime newer than the embedded ranlib timestamp) —
   warns and falls back to linear scan. Freshly-uploaded archives almost always hit
   this path, since the host mtime becomes "now" on upload.

Fixed a use-after-free bug in `getfile()`: `cur_filname` must own stable storage
(`strdup`) since `archive_load1()`'s `stat()`/`error()` calls happen after the `-l`
path-resolution buffer would otherwise already be freed.

Reference test archive: real MUTOS `libc.a`, 72178 bytes, 167 object members,
`__.SYMDEF` table 264 entries / 3168 bytes, embedded ranlib timestamp decodes to
~1989 (always triggers the stale-fallback path on modern hosts).

## MUTOS-specific `a.out`/`ld` deviations from stock V7

(shared with `mutos_as` — the object format is common to both tools)

1. Relocation word bit 15 = 1-byte target-shift flag (x86 is byte-oriented, unlike
   the word-aligned PDP-11 the original V7 `ld` targeted) — the actual relocated word
   may sit 1 byte off from the reloc table's nominal 2-byte grid.
2. DATA/BSS-relative values (symbol table + content words) are pure segment-local
   offsets, **not** V7's "baked-in preceding segment size" convention.
3. `R_EXT` relocation symbol-index field is **11 bits** (bits 4–14), not 12 bits as
   in stock V7.
4. `nflag`/`iflag`'s additional stock-V7 64-byte rounding is a separate padding stage
   filled with zero bytes.
5. `ar` archive header's `long` fields (`atime`, `asize`) use **PDP-11 middle-endian**
   encoding (high 16-bit word first, each word itself little-endian) — unlike the
   pure-16-bit-LE `a.out` exec header, which has no `long` field at all and is not
   middle-endian (see `mutos_aout.h`'s own top-of-file comment).

**The text padding - found 2026-10-20 against the emulated original `ld`**: the
original appends **6 bytes** of `eb fe` (three `jmp $`) to the text, whatever its
length; `mutos_ld` (`g_tsize = (g_tsize + 7u) & ~7u`) rounds it up to a multiple of
8 instead. The `myhello` golden this rule came from (3,498 bytes of text, 3,504 in
the header) fits both; `idhello`'s 6 bytes of `eb fe` before its zero fill fit only
the original's. On fourteen compiled goldens linked both ways, nine differ by it
and by nothing else (every data/BSS address shifted by the difference), five are
identical. Left for the port of `ld` (`docs/TOOLCHAIN_PLAN.md`, step 2); details in
`docs/devlog/originals_and_ports.md`, "Step 0 of the plan: the infrastructure".

**The port (2026-10-22)** read the rest from the binary: the six bytes are counted
in the text size in `middle()` before `-i`'s 64-byte rounding (so `idhello`'s 3,504
bytes were 3,498 + 6), the relocation of a byte-shifted word is a function of its own
(`bytrel()`, the carry out of the low byte into the next word), the deviations 2 and
3 above are the original's, deviation 4 too; and the binary has more: symbol types
compared as words, `-n` the same as `-i`, the output under a temporary name in the
current directory. `docs/devlog/originals_and_ports.md`, "The port of `ld`".

---
