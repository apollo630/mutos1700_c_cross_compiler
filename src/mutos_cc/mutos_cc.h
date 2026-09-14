/*
 * mutos_cc.h - shared constants for the MUTOS 1700 C compiler's
 * two passes, mutos_c0 (front end: lex/parse/typecheck, emits the
 * temp1/temp2 intermediate-code stream) and mutos_c1 (back end:
 * reads temp1/temp2, emits mutos_as-syntax assembly text).
 *
 * The intermediate-code "operator" values below are transcribed
 * VERBATIM from v7/cc/c0.h's manifest constants (see that file for
 * the original comments/grouping). Per CLAUDE.md's Workflow
 * Guideline 3, /v7/ is historical reference only - these values are
 * carried over deliberately (they are load-bearing: they are the
 * literal byte tags written into the temp1 stream by c0 and read
 * back by c1), not "mixed in" logic. Where MUTOS 1700's real
 * compiler diverges from this vanilla V7 source, the divergence is
 * called out explicitly below with the golden evidence that
 * establishes it - see docs/DEVLOG.md's Milestone 4 "temp1/temp2
 * wire format" section for the full derivation.
 */

#ifndef MUTOS_CC_H
#define MUTOS_CC_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Compiler-wide limits */
#define MCC_NCPS        8   /* significant chars per internal symbol,
                              * v7/cc/c0.h's NCPS - confirmed load-
                              * bearing project-wide, see CLAUDE.md's
                              * "Identifier length limits" rule. */

/* ------------------------------------------------------------------ */
/* Punctuators / literal-class tokens (v7/cc/c0.h) - only the ones
 * mutos_c0's lexer currently needs are used, but the full set is
 * transcribed for forward compatibility with later grammar coverage. */
enum {
    OP_EOFC    = 0,
    OP_SEMI    = 1,
    OP_LBRACE  = 2,
    OP_RBRACE  = 3,
    OP_LBRACK  = 4,
    OP_RBRACK  = 5,
    OP_LPARN   = 6,
    OP_RPARN   = 7,
    OP_COLON   = 8,
    OP_COMMA   = 9,
    OP_FSEL    = 10,
    OP_CAST    = 11,
    OP_ETYPE   = 12,
    OP_ITOP    = 13,
    OP_PTOI    = 14,
    OP_LTOP    = 15,

    OP_KEYW    = 19,
    OP_NAME    = 20,
    OP_CON     = 21,
    OP_STRING  = 22,
    OP_FCON    = 23,
    OP_SFCON   = 24,
    OP_LCON    = 25,
    OP_SLCON   = 26,

    OP_INCBEF  = 30,
    OP_DECBEF  = 31,
    OP_INCAFT  = 32,
    OP_DECAFT  = 33,
    OP_EXCLA   = 34,
    OP_AMPER   = 35,
    OP_STAR    = 36,
    OP_NEG     = 37,
    OP_COMPL   = 38,

    OP_DOT     = 39,
    OP_PLUS    = 40,
    OP_MINUS   = 41,
    OP_TIMES   = 42,
    OP_DIVIDE  = 43,
    OP_MOD     = 44,
    OP_RSHIFT  = 45,
    OP_LSHIFT  = 46,
    OP_AND     = 47,
    OP_OR      = 48,
    OP_EXOR    = 49,
    OP_ARROW   = 50,
    OP_ITOF    = 51,
    OP_FTOI    = 52,
    OP_LOGAND  = 53,
    OP_LOGOR   = 54,
    OP_FTOL    = 56,
    OP_LTOF    = 57,
    OP_ITOL    = 58,
    OP_LTOI    = 59,

    OP_EQUAL   = 60,
    OP_NEQUAL  = 61,
    OP_LESSEQ  = 62,
    OP_LESS    = 63,
    OP_GREATEQ = 64,
    OP_GREAT   = 65,
    OP_LESSEQP = 66,
    OP_LESSP   = 67,
    OP_GREATQP = 68,
    OP_GREATP  = 69,

    OP_ASPLUS  = 70,
    OP_ASMINUS = 71,
    OP_ASTIMES = 72,
    OP_ASDIV   = 73,
    OP_ASMOD   = 74,
    OP_ASRSH   = 75,
    OP_ASLSH   = 76,
    OP_ASSAND  = 77,
    OP_ASOR    = 78,
    OP_ASXOR   = 79,
    OP_ASSIGN  = 80,

    OP_SIZEOF  = 91,
    OP_QUEST   = 90,
    OP_MAX     = 93,
    OP_MAXP    = 94,
    OP_MIN     = 95,
    OP_MINP    = 96,
    OP_SEQNC   = 97,
    OP_CALL    = 100,
    OP_MCALL   = 101,
    OP_JUMP    = 102,
    OP_CBRANCH = 103,
    OP_INIT    = 104,
    OP_SETREG  = 105,
    OP_ITOC    = 109,
    OP_RFORCE  = 110,
    OP_BRANCH  = 111,
    OP_LABEL   = 112,
    OP_NLABEL  = 113,
    OP_RLABEL  = 114,
    OP_STRASG  = 115,

