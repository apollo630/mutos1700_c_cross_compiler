/*
 * ld.c - MUTOS 1700 / Unix V7 Cross-Linker (Milestone 1)
 *
 * Port of the original MUTOS ld.c to modern C99/C11 for operation as a
 * cross-linker on a 64-bit Linux host.
 *
 * SCOPE OF THIS VERSION:
 *   - Archive/library support (.a files, __.SYMDEF ranlib table of
 *     contents, ldrand()/step() member-scanning logic) IS implemented,
 *     per project scope (archive support belongs to Milestone 1).
 *   - Overlay support (-v) is NOT included in this version (in the
 *     original it is tightly coupled to argument bookkeeping that is
 *     out of scope for now).
 *   - All other original CLI switches are supported 1:1:
 *     -o -u -e -D -l -x -X -S -r -s -n -d -i -O -t
 *   - Using -v produces a clear, clean error message instead of
 *     silent misbehavior.
 *
 * ARCHITECTURAL SIMPLIFICATION (deliberate, see chat):
 *   The original's PDP-11 double-buffered paging system (struct page,
 *   dseek/get/mget) has been replaced by simply reading whole files
 *   into memory. All file offsets here are computed directly in BYTES
 *   (not in 16-bit word addresses as in the original). The byte layout
 *   was derived exactly from the original:
 *
 *     Input file (always WITH relocation info, otherwise an error):
 *       offset 0                       : header (16 bytes)
 *       offset 16                      : text   (tsize bytes)
 *       offset 16+tsize                : data   (dsize bytes)
 *       offset 16+tsize+dsize          : reloc-text (tsize bytes)
 *       offset 16+2*tsize+dsize        : reloc-data (dsize bytes)
 *       offset 16+2*(tsize+dsize)      : symbol table (ssize bytes)
 *
 *     Output file: same as above, but the reloc section is only present
 *     when -r was given (relflg==0 in the header).
 *
 * Output format: see mutos_aout.h (based on v7_a_out.h, NOT the
 * x.out/MS-DOS format).
 */

#define _POSIX_C_SOURCE 200809L /* for strdup() under -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../h/mutos_aout.h"

#define TRUE  1
#define FALSE 0

/* Magic numbers (see mutos_aout.h) */
#define OMAGIC A_MAGIC4   /* 0405 - overlay/pure binary, "OMAGIC" as in the original */
#define FMAGIC A_MAGIC1   /* 0407 - normal */
#define NMAGIC A_MAGIC2   /* 0410 - pure text */
#define IMAGIC A_MAGIC3   /* 0411 - separate I&D */

#define ARCMAGIC MUTOS_ARCMAGIC  /* archive marker */

/* ------------------------------------------------------------------ */
/* Error handling                                                       */
/* ------------------------------------------------------------------ */

static int  errlev = 0;
static const char *cur_filname = NULL;
static char *cur_filname_storage = NULL; /* owns the string cur_filname points at */

/* Set while processing an archive member, mirrors the original's global
 * `archdr` which error() also inspected (archdr.aname) for diagnostics. */
static mutos_archdr_t g_cur_archdr;
static bool g_cur_archdr_valid = false;

static void error(int n, const char *msg)
{
    if (errlev == 0)
        fprintf(stderr, "ld:");
    if (cur_filname) {
        fprintf(stderr, "%s", cur_filname);
        if (g_cur_archdr_valid && g_cur_archdr.aname[0])
            fprintf(stderr, "(%.14s)", g_cur_archdr.aname);
        fprintf(stderr, ": ");
    }
    fprintf(stderr, "%s\n", msg);
    if (n > 1)
        exit(n);
    errlev = n;
}

/* Overflow-checked addition of two 16-bit target sizes (like add() in the original) */
static uint16_t add16(uint32_t a, uint32_t b, const char *what)
{
    uint32_t r = a + b;
    if (r >= 0x10000u)
        error(1, what);
    return (uint16_t)r;
}

/* ------------------------------------------------------------------ */
/* Symbol table                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char     name[8];
    uint8_t  type;    /* N_EXT | (N_UNDF/N_ABS/N_TEXT/N_DATA/N_BSS/N_COMM) */
    uint16_t value;
} sym_t;

#define COMM (N_COMM)  /* used internally while loading, as in the original */

static sym_t   *symtab = NULL;
static size_t   symtab_count = 0, symtab_cap = 0;

#define HASH_SIZE 4099u
static int64_t hashtab[HASH_SIZE];   /* index into symtab, or -1 = empty */

static sym_t cursym;      /* symbol currently under consideration (as in the original) */
static sym_t *lastsym = NULL;

static void cp8c(const char *from, char to[8])
{
    size_t i = 0;
    for (; i < 8 && from[i]; i++)
        to[i] = from[i];
    for (; i < 8; i++)
        to[i] = 0;
}

static bool names_eq(const char a[8], const char b[8])
{
    return memcmp(a, b, 8) == 0;
}

static uint32_t hash_name(const char name[8])
{
    uint32_t h = 0;
    for (int i = 0; i < 8; i++)
        h = (h << 1) + (uint8_t)name[i];
    return h % HASH_SIZE;
}

/* Finds the slot for cursym.name; returns a pointer to the hash slot
 * (value -1 = not present yet, otherwise index into symtab). */
