# `tests/mutos1700_arith/` — the MUTOS kernel's `arith.a`

The archive `conf/arith.a` of the original MUTOS 1700 kernel tree (`/usr/sys`
on MUTOS), which no source of the tree makes; the kernel's makefile links it
by its path after the kernel's objects (`docs/KERNEL_BUILD.md`). Added by the
project owner on 2026-11-09 from his tree, as base64 text like
`tests/mutos1700_libc/` - precious reference data, never to be regenerated
or re-encoded (`CLAUDE.md`).

| File | What it is |
|---|---|
| `arith.a.base64.txt` | the archive: seven members, all dated Apr 10 22:12 1989, no table of contents (`__.SYMDEF`) |
| `locking.o.base64.txt` | the kernel's file locking: `_locking`, `_locked`, `_unlckf`, `_deadloc`, `_lockadd`, `_lockall`, `_lockfre` |
| `aldiv.o`, `almul.o`, `alrem.o`, `ldiv.o`, `lmul.o`, `lrem.o` `.base64.txt` | the long helpers (`docs/MUTOS_C_ABI.md` 1.8) - `libc.a`'s members of the same names byte for byte, bugs L-04 and L-08 included (`docs/MUTOS_ORIGINAL_BUGS.md`) |

Each `.o` file is the archive's member of that name byte for byte.

Used by:

- `src/libc_fixed/` (step F3b): `make` builds the corrected copy
  `src/libc_fixed/arith.a` from `arith.a.base64.txt` - `aldiv.o`, `ldiv.o`,
  `alrem.o`, `lrem.o` replaced by the corrected ones, everything else, the
  headers included, as it is; `check_copy.py` checks it (`make test`);
- `tests/libc_runtime/rtcheck.py --archive
  tests/mutos1700_arith/arith.a.base64.txt long` (the original's long
  helpers) and `--copy src/libc_fixed/arith.a long` (the copy), both in
  `make check-runtime`.
