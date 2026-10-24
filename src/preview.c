#include <dirent.h>
#include <limits.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "colors.h"
#include "filetype.h"
#include "fs.h"
#include "preview.h"
#include "util.h"

typedef struct {
	int is_empty;
	int is_dir;
	mode_t mode;
	char name[MAX_FILENAME];
} PreviewEntry;

static PreviewEntry *pv_names; ///< Cached, sorted listing of the previewed directory
static int pv_total; ///< Number of valid entries in pv_names
static int pv_cap; ///< Allocated capacity of pv_names
static int pv_valid; ///< Non-zero when pv_names holds the listing for pv_path
static unsigned pv_gen; ///< files.load_gen the cache was built under
static char pv_path[PATH_MAX]; ///< Directory currently held in the preview cache

#define BAT_CACHE_BUF (512 * 1024) ///< Maximum bytes buffered from a bat(1) subprocess
#define BAT_CACHE_LMAX 4096 ///< Maximum number of lines indexed in the bat cache

static char bat_path_cache[PATH_MAX]; ///< Path of the file currently held in the bat cache
static char bat_buf[BAT_CACHE_BUF]; ///< Raw output buffer from the last bat(1) run
static char *bat_lines[BAT_CACHE_LMAX]; ///< Pointers into bat_buf, one per line
static int bat_nlines = 0; ///< Number of valid entries in bat_lines
static int bat_valid = 0; ///< Non-zero when bat_buf holds usable content for bat_path_cache
static unsigned bat_gen; ///< files.load_gen the bat cache was filled under

static int preview_entry_cmp(const void *a, const void *b);
static void preview_fill_dir_cache(const char *path, unsigned gen);
static void preview_render_dir(TUI *tui, const char *path, int col, int avail, int max_rows);
static int bat_available(void);
static short rgb_to_256(int r, int g, int b);
static short bat_color_pair(short fg);
static void render_bat_line(WINDOW *win, int row, int col, int avail, const char *s);
static void bat_fill_cache(const char *path);
static void preview_render_file(TUI *tui, const char *path, DirEntry *e, int col, int avail, int max_rows);

/**
 * @brief Comparator for sorting preview entries.
 *
 * Directories are sorted before files, and within each group
 * entries are sorted alphabetically (case-insensitive).
 *
 * @param a Pointer to the first PreviewEntry.
 * @param b Pointer to the second PreviewEntry.
 * @return Negative if "a" comes first, positive if "b" comes first, "ero if equal.
 */
static int preview_entry_cmp(const void *a, const void *b) {
	const PreviewEntry *ea;
	const PreviewEntry *eb;

	ea = (const PreviewEntry *)a;
	eb = (const PreviewEntry *)b;
	if (ea->is_dir != eb->is_dir) {
		return (ea->is_dir == 0) ? 1 : -1;
	}
	return strcasecmp(ea->name, eb->name);
}

/* Reads and sorts the directory at path into the preview cache. */
static void preview_fill_dir_cache(const char *path, unsigned gen) {
	DIR *d;
	struct dirent *de;
	struct stat st;
	char entry_path[PATH_MAX];
	PreviewEntry *grown;
	PreviewEntry *pe;

	pv_valid = 0;
	pv_total = 0;
	d = opendir(path);
	if (!d) { return; }

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') { continue; }
		if (pv_total == pv_cap) {
			grown = realloc(pv_names, (size_t)(pv_cap ? pv_cap * 2 : 256) * sizeof(PreviewEntry));
			if (!grown) { break; }
			pv_names = grown;
			pv_cap = pv_cap ? pv_cap * 2 : 256;
		}
		pe = &pv_names[pv_total++];
		snprintf(pe->name, sizeof(pe->name), "%s", de->d_name);
		path_join(entry_path, PATH_MAX, path, de->d_name);
		if (stat(entry_path, &st) == 0) {
			pe->is_dir = S_ISDIR(st.st_mode);
			pe->is_empty = pe->is_dir ? dir_is_empty(entry_path) : 0;
			pe->mode = st.st_mode;
		} else {
			pe->is_dir = 0;
			pe->is_empty = 0;
			pe->mode = 0;
		}
	}
	closedir(d);

	qsort(pv_names, (size_t)pv_total, sizeof(PreviewEntry), preview_entry_cmp);
	snprintf(pv_path, sizeof(pv_path), "%s", path);
	pv_gen = gen;
	pv_valid = 1;
}