static int64_t *hash_lookup(void)
{
    uint32_t h = hash_name(cursym.name);
    uint32_t i = h;
    for (;;) {
        if (hashtab[i] == -1)
            return &hashtab[i];
        if (names_eq(symtab[hashtab[i]].name, cursym.name))
            return &hashtab[i];
        i = (i + 1) % HASH_SIZE;
        if (i == h)
            error(2, "symbol table (hash) full");
    }
}

static int64_t *slookup(const char *name)
{
    cp8c(name, cursym.name);
    cursym.type = (uint8_t)(N_EXT | N_UNDF);
    cursym.value = 0;
    return hash_lookup();
}

/* Creates cursym as a new entry if *slot==-1. Returns 1 if newly
 * created, 0 if the symbol already existed (lastsym then points to it). */
static int do_enter(int64_t *slot)
{
    if (*slot == -1) {
        if (symtab_count >= symtab_cap) {
            symtab_cap = symtab_cap ? symtab_cap * 2 : 256;
            symtab = realloc(symtab, symtab_cap * sizeof(sym_t));
            if (!symtab)
                error(2, "out of memory for symbol table");
        }
        symtab[symtab_count] = cursym;
        *slot = (int64_t)symtab_count;
        lastsym = &symtab[symtab_count];
        symtab_count++;
        return 1;
    } else {
        lastsym = &symtab[*slot];
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Dynamic byte buffers for the output sections                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} vec_t;

static void vec_reserve(vec_t *v, size_t more)
{
    if (v->len + more > v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 1024;
        while (ncap < v->len + more)
            ncap *= 2;
        v->buf = realloc(v->buf, ncap);
        if (!v->buf)
            error(2, "out of memory for output buffer");
        v->cap = ncap;
    }
}

static void vec_push_u16(vec_t *v, uint16_t x)
{
    vec_reserve(v, 2);
    mutos_put_u16le(v->buf + v->len, x);
    v->len += 2;
}

static void vec_push_bytes(vec_t *v, const uint8_t *b, size_t n)
{
    vec_reserve(v, n);
    memcpy(v->buf + v->len, b, n);
    v->len += n;
}

static vec_t out_text, out_data, out_reloc_text, out_reloc_data, out_syms;

/* ------------------------------------------------------------------ */
/* Input object files                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *data;
    size_t   size;
} objfile_t;

static objfile_t load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        error(2, "cannot open");
    if (fseek(f, 0, SEEK_END) != 0)
        error(2, "seek error");
    long tell = ftell(f);
    if (tell < 0) {
        error(2, "tell error");
        tell = 0; /* unreachable (error() terminates the program), only for compiler analysis */
    }
    size_t sz = (size_t)tell;
    rewind(f);
    size_t alloc_sz = sz > 0 ? sz : 1;
    uint8_t *buf = malloc(alloc_sz);
    if (!buf)
        error(2, "out of memory while reading the file");
    if (sz > 0 && fread(buf, 1, sz, f) != sz)
        error(2, "read error");
    fclose(f);
    objfile_t o = { buf, sz };
    return o;
}

/* Resolves "-lxxx" as in the original: first "libxxx.a" in the current
 * directory, otherwise "/usr/lib/libxxx.a". */
static char *resolve_dash_l(const char *arg)
{
    const char *x = arg + 2; /* after "-l" */
    if (*x == '\0')
        x = "a";
    size_t len = strlen(x);
    char *local = malloc(len + 8);
    sprintf(local, "lib%s.a", x);
    if (access(local, F_OK) == 0)
        return local;
    char *usr = malloc(len + 20);
    sprintf(usr, "/usr/lib/lib%s.a", x);
    free(local);
    return usr;
}

/* Classification returned by getfile(): a plain relocatable object file,
 * or an archive (further sub-classified internally by archive_load1()). */
typedef enum { GF_FILE = 0, GF_ARCHIVE = 1 } gf_kind_t;

typedef struct {
    objfile_t  o;
    gf_kind_t  kind;
} gfresult_t;

/* Loads an input file and classifies it as a plain object file or an
 * archive, based on the leading ARCMAGIC word. */
static gfresult_t getfile(const char *arg)
{
    char *resolved = NULL;
    const char *path = arg;

    if (arg[0] == '-' && arg[1] == 'l') {
        resolved = resolve_dash_l(arg);
        path = resolved;
    }
    free(cur_filname_storage);
    cur_filname_storage = strdup(path);
    cur_filname = cur_filname_storage;
    g_cur_archdr_valid = false;
    objfile_t o = load_file(path);
    gfresult_t r;
    r.o = o;
    r.kind = GF_FILE;
    if (o.size >= 2) {
        uint16_t first = mutos_get_u16le(o.data);
        if (first == ARCMAGIC)
            r.kind = GF_ARCHIVE;
    }
    if (resolved)
        free(resolved);
    return r;
}

/* Reads the 16-byte a.out header of an input file at the given byte offset. */
static void read_input_hdr(const objfile_t *o, size_t off, mutos_hdr_t *h)
{
    if (off + MUTOS_HDR_SIZE > o->size)
        error(2, "Premature EOF");
    mutos_hdr_read(o->data + off, h);
    if (h->magic != FMAGIC)
        error(2, "Bad format");
}

/* ------------------------------------------------------------------ */
/* Global link state                                                    */
/* ------------------------------------------------------------------ */

