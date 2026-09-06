/*
 * main.c - CLI entry point for mutos_cpp, the MUTOS 1700 cross
 * C preprocessor.
 *
 * Usage:
 *   mutos_cpp [-Dname[=value]] [-Uname] [-Idir] [-P] [-C] [-R]
 *             [-o outfile] [infile [outfile]]
 *
 * With no infile, reads stdin (matching the real cpp). With no
 * outfile (neither -o nor positional), writes to stdout.
 *
 * -P suppresses "# N \"file\"" line-marker output. This tool never
 * emits those markers regardless (see README for why - matching this
 * project's golden reference files, which were all generated with -P
 * and contain zero such markers), so -P is accepted for command-line
 * compatibility but has no additional effect.
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mutos_cpp.h"

void cpp_error(CppState *cs, const char *fmt, ...) {
    const char *fn = cur_filename(cs);
    if (fn && fn[0]) fprintf(stderr, "%s: ", fn);
    fprintf(stderr, "%d: ", cur_lineno(cs));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    cs->exit_status++;
}

void cpp_warn(CppState *cs, const char *fmt, ...) {
    const char *fn = cur_filename(cs);
    if (fn && fn[0]) fprintf(stderr, "%s: ", fn);
    fprintf(stderr, "%d: ", cur_lineno(cs));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    /* Warnings are non-fatal and do not affect exit_status, matching
     * the reference cpp's ppwarn() (which restores exfail afterward). */
}

static void define_from_cli(CppState *cs, const char *arg) {
    /* "-Dname" -> defined as 1; "-Dname=value" -> defined as value. */
    const char *eq = strchr(arg, '=');
    Macro *m = calloc(1, sizeof *m);
    if (eq) {
        m->name = xstrndup(arg, (size_t)(eq - arg));
        m->is_function_like = false;
        m->nparams = 0;
        MacroBodyPiece *pc = calloc(1, sizeof *pc);
        pc->literal = xstrdup(eq + 1);
        pc->param_index = -1;
        m->body = pc;
    } else {
        m->name = xstrdup(arg);
        m->is_function_like = false;
        m->nparams = 0;
        MacroBodyPiece *pc = calloc(1, sizeof *pc);
        pc->literal = xstrdup("1");
        pc->param_index = -1;
        m->body = pc;
    }
    unsigned h = macro_hash(m->name, cs->macro_table_size);
    m->next = cs->macros[h];
    cs->macros[h] = m;
}

int main(int argc, char **argv) {
    CppState cs;
    memset(&cs, 0, sizeof cs);
    macro_table_init(&cs, 4096);
    cs.out = stdout;

    const char *infile = NULL;
    const char *outfile = NULL;

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] == '-' && a[1] != '\0') {
            switch (a[1]) {
                case 'D': define_from_cli(&cs, a + 2); break;
                case 'U': macro_undef(&cs, a + 2); break;
                case 'I':
                    if (cs.n_include_dirs < MCPP_MAX_INCLUDE_DIRS)
                        cs.include_dirs[cs.n_include_dirs++] = xstrdup(a + 2);
                    else
                        fprintf(stderr, "mutos_cpp: too many -I directories, ignoring %s\n", a);
                    break;
                case 'P': cs.pflag = true; break;
                case 'C': cs.cflag = true; break;
                case 'R': cs.rflag = true; break;
                case 'o':
                    if (a[2]) outfile = a + 2;
                    else if (i + 1 < argc) outfile = argv[++i];
                    break;
                case 'E': /* accepted no-op, matches reference cpp */ break;
                default:
                    fprintf(stderr, "mutos_cpp: unknown flag %s\n", a);
                    break;
            }
            continue;
        }
        if (!infile) infile = a;
        else if (!outfile) outfile = a;
        else fprintf(stderr, "mutos_cpp: extraneous name %s\n", a);
    }

    if (outfile) {
        cs.out = fopen(outfile, "w");
        if (!cs.out) {
            fprintf(stderr, "mutos_cpp: Can't create %s\n", outfile);
            return 8;
        }
    }

    if (infile) {
        char *dir = dir_of(infile);
        if (!source_push_file(&cs, infile, dir)) {
            fprintf(stderr, "mutos_cpp: No source file %s\n", infile);
            free(dir);
            return 8;
        }
        free(dir);
    } else {
        Source *s = calloc(1, sizeof *s);
        s->kind = SRC_FILE;
        s->fp = stdin;
        s->filename = xstrdup("");
        s->dir = xstrdup(".");
        s->lineno = 1;
        s->parent = NULL;
        cs.src = s;
    }

    scan_run(&cs);

    if (cs.out != stdout) fclose(cs.out);
    macro_free_all(&cs);
    for (int i = 0; i < cs.n_include_dirs; i++) free(cs.include_dirs[i]);

    return cs.exit_status ? 1 : 0;
}