/**
 * @brief Renders a directory listing into the preview buffer.
 *
 * Uses the preview cache, which is rebuilt when the previewed path changes or
 * the file buffer reloads. Respects tui->preview_scroll.
 *
 * @param tui			 The TUI instance.
 * @param path		 Absolute path of the directory to preview.
 * @param col			 Starting column in files_win to draw into.
 * @param avail		 Available column width for entry names.
 * @param max_rows Maximum number of rows to render.
 */
static void preview_render_dir(TUI *tui, const char *path, int col, int avail, int max_rows) {
	DirEntry tmp_e;
	int max_scroll;
	int rendered;
	int cp;
	attr_t at;
	char fit[1024];
	PreviewEntry *pe;

	if (!pv_valid || pv_gen != tui->files.load_gen || strcmp(pv_path, path) != 0) {
		preview_fill_dir_cache(path, tui->files.load_gen);
	}
	if (!pv_valid) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(cannot open dir)");
		wattroff(tui->files_win, A_DIM);
		return;
	}

	max_scroll = pv_total - max_rows;
	if (max_scroll < 0) { max_scroll = 0; }
	if (tui->preview_scroll > max_scroll) { tui->preview_scroll = max_scroll; }

	rendered = 0;
	for (int i = tui->preview_scroll; i < pv_total && rendered < max_rows; i++, rendered++) {
		pe = &pv_names[i];
		str_fit_cols(fit, sizeof(fit), pe->name, avail);
		if (pe->is_dir) {
			cp = pe->is_empty ? CP_DIR_EMPTY : CP_DIR_FULL;
			at = pe->is_empty ? A_DIM : A_BOLD;
		} else {
			snprintf(tmp_e.name, sizeof(tmp_e.name), "%s", pe->name);
			tmp_e.type = ENTRY_FILE;
			tmp_e.is_empty = 0;
			tmp_e.mode = pe->mode;
			cp = entry_color_pair(&tmp_e, 0);
			at = (cp == CP_EXT_EXEC) ? A_BOLD : A_NORMAL;
		}
		wattron(tui->files_win, COLOR_PAIR(cp) | at);
		mvwaddstr(tui->files_win, 1 + rendered, col, fit);
		wattroff(tui->files_win, COLOR_PAIR(cp) | at);
	}

	if (pv_total == 0) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(empty)");
		wattroff(tui->files_win, A_DIM);
	}
}

/* Returns 1 if bat(1) is on PATH, 0 otherwise. Result is cached after the first call. */
static int bat_available(void) {
	static int result = -1;
	FILE *f;
	char buf[4];
	if (result != -1) { return result; }
	f = popen("command -v bat 2>/dev/null", "r");
	result = (f && fgets(buf, sizeof(buf), f)) ? 1 : 0;
	if (f) { pclose(f); }
	return result;
}

/* Maps 24-bit RGB to the nearest xterm-256 color index. */
static short rgb_to_256(int r, int g, int b) {
	return (short)(16 + 36 * ((r * 5 + 127) / 255) + 6 * ((g * 5 + 127) / 255) + ((b * 5 + 127) / 255));
}

/* Returns an ncurses color-pair number for the given xterm-256 foreground index,
	 allocating a new pair on first use. Returns 0 if the terminal has too few colors
	 or if the dynamic pair pool is exhausted. */
