/*
 * parser.c - groups the token stream into Statements.
 *
 * This is a STATEMENT-level parser only. Operands are collected as flat
 * token runs (split on top-level commas, respecting parenthesis depth)
 * rather than parsed into an expression tree - that belongs to the
 * Pass 2 encoder, once the opcode/addressing-mode tables exist. The
 * goal here is to prove that real c1-generated .s files (malloc.s,
 * mch.s, reloctest.s, opcodetest.s, the add.o milestone-1 example)
 * parse cleanly end-to-end at the statement/label/directive level.
 */

#include <stdio.h>
#include <string.h>

#include "mutos_as.h"

void parser_init(Parser *p, const char *src, size_t len, const char *filename)
{
    lexer_init(&p->lx, src, len, filename);
    p->queued = 0;
    p->error_count = 0;
    p->filename = filename;
}

/* Returns the token `ahead` positions from the current position without
 * consuming it (ahead=0 is the very next token). */
static Token peek_tok(Parser *p, int ahead)
{
    while (p->queued <= ahead) {
        p->queue[p->queued++] = lexer_next(&p->lx);
    }
    return p->queue[ahead];
}

static Token consume_tok(Parser *p)
{
    Token t = peek_tok(p, 0);
    /* shift queue down by one */
    for (int i = 1; i < p->queued; i++)
        p->queue[i - 1] = p->queue[i];
    p->queued--;
    return t;
}

static void parse_error(Parser *p, const Token *t, const char *msg)
{
    fprintf(stderr, "%s:%d: error: %s (near token '%.*s')\n",
            p->filename ? p->filename : "<input>",
            t->line, msg, (int)t->len, t->text);
    p->error_count++;
}

static bool is_separator(TokenType tt)
{
    return tt == TOK_NEWLINE || tt == TOK_SEMI || tt == TOK_EOF;
}

/* Collects tokens for a single operand into `op`, stopping at a
 * top-level comma (paren depth 0) or a statement separator. Does not
 * consume the terminating comma/separator. */
static void parse_operand(Parser *p, Operand *op)
{
    op->ntokens = 0;
    int depth = 0;
    for (;;) {
        Token t = peek_tok(p, 0);
        if (is_separator(t.type))
            break;
        if (t.type == TOK_COMMA && depth == 0)
            break;
        if (t.type == TOK_LPAREN) depth++;
        if (t.type == TOK_RPAREN) { if (depth > 0) depth--; }

        if (op->ntokens < MAX_TOKENS_PER_OPERAND) {
            op->tokens[op->ntokens++] = t;
        } /* else: silently truncate for this skeleton; Pass 2 can widen the limit */
        consume_tok(p);
    }
}

static void parse_operand_list(Parser *p, Statement *stmt)
{
    stmt->noperands = 0;
    /* Nothing to do if we're already at a separator (no operands at all). */
    if (is_separator(peek_tok(p, 0).type))
        return;

    for (;;) {
        if (stmt->noperands < MAX_OPERANDS) {
            parse_operand(p, &stmt->operands[stmt->noperands]);
            stmt->noperands++;
        } else {
            /* Too many operands for this skeleton's fixed array - drain
             * and drop, rather than corrupt memory. Real .byte lists
             * with many entries will need MAX_OPERANDS raised later. */
            Operand dummy;
            parse_operand(p, &dummy);
        }
        Token t = peek_tok(p, 0);
        if (t.type == TOK_COMMA) {
            consume_tok(p);
            continue;
        }
        break;
    }
}

