#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_FILENAME 256 ///< Longest file name kept, NAME_MAX plus the terminator

#define INPUT_TIMEOUT_MS 50 ///< How long getch() waits for a key before the main loop checks for resizes

/* Joins dir and name with one slash (none extra when dir already ends in one),
   truncating to fit dsz. */
static inline void path_join(char *dst, size_t dsz, const char *dir, const char *name) {
	size_t dlen;
	size_t sep;
	size_t nlen;

	if (dsz == 0) { return; }
	dlen = strlen(dir);
	if (dlen >= dsz) { dlen = dsz - 1; }
	sep = (dlen > 0 && dir[dlen - 1] == '/') ? 0 : 1;
	if (dlen + sep >= dsz) { sep = 0; }
	memcpy(dst, dir, dlen);
	if (sep) { dst[dlen] = '/'; }
	nlen = strlen(name);
	if (dlen + sep + nlen >= dsz) { nlen = dsz - 1 - dlen - sep; }
	memcpy(dst + dlen + sep, name, nlen);
	dst[dlen + sep + nlen] = '\0';
}

static inline void format_size(off_t size, char *buf, int bufsize) {
			if (size >= 1024 * 1024 * 1024) {
				snprintf(buf, bufsize, "%.1fG", (double)size / (1024 * 1024 * 1024));
			} else if (size >= 1024 * 1024) {
				snprintf(buf, bufsize, "%.1fM", (double)size / (1024 * 1024));
			} else if (size >= 1024) {
				snprintf(buf, bufsize, "%.1fK", (double)size / 1024);
			} else {
				snprintf(buf, bufsize, "%ldB", (long)size);
			}

}

void str_copy(char *dst, size_t dst_size, const char *src);
int str_shell_quote(char *out, int out_size, int pos, const char *s);
void str_fit_cols(char *dst, size_t dst_size, const char *src, int cols);
void str_fit_trimmed(char *dst, size_t dst_size, const char *src, int cols);
