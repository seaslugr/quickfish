#include <dirent.h>
#include <limits.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "colors.h"
#include "command.h"
#include "fs.h"
#include "ops.h"
#include "prompt.h"
#include "util.h"

static void edit_handle_ch(char *buf, int *len, int *cursor, int max, int ch);
static void goto_cancel(TUI *tui);
static void goto_tab_complete(TUI *tui);
static void rename_commit(TUI *tui);
static void rename_cancel(TUI *tui);
static void handle_rename_input(TUI *tui, int ch);
static void handle_goto_input(TUI *tui, int ch);
static void quickshell_cancel(TUI *tui);
static void quickshell_execute(TUI *tui);
static void handle_quickshell_input(TUI *tui, int ch);

/**
 * @brief Applies a single keypress to an editable text buffer.
 *
 * Handles cursor movement, insertion, backspace, and delete-forward.
 * Used by the goto, rename, and quickshell overlays.
 *
 * @param buf 	 The text buffer (null-terminated, caller-owned).
 * @param len 	 Current length of text in buf (updated in place).
 * @param cursor Current cursor position within buf (updated in place).
 * @param max 	 Maximum capacity of buf including the null terminator.
 * @param ch 		 ncurses key value to process.
 */
static void edit_handle_ch(char *buf, int *len, int *cursor, int max, int ch) {
	if (ch == KEY_LEFT) {
		if (*cursor > 0) { (*cursor)--; }
	} else if (ch == KEY_RIGHT) {
		if (*cursor < *len) { (*cursor)++; }
	} else if (ch == KEY_HOME) {
		*cursor = 0;
	} else if (ch == KEY_END) {
		*cursor = *len;
	} else if (ch == '\x7f' || ch == KEY_BACKSPACE) {
		if (*cursor > 0) {
			memmove(buf + *cursor - 1, buf + *cursor, (size_t)(*len - *cursor + 1));
			(*cursor)--;
			(*len)--;
		}
	} else if (ch == KEY_DC) {
		if (*cursor < *len) {
			memmove(buf + *cursor, buf + *cursor + 1, (size_t)(*len - *cursor));
			(*len)--;
		}
	} else if (ch >= 32 && ch < 127 && *len < max - 1) {
		memmove(buf + *cursor + 1, buf + *cursor, (size_t)(*len - *cursor + 1));
		buf[(*cursor)++] = (unsigned char)ch;
		(*len)++;
	}
}

