#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "colors.h"
#include "fileview.h"
#include "filetype.h"
#include "util.h"

#define INDEX_W 4
#define NAME_W 22
#define COL_W (INDEX_W + NAME_W)
#define COL_PAD 2

void files_render(FilesBuffer *files, WINDOW *win, int height, int width, int focused) {
	int rows;
	const char *cwd;
	int avail;
	char count_str[32];
	char sel_str[32];
	int cell_w;
	int n_cols;
	int first;
	int last;
	int visible;
	int cwdlen;
	int cx;
	int sx;
	DirEntry *selected_entry;
	char full[PATH_MAX];
	struct stat st;
	char perms[11];
	char size_str[32];
	off_t size;
	char info[64];
	int ix;
	int total_cols;
	int sel_col;
	int ac;
	int x;
	int idx;
	DirEntry *e;
	int y;
	int is_sel;
	int name_pair;
	int sel_pair;
	attr_t name_attrs;
	char display[NAME_W * 4 + 1];

	rows = height - 1;
	if (rows <= 0 || width <= 0) { return; }

	werase(win);

	if (focused) {
		wattron(win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
		mvwprintw(win, 0, 0, " FILES ");
		wattroff(win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	} else {
		wattron(win, COLOR_PAIR(CP_TITLE_UF) | A_BOLD);
		mvwprintw(win, 0, 0, " FILES ");
		wattroff(win, COLOR_PAIR(CP_TITLE_UF) | A_BOLD);
	}

	cwd = files->cwd;
	avail = width - 9;
	if (avail > 0) {
		cwdlen = (int)strlen(cwd);
		if (cwdlen > avail) {
			cwd += cwdlen - avail;
			while ((*cwd & 0xC0) == 0x80) { cwd++; }
		}
		wattron(win, A_BOLD);
		mvwprintw(win, 0, 8, " %.*s", avail, cwd);
		wattroff(win, A_BOLD);
	}

	cell_w = COL_W + COL_PAD;
	n_cols = (width / cell_w > 0) ? width / cell_w : 1;
	first = files->col_offset * rows;
	last = (files->col_offset + n_cols) * rows;
	if (last > files->entry_count) { last = files->entry_count; }
	visible = (files->entry_count > 0) ? (last - first) : 0;
	snprintf(count_str, sizeof(count_str), " %d/%d i ", visible, files->entry_count);
	cx = width - (int)strlen(count_str);
	if (files->sel_count > 0 && cx > 0) {
		snprintf(sel_str, sizeof(sel_str), " [%d sel] ", files->sel_count);
		sx = cx - (int)strlen(sel_str);
		if (sx > 0) {
			wattron(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
			mvwprintw(win, 0, sx, "%s", sel_str);
			wattroff(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
		} else {
			int sel_len = (int)strlen(sel_str);
			int sel_x = width - sel_len;
			if (sel_x < 0) { sel_x = 0; }
			wattron(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
			mvwprintw(win, 1, sel_x, "%s", sel_str);
			wattroff(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
		}
	}
	if (cx > 0) { mvwprintw(win, 0, cx, "%s", count_str); }

	if (files->entry_count > 0) {
		selected_entry = &files->entries[files->selected];
		path_join(full, sizeof(full), files->cwd, selected_entry->name);

		if (stat(full, &st) == 0) {
			perms[0] = S_ISDIR(st.st_mode) ? 'd' : '-';
			perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
			perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
			perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
			perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
			perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
			perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
			perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
			perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
			perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
			perms[10] = '\0';

			size = st.st_size;
			if (S_ISDIR(st.st_mode)) {
				snprintf(size_str, sizeof(size_str), "-");
			} else {
				format_size(size, size_str, sizeof(size_str));
			}
			snprintf(info, sizeof(info), " %s %s ", perms, size_str);
			ix = width - (int)strlen(info);
			if (ix > 0) {
				wattron(win, A_DIM);
				mvwprintw(win, rows, ix, "%s", info);
				wattroff(win, A_DIM);
			}
		}
	}

	if (files->entry_count == 0) {
		mvwprintw(win, 2, 2, "(empty directory)");
		wnoutrefresh(win);
		return;
	}

	total_cols = (files->entry_count + rows - 1) / rows;
	if (total_cols < 1) { total_cols = 1; }

	sel_col = files->selected / rows;
	if (sel_col < files->col_offset) { files->col_offset = sel_col; }
	if (sel_col >= files->col_offset + n_cols) { files->col_offset = sel_col - n_cols + 1; }
	if (files->col_offset < 0) { files->col_offset = 0; }

	for (int vc = 0; vc < n_cols; vc++) {
		ac = files->col_offset + vc;
		if (ac >= total_cols) { break; }
		x = vc * cell_w;

		for (int row = 0; row < rows; row++) {
			idx = ac * rows + row;
			if (idx >= files->entry_count) { break; }

			e = &files->entries[idx];
			y = row + 1;
			is_sel = (idx == files->selected);

			wmove(win, y, x + 1);

			name_pair = entry_color_pair(e, 0);
			sel_pair = entry_color_pair(e, 1);

			if (files->entries[idx].in_move_reg) {
				wattron(win, COLOR_PAIR(CP_EXT_ARCHIVE) | A_BOLD);
				wprintw(win, "mv %3d ", e->index);
				wattroff(win, COLOR_PAIR(CP_EXT_ARCHIVE) | A_BOLD);
			} else if (is_sel) {
				wattron(win, COLOR_PAIR(sel_pair) | A_BOLD);
				wprintw(win, "%3d ", e->index);
				wattroff(win, COLOR_PAIR(sel_pair) | A_BOLD);
			} else if (files->entries[idx].marked) {
				wattron(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
				wprintw(win, "%3d ", e->index);
				wattroff(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
			} else {
				wattron(win, COLOR_PAIR(CP_INDEX));
				wprintw(win, "%3d ", e->index);
				wattroff(win, COLOR_PAIR(CP_INDEX));
			}

			name_attrs = A_NORMAL;
			if (e->type == ENTRY_DIR) {
				name_attrs = e->is_empty ? A_DIM : A_BOLD;
			} else if (name_pair == CP_EXT_EXEC) {
				name_attrs = A_BOLD;
			}

			if (is_sel) {
				wattron(win, COLOR_PAIR(sel_pair) | A_BOLD);
			} else if (files->entries[idx].marked) {
				wattron(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
			} else {
				wattron(win, COLOR_PAIR(name_pair) | name_attrs);
			}

			str_fit_cols(display, sizeof(display), e->name, NAME_W);
			waddstr(win, display);

			if (is_sel) {
				wattroff(win, COLOR_PAIR(sel_pair) | A_BOLD);
			} else if (files->entries[idx].marked) {
				wattroff(win, COLOR_PAIR(CP_MULTI_SEL) | A_BOLD);
			} else {
				wattroff(win, COLOR_PAIR(name_pair) | name_attrs);
			}
		}
	}

	if (files->col_offset > 0) { mvwprintw(win, 1, 0, "<"); }
	if (files->col_offset + n_cols < total_cols) { mvwprintw(win, 1, width - 1, ">"); }

	wnoutrefresh(win);
}
