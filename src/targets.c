#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "targets.h"

static int is_index(const char *s);
static int eval_word(FilesBuffer *files, const char *word, int quoted, char *hit);
static int eval_token(FilesBuffer *files, struct ShellBuffer *shell, const char *token, int quoted, char *hit, char *alt);

static int is_index(const char *s) {
	if (*s == '\0') { return 0; }
	for (; *s; s++) {
		if (*s < '0' || *s > '9') { return 0; }
	}
	return 1;
}

/* Flags the entries named by one word. Returns how many matched, or -1 when
   an index or name does not exist. Keywords that match nothing return 0. */
static int eval_word(FilesBuffer *files, const char *word, int quoted, char *hit) {
	int n;
	int idx;

	n = 0;
	if (!quoted && strcmp(word, "sel") == 0) {
		for (int i = 0; i < files->entry_count; i++) {
			if (files->entries[i].marked) { hit[i] = 1; n++; }
		}
		return n;
	}
	if (!quoted && strcmp(word, "cur") == 0) {
		if (files->entry_count == 0) { return 0; }
		hit[files->selected] = 1;
		return 1;
	}
	if (!quoted && strcmp(word, "all") == 0) {
		memset(hit, 1, (size_t)files->entry_count);
		return files->entry_count;
	}
	if (!quoted && is_index(word)) {
		idx = atoi(word);
		for (int i = 0; i < files->entry_count; i++) {
			if (files->entries[i].index == idx) { hit[i] = 1; return 1; }
		}
		return -1;
	}
	for (int i = 0; i < files->entry_count; i++) {
		if (strcmp(files->entries[i].name, word) == 0) { hit[i] = 1; return 1; }
	}
	return -1;
}

/* Evaluates "a|b|c": the first alternative that names anything wins. */
static int eval_token(FilesBuffer *files, struct ShellBuffer *shell, const char *token, int quoted, char *hit, char *alt) {
	char word[CMD_LINE_MAX];
	char msg[CMD_LINE_MAX + 64];
	const char *p;
	const char *bar;
	size_t len;
	int n;

	p = token;
	while (1) {
		bar = quoted ? NULL : strchr(p, '|');
		len = bar ? (size_t)(bar - p) : strlen(p);
		memcpy(word, p, len);
		word[len] = '\0';

		memset(alt, 0, (size_t)files->entry_count);
		n = eval_word(files, word, quoted, alt);
		if (n < 0) {
			snprintf(msg, sizeof(msg), "no entry '%s' in this directory\n", word);
			print_to_shell(shell, msg, SHELL_MSG_ERROR);
			return -1;
		}
		if (n > 0 || !bar) { break; }
		p = bar + 1;
	}
	for (int i = 0; i < files->entry_count; i++) {
		hit[i] |= alt[i];
	}
	return n;
}

/* Resolves args->argv[first..last) to entry indices in listing order, using
   fallback when the range is empty. out needs room for files->entry_count
   entries. Returns the count, or -1 after printing an error. */
int targets_resolve(FilesBuffer *files, struct ShellBuffer *shell, const CmdArgs *args, int first, int last, const char *fallback, int *out) {
	CmdArgs fb;
	const CmdArgs *src;
	char *hit;
	char *alt;
	int count;

	src = args;
	if (first >= last) {
		if (!fallback || cmdline_split(fallback, &fb) < 0) { return 0; }
		src = &fb;
		first = 0;
		last = fb.argc;
	}

	hit = calloc((size_t)files->entry_count + 1, 2);
	if (!hit) {
		print_to_shell(shell, "out of memory\n", SHELL_MSG_ERROR);
		return -1;
	}
	alt = hit + files->entry_count + 1;

	for (int i = first; i < last; i++) {
		if (eval_token(files, shell, src->argv[i], src->quoted[i], hit, alt) < 0) {
			free(hit);
			return -1;
		}
	}

	count = 0;
	for (int i = 0; i < files->entry_count; i++) {
		if (hit[i]) { out[count++] = i; }
	}
	free(hit);
	return count;
}
