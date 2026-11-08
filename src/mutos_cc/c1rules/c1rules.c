/*
 * c1rules.c - what mutos_c1's rules share (c1rules.h): the mode, -1, and
 * the rule log's entries, which wait for the end of the operation they
 * were met in.
 */
#include "../c1port/c1.h"

#include <stdarg.h>
#include <string.h>

int	c1_mode = MUTOS_MODE_DEFAULT;
int	c1_v30;
const char *c1_outname;

#define	NPENDING	32

static struct pending {
	const char	*rule;
	int		applied;
	int		line;
	c1_rule_check	*check;
	const void	*ctx;
	char		keys[160];
} pending[NPENDING];
static	int	npending;
static	int	logon = -1;	/* -1: not looked at yet */
static	long	opstart;	/* where the current operation's output starts */

void
c1_rule(const char *rule, int applied, c1_rule_check *check, const void *ctx,
    const char *fmt, ...)
{
	struct pending *e;

	if (logon < 0)
		logon = mutos_rulelog_on();
	if (!logon || npending >= NPENDING)
		return;
	e = &pending[npending++];
	e->rule = rule;
	e->applied = applied;
	e->line = line;
	e->check = check;
	e->ctx = ctx;
	e->keys[0] = '\0';
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(e->keys, sizeof e->keys, fmt, ap);
		va_end(ap);
	}
}

/*
 * The bytes c1 has written from opstart to end, for a check: stdout is
 * flushed (its contents do not change) and the file read back by name.
 * NULL if they cannot be read.
 */
static char *
outtext(long end, size_t *n)
{
	FILE *f;
	char *buf;

	if (c1_outname == NULL || end < opstart || fflush(stdout) != 0)
		return NULL;
	if ((f = fopen(c1_outname, "rb")) == NULL)
		return NULL;
	*n = (size_t)(end - opstart);
	if ((buf = malloc(*n + 1)) == NULL || fseek(f, opstart, SEEK_SET) != 0
	 || fread(buf, 1, *n, f) != *n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	buf[*n] = '\0';
	fclose(f);
	return buf;
}

void
c1_rule_mark(void)
{
	long end;
	char *text = NULL;
	size_t n = 0;
	int i, textread = 0;
	const char *src = getenv(MUTOS_RULELOG_SRC_ENV);

	if (logon < 0)
		logon = mutos_rulelog_on();
	if (!logon)
		return;
	end = ftell(stdout);
	for (i = 0; i < npending; i++) {
		struct pending *e = &pending[i];
		char keys[160];

		keys[0] = '\0';
		if (e->check != NULL) {
			if (!textread) {
				text = outtext(end, &n);
				textread = 1;
			}
			if (!e->check(e->ctx, text, n, keys, sizeof keys))
				continue;
		}
		mutos_rulelog("c1", e->rule, e->applied ? "applied" : "met",
		    "%s%s%sline=%d out=%ld-%ld%s%s%s%s", src ? "src=" : "",
		    src ? src : "", src ? " " : "", e->line, opstart, end,
		    e->keys[0] ? " " : "", e->keys, keys[0] ? " " : "", keys);
	}
	free(text);
	npending = 0;
	opstart = end;
}
