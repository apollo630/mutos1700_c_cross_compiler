## Overview

This project focuses on the development of a historically accurate cross-compiler toolchain built on modern Linux (x86_64) that generates executable code for the **MUTOS 1700** operating system. 

MUTOS 1700 was a lesser-known operating system for the East German (DDR) office computers **Robotron A7100** and **A7150**. While it is binary- and architecture-compatible with **Version 7 Unix (V7)**, it targets a 16-bit x86 architecture (Intel 8086 / K1810WM86).

### Project Goals & Philosophy

* **100% V7 Compatibility**: Modernized to run on current systems while remaining strictly faithful to the original Unix V7 philosophy.
* **Historical Baseline**: Built directly from the original source code of Dennis Ritchie’s historic V7 compiler (`cc`), assembler (`as`), and linker (`ld`).
* **Cross-Compilation**: Seamlessly compile legacy 16-bit x86 UNIX binaries directly from modern 64-bit Linux environments.