    OP_SEOF    = 200,  /* stack EOF marker inside c0's own expr
                         * compilation - never written to temp1/2 */

    /* "Special operators in intermediate code" - segment/pseudo-op
     * tags written directly to temp1 by c0's declaration/function
     * driver (c02.c in the v7 reference), not by treeout(). */
    OP_BDATA   = 200,
    OP_WDATA   = 201,
    OP_PROG    = 202,
    OP_DATA    = 203,
    OP_BSS     = 204,
    OP_CSPACE  = 205,
    OP_SSPACE  = 206,
    OP_SYMDEF  = 207,
    OP_SAVE    = 208,
    OP_RETRN   = 209,
    OP_EVEN    = 210,
    OP_PROFIL  = 212,
    OP_SWIT    = 213,
    OP_EXPR    = 214,
    OP_SNAME   = 215,
    OP_RNAME   = 216,
    OP_ANAME   = 217,
    OP_NULLOP  = 218,
    OP_SETSTK  = 219,
    OP_SINIT   = 220
};

/* ------------------------------------------------------------------ */
/* Data types (v7/cc/c0.h's TYPE field encoding; only the base kinds
 * are listed here - PTR/FUNC/ARRAY degree-of-reference bits are not
 * yet needed by mutos_c0/c1's current (00_smoke) grammar coverage). */
enum {
    TY_INT    = 0,
    TY_CHAR   = 1,
    TY_FLOAT  = 2,
    TY_DOUBLE = 3,
    TY_STRUCT = 4,
    TY_LONG   = 6,
    TY_UNSIGN = 7,
    TY_UNION  = 8
};

/* ------------------------------------------------------------------ */
/* Storage classes (v7/cc/c0.h) - transcribed for forward
 * compatibility; mutos_c0's current grammar coverage only ever
 * assigns EXTERN (to file-scope function names). */
enum {
    SC_TYPEDEF = 9,
    SC_MOS     = 10,
    SC_AUTO    = 11,
    SC_EXTERN  = 12,
    SC_STATIC  = 13,
    SC_REG     = 14,
    SC_STRTAG  = 15,
    SC_ARG     = 16,
    SC_ARG1    = 17,
    SC_AREG    = 18,
    SC_DEFXTRN = 20,
    SC_MOU     = 21,
    SC_ENUMTAG = 22,
    SC_ENUMCON = 24
};

/* ------------------------------------------------------------------ */
/* MUTOS 1700 deltas from vanilla V7 cc - each confirmed directly
 * against the real-hardware-generated temp1/.s goldens in
 * tests/mutos_cc/00_smoke/ (see docs/DEVLOG.md's Milestone 4 "temp1/
 * temp2 wire format" section for the byte-level derivation of each):
 *
 *   1. STAUTO is -4, not V7 PDP-11's -6. MUTOS's fixed prologue only
 *      callee-saves 2 registers (di, si - see docs/MUTOS_C_ABI.md
 *      sect. 1.2/1.4), vs V7's 3 (r2,r3,r4), so the first local slot
 *      sits 4 bytes below bp, not 6 - confirmed via SETSTK's emitted
 *      value (4) for a function with zero real locals.
 *   2. cfunc()'s function-header opcode sequence emits an extra EVEN
 *      between PROG and RLABEL (i.e. "PROG, EVEN, RLABEL, name" where
 *      V7 emits only "PROG, RLABEL, name") - presumably added for the
 *      8086's word-alignment needs at a function's entry point.
 *      Confirmed: EVEN's tag byte (0xD2) appears, byte-exact, between
 *      PROG's (0xCA) and RLABEL's (0x72) in every 00_smoke golden.
 *   3. RETRN carries one extra numeric argument (the function's
 *      return type code), which c1 renders as a "|RTYP n" comment
 *      immediately before the "jmp cret" epilogue tail-jump. V7's
 *      outcode("BNB", LABEL, retlab, RETRN) has no such argument;
 *      MUTOS's is effectively outcode("BNBN", LABEL, retlab, RETRN,
 *      type). Confirmed via the trailing "00 00" word after RETRN's
 *      tag byte in every 00_smoke golden, and the matching "|RTYP 0"
 *      text in every 00_smoke .s.golden.
 *   4. The initial "register variable" budget (funchead()'s `regvar`,
 *      V7 initializes to 5 in cfunc()) is 4 for MUTOS - confirmed via
 *      SETREG's emitted value (4) immediately after SAVE, for a
 *      function with no register-class parameters/locals to consume
 *      any of the budget.
 */
#define MCC_STAUTO         (-4)
#define MCC_INIT_REGVAR    4
#define MCC_NSAVEREG       3   /* bp, di, si - see docs/MUTOS_C_ABI.md
                                 * sect. 1.2; SAVE's fixed prologue
                                 * always reports this via the
                                 * "|NREG 3" comment, unconditionally -
                                 * see sect. 1.2's "no leaf-function
                                 * elision" finding. */

#endif /* MUTOS_CC_H */
