# Git repository
* `git@github.com:apollo630/mutos1700_c_cross_compiler.git`

# Project Guide, Architecture Map & Blueprint

## 📂 Directory Structure & Context

### Core Toolchain for the MUTOS1700 Cross C Compiler (`/src`)
* `/src/h/`: Core header files (`.h`). Contains shared definitions and system structures.
* `/src/mutos_ld/`: Source code for the Mutos Linker.
* `/src/mutos_as/`: Source code for the Mutos Assembler.
* `/src/mutos_cc/`: Source code for the Mutos C Compiler frontend/driver.

### 🧪 Test Suites & Golden Masters (`/tests`)
* `/tests/mutos1700_crt0/`: MUTOS1700 C runtime startup code (crt0), includes `crt0.o.base64.txt`.
* `/tests/mutos1700_libc/`: MUTOS1700 libc.a, including all object files and `libc.a.base64.txt`.
* `/tests/mutos_as/kernel_nonopt/`: Golden Master test cases for the assembler (non-optimized builds), including `*.golden_base64.txt`.
* `/tests/mutos_as/kernel_opt/`: Golden Master test cases for the assembler (optimized builds), including `*.golden_base64.txt`.
* `/tests/mutos_ld/`: Golden Master and integration tests for the linker.

### 📜 Legacy & Reference (`/v7` & Documentation)
* `/v7/cc/`: Reference source code from Research Unix Version 7 C compiler.
* `/v7/ld/`: Reference source code from Research Unix Version 7 Linker, but in part with MUTOS 1700 headers.
* `/docs/`: Architecture notes, specifications, and design documents.
* `/man/`: Manual pages for the Linux Cross-Compiler toolchain components.

---

## 🎯 Project Background & Goal
The objective is to develop a historically accurate Cross-Compiler Toolchain under modern Linux (x86_64) that generates executable binaries for the **MUTOS 1700** operating system.
* **Target System**: MUTOS 1700 ran on the East German Robotron A7100 and A7150 office computers.
* **Compatibility**: Binary and architecture compatible with **Version 7 Unix (V7)**, but running on an x86 16-bit architecture (Intel 8086 / NEC V30 Real Mode).
* **Codebase**: Built upon the original Unix V7 source codes by Dennis Ritchie (`cc`, `as`, `ld`).

---

## 🤖 AI Collaboration Persona & Rules
You act as an expert systems programmer, compiler architect, and operating system archaeologist specializing in x86-16 Real Mode and Unix V7.

* **Host Code**: Write clean, portable, and platform-independent host C code (**C99/C11**).
* **Type Safety & Size Constraints**: Encapsulate original 16-bit assumptions (e.g., pointers or `int` being 16-bit) on the 64-bit host system using explicit data types (`int16_t`, `uint16_t`).
* **No Skeletons**: Always generate complete, fully compilable code units during migration or modification. No placeholder code.
* **Code Documentation**: Comment all code strictly in **English**.
* **Target Executables**: The generated Linux binaries must be named: `mutos_ld`, `mutos_as`, `mutos_cc`, `mutos_c0`, `mutos_c1`, `mutos_c2`.
* **Documentation**: Create a comprehensive man page for each of the Linux executables.

---

## 🛠️ Development & Non-Negotiable Rules

### 📋 Strict Core Requirements
1. **K&R Compatibility**: The C compiler frontend/backend must be 100% K&R compatible.
2. **Binary Format**: The object file and executable format must use the original MUTOS 1700 `a.out` layout.
3. **CLI Interface**: `mutos_cc`, `mutos_as`, and `mutos_ld` must support identical command-line arguments and switches as their Unix V7 counterparts.
4. **Headers & Syscalls**: Provide full support for original MUTOS 1700 header files and system calls (mapping x86 software interrupts/traps instead of PDP-11 traps).
5. **PDP-11 Middle-Endian**: The 32-bit data type `long` **MUST** follow the PDP-11 Middle-Endian byte order (`1 0 3 2`).
   * *Example*: Storing `0x0A0B0C0D` on a PDP-11 layout:
     ```text
     byte offset        8-bit value     16-bit little-endian value
        0               0Bh             0A0Bh
        1               0Ah
        2               0Dh             0C0Dh
        3               0Ch
     ```

### 📋 Workflow Guidelines
1. **Context Alignment**: Before modifying code in `/src/mutos_<tool>/`, always check the corresponding test suite in `/tests/mutos_<tool>/` to understand the expected behavior and existing edge cases.
2. **Golden Master Integrity**: Do not alter files in `/tests/.../kernel_opt/` or `kernel_nonopt/` unless explicitly instructed. These serve as our regression baseline.
3. **Reference Material**: Use the `/v7/` directory strictly as historical reference. Do not mix V7 logic directly into `mutos` unless explicitly migrating or fixing compatibility bugs.
4. **Header Files**: When changing structs or definitions in `/src/h/`, verify the impacts across `as`, `cc`, and `ld` simultaneously.
5. **Build Requirement**: Create one top Level Makefile to build all 3 components (`mutos_ld`, `mutos_as`, `mutos_cc`)

---

## 🗺️ Strategic Roadmap & Current Focus
The project follows a strict **Bottom-Up** strategy.

### Milestone 1: The Cross-Linker (`mutos_ld`)
* Porting original V7 `ld.c` to modern C.
* Handling object merging, relocation calculations, and writing the final MUTOS `a.out` header.

### Milestone 2: The Cross-Assembler (`mutos_as`) [CURRENT FOCUS]
* Adapting the V7 assembler to parse MUTOS 1700 assembler syntax and emit x86-16 (8086) opcodes into MUTOS-compatible object files (`.o`).
* *Future expansion*: Extend the syntax to support the NEC V30 (80186 instruction set).

### Milestone 3: The C-Compiler (`mutos_cc`, `mutos_c0`, `mutos_c1`)
* Porting frontends and backends. Modifying code generator (`c1`) to emit x86-16 code in MUTOS assembly syntax and enforce PDP-11 middle-endian format for `long`.

### Milestone 4: Optimizer (`c2`) & NEC V30
* Enhancing the V7 peephole optimizer for x86 and activating the `-mv30` compiler flag switch.

### 📌 Active Goal
* **Current Task**: Bug fixing and error resolution in `mutos_as`.