static uint32_t g_tsize = 0, g_dsize = 0, g_bsize = 0, g_ssize = 0; /* pass-1 accumulators */
static uint32_t torigin = 0, dorigin = 0, borigin = 0;              /* final bases (pass 2) */
static uint32_t text_pad8_target = 0;  /* end of the unconditional MUTOS 8-byte rounding (EB FE fill) */
static uint32_t text_pad_target = 0;   /* end of the additional nflag/iflag 64-byte rounding (zero fill), if any */

static int32_t ctrel = 0, cdrel = 0, cbrel = 0; /* conversion offsets, passes 1 and 2 */

static int xflag=0, Xflag=0, Sflag=0, rflag=0, arflag=0, sflag=0;
static int nflag=0, Oflag=0, dflag=0, iflag=0, trace=0;

static const char *ofilename = "a.out";
static sym_t *entrypt = NULL;

static sym_t *p_etext, *p_edata, *p_end;

/* ------------------------------------------------------------------ */
/* Symbol relocation (identical to the original logic)                  */
/* ------------------------------------------------------------------ */

static void symreloc(void)
{
    switch (cursym.type) {
    case N_TEXT:
    case N_EXT|N_TEXT:
        cursym.value = (uint16_t)(cursym.value + ctrel);
        return;
    case N_DATA:
    case N_EXT|N_DATA:
        cursym.value = (uint16_t)(cursym.value + cdrel);
        return;
    case N_BSS:
    case N_EXT|N_BSS:
        cursym.value = (uint16_t)(cursym.value + cbrel);
        return;
    case N_EXT|N_UNDF:
        return;
    }
    if (cursym.type & N_EXT)
        cursym.type = (uint8_t)(N_EXT | N_ABS);
}

/* ------------------------------------------------------------------ */
/* Pass 1: collect symbol definitions                                    */
/* ------------------------------------------------------------------ */

/* Returns TRUE if the file is actually included in the link (always
 * TRUE for standalone files; for archive members (libflg=1), only if
 * the member actually defines a symbol we don't already have). */
