/*
 * classify_test.c - Step 2 validation driver.
 *
 * Runs the full pipeline (lexer -> statement parser -> operand
 * classifier -> expression parser) over real c1-generated .s files and
 * reports, for every operand encountered, which addressing mode it was
 * classified as and how its expression parsed. Purpose: prove the
 * Step 2 groundwork (pass2.c) handles every operand shape actually
 * occurring in the corpus before any opcode-encoding table is built on
 * top of it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_as.h"
#include "pass2.h"

static const char *addrmode_name(AddrMode m)
{
    switch (m) {
        case ADDR_REGISTER:  return "REGISTER ";
        case ADDR_INDIRECT:  return "INDIRECT ";
        case ADDR_IMMEDIATE: return "IMMEDIATE";
        case ADDR_DIRECT:    return "DIRECT   ";
    }
    return "?";
}

static const char *size_name(SizeMarker s)
{
    switch (s) {
        case SZ_NONE: return "-";
        case SZ_BYTE: return "byte(*)";
        case SZ_WORD: return "word(#)";
    }
    return "?";
}

typedef struct {
    long total;
    long ok;
    long by_mode[4];
} Stats;

static void classify_and_report(const Operand *op, Stats *st, const char *ctx, bool verbose)
{
    ParsedOperand po = classify_operand(op);
    st->total++;
    st->by_mode[po.mode]++;
    if (po.ok) st->ok++;

    if (verbose || !po.ok) {
        char exprbuf[128] = "-";
        if (po.expr)
            expr_to_string(po.expr, exprbuf, sizeof(exprbuf));

        long constval;
        const char *cf = expr_try_eval_const(po.expr, &constval) ? "const" : "sym";

        printf("    %-9s size=%-8s reg=%.*s expr=%-24s [%s]%s  <- %s\n",
               addrmode_name(po.mode), size_name(po.size),
               po.reg.len ? (int)po.reg.len : 1, po.reg.len ? po.reg.text : "-",
               exprbuf, po.expr ? cf : "-",
               po.ok ? "" : "  *** CLASSIFY FAILED ***",
               ctx);
    }

    parsed_operand_free(&po);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s [-v] <file.s> [file2.s ...]\n", argv[0]);
        return 1;
    }

    bool verbose = false;
    int argi = 1;
    if (strcmp(argv[argi], "-v") == 0) { verbose = true; argi++; }

    Stats st;
    memset(&st, 0, sizeof(st));

    for (; argi < argc; argi++) {
        const char *path = argv[argi];
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); continue; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *src = malloc((size_t)sz + 1);
        size_t rd = fread(src, 1, (size_t)sz, f);
        src[rd] = '\0';
        fclose(f);

        printf("=== %s ===\n", path);

        Parser p;
        parser_init(&p, src, rd, path);
        Statement stmt;
        while (parser_next_statement(&p, &stmt)) {
            if (stmt.kind != STMT_INSTRUCTION && stmt.kind != STMT_DATA_VALUE &&
                stmt.kind != STMT_ASSIGNMENT)
                continue;

            char ctx[64];
            snprintf(ctx, sizeof(ctx), "%s:%d", path, stmt.line);

            for (int i = 0; i < stmt.noperands; i++)
                classify_and_report(&stmt.operands[i], &st, ctx, verbose);
        }
        free(src);
    }

    printf("\n=== SUMMARY ===\n");
    printf("total operands classified: %ld\n", st.total);
    printf("  ok:     %ld\n", st.ok);
    printf("  failed: %ld\n", st.total - st.ok);
    printf("by mode: REGISTER=%ld INDIRECT=%ld IMMEDIATE=%ld DIRECT=%ld\n",
           st.by_mode[ADDR_REGISTER], st.by_mode[ADDR_INDIRECT],
           st.by_mode[ADDR_IMMEDIATE], st.by_mode[ADDR_DIRECT]);

    return (st.total != st.ok) ? 1 : 0;
}
