#include <ncurses.h>
#include <string.h>

#include "colors.h"
#include "popup.h"

/* Blocking yes/no popup used through tui->confirm. danger picks the red warning color. */
int popup_confirm(TUI *tui, const char *body, const char *warning, int danger) {
	int body_len;
	int warn_len;
	int w_width;
	int w_height;
	int w_y;
	int w_x;
	int confirmed;
	int ch;
	WINDOW *popup;

	body_len = (int)strlen(body);
	warn_len = warning ? (int)strlen(warning) : 0;
	w_width = body_len + 6;
	if (warn_len + 6 > w_width) { w_width = warn_len + 6; }
	if (w_width < 46) { w_width = 46; }
	if (w_width > tui->max_x) { w_width = tui->max_x; }
	w_height = warning ? 6 : 5;
	w_y = (tui->max_y - w_height) / 2;
	w_x = (tui->max_x - w_width) / 2;
	if (w_y < 0) { w_y = 0; }
	if (w_x < 0) { w_x = 0; }

	popup = newwin(w_height, w_width, w_y, w_x);
	if (!popup) { return 0; }

	wbkgd(popup, COLOR_PAIR(1));
	wattron(popup, COLOR_PAIR(1) | A_BOLD);
	box(popup, 0, 0);
	mvwprintw(popup, 1, 2, "%.*s", w_width - 4, body);
	wattroff(popup, COLOR_PAIR(1) | A_BOLD);
	if (warning) {
		wattron(popup, COLOR_PAIR(danger ? CP_DELETE_CONFIRM : CP_TRASH_CONFIRM) | A_BOLD);
		mvwprintw(popup, 2, 2, "%.*s", w_width - 4, warning);
		wattroff(popup, COLOR_PAIR(danger ? CP_DELETE_CONFIRM : CP_TRASH_CONFIRM) | A_BOLD);
	}
	wattron(popup, COLOR_PAIR(1) | A_DIM);
	mvwaddstr(popup, w_height - 2, 2, "[y] confirm   [n / Esc] cancel");
	wattroff(popup, COLOR_PAIR(1) | A_DIM);
	wnoutrefresh(popup);
	doupdate();

	keypad(popup, TRUE);
	confirmed = 0;
	while (1) {
		ch = wgetch(popup);
		if (ch == 'y' || ch == 'Y') {
			confirmed = 1;
			break;
		}
		if (ch == 'n' || ch == 'N' || ch == 27) { break; }
	}
	delwin(popup);

	clearok(curscr, TRUE);
	touchwin(tui->files_win);
	touchwin(tui->shell_win);
	tui->dirty = 1;
	return confirmed;
}
