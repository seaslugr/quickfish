#include <ncurses.h>
#include <panel.h>
#include <stdio.h>
#include <string.h>

#include "command.h"
#include "help.h"
#include "keymap.h"

void help_toggle(TUI *tui) {
	if (tui->info_mode) {
		tui->info_mode = 0;
		tui->active_buffer = tui->prev_buffer;
		hide_panel(tui->info_panel);
		return;
	}
	tui->prev_buffer = tui->active_buffer;
	tui->active_buffer = BUFFER_INFO;
	tui->info_mode = 1;
	if (!tui->info_win) {
		tui->info_win = newwin(tui->max_y, tui->max_x, 0, 0);
		tui->info_panel = new_panel(tui->info_win);
	}
}

/* Builds the F1 overlay from the keymap: bindings with the same command line
   and help share a row ("j / Down"), grouped under their command's category. */
void help_draw(TUI *tui) {
	char keys[64];
	char key[16];
	char word[64];
	char cell[128];
	const Keybind *kb;
	const Keybind *other;
	const Command *c;
	size_t wlen;
	int w;
	int row;
	int col;
	int mid;
	int dup;
	CmdCategory cats[] = { CMD_CAT_NAV, CMD_CAT_FILES, CMD_CAT_PANES };

	werase(tui->info_win);
	box(tui->info_win, 0, 0);

	w = tui->max_x - 4;
	row = 1;
	mid = (tui->max_x / 4 < 50) ? 50 : tui->max_x / 4;

	wattron(tui->info_win, A_BOLD);
	mvwprintw(tui->info_win, row++, 2, "Quickfish Help");
	wattroff(tui->info_win, A_BOLD);

	for (size_t ci = 0; ci < sizeof(cats) / sizeof(cats[0]); ci++) {
		wattron(tui->info_win, A_BOLD | A_UNDERLINE);
		mvwprintw(tui->info_win, row++, 2, "%s", cmd_category_name(cats[ci]));
		wattroff(tui->info_win, A_BOLD | A_UNDERLINE);

		col = 0;
		for (int i = 0; i < keymap_count(); i++) {
			kb = keymap_at(i);
			if (!kb->help) { continue; }
			wlen = strcspn(kb->line, " ");
			if (wlen >= sizeof(word)) { continue; }
			memcpy(word, kb->line, wlen);
			word[wlen] = '\0';
			c = cmd_find(word);
			if (!c || c->category != cats[ci]) { continue; }

			dup = 0;
			for (int j = 0; j < i && !dup; j++) {
				other = keymap_at(j);
				dup = other->help && strcmp(other->line, kb->line) == 0 && strcmp(other->help, kb->help) == 0;
			}
			if (dup) { continue; }

			keys[0] = '\0';
			for (int j = i; j < keymap_count(); j++) {
				other = keymap_at(j);
				if (!other->help || strcmp(other->line, kb->line) != 0 || strcmp(other->help, kb->help) != 0) { continue; }
				keymap_key_name(other->key, key, sizeof(key));
				if (strlen(keys) + strlen(key) + 4 < sizeof(keys)) {
					if (keys[0]) { strcat(keys, " / "); }
					strcat(keys, key);
				}
			}
			snprintf(cell, sizeof(cell), "  %-14s: %s", keys, kb->help);
			mvwprintw(tui->info_win, row, 2 + col * mid, "%.*s", mid - 1, cell);
			col = !col;
			if (col == 0) { row++; }
		}
		if (col) { row++; }
	}

	row++;
	wattron(tui->info_win, A_DIM);
	mvwprintw(tui->info_win, row, 2, "%-*.*s", w, w, "Type help in the shell for every command, ! runs a system command.");
	mvwprintw(tui->info_win, tui->max_y - 2, 2, "%-*.*s", w, w, "Press any key to close.");
	wattroff(tui->info_win, A_DIM);

	wnoutrefresh(tui->info_win);
}
