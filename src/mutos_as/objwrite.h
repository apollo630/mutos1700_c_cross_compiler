/*
 * objwrite.h - serializes the assembler's in-memory results (text/data
 * buffers, symbol table, relocation lists) into a real MUTOS 1700
 * a.out object file, matching mutos_aout.h exactly.
 *
 * File layout (all confirmed against real hardware-linked .o files
 * throughout Milestone 2's verification work):
 *   header (16 bytes)
 *   text   (a_text bytes)
 *   data   (a_data bytes)
 *   trel   (a_text bytes - one 2-byte relocation word per text word)
 *   drel   (a_data bytes - one 2-byte relocation word per data word)
 *   symtab (a_syms bytes - mutos_sym_t entries, 12 bytes each)
 */

#ifndef MUTOS_AS_OBJWRITE_H
#define MUTOS_AS_OBJWRITE_H

#include "encode.h"
#include "symtab.h"

/* Writes a complete, unstripped (a_flag=0) relocatable object file to
 * `path`. `nsyms` is the value returned by symtab_finalize() and must
 * match the out_index values already assigned on every symbol in `st`.
 * Returns true on success. */
bool objwrite_write(const char *path, SymTab *st, int nsyms,
                     const CodeBuf *text, const CodeBuf *data, size_t bss_size,
                     const RelocList *text_relocs, const RelocList *data_relocs);

#endif /* MUTOS_AS_OBJWRITE_H */
