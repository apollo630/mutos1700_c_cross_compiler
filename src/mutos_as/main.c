/*
 * main.c - Milestone 2 skeleton driver.
 *
 * Reads a .s source file, runs it through the statement parser, and
 * prints a human-readable dump of every statement. This is NOT the
 * final mutos_as CLI - it exists to validate the lexer/parser against
 * real c1-generated assembly (malloc.s, mch.s, reloctest.s,
 * opcodetest.s, the add.o milestone-1 example) before Pass 2
 * (instruction encoding) is built on top of it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_as.h"

static const char *stmt_kind_name(StmtKind k)
{
    switch (k) {
        case STMT_EMPTY:       return "EMPTY";
        case STMT_DIRECTIVE:   return "DIRECTIVE";
        case STMT_ASSIGNMENT:  return "ASSIGNMENT";
        case STMT_INSTRUCTION: return "INSTRUCTION";
        case STMT_LABEL_ONLY:  return "LABEL_ONLY";
        case STMT_DATA_VALUE:  return "DATA_VALUE";
    }
    return "?";
}

static void print_token(const Token *t)
{
    switch (t->type) {
        case TOK_NUMBER:
            printf("%.*s{=%ld,base=%d}", (int)t->len, t->text, t->num_value, (int)t->num_base);
            break;
        case TOK_LOCAL_LABEL_REF:
            printf("localref(%d,%c)", t->local_digit, t->text[t->len - 1]);
            break;
        default:
            printf("%.*s", (int)t->len, t->text);
            break;
    }
}

static void print_operand(const Operand *op)
{
    for (int i = 0; i < op->ntokens; i++) {
        if (i) printf(" ");
        print_token(&op->tokens[i]);
    }
}

static void print_statement(const Statement *s)
{
    printf("[line %3d] %-11s", s->line, stmt_kind_name(s->kind));

    if (s->nlabels > 0) {
        printf(" labels=");
        for (int i = 0; i < s->nlabels; i++) {
            if (i) printf(",");
            print_token(&s->labels[i]);
        }
    }

    if (s->kind == STMT_DIRECTIVE || s->kind == STMT_INSTRUCTION || s->kind == STMT_ASSIGNMENT) {
        printf(" name=");
        print_token(&s->mnemonic_or_name);
    }

    if (s->kind == STMT_ASSIGNMENT) {
        printf(" rhs=[");
        print_operand(&s->operands[0]);
        printf("]");
    } else if (s->kind == STMT_DATA_VALUE) {
        printf(" value=[");
        print_operand(&s->operands[0]);
        printf("]");
    } else if (s->noperands > 0) {
        printf(" operands=");
        for (int i = 0; i < s->noperands; i++) {
            if (i) printf(" | ");
            printf("[");
            print_operand(&s->operands[i]);
            printf("]");
        }
    }

    printf("\n");
}

static char *read_whole_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    if (out_len) *out_len = rd;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.s> [file2.s ...]\n", argv[0]);
        return 1;
    }

    int total_errors = 0;
    int total_statements = 0;

    for (int fi = 1; fi < argc; fi++) {
        const char *path = argv[fi];
        size_t len = 0;
        char *src = read_whole_file(path, &len);
        if (!src) {
            total_errors++;
            continue;
        }

        printf("=== %s ===\n", path);

        Parser p;
        parser_init(&p, src, len, path);

        Statement stmt;
        int count = 0;
        while (parser_next_statement(&p, &stmt)) {
            if (stmt.kind != STMT_EMPTY) {
                print_statement(&stmt);
                count++;
            }
        }
        printf("--- %d non-empty statements, %d parse error(s) ---\n\n",
               count, p.error_count);

        total_statements += count;
        total_errors += p.error_count;
        free(src);
    }

    printf("=== TOTAL: %d statements across %d file(s), %d error(s) ===\n",
           total_statements, argc - 1, total_errors);

    return total_errors > 0 ? 1 : 0;
}
