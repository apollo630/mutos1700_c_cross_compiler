/*
 * misc.c - names, strings, macros and messages: v7's misc.c, which the
 * original MUTOS 1700 make (text 0fb6..157a, 17 functions) has byte for byte.
 * The offset of each function in the original's text is in its comment.
 *
 * What the port adds to v7's code, all of it on the host's side of the
 * line: the buffers of the original's stack frames - sourcename[100],
 * string[2500] and the others - are bounded, and a name or a command that
 * would run over one is refused (mk_trap()); the sizes of the heap blocks
 * and of the stack frames are counted for the 64K segment; the original's
 * ints are 16 bits.
 */
#include "make.h"

#include <stdlib.h>
#include <string.h>

static struct nameblock *hashtab[HASHSIZE];
static mint nhashed;

/* The AX the original's setvar() returns. Its last statement is the test of
 * the used bit, and the function has no return: the caller of "setvar("@",
 * ..); setvar("?", mkqlist(qchain))" passes that AX into mkqlist(), which
 * does not set it when its chain is empty (see mkqlist() below). */
static int setvar_ax_ptr;

/* simple linear hash. hash function is sum of characters mod hash table
 * size. 0fb6 */
mint
hashloc(const char *s)
{
	mint i;
	mint hashval;
	const char *t;

	hashval = 0;
	for (t = s; *t != '\0'; ++t)
		hashval = (mint)(hashval + *t);
	hashval = (mint)(hashval % HASHSIZE);
	if (hashval < 0)
		/* the sum of a long name of bytes above 0x7f wrapped: the
		 * original indexes hashtab[] below its start */
		mk_trap("the hash of a name is negative");

	for (i = hashval; hashtab[i] != NULL && unequal(s, hashtab[i]->namep);
	    i = (mint)((i + 1) % HASHSIZE))
		;
	return i;
}

/* 101e */
struct nameblock *
srchname(const char *s)
{
	return hashtab[hashloc(s)];
}

/* 1038 */
struct nameblock *
makename(const char *s)
{
	/* make a fresh copy of the string s */
	struct nameblock *p;

	if (nhashed++ > HASHSIZE - 3)
		fatal("Hash table overflow");

	p = ALLOC(nameblock);
	p->nxtnameblock = firstname;
	p->namep = copys(s);
	p->linep = NULL;
	p->done = 0;
	p->septype = 0;
	p->modtime = 0;

	firstname = p;
	if (mainname == NULL)
		if (s[0] != '.' || hasslash(s))
			mainname = p;

	hashtab[hashloc(s)] = p;

	return p;
}

/* 10c6 */
mint
hasslash(const char *s)
{
	for (; *s; ++s)
		if (*s == '/')
			return YES;
	return NO;
}

/* 10ee */
char *
copys(const char *s)
{
	char *t;

	mk_account(strlen(s) + 1);
	t = calloc(strlen(s) + 1, sizeof(char));
	if (t == NULL)
		fatal("out of memory");
	strcpy(t, s);
	return t;
}

/* c = concatenation of a and b; the original's c is an array of its
 * caller's frame, cap bytes long: a longer result is refused. 1132 */
char *
concat(const char *a, const char *b, char *c, size_t cap)
{
	if (strlen(a) + strlen(b) + 1 > cap)
		mk_trap("a name would run over a buffer of the original's stack");
	strcpy(c, a);
	strcat(c, b);
	return c;
}

/* is b the suffix of a? if so, set p = prefix. 116c */
mint
suffix(const char *a, const char *b, char *p, size_t cap)
{
	size_t la = strlen(a), lb = strlen(b);

	if (la < lb)
		return 0;
	if (strcmp(a + la - lb, b) != 0)
		return 0;
	if (la - lb + 1 > cap)
		mk_trap("a name would run over a buffer of the original's stack");
	memcpy(p, a, la - lb);
	p[la - lb] = '\0';
	return 1;
}

/* 11d8. n is the size of the block in the original, hostsize its size here */
void *
ckalloc(mint n, size_t hostsize)
{
	void *p;

	mk_account((size_t)n);
	p = calloc(1, hostsize);
	if (p != NULL)
		return p;

	fatal("out of memory");
}

/* A macro's value: the original reads it as a string wherever it stands.
 * The three values it does not have as one are refused: the garbage that
 * mkqlist() hands over (below), and $* when the doname() that made it has
 * returned (its prefix is gone with its stack frame). */
static char qbuf[QBUFMAX];
static char *const garbage = qbuf + QBUFMAX - 1;	/* a mark, never a string */

static mint
star_live(int serial)
{
	return doname_live(serial);
}

const char *
varstr(const struct varblock *vp)
{
	if (vp->varval == NULL)
		return "(null)";	/* libc.a's printf prints a null %s so */
	if (vp->varval == garbage)
		mk_trap("a macro holds what the original's mkqlist() returned "
		    "without a value (the address of a variable block)");
	if (vp->star != 0 && !star_live(vp->star))
		mk_trap("$* is the prefix of a doname() that has returned");
	return vp->varval;
}

/* copy string a into b, substituting for arguments; b is an array of the
 * original's frame ending at end. 1202 */
