#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "util.h"

static size_t char_step(const char *s, mbstate_t *ps, int *width);

void str_copy(char *dst, size_t dst_size, const char *src) {
	size_t len;

	if (dst_size == 0) { return; }
	len = strlen(src);
	if (len >= dst_size) { len = dst_size - 1; }
	memcpy(dst, src, len);
	dst[len] = '\0';
}

/* Appends s to out wrapped in single quotes, escaping embedded quotes as '\''.
   Returns the new length, or -1 if it does not fit. */
int str_shell_quote(char *out, int out_size, int pos, const char *s) {
	if (pos + 1 >= out_size) { return -1; }
	out[pos++] = '\'';
	for (; *s; s++) {
		if (*s == '\'') {
			if (pos + 4 >= out_size) { return -1; }
			memcpy(out + pos, "'\\''", 4);
			pos += 4;
		} else {
			if (pos + 1 >= out_size) { return -1; }
			out[pos++] = *s;
		}
	}
	if (pos + 1 >= out_size) { return -1; }
	out[pos++] = '\'';
	out[pos] = '\0';
	return pos;
}

/* Decodes one character at s. Sets width to its column count, or -1 for an
   invalid or non-printable byte. Returns how many bytes it spans. */
static size_t char_step(const char *s, mbstate_t *ps, int *width) {
	wchar_t wc;
	size_t n;

	n = mbrtowc(&wc, s, MB_CUR_MAX, ps);
	if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
		memset(ps, 0, sizeof(*ps));
		*width = -1;
		return 1;
	}
	*width = wcwidth(wc);
	return n;
}

/* Writes src into dst as exactly cols terminal columns. Unprintable characters
   become '?', text that does not fit ends in "..", and the rest is space padded. */
void str_fit_cols(char *dst, size_t dst_size, const char *src, int cols) {
	mbstate_t ps;
	size_t n;
	size_t pos;
	int total;
	int limit;
	int used;
	int w;
	int cw;
	int cut;

	if (dst_size == 0) { return; }
	if (cols < 0) { cols = 0; }

	total = 0;
	memset(&ps, 0, sizeof(ps));
	for (const char *p = src; *p && total <= cols; p += n) {
		n = char_step(p, &ps, &w);
		total += (w < 0) ? 1 : w;
	}
	cut = (total > cols);
	limit = (cut && cols >= 2) ? cols - 2 : cols;

	pos = 0;
	used = 0;
	memset(&ps, 0, sizeof(ps));
	for (const char *p = src; *p; p += n) {
		n = char_step(p, &ps, &w);
		cw = (w < 0) ? 1 : w;
		if (used + cw > limit) { break; }
		if (w < 0) {
			if (pos + 1 >= dst_size) { break; }
			dst[pos++] = '?';
		} else {
			if (pos + n >= dst_size) { break; }
			memcpy(dst + pos, p, n);
			pos += n;
		}
		used += cw;
	}
	for (; used < cols && pos + 1 < dst_size; used++) {
		dst[pos++] = (cut && used >= limit) ? '.' : ' ';
	}
	dst[pos] = '\0';
}

/* Like str_fit_cols() but without the trailing padding, for names inside sentences. */
void str_fit_trimmed(char *dst, size_t dst_size, const char *src, int cols) {
	size_t len;

	str_fit_cols(dst, dst_size, src, cols);
	len = strlen(dst);
	while (len > 0 && dst[len - 1] == ' ') { dst[--len] = '\0'; }
}
