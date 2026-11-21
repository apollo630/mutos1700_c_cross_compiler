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
- the project owner's code in the MUTOS kernel for the V30 expansion of his A7100
  (below).

### The owner's code in the kernel: the V30 IDE driver

The project owner's A7100 has an expansion board with an IDE adapter that also
switches in the NEC V30. The code he wrote for it, in his MUTOS 1700 kernel and its
makefile, is his own work and under the MIT License as above:

- the driver: `mutos/mutos_include/sys/h/v30ide.h`, its copy
  `tests/mutos_cpp/h/v30ide.h`, `tests/mutos_cpp/c/v30ide.c`, and what the toolchain
  made of it (`tests/mutos_cpp/c/v30ide.i.golden`,
  `tests/mutos_as/kernel_opt/v30ide.s`, `v30ide.o.golden`,
  `v30ide.o.golden_base64.txt`);
- in the kernel's `mch.c`, the `#include` of `v30ide.h` and the two parts under
  `#ifdef V30IDE`: the one in `_haltcpu`, which switches the CPU back to the slow
  clock before a reboot or the return to the monitor, and the one at the end, the
  routines `_isNEC` and `_hdio`. So also in `tests/mutos_cpp/c/mch.c`,
  `mch.i.golden`, `tests/mutos_as/kernel_opt/mch.s`, `mch_insw_outsw.s` and their
  objects;
- in the kernel's makefile, `tests/mutos_ed/kernel/conf.Makefile`, what serves the
  option `V30IDE`: its name in the list of drivers and in `DRIVER`, `-DV30IDE` in
  `CONFOPT`, `v30ide.o` among the objects, in their rule and in the link, and the
  targets `V30IDE` and `noV30IDE`.

The rest of `mch.c` and of the makefile is MUTOS's (section 3).

Some parts of the driver were taken from V7/x86, Robert Nordier's port of UNIX
Version 7 to the PC (www.nordier.com; its `/usr/sys/dev/hd.c`). Those parts remain
his work: his notice, "Copyright (c) 2006 Robert Nordier. All rights reserved.",
stays in the files, and they are used under the terms of V7/x86, which the MIT
License of the rest does not change. What V7/x86 has of UNIX Version 7 is under
section 2's license.

`_isNEC` tells a NEC V20/V30 from an Intel 8086/8088 by the zero flag after `mul`,
the method of "Processor Identification" by Chris Dragan and Chili (Assembly
Programmer's Journal, issue 6), which its comment names.

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

## 3. MUTOS 1700 - historical files, not licensed by this project

**The MUTOS 1700 files in this repository are historical files from the German
Democratic Republic (GDR): software and documentation of the VEB Kombinat
Robotron. They are provided exclusively for archival, scientific and educational
purposes. If a holder of rights in them objects, they will be removed immediately:
please open an issue in the repository.**

MUTOS 1700 was developed by the VEB Kombinat Robotron for its office computers A7100
and A7150. Its files and what was derived from them are not the project's work, and
this repository grants no license to them. Who holds the rights in Robotron's
software today is not known to the project. The files are kept to preserve this
system, for interoperability, and as the reference against which the toolchain is
tested. That is:

- `mutos/`: the original binaries, header files and manual pages (but the V30 IDE
  driver's header, section 1);
- in `tests/`: the files brought from the hardware and their contents - the
  golden outputs, objects and images, `libc.a`, `crt0.o` and the kernel's
  `arith.a` (`tests/mutos1700_libc/`, `tests/mutos1700_crt0/`,
  `tests/mutos1700_arith/`), and the sources and makefile of the MUTOS kernel
  used as test input (`tests/mutos_cc/11_kernel/`, `tests/mutos_cpp/c/`,
  `tests/mutos_cpp/h/`, `tests/mutos_as/kernel_opt/`, `tests/mutos_ed/kernel/`;
  but the owner's code in them, section 1), and the code tables read from the
  original `c1` (`tests/realbin/c1_tables.txt`);
- `src/libc_fixed/orig/` and `src/libc_fixed/fixed/` - sources of `libc.a`'s
  members, reconstructed from the archive, and their corrected copies;
  likewise `tests/mutos_as/libc_recon/ldexp.s`, reconstructed from `libc.a`'s
  `ldexp.o`;
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
