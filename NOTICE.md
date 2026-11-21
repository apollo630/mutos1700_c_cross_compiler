# Licensing and notices

This repository holds material of three origins, under different terms. The
project's own work is free under the MIT License; what comes from UNIX Version 7
keeps the Caldera license it was released under; what comes from MUTOS 1700
itself is not the project's to license and is not licensed by it. This file says
which is which. It is not legal advice.

## 1. The project's own work - the MIT License

Everything written for this project and not listed in sections 2 and 3 is
Copyright (c) 2026 The MUTOS 1700 Cross-Compiler Toolchain authors and is licensed
under the MIT License, [`LICENSE`](LICENSE). The authors are those of the
repository's history. That is in particular:

- the assembler `src/mutos_as/` (a re-implementation, not a port), `src/h/`, and
  the rules of the corrected mode and of `-1` (`src/mutos_cc/c1rules/`,
  `src/mutos_cc/c2rules/`, `src/mutos_ld/ldrules/`, `src/mutos_as/asrules/`);
- the build (`Makefile`s), `scripts/`, the test tools and test programs written
  for the project in `tests/` (the emulator, the simulators, the fuzzers, the
  checks, the kits for the hardware), and the tools in `src/libc_fixed/`;
- the documentation: the Markdown documents and the manual pages in `man/`;
- the IDE driver for the NEC V30 expansion of the A7100 (below).

### The V30 IDE driver

The driver for the IDE adapter of the project owner's A7100 (an expansion board
that also switches in the NEC V30) is the owner's own work, written for his MUTOS
1700 kernel: `mutos/mutos_include/sys/h/v30ide.h`, and in `tests/` its copy
`tests/mutos_cpp/h/v30ide.h`, the driver `tests/mutos_cpp/c/v30ide.c`, and what the
toolchain made of it (`tests/mutos_cpp/c/v30ide.i.golden`,
`tests/mutos_as/kernel_opt/v30ide.s`, `v30ide.o.golden` and
`v30ide.o.golden_base64.txt`). It is under the MIT License as above.

Some parts of it were taken from V7/x86, Robert Nordier's port of UNIX Version 7
to the PC (www.nordier.com; its `/usr/sys/dev/hd.c`). Those parts remain his work:
his notice, "Copyright (c) 2006 Robert Nordier. All rights reserved.", stays in
the files, and they are used under the terms of V7/x86, which the MIT License of
the rest does not change. What V7/x86 has of UNIX Version 7 is under section 2's
license.

## 2. UNIX Version 7 - the Caldera license

- `v7/`: sources of UNIX Version 7.
- The ports in `src/` that are built on V7's sources: `src/mutos_cc/c0port/`,
  `c1port/`, `c2port/`, `ccport/`, and `src/mutos_cpp/`, `src/mutos_ld/` (but its
  `ldrules/`), `src/mutos_make/`, `src/mutos_ar/`, `src/mutos_ranlib/`,
  `src/mutos_nm/`, `src/mutos_size/`, `src/mutos_strip/`, `src/mutos_tsort/`,
  `src/mutos_lorder/`, `src/mutos_ed/`.

Caldera International released UNIX Versions 1 to 7 and 32V in January 2002
under a BSD-style license; its text is [`LICENSES/Caldera.txt`](LICENSES/Caldera.txt)
and applies to these files as a whole. The changes the project made in them may
also be used under the MIT License. As that license asks of advertising material
that mentions the software:

> This product includes software developed or owned by Caldera International, Inc.

## 3. MUTOS 1700 - not licensed by this project

MUTOS 1700 was developed by the VEB Kombinat Robotron for the A7100 and A7150. Its
files and what was derived from them are not the project's work, and this
repository grants no license to them. Who holds the rights in Robotron's software
today is not known to the project. The material is kept for the preservation of
this system, for interoperability, and as the reference against which the
toolchain is tested. It will be removed at the request of a rights holder (open an
issue in the repository). That is:

- `mutos/`: the original binaries, header files and manual pages (but the V30 IDE
  driver's header, section 1);
- in `tests/`: the files brought from the hardware and their contents - the
  golden outputs, objects and images, `libc.a`, `crt0.o` and the kernel's
  `arith.a` (`tests/mutos1700_libc/`, `tests/mutos1700_crt0/`,
  `tests/mutos1700_arith/`), and the sources and makefile of the MUTOS kernel
  used as test input (`tests/mutos_cc/11_kernel/`, `tests/mutos_cpp/c/`,
  `tests/mutos_cpp/h/`, `tests/mutos_as/kernel_opt/`, `tests/mutos_ed/kernel/`;
  but the V30 IDE driver's files, section 1), and the code tables read from the
  original `c1` (`tests/realbin/c1_tables.txt`);
- `src/libc_fixed/orig/` and `src/libc_fixed/fixed/` - sources of `libc.a`'s
  members, reconstructed from the archive, and their corrected copies;
- `docs/MUTOS1700_Assembler_as.pdf`, a Robotron manual;
- in the ports of section 2 and in `src/mutos_em/` (a port of MUTOS's `em`, for
  which there is no V7 source): what they reproduce of the MUTOS programs beyond
  V7 - read from their binaries -, in which the project claims no rights.

MUTOS 1700 is itself derived from UNIX Version 7; the parts of it that are V7's
are under section 2's license.

## Names

UNIX is a registered trademark of The Open Group. Intel, NEC, V30, Robotron and
the other names used here belong to their owners. The project is not affiliated
with any of them.
