## Overview

This project focuses on the development of a historically accurate cross-compiler toolchain built on modern Linux (x86_64) that generates executable code for the **MUTOS 1700** operating system. 

MUTOS 1700 was a lesser-known operating system for the East German (DDR) office computers **Robotron A7100** and **A7150**. It is based on **Version 7 Unix (V7)** and keeps V7's system call numbers and `a.out` object format, but runs on a 16-bit x86 architecture (Intel 8086 / K1810WM86) instead of the PDP-11.

### Project Goals & Philosophy

* **100% V7 Compatibility**: Modernized to run on current systems while remaining strictly faithful to the original Unix V7 philosophy.
* **Historical Baseline**: Built on the historic V7 sources (Dennis Ritchie’s compiler `cc`, the linker `ld`, John F. Reiser’s preprocessor `cpp`) and on the original MUTOS 1700 binaries themselves: the preprocessor `cpp`, the compiler passes `c0` and `c1`, the optimizer `c2`, the driver `cc`, the linker `ld`, the archiver `ar`, the archive indexer `ranlib`, `make`, `nm`, `size`, `strip`, `tsort` and the editors `ed` and `em` are ports of those binaries, read with the V7 sources as the scaffold and checked against the originals running under an 8086 emulator, and `lorder` is the original shell script. The assembler `as` is a re-implementation, measured against the original by a differential comparison; whether it is ported too is still to be decided.
* **Cross-Compilation**: Seamlessly compile legacy 16-bit x86 UNIX binaries directly from modern 64-bit Linux environments.
* **Proven on the MUTOS kernel** (2026-11-07): the MUTOS 1700 kernel, built on Linux with its original, unchanged makefile, is the kernel built on the hardware byte for byte - see [`docs/KERNEL_BUILD.md`](docs/KERNEL_BUILD.md).
* **Beyond the originals, only behind a switch** (since 2026-11-08): a corrected mode (`-F`) in which bugs of the original tools and of `libc.a`'s runtime are corrected - it is to become the default at the end, the original's behaviour staying selectable (`-H`) - and 80186/NEC V30 code (`-1`), run on an A7100 with a V30. Without a switch every tool does what the original does. Every such change is listed in [`docs/RULES.md`](docs/RULES.md).

### Where to read on

* [`STATUS.md`](STATUS.md) — what is built and verified, tool by tool.
* [`docs/KERNEL_BUILD.md`](docs/KERNEL_BUILD.md) — building the MUTOS kernel with the cross toolchain, step by step.
* [`docs/TOOLCHAIN_PLAN.md`](docs/TOOLCHAIN_PLAN.md) — the decisions and the order of the work ahead.
* [`CLAUDE.md`](CLAUDE.md) — the project guide: layout, rules, roadmap.
* [`docs/DEVLOG.md`](docs/DEVLOG.md) — the development log (index of `docs/devlog/`).
* [`docs/MUTOS_C_ABI.md`](docs/MUTOS_C_ABI.md), [`docs/MUTOS_ORIGINAL_BUGS.md`](docs/MUTOS_ORIGINAL_BUGS.md) — the calling convention, and the bugs found in the original binaries.
* [`docs/CPU_REFERENCE.md`](docs/CPU_REFERENCE.md) — the 8086, the 80186 and the NEC V30: their differences, the forms `-1` writes, the V30's clocks.
* [`mutos/`](mutos/README.md) — the original MUTOS 1700 files: binaries, header files, manual pages.

### License

The project's own work is under the MIT License ([`LICENSE`](LICENSE)). The parts
taken from UNIX Version 7, and the ports built on them, keep the Caldera license
([`LICENSES/Caldera.txt`](LICENSES/Caldera.txt)) - this product includes software
developed or owned by Caldera International, Inc.

The original MUTOS 1700 files are historical files from the German Democratic
Republic (GDR), of the VEB Kombinat Robotron. They are provided exclusively for
archival, scientific and educational purposes, are the reference of the tests, and
are not licensed by the project. If a holder of rights in them objects, they will
be removed immediately - please open an issue. Which files are which:
[`NOTICE.md`](NOTICE.md).
