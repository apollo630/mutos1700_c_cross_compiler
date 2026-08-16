/*
 * mutos_aout.h
 *
 * Definition of the MUTOS 1700 / Unix V7 a.out object and executable format.
 *
 * Basis: v7_a_out.h (original Unix V7), NOT the MUTOS-specific a_out.h,
 * which additionally defines the (here not needed) x.out/struct-exec
 * format for MS-DOS compatibility.
 *
 * IMPORTANT: The V7/MUTOS a.out header is entirely 16-bit based.
 * There is NOT a single `long` field anywhere in the header or the
 * symbol table. PDP-11 middle-endian encoding is therefore NOT required
 * for this format (that concern only applies to `long` variables inside
 * C code compiled by `cc`, relevant starting at milestone 3).
 *
 * All multi-byte fields are explicitly serialized as little-endian
 * (native x86 byte order), independent of the host compiler's struct
 * layout, to avoid struct-padding/endianness surprises on the 64-bit host.
 */

#ifndef MUTOS_AOUT_H
#define MUTOS_AOUT_H

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* a.out exec header (16 bytes, 8 x 16-bit words)                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t magic;   /* a_magic  */
    uint16_t text;    /* a_text   - text segment size (bytes)          */
    uint16_t data;    /* a_data   - initialized data size              */
    uint16_t bss;     /* a_bss    - uninitialized data size            */
    uint16_t syms;    /* a_syms   - symbol table size (bytes)          */
    uint16_t entry;   /* a_entry  - entry point                        */
    uint16_t unused;  /* a_unused - unused                             */
    uint16_t flag;    /* a_flag   - 1 = relocation info stripped       */
} mutos_hdr_t;

#define MUTOS_HDR_SIZE 16u  /* bytes, matches sizeof(filhdr) in the original */

/* Magic numbers (octal, as in the original) */
#define A_MAGIC1 0407  /* normal                      */
#define A_MAGIC2 0410  /* read-only text (pure)       */
#define A_MAGIC3 0411  /* separate I&D address spaces */
#define A_MAGIC4 0405  /* overlay                     */

/* ------------------------------------------------------------------ */
/* Symbol table entry (12 bytes)                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char     name[8];
    uint8_t  type;    /* corresponds to n_type (original: int, fits in 8 bits) */
    uint8_t  spare;   /* filler byte, corresponds to the upper byte of n_type  */
    uint16_t value;   /* corresponds to n_value (unsigned)                     */
} mutos_sym_t;

#define MUTOS_SYM_SIZE 12u

/* Values for .type (original: EXTERN/UNDEF/ABS/TEXT/DATA/BSS/COMM) */
#define N_UNDF   0x00
#define N_ABS    0x01
#define N_TEXT   0x02
#define N_DATA   0x03
#define N_BSS    0x04
#define N_COMM   0x05   /* used internally during loading only */
#define N_TYPE   0x1F
#define N_EXT    0x20   /* external bit, OR'ed in */

/* Relocation types (low bits of the per-word relocation tag) */
#define R_ABS    0x00
#define R_TEXT   0x02
#define R_BSS    0x06
#define R_DATA   0x04
#define R_EXT    0x08

/* ------------------------------------------------------------------ */
/* Little-endian byte helpers (explicit, no struct-padding risk)        */
/* ------------------------------------------------------------------ */

static inline uint16_t mutos_get_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline void mutos_put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void mutos_hdr_read(const uint8_t *buf, mutos_hdr_t *h)
{
    h->magic  = mutos_get_u16le(buf + 0);
    h->text   = mutos_get_u16le(buf + 2);
    h->data   = mutos_get_u16le(buf + 4);
    h->bss    = mutos_get_u16le(buf + 6);
    h->syms   = mutos_get_u16le(buf + 8);
    h->entry  = mutos_get_u16le(buf + 10);
    h->unused = mutos_get_u16le(buf + 12);
    h->flag   = mutos_get_u16le(buf + 14);
}

static inline void mutos_hdr_write(uint8_t *buf, const mutos_hdr_t *h)
{
    mutos_put_u16le(buf + 0,  h->magic);
    mutos_put_u16le(buf + 2,  h->text);
    mutos_put_u16le(buf + 4,  h->data);
    mutos_put_u16le(buf + 6,  h->bss);
    mutos_put_u16le(buf + 8,  h->syms);
    mutos_put_u16le(buf + 10, h->entry);
    mutos_put_u16le(buf + 12, h->unused);
    mutos_put_u16le(buf + 14, h->flag);
}

