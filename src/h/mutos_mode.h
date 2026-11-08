/*
 * mutos_mode.h
 *
 * The corrected mode and the rule log, shared by the tools
 * (docs/TOOLCHAIN_PLAN.md, section 7, decisions 13 and 15; the list of
 * the rules and their hooks is docs/RULES.md).
 *
 * THE MODE. A tool behaves either as the original MUTOS 1700 binary does,
 * its bugs included (the original mode), or with the bugs of
 * docs/RULES.md corrected (the corrected mode). Until step F5 of the plan
 * the original mode is the default; F5 makes the corrected mode the
 * default, and the original's behaviour stays selectable. The mode is
 * chosen
 *   - by a switch on the command line of cc, as and ld: -F (the corrected
 *     mode, "fixed") or -H (the original's behaviour, "historic"); when
 *     both are given, the last one counts;
 *   - else by the environment variable MUTOS_MODE: "corrected" or
 *     "original"; unset or empty, it is the default. It is there for
 *     makefiles whose command lines cannot carry a switch (the kernel's
 *     rules for c.c, symtab.c and mch, and its ld). Any other value is an
 *     error.
 * cc puts the mode a switch chose into the environment of the passes it
 * runs (c0, c1, as, ld): the passes need no new argument - c0, for one,
 * takes any fourth argument for -p's profiling. The names were confirmed
 * by the project owner on 2026-11-08.
 *
 * THE RULE LOG. A developer aid: when the environment variable
 * MUTOS_RULELOG names a file, every rule hook a tool passes appends one
 * line to it -
 *
 *     <tool> <rule> <what> [<key>=<value> ...]
 *
 * <tool> is c0, c1, c2, as or ld; <rule> the rule's name in docs/RULES.md
 * (C1-03, V30-PUSH, ...). <what> is "applied" when the rule changed what
 * the tool writes (a correction in the corrected mode, a V30 rule under
 * -1), "met" when the original's path was taken where a rule is hooked:
 * in the original mode, or by a rule that only reports so far (a
 * diagnostic hook, docs/RULES.md) - what the tool writes is then the
 * original's. The keys say where: line= (the source line the tool was
 * compiling, as its messages give it), src= (the source cc runs the passes
 * for: with the log on, cc puts cpp's input into MUTOS_RULELOG_SRC, as c1
 * knows only line numbers), out=<first>-<end> (the bytes of
 * the tool's output that came from the statement or the instruction the
 * rule concerns: offsets in the file it wrote, the end exclusive), and
 * what the rule itself adds. tests/rules/check_accounting.py reads the
 * log: every byte that differs between the two modes, or with and
 * without -1, must lie in an "applied" rule's out= range.
 *
 * Header only (static inline functions, C11 stdio), as mutos_root.h; the
 * locals are named mm_... so that a port's macros for its globals (c0.h
 * has "line", for one) cannot reach them.
 */

#ifndef MUTOS_MODE_H
#define MUTOS_MODE_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUTOS_MODE_ENV      "MUTOS_MODE"
#define MUTOS_RULELOG_ENV   "MUTOS_RULELOG"
#define MUTOS_RULELOG_SRC_ENV "MUTOS_RULELOG_SRC"

#define MUTOS_ORIGINAL      0
#define MUTOS_CORRECTED     1

/* The default, until step F5 of the plan (decision 13). */
#define MUTOS_MODE_DEFAULT  MUTOS_ORIGINAL

/* The switch letters: -F the corrected mode, -H the original's. */
#define MUTOS_MODE_FIXED    'F'
#define MUTOS_MODE_HISTORIC 'H'

/* The mode a switch letter chooses, or -1 for another letter. */
static inline int mutos_mode_letter(int mm_c)
{
    if (mm_c == MUTOS_MODE_FIXED)
        return MUTOS_CORRECTED;
    if (mm_c == MUTOS_MODE_HISTORIC)
        return MUTOS_ORIGINAL;
    return -1;
}

/* The value MUTOS_MODE has for a mode. */
static inline const char *mutos_mode_name(int mm_mode)
{
    return mm_mode == MUTOS_CORRECTED ? "corrected" : "original";
}

/*
 * mutos_mode_env - the mode MUTOS_MODE chooses: the default when it is
 * unset or empty; -1 when it is neither "corrected" nor "original"
 * (*bad, when not NULL, is then its value).
 */
static inline int mutos_mode_env(const char **mm_bad)
{
    const char *mm_v = getenv(MUTOS_MODE_ENV);

    if (mm_v == NULL || mm_v[0] == '\0')
        return MUTOS_MODE_DEFAULT;
    if (strcmp(mm_v, "corrected") == 0)
        return MUTOS_CORRECTED;
    if (strcmp(mm_v, "original") == 0)
        return MUTOS_ORIGINAL;
    if (mm_bad != NULL)
        *mm_bad = mm_v;
    return -1;
}

/*
 * mutos_mode_init - the mode of a tool whose command line has no switch
 * (yet): MUTOS_MODE's. A value that is neither word is reported on the
 * standard error, as "<prog>: MUTOS_MODE=<value>: neither corrected nor
 * original", and the tool exits with status 1.
 */
static inline int mutos_mode_init(const char *mm_prog)
{
    const char *mm_bad = "";
    int mm_mode = mutos_mode_env(&mm_bad);

    if (mm_mode < 0) {
        fprintf(stderr, "%s: %s=%s: neither corrected nor original\n",
                mm_prog, MUTOS_MODE_ENV, mm_bad);
        exit(1);
    }
    return mm_mode;
}

/* Whether the rule log is on. */
static inline int mutos_rulelog_on(void)
{
    const char *mm_f = getenv(MUTOS_RULELOG_ENV);

    return mm_f != NULL && mm_f[0] != '\0';
}

/*
 * mutos_rulelog - append "<tool> <rule> <what> <keys>\n" to the rule log
 * (nothing when it is off). fmt gives the keys ("line=%d out=%ld-%ld"),
 * or is NULL. The line is written by one fclose() of a file opened for
 * appending, so that the passes of parallel compilations can share a log.
 * A log that cannot be opened is skipped: it is a developer aid, and the
 * tool's own work must not depend on it.
 */
static inline void mutos_rulelog(const char *mm_tool, const char *mm_rule,
                                 const char *mm_what, const char *mm_fmt, ...)
{
    const char *mm_path = getenv(MUTOS_RULELOG_ENV);
    char mm_line[512];
    size_t mm_n;
    FILE *mm_f;

    if (mm_path == NULL || mm_path[0] == '\0')
        return;
    mm_n = (size_t)snprintf(mm_line, sizeof mm_line, "%s %s %s", mm_tool, mm_rule, mm_what);
    if (mm_fmt != NULL && mm_n + 1 < sizeof mm_line) {
        va_list mm_ap;

        mm_line[mm_n++] = ' ';
        va_start(mm_ap, mm_fmt);
        vsnprintf(mm_line + mm_n, sizeof mm_line - mm_n, mm_fmt, mm_ap);
        va_end(mm_ap);
        mm_n = strlen(mm_line);
    }
    if (mm_n >= sizeof mm_line - 1)
        mm_n = sizeof mm_line - 2;
    mm_line[mm_n++] = '\n';
    mm_line[mm_n] = '\0';
    if ((mm_f = fopen(mm_path, "a")) == NULL)
        return;
    fputs(mm_line, mm_f);
    fclose(mm_f);
}

#endif /* MUTOS_MODE_H */
