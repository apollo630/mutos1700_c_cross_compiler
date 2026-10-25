## Overview

This project focuses on the development of a historically accurate cross-compiler toolchain built on modern Linux (x86_64) that generates executable code for the **MUTOS 1700** operating system. 

MUTOS 1700 was a lesser-known operating system for the East German (DDR) office computers **Robotron A7100** and **A7150**. While it is binary- and architecture-compatible with **Version 7 Unix (V7)**, it targets a 16-bit x86 architecture (Intel 8086 / K1810WM86).

### Project Goals & Philosophy

* **100% V7 Compatibility**: Modernized to run on current systems while remaining strictly faithful to the original Unix V7 philosophy.
* **Historical Baseline**: Built on the historic V7 sources (Dennis Ritchie’s compiler `cc`, the linker `ld`, John F. Reiser’s preprocessor `cpp`) and on the original MUTOS 1700 binaries themselves: the preprocessor `cpp`, the compiler passes `c0` and `c1`, the optimizer `c2`, the driver `cc` and the linker `ld` are ports of those binaries, read with the V7 sources as the scaffold and checked against the originals running under an 8086 emulator; `ar` and `ranlib` are to follow the same way, and whether the assembler `as` does is decided after a differential comparison with the original.
* **Cross-Compilation**: Seamlessly compile legacy 16-bit x86 UNIX binaries directly from modern 64-bit Linux environments.

### Where to read on

* [`STATUS.md`](STATUS.md) — what is built and verified, tool by tool.
* [`docs/TOOLCHAIN_PLAN.md`](docs/TOOLCHAIN_PLAN.md) — the decisions and the order of the work ahead.
* [`CLAUDE.md`](CLAUDE.md) — the project guide: layout, rules, roadmap.
* [`docs/DEVLOG.md`](docs/DEVLOG.md) — the development log (index of `docs/devlog/`).
* [`docs/MUTOS_C_ABI.md`](docs/MUTOS_C_ABI.md), [`docs/MUTOS_ORIGINAL_BUGS.md`](docs/MUTOS_ORIGINAL_BUGS.md) — the calling convention, and the bugs found in the original binaries.
* [`mutos/`](mutos/README.md) — the original MUTOS 1700 files: binaries, header files, manual pages.