static short bat_color_pair(short fg) {
	static short cache[256];
	static int ready = 0;
	static int used = 0;
	short pair;

	if (!ready) { memset(cache, -1, sizeof(cache)); ready = 1; }
	if (COLORS < 256 || COLOR_PAIRS < 256 || fg < 0 || fg > 255) { return 0; }
	if (cache[fg] < 0) {
		if (64 + used >= COLOR_PAIRS || used >= 192) { return 0; }
		pair = (short)(64 + used++);
		init_pair(pair, fg, -1);
		cache[fg] = pair;
	}
	return cache[fg];
}

/**
 * @brief Renders one ANSI-escaped line from bat(1) into an ncurses window.
 *
 * Parses SGR escape sequences inline, mapping colors and attributes to
 * ncurses equivalents. Non-printable or unrecognised sequences are skipped.
 *
 * @param win   Target ncurses window.
 * @param row   Row to write into.
 * @param col   Starting column.
 * @param avail Maximum number of visible characters to write.
 * @param s     Null-terminated ANSI-escaped string to render.
 */
static void render_bat_line(WINDOW *win, int row, int col, int avail, const char *s) {
	attr_t attrs = A_NORMAL;
	short fg = -1;
	int budget = avail;
	int params[16];
	int np;
	int i;

	wmove(win, row, col);

	while (*s && budget > 0) {
		if (s[0] != '\033' || s[1] != '[') {
			short pr = (fg >= 0) ? bat_color_pair(fg) : 0;

			if (pr) {
				wattron(win, COLOR_PAIR(pr) | attrs);
			} else if (attrs) {
				wattron(win, attrs);
			}

			waddch(win, (unsigned char)*s);
			if (pr) {
				wattroff(win, COLOR_PAIR(pr) | attrs);
			} else if (attrs) {
				wattroff(win, attrs);
			}
			s++;
			budget--;
			continue;
		}

		s += 2;
		np = 0;
		while (np < 16) {
			char *end;
			long pv = strtol(s, &end, 10);
			params[np] = (end == s) ? 0 : (pv < 0 ? 0 : pv > 255 ? 255 : (int)pv);
			np++;
			s = end;
			if (*s == ';') { s++; } else { break; }
		}

		if (*s) { s++; }
		for (i = 0; i < np; i++) {
			switch (params[i]) {
			case 0: attrs = A_NORMAL; fg = -1; break;
			case 1: attrs |= A_BOLD; break;
			case 2: attrs |= A_DIM; break;
			case 4: attrs |= A_UNDERLINE; break;
			case 22: attrs &= ~(A_BOLD | A_DIM); break;
			case 24: attrs &= ~A_UNDERLINE; break;
			case 39: fg = -1; break;
			case 30: case 31: case 32: case 33:
			case 34: case 35: case 36: case 37:
				fg = (short)(params[i] - 30); break;
			case 90: case 91: case 92: case 93:
			case 94: case 95: case 96: case 97:
				fg = (short)(params[i] - 90 + 8); break;
			case 38:
				if (i + 2 < np && params[i + 1] == 5) {
					int c = params[i + 2];
					fg = (c >= 0 && c <= 255) ? (short)c : -1; i += 2;
				} else if (i + 4 < np && params[i + 1] == 2) {
					fg = rgb_to_256(params[i+2], params[i+3], params[i+4]);
					i += 4;
				}
				break;
			default: break;
			}
		}
	}
	wstandend(win);
}

/**
 * @brief Populates the bat cache by running bat(1) on the given file.
 *
 * Shell-escapes the path, spawns bat with plain style and no pager, reads
 * up to BAT_CACHE_BUF bytes, then splits the output into lines stored in
 * bat_lines[]. Sets bat_valid on success.
 *
 * @param path Absolute path of the file to render.
 */
