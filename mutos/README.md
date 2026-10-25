# `mutos/` — the original MUTOS 1700 files

Every file in this directory comes from MUTOS 1700 itself (moved here on
2026-10-25; until then `mutos_bin/` sat at the top of the repository). It
is reference data under `CLAUDE.md`'s Golden Master Integrity rule
(Workflow Guideline 2): never regenerate, re-encode, reformat or
otherwise alter it - a file that is wrong is replaced from the hardware,
not edited.

| Directory | What |
|---|---|
| [`mutos_bin/`](mutos_bin/README.md) | the binaries, as base64 text (`<name>.golden.base64.txt`): the toolchain `as`, `c0`, `c1`, `c2`, `cc`, `cpp`, `ld`; `make`, `ar`, `ranlib`, `nm`, `size`, `strip`, `tsort` and the shell script `lorder`; the editors `ed`, `em`. Headers and provenance in its `README.md`. |
| `mutos_include/usr/include/` | the header files C programs include (`stdio.h`, `a.out.h`, `ar.h`, ...), with `sys/` and `local/` |
| `mutos_include/sys/h/` | the kernel's header files; `tests/mutos_cpp/h/` is a copy of them, file for file |
| `mutos_man/` | manual pages (German): `as`, `cc`, `ld`, `make`, `ar`, `ranlib`, `lorder`, `tsort`, `nm`, `size`, `strip`, `ed` (section 1), `a.out`, `ar`, `types`, `filsys` (section 5) |

What the files showed when they came - the two `sys` directories
compared, v7's header copies against these, the manuals against the
binaries - is in `docs/devlog/originals_and_ports.md`, "The originals of
2026-10-25".

## The headers and the MUTOS root

`make` copies `mutos_include/usr/include/` as it is into the MUTOS root's
`usr/include/` (`scripts/mutos_root.sh`; `docs/TOOLCHAIN_PLAN.md`,
decisions 2 and 3), where the tools look up `/usr/include` - so a source
that includes `<stdio.h>` compiles against the original headers. A file
is rewritten there only when it differs; edit nothing in the root, it is
rebuilt from here. The kernel's `sys/h/` is not copied.

## Reading the manual pages

The pages `<name>.<section>.d` are in a terminal format: `ESC G` and
`ESC H` around highlighted text, `ESC 9` and `ESC 8` around subscripts,
and overstrikes with backspace - umlauts as letter, backspace, `"`
(`a`, BS, `"` is `ä`), `ß` as `p`, BS, `8`, underlining as `_`, BS,
letter. To read one as plain text (umlauts as `ae`, `oe`, `ue`):

```sh
sed -e 's/\x1b[GH89]//g' -e 's/p\x088/ss/g' \
    -e 's/\([aouAOU]\)\x08"/\1e/g' -e 's/.\x08//g' mutos/mutos_man/ld.1.d
```