bool parser_next_statement(Parser *p, Statement *out)
{
    memset(out, 0, sizeof(*out));

    /* Skip blank statements (bare separators). */
    for (;;) {
        Token t = peek_tok(p, 0);
        if (t.type == TOK_EOF)
            return false;
        if (t.type == TOK_NEWLINE || t.type == TOK_SEMI) {
            consume_tok(p);
            continue;
        }
        break;
    }

    out->line = peek_tok(p, 0).line;

    /* Leading labels: either "IDENT ':'" (name label) or a single
     * TOK_LOCAL_LABEL_DEF (numeric local label; the lexer already
     * consumed its trailing ':'). Real c1 output can chain several,
     * e.g. "L10:L8:mov dx,di" from malloc.s. */
    for (;;) {
        Token t0 = peek_tok(p, 0);

        if (t0.type == TOK_LOCAL_LABEL_DEF) {
            if (out->nlabels < 4)
                out->labels[out->nlabels++] = t0;
            out->has_local_label_def = true;
            out->local_label_digit = t0.local_digit;
            consume_tok(p);
            continue;
        }

        if (t0.type == TOK_IDENT) {
            Token t1 = peek_tok(p, 1);
            if (t1.type == TOK_COLON) {
                if (out->nlabels < 4)
                    out->labels[out->nlabels++] = t0;
                consume_tok(p); /* ident */
                consume_tok(p); /* ':' */
                continue;
            }
        }
        break;
    }

    Token t = peek_tok(p, 0);

    if (is_separator(t.type)) {
        /* Label(s) with nothing else on the statement. */
        out->kind = out->nlabels > 0 ? STMT_LABEL_ONLY : STMT_EMPTY;
        return true;
    }

    if (t.type == TOK_DOT_IDENT) {
        out->kind = STMT_DIRECTIVE;
        out->mnemonic_or_name = t;
        consume_tok(p);
        parse_operand_list(p, out);
        return true;
    }

    if (t.type == TOK_IDENT || t.type == TOK_DOT) {
        Token t1 = peek_tok(p, 1);
        if (t1.type == TOK_EQUALS) {
            /* Assignment form: name = expression, OR the location-
             * counter assignment ".=.+4" seen in mch.s (crt0 uses this
             * to reserve raw bytes without a .word/.byte directive). */
            out->kind = STMT_ASSIGNMENT;
            out->mnemonic_or_name = t;
            consume_tok(p); /* name or '.' */
            consume_tok(p); /* '=' */
            out->noperands = 1;
            parse_operand(p, &out->operands[0]);
            return true;
        }

        if (t.type == TOK_IDENT) {
            /* Instruction: mnemonic (possibly with a byte-size 'b'
             * suffix such as 'movb', or a prefix like a segment
             * override - both are just part of the identifier text at
             * this level) followed by zero or more comma-separated
             * operands. */
            out->kind = STMT_INSTRUCTION;
            out->mnemonic_or_name = t;
            consume_tok(p);
            parse_operand_list(p, out);
            return true;
        }
        /* else: bare '.' not followed by '=' falls through to the
         * data-value case below (e.g. "_szicode: . - _icode"). */
    }

    /* A label followed directly by a raw expression with no directive/
     * mnemonic name at all - real c1/crt0 output uses this to place an
     * initialized data word right after its label, e.g.
     * "_daten_v:/1234" (reloctest.s) or "_szicode: . - _icode"
     * (mch.s, location-counter arithmetic). Treat like an implicit
     * ".word <expr>". */
    if (t.type == TOK_NUMBER || t.type == TOK_DOT || t.type == TOK_STAR ||
        t.type == TOK_HASH || t.type == TOK_SLASH || t.type == TOK_MINUS ||
        t.type == TOK_PLUS || t.type == TOK_LOCAL_LABEL_REF || t.type == TOK_LPAREN) {
        out->kind = STMT_DATA_VALUE;
        out->noperands = 1;
        parse_operand(p, &out->operands[0]);
        return true;
    }

    /* Anything else at statement-start is unexpected; report and skip
     * to the next separator so a single bad line doesn't cascade. */
    parse_error(p, &t, "unexpected token at start of statement");
    while (!is_separator(peek_tok(p, 0).type))
        consume_tok(p);
    out->kind = STMT_EMPTY;
    return true;
}