/* Draws the GOTO prompt centred on the title row and positions the cursor. */
void prompt_render_goto(TUI *tui) {
	char label[GOTO_BUF_MAX + 16];
	int llen;
	int lx;

	snprintf(label, sizeof(label), " GOTO: %s ", tui->goto_buf);
	llen = (int)strlen(label);
	lx = (tui->max_x - llen) / 2;
	if (lx < 0) { lx = 0; }
	wattron(tui->files_win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	mvwprintw(tui->files_win, 0, lx, "%s", label);
	wattroff(tui->files_win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	wmove(tui->files_win, 0, lx + 7 + tui->goto_len);
	leaveok(tui->files_win, FALSE);
	curs_set(2);
	wnoutrefresh(tui->files_win);
}

/* Draws the RENAME prompt on the title row with an inline editable field. */
void prompt_render_rename(TUI *tui) {
	const char *prefix;
	char label[RENAME_BUF_MAX + 16];
	int llen;
	int lx;
	int px;
	int name_x;
	int avail;

	prefix = " RENAME: ";
	snprintf(label, sizeof(label), " RENAME: %s ", tui->rename_buf);
	llen = (int)strlen(label);
	lx = (tui->max_x - llen) / 2;
	if (lx < 0) { lx = 0; }
	px = lx;
	wattron(tui->files_win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	mvwprintw(tui->files_win, 0, px, "%s", prefix);
	wattroff(tui->files_win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	name_x = px + (int)strlen(prefix);
	avail = tui->max_x - name_x - 1;
	if (avail > 0) {
		mvwprintw(tui->files_win, 0, name_x, "%-*.*s", avail, avail, tui->rename_buf);
	}
	wmove(tui->files_win, 0, name_x + tui->rename_cursor);
	leaveok(tui->files_win, FALSE);
	curs_set(2);
	wnoutrefresh(tui->files_win);
}

/**
 * @brief Draws the quickshell input bar at the bottom of the files pane.
 *
 * Renders the ":" prompt, the current input buffer, and a ghost completion
 * hint (dimmed) if one is available.
 *
 * @param tui            The TUI instance.
 * @param files_render_w Width of the files pane in columns.
 */
void prompt_render_quickshell(TUI *tui, int files_render_w) {
	int qrow;
	int avail;
	char ghost[MAX_FILENAME];
	int gx;
	int max_input_w;

	qrow = tui->files_height - 1;
	max_input_w = files_render_w / 2;
	avail = max_input_w - 2;
	wattron(tui->files_win, COLOR_PAIR(1));
	mvwprintw(tui->files_win, qrow, 0, "%s", ":");
	wattroff(tui->files_win, COLOR_PAIR(1));
	if (avail > 0) {
		mvwprintw(tui->files_win, qrow, 1, "%-*.*s", avail, avail, tui->quickshell_buf);
	}

	compute_ghost(tui->quickshell_buf, tui->quickshell_cursor,
	              tui->files.cwd, &tui->files, &tui->shell, ghost, sizeof(ghost));
	if (ghost[0] != '\0') {
		gx = 1 + tui->quickshell_cursor;
		if (gx < max_input_w) {
			wmove(tui->files_win, qrow, gx);
			wattron(tui->files_win, A_DIM);
			waddnstr(tui->files_win, ghost, max_input_w - gx);
			wattroff(tui->files_win, A_DIM);
		}
	}
	wmove(tui->files_win, qrow, 1 + tui->quickshell_cursor);
	leaveok(tui->files_win, FALSE);
	curs_set(2);
	wnoutrefresh(tui->files_win);
}

void prompt_goto_begin(TUI *tui) {
	tui->goto_mode = 1;
	tui->goto_len = 0;
	tui->goto_buf[0] = '\0';
	tui->active_buffer = BUFFER_FILES;
}

/* Cancels goto mode and clears the input buffer. */
static void goto_cancel(TUI *tui) {
	tui->goto_mode = 0;
	tui->goto_len = 0;
	tui->goto_buf[0] = '\0';
}

/**
 * @brief Resolves and navigates to the path in the goto buffer.
 *
 * Resolution order: exact absolute/relative path, numeric index jump,
 * exact name match in the current (or specified) directory, then prefix
 * match. Navigates into directories. Selecting files is not supported.
 *
 * @param tui The TUI instance.
 */
void prompt_goto(TUI *tui, const char *text) {
	char target[PATH_MAX];
	struct stat st;
	char dir_buf[PATH_MAX];
	char *last_slash;
	const char *search_name;
	char search_dir[PATH_MAX];
	char *endp;
	long idx;
	DIR *d;
	char exact[MAX_FILENAME];
	char prefix_m[MAX_FILENAME];
	int slen;
	struct dirent *de;
	const char *match;
	char resolved[PATH_MAX];
	const char *use;
	DirEntry *e;

	if (text[0] == '\0') { return; }

	if (text[0] == '/') {
		strncpy(target, text, PATH_MAX - 1);
		target[PATH_MAX - 1] = '\0';
	} else if (strcmp(tui->files.cwd, "/") == 0) {
		snprintf(target, PATH_MAX, "/%.*s", (int)(PATH_MAX - 2), text);
	} else {
		path_join(target, PATH_MAX, tui->files.cwd, text);
	}

	if (stat(target, &st) == 0) {
		if (S_ISDIR(st.st_mode)) { tui_load_dir(tui, target); }
		return;
	}

	strncpy(dir_buf, target, PATH_MAX - 1);
	dir_buf[PATH_MAX - 1] = '\0';

	last_slash = strrchr(dir_buf, '/');

	if (last_slash && last_slash != dir_buf) {
		*last_slash = '\0';
		snprintf(search_dir, sizeof(search_dir), "%s", dir_buf);
		search_name = last_slash + 1;
	} else {
		snprintf(search_dir, sizeof(search_dir), "%s", tui->files.cwd);
		search_name = text;
	}

	if (!strchr(text, '/')) {
		idx = strtol(search_name, &endp, 10);
		if (*endp == '\0' && idx > 0) {
			for (int i = 0; i < tui->files.entry_count; i++) {
				if (tui->files.entries[i].index != (int)idx) { continue; }
				e = &tui->files.entries[i];
				if (e->type == ENTRY_DIR) { tui_change_dir(tui, e->name); }
				return;
			}
			return;
		}
	}

	d = opendir(search_dir);
	if (!d) { return; }

	exact[0] = '\0';
	prefix_m[0] = '\0';
	slen = (int)strlen(search_name);

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') { continue; }
		if (strcasecmp(de->d_name, search_name) == 0 && !exact[0]) {
			snprintf(exact, sizeof(exact), "%s", de->d_name);
		} else if (slen > 0 &&
		           strncasecmp(de->d_name, search_name, slen) == 0 &&
		           !prefix_m[0]) {
			snprintf(prefix_m, sizeof(prefix_m), "%s", de->d_name);
		}
	}
	closedir(d);

	match = exact[0] ? exact : prefix_m;
	if (!match[0]) { return; }

	use = realpath(search_dir, resolved) ? resolved : search_dir;
	if (strcmp(use, tui->files.cwd) != 0) { tui_load_dir(tui, use); }

	for (int i = 0; i < tui->files.entry_count; i++) {
		if (strcasecmp(tui->files.entries[i].name, match) != 0) { continue; }
		e = &tui->files.entries[i];
		if (e->type == ENTRY_DIR) { tui_change_dir(tui, e->name); }
		return;
	}
}

/**
 * @brief Tab-completes the current goto buffer against the filesystem.
 *
 * Splits the buffer into a directory part and a name prefix, then calls
 * complete_in_dir() to find a match. On success the buffer is updated in
 * place with the completed name.
 *
 * @param tui The TUI instance.
 */
static void goto_tab_complete(TUI *tui) {
	char dir_part[PATH_MAX];
	char name_part[MAX_FILENAME];
	const char *last_slash;
	char search_dir[PATH_MAX];
	char completed[MAX_FILENAME];
	int count;

	last_slash = strrchr(tui->goto_buf, '/');
	if (last_slash) {
		int dlen = (int)(last_slash - tui->goto_buf);
		strncpy(dir_part, tui->goto_buf, dlen);
		dir_part[dlen] = '\0';
		strncpy(name_part, last_slash + 1, MAX_FILENAME - 1);
		name_part[MAX_FILENAME - 1] = '\0';
	} else {
		dir_part[0] = '\0';
		strncpy(name_part, tui->goto_buf, MAX_FILENAME - 1);
		name_part[MAX_FILENAME - 1] = '\0';
	}

	if (dir_part[0] == '\0') {
		strncpy(search_dir, tui->files.cwd, PATH_MAX - 1);
		search_dir[PATH_MAX - 1] = '\0';
	} else if (dir_part[0] == '/') {
		strncpy(search_dir, dir_part, PATH_MAX - 1);
		search_dir[PATH_MAX - 1] = '\0';
	} else if (strcmp(tui->files.cwd, "/") == 0) {
		snprintf(search_dir, PATH_MAX, "/%.*s", (int)(PATH_MAX - 2), dir_part);
	} else {
		path_join(search_dir, PATH_MAX, tui->files.cwd, dir_part);
	}

	count = complete_in_dir(search_dir, name_part, completed, sizeof(completed));
	if (count == 0 || completed[0] == '\0') { return; }

	if (dir_part[0] == '\0') {
		strncpy(tui->goto_buf, completed, GOTO_BUF_MAX - 1);
		tui->goto_buf[GOTO_BUF_MAX - 1] = '\0';
	} else {
		path_join(tui->goto_buf, GOTO_BUF_MAX, dir_part, completed);
	}
	tui->goto_len = (int)strlen(tui->goto_buf);
}

void prompt_rename_begin(TUI *tui, int idx) {
	if (idx < 0 || idx >= tui->files.entry_count) { return; }
	tui->rename_mode = 1;
	tui->rename_target = idx;
	tui->files.selected = idx;
	tui->active_buffer = BUFFER_FILES;
	snprintf(tui->rename_buf, sizeof(tui->rename_buf), "%s", tui->files.entries[idx].name);
	tui->rename_len = (int)strlen(tui->rename_buf);
	tui->rename_cursor = tui->rename_len;
}

static void rename_commit(TUI *tui) {
	char name[RENAME_BUF_MAX];

	if (tui->rename_len == 0) { return; }
	memcpy(name, tui->rename_buf, (size_t)tui->rename_len + 1);
	if (files_rename_entry(&tui->files, &tui->shell, tui->rename_target, name) == 0) {
		tui_refresh_files(tui);
		tui_select_name(tui, name);
	}
}

/* Cancels rename mode and hides the cursor. */
static void rename_cancel(TUI *tui) {
	tui->rename_mode = 0;
	tui->rename_len = 0;
	tui->rename_cursor = 0;
	tui->rename_buf[0] = '\0';
	curs_set(0);
}

/* Dispatches a keypress while rename mode is active. */
static void handle_rename_input(TUI *tui, int ch) {
	switch (ch) {
	case 27:
		rename_cancel(tui);
		break;
	case '\n': case '\r': case KEY_ENTER:
		rename_commit(tui);
		rename_cancel(tui);
		break;
	default:
		edit_handle_ch(tui->rename_buf, &tui->rename_len, &tui->rename_cursor, RENAME_BUF_MAX, ch);
		break;
	}
}

/* Dispatches a keypress while goto mode is active. */
static void handle_goto_input(TUI *tui, int ch) {
	switch (ch) {
	case 27:
		goto_cancel(tui);
		break;
	case '\t':
		goto_tab_complete(tui);
		break;
	case '\n': case '\r': case KEY_ENTER:
		prompt_goto(tui, tui->goto_buf);
		goto_cancel(tui);
		break;
	case '\x7f': case KEY_BACKSPACE:
		if (tui->goto_len > 0) { tui->goto_buf[--tui->goto_len] = '\0'; }
		break;
	default:
		if (ch >= 32 && ch < 127 && tui->goto_len < GOTO_BUF_MAX - 1) {
			tui->goto_buf[tui->goto_len++] = (unsigned char)ch;
			tui->goto_buf[tui->goto_len] = '\0';
		}
		break;
	}
}

void prompt_quickshell_begin(TUI *tui) {
	tui->quickshell_mode = 1;
	tui->quickshell_len = 0;
	tui->quickshell_cursor = 0;
	tui->quickshell_buf[0] = '\0';
}

/* Cancels quickshell mode, clears the buffer, and hides the cursor. */
static void quickshell_cancel(TUI *tui) {
	tui->quickshell_mode = 0;
	tui->quickshell_len = 0;
	tui->quickshell_cursor = 0;
	tui->quickshell_buf[0] = '\0';
	curs_set(0);
}

static void quickshell_execute(TUI *tui) {
	char line[SHELL_MAX_INPUT];

	memcpy(line, tui->quickshell_buf, (size_t)tui->quickshell_len + 1);
	quickshell_cancel(tui);
	cmd_submit(tui, line, 0);
}

/* Dispatches a keypress while quickshell mode is active. */
static void handle_quickshell_input(TUI *tui, int ch) {
	switch (ch) {
	case 27:
		quickshell_cancel(tui);
		break;
	case '\n': case '\r': case KEY_ENTER:
		quickshell_execute(tui);
		break;
	default:
		edit_handle_ch(tui->quickshell_buf, &tui->quickshell_len, &tui->quickshell_cursor, SHELL_MAX_INPUT, ch);
		break;
	}
}

/* Feeds ch to whichever prompt is open. Returns 0 when none is. */
int prompt_handle_input(TUI *tui, int ch) {
	if (tui->rename_mode) {
		handle_rename_input(tui, ch);
		return 1;
	}
	if (tui->goto_mode) {
		handle_goto_input(tui, ch);
		return 1;
	}
	if (tui->quickshell_mode) {
		handle_quickshell_input(tui, ch);
		return 1;
	}
	return 0;
}