static void bat_fill_cache(const char *path) {
	char cmd[PATH_MAX * 2 + 256];
	char esc[PATH_MAX * 2];
	const char *color_flag, *p;
	FILE *f;
	int ei, total, n, nl;
	char *c;

	bat_valid = 0;
	bat_nlines = 0;

	ei = 0;
	for (p = path; *p && ei < (int)sizeof(esc) - 4; p++) {
		if (*p == '\'') {
			esc[ei++] = '\''; esc[ei++] = '\\';
			esc[ei++] = '\''; esc[ei++] = '\'';
		} else {
			esc[ei++] = *p;
		}
	}
	esc[ei] = '\0';

	strncpy(bat_path_cache, path, PATH_MAX - 1);
	bat_path_cache[PATH_MAX - 1] = '\0';

	color_flag = (COLORS >= 256) ? "always" : "never";
	snprintf(cmd, sizeof(cmd), "bat --color=%s --style=plain --pager=never --tabs=4 --wrap=never '%s' 2>/dev/null", color_flag, esc);

	f = popen(cmd, "r");
	if (!f) { return; }

	total = 0;
	while (total < BAT_CACHE_BUF - 1) {
		n = (int)fread(bat_buf + total, 1, BAT_CACHE_BUF - 1 - total, f);
		if (n <= 0) { break; }
		total += n;
	}

	bat_buf[total] = '\0';
	pclose(f);

	nl = 0;
	bat_lines[nl++] = bat_buf;
	for (c = bat_buf; *c && nl < BAT_CACHE_LMAX; c++) {
		if (*c == '\n') {
			*c = '\0';
			if (c > bat_buf && *(c - 1) == '\r') { *(c - 1) = '\0'; }
			if (*(c + 1) != '\0') { bat_lines[nl++] = c + 1; }
		}
	}
	bat_nlines = nl;
	bat_valid = (total > 0) ? 1 : 0;
}

/**
 * @brief Renders a file's contents into the preview column.
 *
 * Uses bat(1) with ANSI color if available, falling back to plain fread.
 * Binary files and media types (image, video, audio, archive) display a
 * short label instead of content. Respects tui->preview_scroll.
 *
 * @param tui      The TUI instance.
 * @param path     Absolute path of the file to preview.
 * @param e        DirEntry for the file (used for extension-based color).
 * @param col      Starting column in files_win to draw into.
 * @param avail    Available column width.
 * @param max_rows Maximum number of rows to render.
 */
static void preview_render_file(TUI *tui, const char *path, DirEntry *e, int col, int avail, int max_rows) {
	FILE *f;
	char peek[512];
	char line[256];
	size_t n;
	int cp;
	int binary;
	int row;
	int len;

	cp = entry_color_pair(e, 0);

	if (cp == CP_EXT_IMAGE) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(image)");
		wattroff(tui->files_win, A_DIM);
		return;
	}
	if (cp == CP_EXT_VIDEO) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(video)");
		wattroff(tui->files_win, A_DIM);
		return;
	}
	if (cp == CP_EXT_AUDIO) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(audio)");
		wattroff(tui->files_win, A_DIM);
		return;
	}
	if (cp == CP_EXT_ARCHIVE) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(archive)");
		wattroff(tui->files_win, A_DIM);
		return;
	}

	if (bat_available()) {
		if (!bat_valid || bat_gen != tui->files.load_gen || strcmp(bat_path_cache, path) != 0) {
			bat_fill_cache(path);
			bat_gen = tui->files.load_gen;
		}
		if (bat_valid && bat_nlines > 0) {
			int max_scroll = bat_nlines - max_rows;
			if (max_scroll < 0) { max_scroll = 0; }
			if (tui->preview_scroll > max_scroll) {
				tui->preview_scroll = max_scroll;
			}
			for (row = 0; row < max_rows; row++) {
				int li = tui->preview_scroll + row;
				if (li >= bat_nlines) { break; }
				mvwprintw(tui->files_win, 1 + row, col, "%-*.*s", avail, avail, "");
				if (COLORS >= 256) {
					render_bat_line(tui->files_win, 1 + row, col, avail, bat_lines[li]);
				} else {
					const char *src = bat_lines[li];
					char plain[512];
					int pi = 0;
					while (*src && pi < (int)sizeof(plain) - 1) {
						if (src[0] == '\033' && src[1] == '[') {
							src += 2;
							while (*src && *src != 'm') { src++; }
							if (*src) { src++; }
						} else {
							plain[pi++] = *src++;
						}
					}
					plain[pi] = '\0';
					mvwprintw(tui->files_win, 1 + row, col, "%-*.*s", avail, avail, plain);
				}
			}
			return;
		}
	}

	f = fopen(path, "r");
	if (!f) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(cannot read)");
		wattroff(tui->files_win, A_DIM);
		return;
	}

	n = fread(peek, 1, sizeof(peek), f);
	rewind(f);
	binary = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)peek[i];
		if (c == 0 || (c < 7 && c != '\n' && c != '\r' && c != '\t')) {
			binary = 1;
			break;
		}
	}

	if (n == 0) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(empty)");
		wattroff(tui->files_win, A_DIM);
	} else if (binary) {
		wattron(tui->files_win, A_DIM);
		mvwprintw(tui->files_win, 1, col, "(binary)");
		wattroff(tui->files_win, A_DIM);
	} else {
		rewind(f);
		row = 0;
		while (row < max_rows && fgets(line, sizeof(line), f)) {
			len = (int)strlen(line);
			if (len > 0 && line[len - 1] == '\n') { line[--len] = '\0'; }
			if (len > 0 && line[len - 1] == '\r') { line[--len] = '\0'; }
			mvwprintw(tui->files_win, 1 + row, col, "%-*.*s", avail, avail, line);
			row++;
		}
	}

	fclose(f);
}