static int load1(int libflg, const objfile_t *o, size_t loc)
{
    mutos_hdr_t h;
    read_input_hdr(o, loc, &h);

    uint32_t st = (h.text + 1u) & ~1u;
    uint32_t sd = (h.data + 1u) & ~1u;

    if (h.flag != 0) {
        error(1, "No relocation bits");
        return 0;
    }

    ctrel = (int32_t)g_tsize;
    /* MUTOS-specific: unlike classic V7 (where DATA/BSS-relative raw
     * values have the file's own tsize/(tsize+dsize) baked in), MUTOS
     * stores pure segment-local offsets. Verified against real object
     * files: Isgadr/Iscadr (EXT+DATA in crt0.o, raw values 2 and 6)
     * resolve to 3506/3510 = raw + dorigin exactly, with no
     * subtraction of crt0's own text size. */
    cdrel = (int32_t)g_dsize;
    cbrel = (int32_t)g_bsize;

    size_t symtab_off = loc + MUTOS_HDR_SIZE + 2u * (st + sd);
    size_t symtab_end = symtab_off + h.syms;
    if (symtab_end > o->size)
        error(2, "Premature EOF");

    int ndef = 0;
    uint32_t nloc = MUTOS_SYM_SIZE; /* reserved for the later per-file symbol entry (mkfsym) */
    int savcount = (int)symtab_count;

    for (size_t p = symtab_off; p < symtab_end; p += MUTOS_SYM_SIZE) {
        mutos_sym_t raw;
        mutos_sym_read(o->data + p, &raw);
        memcpy(cursym.name, raw.name, 8);
        cursym.type = raw.type;
        cursym.value = raw.value;

        if (Sflag) {
            int mtype = cursym.type & N_TYPE;
            if (mtype == N_ABS || mtype > N_BSS)
                continue;
        }
        if ((cursym.type & N_EXT) == 0) {
            if (Xflag == 0 || cursym.name[0] != 'L')
                nloc += MUTOS_SYM_SIZE;
            continue;
        }
        symreloc();
        int64_t *slot = hash_lookup();
        if (do_enter(slot))
            continue;
        sym_t *sp = lastsym;
        if (sp->type != (N_EXT|N_UNDF))
            continue;
        if (cursym.type == (N_EXT|N_UNDF)) {
            if (cursym.value > sp->value)
                sp->value = cursym.value;
            continue;
        }
        if (sp->value != 0 && cursym.type == (N_EXT|N_TEXT))
            continue;
        ndef++;
        sp->type = cursym.type;
        sp->value = cursym.value;
    }

    if (libflg == 0 || ndef) {
        g_tsize = add16(g_tsize, h.text, "text overflow");
        g_dsize = add16(g_dsize, h.data, "data overflow");
        g_bsize = add16(g_bsize, h.bss,  "bss overflow");
        g_ssize = add16(g_ssize, nloc,   "symbol table overflow");
        return 1;
    }

    /* No symbols defined by this library member: rip out the hash
     * table entries and reset the symbol table, as in the original. */
    while ((int)symtab_count > savcount) {
        symtab_count--;
        for (uint32_t i = 0; i < HASH_SIZE; i++)
            if (hashtab[i] == (int64_t)symtab_count)
                hashtab[i] = -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Archive handling (Milestone 1 scope)                                 */
/*                                                                       */
/* Mirrors the original's step()/ldrand()/load1arg() archive logic, but */
/* with all offsets computed directly in bytes (see file header comment */
/* on the architectural simplification). Three archive sub-cases are    */
/* distinguished, exactly as in the original getfile():                 */
/*   1) plain archive (no __.SYMDEF member, or a non-symbol member       */
/*      happens to sit first)      -> full linear scan of all members    */
/*   2) up-to-date __.SYMDEF table -> fast ldrand()-driven symbol lookup */
/*   3) stale __.SYMDEF table (archive file's mtime newer than the       */
/*      timestamp embedded in __.SYMDEF, i.e. modified after the last    */
/*      `ranlib` run) -> warn, then fall back to a full linear scan      */
/*      of the remaining members (matches original behavior exactly;    */
/*      note this path is taken for essentially any freshly-uploaded    */
/*      archive, since the *host* mtime is always "now").                */
/* ------------------------------------------------------------------ */

/* Global cross-file record of archive members pulled into the link
 * during pass 1 (byte offset of the member's archdr), terminated with
 * a -1 sentinel per archive processed. Mirrors the original's global
 * `liblist[]`/`libp`. Consumed again in the same order during pass 2. */
static int64_t *g_liblist = NULL;
static size_t   g_liblist_count = 0, g_liblist_cap = 0;
static size_t   g_libp = 0; /* pass-2 read cursor into g_liblist */

static void liblist_push(int64_t v)
{
    if (g_liblist_count >= g_liblist_cap) {
        g_liblist_cap = g_liblist_cap ? g_liblist_cap * 2 : 256;
        g_liblist = realloc(g_liblist, g_liblist_cap * sizeof(int64_t));
        if (!g_liblist)
            error(2, "out of memory for archive member list");
    }
    g_liblist[g_liblist_count++] = v;
}

static void read_archdr(const objfile_t *o, size_t off, mutos_archdr_t *h)
{
    if (off + MUTOS_ARCHDR_SIZE > o->size)
        error(2, "Premature EOF (archive header)");
    mutos_archdr_read(o->data + off, h);
    g_cur_archdr = *h;
    g_cur_archdr_valid = true;
}

static bool archdr_name_is_symdef(const mutos_archdr_t *h)
{
    return strncmp(h->aname, "__.SYMDEF", 9) == 0 && h->aname[9] == '\0';
}

/* Rounds an archive member's content size up to an even byte count, as
 * the original does via (asize+1)>>1 in its word-addressed arithmetic. */
static uint32_t ar_round_even(uint32_t n)
{
    return (n + 1u) & ~1u;
}

/* Attempts to process one archive member's header at byte offset `pos`.
 * If found, reads its archdr (returned via out_hdr for the caller's
 * bookkeeping), links it in via load1(), and-if it contributes new
 * symbol definitions-records it in g_liblist for pass 2. Returns 1 if
 * a member header was read (regardless of whether it got linked in),
 * or 0 at end of archive (after pushing the -1 sentinel), exactly
 * mirroring the original step()'s TRUE/FALSE contract. */
static int step(const objfile_t *o, size_t pos, mutos_archdr_t *out_hdr)
{
    if (pos + MUTOS_ARCHDR_SIZE > o->size) {
        liblist_push(-1);
        return 0;
    }
    mutos_archdr_t hdr;
    read_archdr(o, pos, &hdr);
    if (out_hdr)
        *out_hdr = hdr;
    size_t content_off = pos + MUTOS_ARCHDR_SIZE;
    if (load1(1, o, content_off))
        liblist_push((int64_t)pos);
    return 1;
}

/* Case 1/3: full linear scan of all members starting at byte offset
 * `start`, as in the original's `nloc=1; while(step(nloc)) nloc += ...`. */
static void archive_scan_linear(const objfile_t *o, size_t start)
{
    size_t pos = start;
    mutos_archdr_t hdr;
    while (step(o, pos, &hdr))
        pos += MUTOS_ARCHDR_SIZE + ar_round_even(hdr.asize);
}

/* Case 2: one pass over the __.SYMDEF table of contents, pulling in
 * any member that defines a symbol we still need. Mirrors the
 * original's ldrand(): the caller repeats this until it stops making
 * progress, since resolving one member's undefineds may require yet
 * another member found only via the table. */
static bool ldrand(const objfile_t *o, const mutos_tab_t *tab, uint32_t tnum)
{
    size_t before = g_liblist_count;
    for (uint32_t i = 0; i < tnum; i++) {
        int64_t *slot = slookup(tab[i].cname);
        if (*slot == -1)
            continue; /* symbol never referenced/defined so far: not needed */
        sym_t *sp = &symtab[*slot];
        if (sp->type != (N_EXT|N_UNDF))
            continue; /* already resolved */
        step(o, tab[i].cloc, NULL);
    }
    return g_liblist_count != before;
}

/* Full archive handling for one -l/.a argument in pass 1. Classifies
 * the archive (plain / up-to-date TOC / stale TOC) exactly as the
 * original getfile() did, and always terminates the run of pulled-in
 * members for this archive with a -1 sentinel in g_liblist. */
static void archive_load1(const objfile_t *o)
{
    size_t first_off = 2; /* right after the 2-byte ARCMAGIC word */

    if (first_off + MUTOS_ARCHDR_SIZE > o->size) {
        /* Empty or truncated archive: step() will immediately hit EOF
         * and push the sentinel by itself. */
        archive_scan_linear(o, first_off);
        return;
    }

    mutos_archdr_t hdr;
    read_archdr(o, first_off, &hdr);

    if (!archdr_name_is_symdef(&hdr)) {
        /* Case 1: plain archive, no ranlib table of contents. */
        archive_scan_linear(o, first_off);
        return;
    }

    /* Compare the archive file's own mtime against the timestamp
     * embedded in __.SYMDEF at the time `ranlib` last ran. */
    struct stat st;
    bool stale = true;
    if (cur_filname && stat(cur_filname, &st) == 0)
        stale = ((uint32_t)st.st_mtime > hdr.atime);

    if (stale) {
        /* Case 3: out of date - warn (non-fatal) and fall back to a
         * full linear scan of the members that follow __.SYMDEF. */
        error(0, "out of date (warning)");
        size_t resume = first_off + MUTOS_ARCHDR_SIZE + ar_round_even(hdr.asize);
        archive_scan_linear(o, resume);
        return;
    }

    /* Case 2: up-to-date table of contents - fast symbol-driven pull. */
    uint32_t tnum = hdr.asize / MUTOS_TAB_SIZE;
    if (tnum > 100000u)
        error(2, "fast load buffer too small");
    mutos_tab_t *tab = malloc((size_t)tnum * sizeof(mutos_tab_t));
    if (tnum && !tab)
        error(2, "out of memory for archive table of contents");
    size_t tab_off = first_off + MUTOS_ARCHDR_SIZE;
    if (tab_off + (size_t)tnum * MUTOS_TAB_SIZE > o->size)
        error(2, "Premature EOF (archive table of contents)");
    for (uint32_t i = 0; i < tnum; i++)
        mutos_tab_read(o->data + tab_off + (size_t)i * MUTOS_TAB_SIZE, &tab[i]);

    while (ldrand(o, tab, tnum))
        ; /* repeat until no further members get pulled in */

    liblist_push(-1); /* end-of-archive sentinel, as in the original */
    free(tab);
}

static void load1arg(const char *arg)
{
    gfresult_t g = getfile(arg);
    if (g.kind == GF_FILE) {
        load1(0, &g.o, 0);
    } else {
        archive_load1(&g.o);
    }
    free(g.o.data);
}

/* ------------------------------------------------------------------ */
/* Intermediate step: assign common symbols, compute final bases        */
/* ------------------------------------------------------------------ */

static void ldrsym(sym_t *sp, uint32_t val, uint8_t type)
{
    if (sp == NULL)
        return;
    if (sp->type != (N_EXT|N_UNDF) || sp->value) {
        fprintf(stderr, "%.8s: ", sp->name);
        error(1, "Multiply defined");
        return;
    }
    sp->type = type;
    sp->value = (uint16_t)val;
}

static void middle(void)
{
    int64_t *s;

    torigin = 0; dorigin = 0; borigin = 0;

    s = slookup("_etext"); p_etext = (*s == -1) ? NULL : &symtab[*s];
    s = slookup("_edata"); p_edata = (*s == -1) ? NULL : &symtab[*s];
    s = slookup("_end");   p_end   = (*s == -1) ? NULL : &symtab[*s];

    /* If there are any undefined symbols, keep the relocation bits */
    if (rflag == 0) {
        for (size_t i = 0; i < symtab_count; i++) {
            sym_t *sp = &symtab[i];
            if (sp->type == (N_EXT|N_UNDF) && sp->value == 0 &&
                sp != p_end && sp != p_edata && sp != p_etext) {
                rflag++;
                dflag = 0;
                break;
            }
        }
    }
    if (rflag)
        nflag = sflag = iflag = Oflag = 0;

    /* Assign common symbols (unresolved externs with a nonzero size) to bss */
    uint32_t csize = 0;
    if (dflag || rflag == 0) {
        ldrsym(p_etext, g_tsize, N_EXT|N_TEXT);
        ldrsym(p_edata, g_dsize, N_EXT|N_DATA);
        ldrsym(p_end,   g_bsize, N_EXT|N_BSS);
        for (size_t i = 0; i < symtab_count; i++) {
            sym_t *sp = &symtab[i];
            if (sp->type == (N_EXT|N_UNDF) && sp->value != 0) {
                uint32_t t = (sp->value + 1u) & ~1u;
                sp->value = (uint16_t)csize;
                sp->type = N_EXT | COMM;
                csize = add16(csize, t, "bss overflow");
            }
        }
    }

    /* MUTOS-specific: unlike stock V7 (which only pads tsize when
     * nflag/iflag is set), MUTOS's ld always pads the text size up to
     * an 8-byte boundary before starting the data segment, regardless
     * of flags. Confirmed against the real "myhello" binary (built
     * with plain FMAGIC, i.e. neither nflag nor iflag set): raw
     * concatenated text was 3498 bytes, but the real header reports
     * 3504 = (3498+7)&~7. This 8-byte padding is filled with the byte
     * pattern EB FE (x86 "jmp $"), see finishout(). */
    g_tsize = (g_tsize + 7u) & ~7u;
    text_pad8_target = g_tsize;
    /* The stock-V7-inherited additional rounding for nflag/iflag (to a
     * 64-byte boundary) is a SEPARATE padding stage filled with zero
     * bytes instead, as confirmed against the real "idhello" binary
     * (built with -i, IMAGIC): text grew from 3504 (the 8-byte
     * boundary) to 3520 (the 64-byte boundary), and only the first 6
     * bytes of that gap use the EB FE pattern -- the remaining 16
     * bytes are zero. */
    if (nflag || iflag)
        g_tsize = (g_tsize + 077u) & ~077u;
    /* The physical text padding in the FILE is always g_tsize bytes,
     * regardless of iflag: dorigin==0 for -i reflects that the data
     * segment's ADDRESS SPACE starts fresh at 0 (separate I&D), but the
     * text region still physically occupies g_tsize bytes in the file
     * before the data bytes follow. */
    text_pad_target = g_tsize;
    dorigin = g_tsize;
    if (nflag)
        dorigin = (g_tsize + 017777u) & ~017777u;
    if (iflag)
        dorigin = 0;
    uint32_t corigin = dorigin + g_dsize;
    borigin = corigin + csize;

    int nund = 0;
    for (size_t i = 0; i < symtab_count; i++) {
        sym_t *sp = &symtab[i];
        switch (sp->type) {
        case N_EXT|N_UNDF:
            errlev |= 01;
            if (arflag == 0 && sp->value == 0) {
                if (nund == 0)
                    printf("Undefined:\n");
                nund++;
                printf("%.8s\n", sp->name);
            }
            continue;
        case N_EXT|N_ABS:
        default:
            continue;
        case N_EXT|N_TEXT:
            sp->value = (uint16_t)(sp->value + torigin);
            continue;
        case N_EXT|N_DATA:
            sp->value = (uint16_t)(sp->value + dorigin);
            continue;
        case N_EXT|N_BSS:
            sp->value = (uint16_t)(sp->value + borigin);
            continue;
        case N_EXT|COMM:
            sp->type = N_EXT|N_BSS;
            sp->value = (uint16_t)(sp->value + corigin);
            continue;
        }
    }
    if (sflag || xflag)
        g_ssize = 0;
    g_bsize = add16(g_bsize, csize, "bss overflow");
}

/* ------------------------------------------------------------------ */
/* Prepare / finalize the output                                        */
/* ------------------------------------------------------------------ */

static mutos_hdr_t out_hdr;

static void setupout(void)
{
    out_hdr.magic  = (uint16_t)(Oflag ? OMAGIC : (iflag ? IMAGIC : (nflag ? NMAGIC : FMAGIC)));
    out_hdr.text   = (uint16_t)g_tsize;
    out_hdr.data   = (uint16_t)g_dsize;
    out_hdr.bss    = (uint16_t)g_bsize;
    out_hdr.syms   = (uint16_t)(sflag ? 0 : (g_ssize + MUTOS_SYM_SIZE * (uint32_t)symtab_count));
    if (entrypt) {
        if (entrypt->type != (N_EXT|N_TEXT))
            error(1, "Entry point not in text");
        else
            out_hdr.entry = (uint16_t)(entrypt->value | 1u);
    } else {
        out_hdr.entry = 0;
    }
    out_hdr.unused = 0;
    out_hdr.flag = (uint16_t)(rflag == 0);
}

static void mkfsym(const char *name)
{
    if (sflag || xflag)
        return;
    mutos_sym_t s;
    cp8c(name, s.name);
    s.type = 037; /* file symbol, as in the original (Fn type) */
    s.spare = 0;
    s.value = (uint16_t)torigin;
    uint8_t raw[MUTOS_SYM_SIZE];
    mutos_sym_write(raw, &s);
    vec_push_bytes(&out_syms, raw, MUTOS_SYM_SIZE);
}

/* Local symbol numbering for the file currently being processed in load2() */
typedef struct {
    uint32_t index;
    sym_t   *sp;
} local_t;

#define MAX_LOCAL 4096
static local_t local_arr[MAX_LOCAL];
static int local_count = 0;

static int local_lookup_svalue(uint32_t symno, sym_t **out_sp)
{
    for (int i = 0; i < local_count; i++) {
        if (local_arr[i].index == symno) {
            *out_sp = local_arr[i].sp;
            return 1;
        }
    }
    return 0;
}

/* Copies and relocates the text or data section of an input file into
 * the output buffer, applying the relocation info. */
static void load2td(const objfile_t *o, size_t content_off, size_t reloc_off,
                     uint32_t nbytes, int32_t creloc, vec_t *outv, vec_t *outr)
{
    /* MUTOS-specific discovery (verified byte-for-byte against real
     * linked binaries): unlike stock V7/PDP-11 where every instruction
     * and operand is word-aligned, 8086 code is byte-oriented, so a
     * relocatable operand routinely starts at an ODD byte offset. Bit
     * 15 of the relocation tag marks this case: when set, the word
     * actually being relocated is NOT at [off, off+1] (the reloc tag's
     * own nominal position) but at [off+1, off+2] instead. */
    vec_push_bytes(outv, o->data + content_off, nbytes);
    size_t base = outv->len - nbytes;

    for (uint32_t off = 0; off < nbytes; off += 2) {
        uint16_t r = mutos_get_u16le(o->data + reloc_off + off);
        if (rflag)
            vec_push_u16(outr, r); /* pass-through; -r re-linking with the byte-shift case is not yet validated */
        if (r == 0)
            continue; /* RABS: no relocation, raw bytes already correct */

        uint32_t shift = (r & 0x8000u) ? 1u : 0u;
        uint32_t target = off + shift;
        if (target + 2 > nbytes)
            error(2, "relocation target out of range");

        uint16_t t = mutos_get_u16le(o->data + content_off + target);

        switch (r & 016) {
        case R_TEXT:
            t = (uint16_t)(t + ctrel);
            break;
        case R_DATA:
            t = (uint16_t)(t + cdrel);
            break;
        case R_BSS:
            t = (uint16_t)(t + cbrel);
            break;
        case R_EXT: {
            /* NOTE: MUTOS extends the V7 relocation word format. The
             * symbol-index field is only 11 bits wide (bits 4-14), not
             * 12 bits (bits 4-15) as in the original V7 ld.c. Bit 15 is
             * the byte-shift flag described above and is NOT part of
             * the symbol index. */
            uint32_t symno = (uint32_t)((r >> 4) & 03777);
            sym_t *sp = NULL;
            if (!local_lookup_svalue(symno, &sp))
                error(2, "Local symbol botch");
            if (sp->type == (N_EXT|N_UNDF)) {
                continue;
            }
            t = (uint16_t)(t + sp->value);
            r = (uint16_t)((r & 01) + ((sp->type - (N_EXT|N_ABS)) << 1));
            break;
        }
        }
        if (r & 01)
            t = (uint16_t)(t - creloc);
        mutos_put_u16le(outv->buf + base + target, t);
    }
}

static void load2(const objfile_t *o, size_t loc)
{
    mutos_hdr_t h;
    read_input_hdr(o, loc, &h);
    uint32_t st = (h.text + 1u) & ~1u;
    uint32_t sd = (h.data + 1u) & ~1u;

    ctrel = (int32_t)torigin;
    /* MUTOS-specific: see the matching note in load1(). */
    cdrel = (int32_t)dorigin;
    cbrel = (int32_t)borigin;

    local_count = 0;

    size_t symtab_off = loc + MUTOS_HDR_SIZE + 2u * (st + sd);
    size_t symtab_end = symtab_off + h.syms;
    uint32_t symno = (uint32_t)-1;

    for (size_t p = symtab_off; p < symtab_end; p += MUTOS_SYM_SIZE) {
        symno++;
        mutos_sym_t raw;
        mutos_sym_read(o->data + p, &raw);
        memcpy(cursym.name, raw.name, 8);
        cursym.type = raw.type;
        cursym.value = raw.value;
        symreloc();

        if (Sflag) {
            int mtype = cursym.type & N_TYPE;
            if (mtype == N_ABS || mtype > N_BSS)
                continue;
        }
        if ((cursym.type & N_EXT) == 0) {
            if (!sflag && !xflag && (!Xflag || cursym.name[0] != 'L')) {
                mutos_sym_t out;
                memcpy(out.name, cursym.name, 8);
                out.type = cursym.type;
                out.spare = 0;
                out.value = cursym.value;
                uint8_t raw2[MUTOS_SYM_SIZE];
                mutos_sym_write(raw2, &out);
                vec_push_bytes(&out_syms, raw2, MUTOS_SYM_SIZE);
            }
            continue;
        }
        int64_t *slot = hash_lookup();
        if (*slot == -1)
            error(2, "internal error: symbol not found");
        sym_t *sp = &symtab[*slot];
        if (cursym.type == (N_EXT|N_UNDF)) {
            if (local_count >= MAX_LOCAL)
                error(2, "Local symbol overflow");
            local_arr[local_count].index = symno;
            local_arr[local_count].sp = sp;
            local_count++;
            continue;
        }
        if (cursym.type != sp->type || cursym.value != sp->value) {
            fprintf(stderr, "%.8s: ", cursym.name);
            error(1, "Multiply defined");
        }
    }

    load2td(o, loc + MUTOS_HDR_SIZE, loc + MUTOS_HDR_SIZE + st + sd,
             h.text, ctrel, &out_text, &out_reloc_text);
    load2td(o, loc + MUTOS_HDR_SIZE + st, loc + MUTOS_HDR_SIZE + 2u*st + sd,
             h.data, cdrel, &out_data, &out_reloc_data);

    torigin += h.text;
    dorigin += h.data;
    borigin += h.bss;
}

static void load2arg(const char *arg)
{
    gfresult_t g = getfile(arg);
    if (g.kind == GF_FILE) {
        const char *base = arg;
        for (const char *cp = arg; *cp; cp++)
            if (*cp == '/')
                base = cp + 1;
        mkfsym(base);
        load2(&g.o, 0);
    } else {
        /* Archive: replay the members recorded for THIS archive during
         * pass 1, starting at the current global read cursor and
         * stopping at the -1 sentinel that pass 1 appended for it.
         * Mirrors the original's `for (lp=libp; lp->loc != -1; lp++)`. */
        while (g_libp < g_liblist_count && g_liblist[g_libp] != -1) {
            size_t member_off = (size_t)g_liblist[g_libp];
            mutos_archdr_t hdr;
            read_archdr(&g.o, member_off, &hdr);
            mkfsym(hdr.aname);
            load2(&g.o, member_off + MUTOS_ARCHDR_SIZE);
            g_libp++;
        }
        if (g_libp < g_liblist_count && g_liblist[g_libp] == -1)
            g_libp++; /* consume this archive's end-of-list sentinel */
    }
    free(g.o.data);
}

static void finishout(void)
{
    /* Stage 1: pad up to text_pad8_target (the unconditional MUTOS
     * 8-byte rounding) with the byte pattern EB FE (x86 short jump to
     * itself, i.e. an infinite-loop trap) -- the same filler pattern
     * crt0.o itself uses at the start of its own text. Verified against
     * the real myhello binary. As a little-endian word this is 0xFEEB
     * (byte[0]=EB, byte[1]=FE). */
    while (torigin < text_pad8_target) {
        torigin += 2;
        vec_push_u16(&out_text, 0xFEEB);
        if (rflag)
            vec_push_u16(&out_reloc_text, 0);
    }
    /* Stage 2: if nflag/iflag additionally rounds up to a 64-byte
     * boundary, that extra padding is filled with zero bytes instead,
     * as in stock V7. Verified against the real "idhello" (-i) binary:
     * only the first 6 bytes of its text tail use EB FE, the remaining
     * 16 bytes (up to the 64-byte boundary) are zero. */
    while (torigin < text_pad_target) {
        torigin += 2;
        vec_push_u16(&out_text, 0);
        if (rflag)
            vec_push_u16(&out_reloc_text, 0);
    }

    FILE *f = fopen(ofilename, "wb+");
    if (!f)
        error(2, "cannot create output");

    uint8_t hdrbuf[MUTOS_HDR_SIZE];
    mutos_hdr_write(hdrbuf, &out_hdr);
    fwrite(hdrbuf, 1, MUTOS_HDR_SIZE, f);
    fwrite(out_text.buf, 1, out_text.len, f);
    fwrite(out_data.buf, 1, out_data.len, f);
    if (rflag) {
        fwrite(out_reloc_text.buf, 1, out_reloc_text.len, f);
        fwrite(out_reloc_data.buf, 1, out_reloc_data.len, f);
    }
    if (sflag == 0) {
        if (xflag == 0)
            fwrite(out_syms.buf, 1, out_syms.len, f);
        uint8_t raw[MUTOS_SYM_SIZE];
        for (size_t i = 0; i < symtab_count; i++) {
            mutos_sym_t s;
            memcpy(s.name, symtab[i].name, 8);
            s.type = symtab[i].type;
            s.spare = 0;
            s.value = symtab[i].value;
            mutos_sym_write(raw, &s);
            fwrite(raw, 1, MUTOS_SYM_SIZE, f);
        }
    }
    fclose(f);

    if (errlev)
        exit(errlev);
}

/* ------------------------------------------------------------------ */
/* main()                                                                */
/* ------------------------------------------------------------------ */

static void endload(int argc, char **argv)
{
    cur_filname = NULL;
    g_cur_archdr_valid = false;
    middle();
    setupout();
    g_libp = 0;
    for (int c = 1; c < argc; c++) {
        char *ap = argv[c];
        if (trace) printf("%s:\n", ap);
        if (ap[0] == '-') {
            int i = 1;
            for (; ap[i]; i++) {
                switch (ap[i]) {
                case 'D':
                case 'u': case 'e': case 'o': case 'v':
                    c++;
                    goto next_arg;
                case 'l': {
                    /* Rewrite the token in place so it reads "-lxxx"
                     * from this position on, exactly as the original
                     * K&R code does (ap[--i]='-'), then hand it to
                     * load2arg() like any other archive argument. */
                    char saved = ap[i - 1];
                    ap[i - 1] = '-';
                    load2arg(&ap[i - 1]);
                    ap[i - 1] = saved;
                    goto next_arg;
                }
                default:
                    goto next_arg;
                }
            }
        } else {
            load2arg(ap);
        }
    next_arg:;
    }
    finishout();
}

int main(int argc, char **argv)
{
    for (size_t i = 0; i < HASH_SIZE; i++)
        hashtab[i] = -1;

    if (argc == 1)
        return 4;

    /* Pass 1: read files, collect symbol definitions */
    for (int c = 1; c < argc; c++) {
        char *ap = argv[c];
        cur_filname = NULL;
        g_cur_archdr_valid = false;
        if (trace) printf("%s:\n", ap);
        if (ap[0] == '-') {
            int i = 1;
            for (; ap[i]; i++) {
                switch (ap[i]) {
                case 'o':
                    if (++c >= argc) error(2, "Bad output file");
                    ofilename = argv[c];
                    continue;
                case 'u':
                case 'e': {
                    if (++c >= argc) error(2, "Bad 'use' or 'entry'");
                    int64_t *slot = slookup(argv[c]);
                    do_enter(slot);
                    if (ap[i] == 'e')
                        entrypt = lastsym;
                    continue;
                }
                case 'v':
                    error(2, "-v (overlays) is not supported in milestone 1");
                    break;
                case 'D': {
                    if (++c >= argc) error(2, "-D: arg missing");
                    long num = atol(argv[c]);
                    if ((long)g_dsize > num) error(2, "-D: too small");
                    g_dsize = (uint32_t)num;
                    continue;
                }
                case 'l': {
                    /* Same in-place token rewrite as in endload(),
                     * mirroring the original's ap[--i]='-' trick. */
                    char saved = ap[i - 1];
                    ap[i - 1] = '-';
                    load1arg(&ap[i - 1]);
                    ap[i - 1] = saved;
                    break;
                }
                case 'x': xflag++; continue;
                case 'X': Xflag++; continue;
                case 'S': Sflag++; continue;
                case 'r': rflag++; arflag++; continue;
                case 's': sflag++; xflag++; continue;
                case 'n': nflag++; continue;
                case 'd': dflag++; continue;
                case 'i': iflag++; continue;
                case 'O': Oflag++; continue;
                case 't': trace++; continue;
                default:
                    error(2, "bad flag");
                }
                break;
            }
        } else {
            load1arg(ap);
        }
    }

    endload(argc, argv);
    return errlev;
}
