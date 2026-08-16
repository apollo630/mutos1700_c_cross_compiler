# Project Guide & Architecture Map

## 📂 Directory Structure & Context

### Core Toolchain for the MUTOS1700 Cross C Compiler(`/src`)
*   `/src/h/`: Core header files (`.h`). Contains shared definitions and system structures.
*   `/src/mutos_ld/`: Source code for the Mutos Linker.
*   `/src/mutos_as/`: Source code for the Mutos Assembler.
*   `/src/mutos_cc/`: Source code for the Mutos C Compiler frontend/driver.

### 🧪 Test Suites & Golden Masters (`/tests`)
*   `/tests/mutos1700_crt0/`: MUTOS1700 C runtime startup code (crt0), includes crt0.o.base64.txt.
*   `/tests/mutos1700_libc/`: MUTOS1700 libc.a, inclusing all object files and libc.a.base64.txt.
*   `/tests/mutos_as/kernel_nonopt/`: Golden Master test cases for the assembler (non-optimized builds), including *.golden_base64.txt 
*   `/tests/mutos_as/kernel_opt/`: Golden Master test cases for the assembler (optimized builds), including *.golden_base64.txt
*   `/tests/mutos_ld/`: Golden Master and integration tests for the linker.

### 📜 Legacy & Reference (`/v7` & Documentation)
*   `/v7/cc/`: Reference source code from Research Unix Version 7 C compiler.
*   `/v7/ld/`: Reference source code from Research Unix Version 7 Linker, but in part with MUTOS 1700 headers.
*   `/docs/`: Architecture notes, specifications, and design documents.
*   `/man/`: Manual pages for the Linux Cross-Compiler toolchain components.

---

## 🛠️ Development Rules for Claude

1.  **Context Alignment:** Before modifying code in `/src/mutos_<tool>/`, always check the corresponding test suite in `/tests/mutos_<tool>/` to understand the expected behavior and existing edge cases.
2.  **Golden Master Integrity:** Do not alter files in `/tests/.../kernel_opt/` or `kernel_nonopt/` unless explicitly instructed. These serve as our regression baseline.
3.  **Reference Material:** Use the `/v7/` directory strictly as historical reference. Do not mix V7 logic directly into `mutos` unless explicitly migrating or fixing compatibility bugs.
4.  **Header Files:** When changing structs or definitions in `/src/h/`, verify the impacts across `as`, `cc`, and `ld` simultaneously.

