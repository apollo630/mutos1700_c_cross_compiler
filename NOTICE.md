# Licensing and notices

This repository holds material of three origins, under different terms. The
project's own work is free under the MIT License; what comes from UNIX Version 7
keeps the Caldera license it was released under; what comes from MUTOS 1700
itself is not the project's to license and is not licensed by it. This file says
which is which. It is not legal advice.

## 1. The project's own work - the MIT License

Everything written for this project and not listed in sections 2 to 4 is
Copyright (c) 2026 Jens Markwardt and is licensed under the MIT License,
[`LICENSE`](LICENSE). That is in particular:

- the assembler `src/mutos_as/` (a re-implementation, not a port), `src/h/`, and
  the rules of the corrected mode and of `-1` (`src/mutos_cc/c1rules/`,
  `src/mutos_cc/c2rules/`, `src/mutos_ld/ldrules/`, `src/mutos_as/asrules/`);
- the build (`Makefile`s), `scripts/`, the test tools and test programs written
  for the project in `tests/` (the emulator, the simulators, the fuzzers, the
  checks, the kits for the hardware), and the tools in `src/libc_fixed/`;
- the documentation: the Markdown documents and the manual pages in `man/`.

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

A caveat, for completeness: in 2010 a jury in SCO v. Novell found that Novell,
not SCO (Caldera's successor), owned the UNIX copyrights, which has raised the
question whether Caldera could grant this license. It has not been challenged
since; projects built on V7 rely on it, as this one does.

## 3. MUTOS 1700 - not licensed by this project

MUTOS 1700 was developed by the VEB Kombinat Robotron for the A7100 and A7150. Its
files and what was derived from them are not the project's work, and this
repository grants no license to them. Who holds the rights in Robotron's software
today is not known to the project. The material is kept for the preservation of
this system, for interoperability, and as the reference against which the
toolchain is tested. It will be removed at the request of a rights holder (open an
issue in the repository). That is:

- `mutos/`: the original binaries, header files and manual pages;
- in `tests/`: the files brought from the hardware and their contents - the
  golden outputs, objects and images, `libc.a`, `crt0.o` and the kernel's
  `arith.a` (`tests/mutos1700_libc/`, `tests/mutos1700_crt0/`,
  `tests/mutos1700_arith/`), and the sources and makefile of the MUTOS kernel
  used as test input (`tests/mutos_cc/11_kernel/`, `tests/mutos_cpp/c/`,
  `tests/mutos_cpp/h/`, `tests/mutos_as/kernel_opt/`, `tests/mutos_ed/kernel/`),
  and the code tables read from the original `c1` (`tests/realbin/c1_tables.txt`);
- `src/libc_fixed/orig/` and `src/libc_fixed/fixed/` - sources of `libc.a`'s
  members, reconstructed from the archive, and their corrected copies;
- `docs/MUTOS1700_Assembler_as.pdf`, a Robotron manual;
- in the ports of section 2 and in `src/mutos_em/` (a port of MUTOS's `em`, for
  which there is no V7 source): what they reproduce of the MUTOS programs beyond
  V7 - read from their binaries -, in which the project claims no rights.

MUTOS 1700 is itself derived from UNIX Version 7; the parts of it that are V7's
are under section 2's license. `mutos/mutos_include/sys/h/v30ide.h` (with its
copies in `tests/`) carries a notice of Robert Nordier for code from V7/x86,
which stays with the file.

## 4. Documents of third parties

`docs/210973-001_AP-186_Introduction_to_the_80186_Microprocessor_Mar83.pdf`
(Intel) and `docs/V20_V30_Users_Manual_Oct86.pdf` (NEC) are their publishers'
documents, kept as references; they are not under the project's license.

## Names

UNIX is a registered trademark of The Open Group. Intel, NEC, V30, Robotron and
the other names used here belong to their owners. The project is not affiliated
with any of them.