static char *
subst1(const char *a, char *b, const char *end)
{
	static mint depth;
	char *s;
	char vname[100];
	struct varblock *vbp;
	char closer;
	char *vend = vname + sizeof(vname) - 1;

	mk_enter(118);
	if (++depth > 100)
		fatal("infinitely recursive macro?");
	if (a != NULL)
		while (*a) {
			if (*a != '$') {
				if (b >= end)
					mk_trap("a command would run over a "
					    "buffer of the original's stack");
				*b++ = *a++;
			} else if (*++a == '\0') {
				/* the original copies the terminator and goes
				 * on reading behind the string */
				mk_trap("a $ is the last character of a string");
			} else if (*a == '$') {
				if (b >= end)
					mk_trap("a command would run over a "
					    "buffer of the original's stack");
				*b++ = *a++;
			} else {
				s = vname;
				if (*a == '(' || *a == '{') {
					closer = (*a == '(' ? ')' : '}');
					++a;
					while (*a == ' ')
						++a;
					while (*a != ' ' && *a != closer &&
					    *a != '\0') {
						if (s >= vend)
							mk_trap("a macro name "
							    "would run over a "
							    "buffer of the "
							    "original's stack");
						*s++ = *a++;
					}
					while (*a != closer && *a != '\0')
						++a;
					if (*a == closer)
						++a;
				} else
					*s++ = *a++;

				*s = '\0';
				if ((vbp = varptr(vname))->varval != NULL) {
					const char *val = varstr(vbp);

					b = subst1(val, b, end);
					vbp->used = YES;
				}
			}
		}

	if (b >= end + 1)
		mk_trap("a command would run over a buffer of the original's stack");
	*b = '\0';
	--depth;
	mk_leave(118);
	return b;
}

/* the buffer b of cap bytes */
char *
subst(const char *a, char *b, size_t cap)
{
	return subst1(a, b, b + cap - 1);
}

/* 1302 */
void
setvar(const char *v, char *s)
{
	struct varblock *p;

	p = varptr(v);
	setvar_ax_ptr = 1;
	if (p->noreset == 0) {
		p->varval = s;
		p->star = 0;
		p->noreset = (int8_t)(inarglist & 1);
		if (p->used) {
			setvar_ax_ptr = 0;	/* the strcmp()s left AX = 0 or a count */
			if (unequal(v, "@") && unequal(v, "*") &&
			    unequal(v, "<") && unequal(v, "?"))
				mk_eprintf("Warning: %s changed after being used\n", v);
		}
	}
}

/* $* of a doname() frame: the prefix array lives as long as the frame */
void
setvar_star(char *s, int serial)
{
	setvar("*", s);
	varptr("*")->star = (varptr("*")->varval == s) ? serial : 0;
}

/* look for arguments with equal signs but not colons. 13a4 */
mint
eqsign(char *a)
{
	char *s, *t;

	while (*a == ' ')
		++a;
	for (s = a; *s != '\0' && *s != ':'; ++s)
		if (*s == '=') {
			for (t = a; *t != '=' && *t != ' ' && *t != '\t'; ++t)
				;
			*t = '\0';

			for (++s; *s == ' ' || *s == '\t'; ++s)
				;
			setvar(a, copys(s));
			return YES;
		}

	return NO;
}

/* 140c */
struct varblock *
varptr(const char *v)
{
	struct varblock *vp;

	for (vp = firstvar; vp; vp = vp->nxtvarblock)
		if (!unequal(v, vp->varname))
			return vp;

	vp = ALLOC(varblock);
	vp->nxtvarblock = firstvar;
	firstvar = vp;
	vp->varname = copys(v);
	vp->varval = NULL;
	return vp;
}

/* 145a. sprintf() into buf[100] of the original's frame */
void
fatal1(const char *s, const char *t)
{
	char buf[100];
	int n;

	n = snprintf(buf, sizeof(buf), s, t);	/* s is one of the format strings below */
	if (n < 0 || (size_t)n >= sizeof(buf))
		mk_trap("a message would run over a buffer of the original's stack");
	fatal(buf);
}

/* 1480 */
void
fatal(const char *s)
{
	if (s)
		mk_eprintf("Make: %s.  Stop.\n", s);
	else
		mk_eprintf("\nStop.\n");
	mk_exit(1);
}

/* 14ba */
void
yyerror(const char *s)
{
	char buf[50];
	int n;

	n = snprintf(buf, sizeof(buf), "line %d: %s", yylineno, s);
	if (n < 0 || (size_t)n >= sizeof(buf))
		mk_trap("a message would run over a buffer of the original's stack");
	fatal(buf);
}

/* 14e2 */
struct chain *
appendq(struct chain *head, char *tail)
{
	struct chain *p, *q;

	p = ALLOC(chain);
	p->datap = tail;

	if (head) {
		for (q = head; q->nextp; q = q->nextp)
			;
		q->nextp = p;
		return head;
	} else
		return p;
}

/*
 * 151a. The list is built in the one static buffer qbuf, and with no chain
 * the original's function does not return a value: it ends without setting
 * AX, which the call "setvar("?", mkqlist(qchain))" passes on as the value
 * of $?. What AX holds then is the return of the setvar("@", ..) before
 * the call: 0 when "@" was used before (the strcmp() against "@" in
 * setvar() left it), else the address of the variable block of "@". A
 * value of 0 is an undefined macro, as the original has it; the address
 * is a string of whatever bytes the block has, which the port does not
 * model: $? is refused when it is read.
 */
char *
mkqlist(struct chain *p)
{
	char *qbufp;
	const char *s;

	if (p == NULL) {
		qbuf[0] = '\0';
		return setvar_ax_ptr ? garbage : NULL;
	}

	qbufp = qbuf;

	for (; p; p = p->nextp) {
		s = p->datap;
		if (qbufp + strlen(s) > &qbuf[QBUFMAX - 3]) {
			mk_eprintf("$? list too long\n");
			break;
		}
		while (*s)
			*qbufp++ = *s++;
		*qbufp++ = ' ';
	}
	if (qbufp == qbuf)
		/* the first name was too long: the original writes qbuf[-1] */
		mk_trap("the first name of the $? list is too long");
	*--qbufp = '\0';
	return qbuf;
}