/**
 * @brief Draws the preview pane for the currently selected entry.
 *
 * Renders a vertical divider, a title bar, and delegates to either
 * preview_render_dir() or preview_render_file() depending on entry type.
 * Resets preview_scroll when the selection changes.
 *
 * @param tui    The TUI instance.
 * @param list_w Width in columns occupied by the file list pane.
 */
void preview_render(TUI *tui, int list_w) {
	int preview_w;
	int height;
	int focused;
	int pair;
	int col;
	int avail;
	int max_rows;
	DirEntry *e;
	char preview_title[MAX_FILENAME + 16];
	char title_fit[1024];
	char path[PATH_MAX];

	preview_w = tui->max_x - list_w - 1;
	height = tui->files_height;
	focused = (tui->active_buffer == BUFFER_PREVIEW);

	if (tui->files.selected != tui->preview_last_selected) {
		tui->preview_scroll = 0;
		tui->preview_last_selected = tui->files.selected;
	}

	for (int r = 0; r < height; r++) {
		mvwaddch(tui->files_win, r, list_w, ACS_VLINE);
	}

	if (tui->files.entry_count == 0) { return; }

	e = &tui->files.entries[tui->files.selected];

	pair = focused ? CP_TITLE_F : CP_TITLE_UF;
	snprintf(preview_title, sizeof(preview_title), "PREVIEW - %s", e->name);
	wattron(tui->files_win, COLOR_PAIR(pair) | A_BOLD);
	str_fit_cols(title_fit, sizeof(title_fit), preview_title, preview_w - 1);
	mvwaddstr(tui->files_win, 0, list_w + 1, title_fit);
	wattroff(tui->files_win, COLOR_PAIR(pair) | A_BOLD);

	files_get_selected_path(&tui->files, path, sizeof(path));

	col = list_w + 2;
	avail = preview_w - 3;
	max_rows = height - 1;
	if (avail <= 0 || max_rows <= 0) { return; }

	if (e->type == ENTRY_DIR) {
		preview_render_dir(tui, path, col, avail, max_rows);
	} else {
		preview_render_file(tui, path, e, col, avail, max_rows);
	}

	wnoutrefresh(tui->files_win);
}

void preview_free() {
	free(pv_names);
	pv_names = NULL;
	pv_cap = 0;
	pv_total = 0;
	pv_valid = 0;
}
