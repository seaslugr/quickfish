#include <string.h>

#include "cmdline.h"

static int is_space(char c);
static int word_at(const char *s, const char *word, size_t len);

static int is_space(char c) {
	return c == ' ' || c == '\t';
}

/* Splits line into words the way a shell would: '...' is literal, "..." allows
   \" and \\, and a backslash outside quotes escapes the next character.
   Words that used any quoting are flagged so they are never read as an index
   or keyword. Returns argc, or -1 on an unterminated quote or overflow. */
int cmdline_split(const char *line, CmdArgs *args) {
	const char *p;
	char *out;
	char *end;
	char q;

	args->argc = 0;
	out = args->buf;
	end = args->buf + sizeof(args->buf) - 1;
	p = line;

	while (1) {
		while (is_space(*p)) { p++; }
		if (*p == '\0') { break; }
		if (args->argc == CMD_MAX_ARGS) { return -1; }

		args->argv[args->argc] = out;
		args->quoted[args->argc] = 0;
		while (*p && !is_space(*p)) {
			if (out >= end) { return -1; }
			if (*p == '\'' || *p == '"') {
				q = *p++;
				args->quoted[args->argc] = 1;
				while (*p && *p != q) {
					if (q == '"' && *p == '\\' && (p[1] == '"' || p[1] == '\\')) { p++; }
					if (out >= end) { return -1; }
					*out++ = *p++;
				}
				if (*p != q) { return -1; }
				p++;
			} else if (*p == '\\' && p[1]) {
				args->quoted[args->argc] = 1;
				*out++ = p[1];
				p += 2;
			} else {
				*out++ = *p++;
			}
		}
		*out++ = '\0';
		args->argc++;
	}
	return args->argc;
}

/* Drops argv[0], so handlers see only their own arguments. */
void cmdline_shift(CmdArgs *args) {
	if (args->argc == 0) { return; }
	memmove(&args->argv[0], &args->argv[1], (size_t)(args->argc - 1) * sizeof(char *));
	memmove(&args->quoted[0], &args->quoted[1], (size_t)(args->argc - 1));
	args->argc--;
}

static int word_at(const char *s, const char *word, size_t len) {
	return strncmp(s, word, len) == 0 && (s[len] == '\0' || is_space(s[len]));
}

/* Copies line to out, replacing every unquoted standalone "sel" or "cur" with
   the given pre-quoted text. Returns the length, or -1 if it does not fit or a
   needed expansion is unavailable. */
int cmdline_expand(const char *line, const char *sel, const char *cur, char *out, int out_size) {
	const char *p;
	const char *rep;
	size_t rlen;
	int pos;
	char q;

	pos = 0;
	q = 0;
	for (p = line; *p; ) {
		rep = NULL;
		if (!q && (p == line || is_space(p[-1]))) {
			if (word_at(p, "sel", 3)) {
				rep = sel;
			} else if (word_at(p, "cur", 3)) {
				rep = cur;
			}
			if ((word_at(p, "sel", 3) || word_at(p, "cur", 3)) && !rep) { return -1; }
		}
		if (rep) {
			rlen = strlen(rep);
			if (pos + (int)rlen >= out_size) { return -1; }
			memcpy(out + pos, rep, rlen);
			pos += (int)rlen;
			p += 3;
			continue;
		}
		if (*p == '\\' && !q && p[1]) {
			if (pos + 2 >= out_size) { return -1; }
			out[pos++] = *p++;
		} else if ((*p == '\'' || *p == '"') && (!q || q == *p)) {
			q = q ? 0 : *p;
		}
		if (pos + 1 >= out_size) { return -1; }
		out[pos++] = *p++;
	}
	out[pos] = '\0';
	return pos;
}