static inline void mutos_sym_read(const uint8_t *buf, mutos_sym_t *s)
{
    for (int i = 0; i < 8; i++) s->name[i] = (char)buf[i];
    s->type  = buf[8];
    s->spare = buf[9];
    s->value = mutos_get_u16le(buf + 10);
}

static inline void mutos_sym_write(uint8_t *buf, const mutos_sym_t *s)
{
    for (int i = 0; i < 8; i++) buf[i] = (uint8_t)s->name[i];
    buf[8] = s->type;
    buf[9] = s->spare;
    mutos_put_u16le(buf + 10, s->value);
}

/* ------------------------------------------------------------------ */
/* Archive (.a) format                                                  */
/*                                                                       */
/* Unlike the a.out exec header above (which MUTOS redefined as pure    */
/* 16-bit little-endian), the legacy V7 `ar`/ranlib archive format was  */
/* carried over unmodified and still uses classic PDP-11 middle-endian  */
/* encoding for its 32-bit `long` fields: the high-order 16-bit word is */
/* stored first, and each 16-bit word is itself little-endian. This was */
/* verified against a real libc.a: __.SYMDEF asize=3168 bytes = exactly */
/* 264 * sizeof(struct tab) (12 bytes) under this scheme, atime decodes */
/* to a plausible 1989 timestamp, and struct tab's cloc field (also a   */
/* `long`) decodes under the same scheme to valid byte offsets pointing */
/* at real archive member headers.                                     */
/* ------------------------------------------------------------------ */

#define MUTOS_ARCMAGIC   0177545u  /* first 16-bit word of an archive file */

/* struct archdr, packed, 26 bytes:
 *   char  aname[14];   member (file) name, NUL-padded
 *   long  atime;       modification time, PDP-11 middle-endian
 *   char  auid, agid;  owner/group
 *   int   amode;       mode, 16-bit LE (like a normal a.out field)
 *   long  asize;       member content size in bytes, PDP-11 middle-endian
 */
#define MUTOS_ARCHDR_SIZE 26u

typedef struct {
    char     aname[15];  /* 14 archive bytes + guaranteed NUL terminator */
    uint32_t atime;
    uint8_t  auid;
    uint8_t  agid;
    uint16_t amode;
    uint32_t asize;
} mutos_archdr_t;

/* struct tab (ranlib table-of-contents entry inside __.SYMDEF), 12 bytes:
 *   char  cname[8];  symbol name
 *   long  cloc;      byte offset of the defining member's archdr,
 *                    PDP-11 middle-endian
 */
#define MUTOS_TAB_SIZE 12u

typedef struct {
    char     cname[9];   /* 8 archive bytes + guaranteed NUL terminator */
    uint32_t cloc;
} mutos_tab_t;

/* Read/write a PDP-11 middle-endian `long`: high-order word first,
 * each word little-endian internally. */
static inline uint32_t mutos_get_u32_pdp11(const uint8_t *p)
{
    uint16_t hi = mutos_get_u16le(p + 0);
    uint16_t lo = mutos_get_u16le(p + 2);
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

static inline void mutos_put_u32_pdp11(uint8_t *p, uint32_t v)
{
    mutos_put_u16le(p + 0, (uint16_t)(v >> 16));
    mutos_put_u16le(p + 2, (uint16_t)(v & 0xFFFF));
}

static inline void mutos_archdr_read(const uint8_t *buf, mutos_archdr_t *h)
{
    for (int i = 0; i < 14; i++) h->aname[i] = (char)buf[i];
    h->aname[14] = '\0';
    h->atime  = mutos_get_u32_pdp11(buf + 14);
    h->auid   = buf[18];
    h->agid   = buf[19];
    h->amode  = mutos_get_u16le(buf + 20);
    h->asize  = mutos_get_u32_pdp11(buf + 22);
}

static inline void mutos_tab_read(const uint8_t *buf, mutos_tab_t *t)
{
    for (int i = 0; i < 8; i++) t->cname[i] = (char)buf[i];
    t->cname[8] = '\0';
    t->cloc = mutos_get_u32_pdp11(buf + 8);
}

#endif /* MUTOS_AOUT_H */
