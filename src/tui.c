/**
 * @file tui.c
 * @brief TUI rendering, input handling, and lifecycle for Quickfish.
 *
 * Manages the ncurses windows (file buffer, shell buffer, preview buffer, info
 * overlay), dispatches keyboard input to the appropriate subsystem, and
 * owns the resize signal handler.
 */

#include <limits.h>
#include <ncurses.h>
#include <panel.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "command.h"
#include "fileview.h"
#include "help.h"
#include "keymap.h"
#include "popup.h"
#include "preview.h"
#include "prompt.h"
#include "tui.h"
#include "util.h"

#define MIN_COLS 51 ///< Minimum terminal width to run the TUI
#define MIN_ROWS 25 ///< Minimum terminal height to run the TUI

static void handle_resize(int sig);
static void rebuild_windows(TUI *tui);
static void render_visual_indicator(TUI *tui);
static void file_cmd_clear(TUI *tui);
static int handle_file_cmd_input(TUI *tui, int ch);
static int take_count(TUI *tui);
static int run_key(TUI *tui, int ch, int scope, int count);

volatile int needs_resize = 0; ///< Flag set by the SIGWINCH handler to indicate that the terminal has been resized and the TUI needs to adjust its layout accordingly

static void handle_resize(int sig) { (void)sig; needs_resize = 1; }


static void file_cmd_clear(TUI *tui) {
	tui->file_cmd_len = 0;
	tui->file_cmd_buf[0] = '\0';
}

void tui_load_dir(TUI *tui, const char *path) {
	if (strcmp(path, tui->files.cwd) == 0) { return; }
	shell_push_dir(&tui->shell, tui->files.cwd);
	files_load_directory(&tui->files, path);
}

void tui_change_dir(TUI *tui, const char *dirname) {
	char old_cwd[PATH_MAX];

	snprintf(old_cwd, sizeof(old_cwd), "%s", tui->files.cwd);
	files_change_dir(&tui->files, dirname);
	if (strcmp(old_cwd, tui->files.cwd) != 0) {
		shell_push_dir(&tui->shell, old_cwd);
	}
}

void tui_refresh_files(TUI *tui) {
	refresh_files_buffer(&tui->files);
}

void tui_select_name(TUI *tui, const char *name) {
	for (int i = 0; i < tui->files.entry_count; i++) {
		if (strcmp(tui->files.entries[i].name, name) == 0) {
			tui->files.selected = i;
			return;
		}
	}
}

void tui_set_preview(TUI *tui, int on) {
	if (on == tui->preview_mode) { return; }
	tui->preview_mode = on;
	if (on) {
		tui->preview_saved_col_offset = tui->files.col_offset;
		return;
	}
	tui->files.col_offset = tui->preview_saved_col_offset;
	if (tui->active_buffer == BUFFER_PREVIEW) { tui->active_buffer = BUFFER_FILES; }
}

void tui_focus(TUI *tui, BufferType buffer) {
	if (buffer == BUFFER_SHELL) { tui->visual_mode = 0; }
	if (buffer != BUFFER_SHELL) { curs_set(0); }
	tui->active_buffer = buffer;
}

/* Digits build a count for the next key, and ".." runs the KEY_DOTDOT binding. */
static int handle_file_cmd_input(TUI *tui, int ch) {
	if (tui->file_cmd_len > 0 && tui->file_cmd_buf[0] == '.') {
		file_cmd_clear(tui);
		if (ch == '.') {
			run_key(tui, KEY_DOTDOT, KEY_SCOPE_FILES, 0);
			return 1;
		}
		return 0;
	}
	if (ch >= '0' && ch <= '9') {
		if (tui->file_cmd_len < FILE_CMD_BUF_MAX - 1) {
			tui->file_cmd_buf[tui->file_cmd_len++] = (char)ch;
			tui->file_cmd_buf[tui->file_cmd_len] = '\0';
		}
		return 1;
	}
	if (ch == '.') {
		file_cmd_clear(tui);
		tui->file_cmd_buf[tui->file_cmd_len++] = '.';
		tui->file_cmd_buf[tui->file_cmd_len] = '\0';
		return 1;
	}
	return 0;
}

static int take_count(TUI *tui) {
	int count;

	count = (tui->file_cmd_len > 0 && tui->file_cmd_buf[0] != '.') ? atoi(tui->file_cmd_buf) : 0;
	file_cmd_clear(tui);
	return count;
}

/* Runs the command bound to ch in scope. Returns 0 when nothing is bound. */
static int run_key(TUI *tui, int ch, int scope, int count) {
	const Keybind *kb;

	kb = keymap_find(ch, scope);
	if (!kb) { return 0; }
	cmd_run(tui, kb->line, count, 1);
	return 1;
}

/* Tears down and recreates all ncurses windows to match the current terminal size.
   Called once on init and again after every SIGWINCH. */
static void rebuild_windows(TUI *tui) {
	getmaxyx(stdscr, tui->max_y, tui->max_x);

	tui->files_height = (tui->max_y - 1) / 2;
	tui->shell_height = tui->max_y - tui->files_height - 1;
	tui->divider_y = tui->files_height;

	if (tui->files_win) { delwin(tui->files_win); }
	if (tui->shell_win) { delwin(tui->shell_win); }

	tui->files_win = newwin(tui->files_height, tui->max_x, 0, 0);
	tui->shell_win = newwin(tui->shell_height, tui->max_x, tui->files_height + 1, 0);
	if (tui->files_win) { keypad(tui->files_win, TRUE); }
	if (tui->shell_win) { keypad(tui->shell_win, TRUE); }

	if (tui->info_win) {
		wresize(tui->info_win, tui->max_y, tui->max_x);
		replace_panel(tui->info_panel, tui->info_win);
		if (!tui->info_mode) { hide_panel(tui->info_panel); }
	}
}

/**
 * @brief Initialises ncurses and allocates a TUI instance.
 *
 * Sets up color pairs, creates the files and shell windows, loads the
 * starting directory, and installs the SIGWINCH handler.
 *
 * @param start_dir Initial directory to display. If NULL or empty the function falls back to $HOME, then getpwuid, then ".".
 * @return Heap-allocated TUI on success, NULL on allocation or window failure.
 */
TUI *tui_init(const char *start_dir) {
	TUI *tui;
	char resolved[PATH_MAX];
	const char *dir;
	struct passwd *pw;

	tui = calloc(1, sizeof(TUI));
	if (!tui) { return NULL; }

	initscr();
	// raw() so Ctrl+C reaches us as a key instead of killing the process
	raw();
	timeout(INPUT_TIMEOUT_MS);
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	set_escdelay(0);

	if (has_colors()) {
		start_color();
		use_default_colors();

		init_pair(1, COLOR_WHITE, -1);
		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		init_pair(3, COLOR_CYAN, -1);
		init_pair(4, COLOR_YELLOW, -1);
		init_pair(5, COLOR_GREEN, -1);
		init_pair(6, COLOR_WHITE, -1);
		init_pair(7, COLOR_BLACK, COLOR_WHITE);
		init_pair(8, COLOR_CYAN, -1);
		init_pair(9, COLOR_CYAN, -1);
		init_pair(10, COLOR_WHITE, -1);

		init_pair(CP_EXT_IMAGE, COLOR_MAGENTA, -1);
		init_pair(CP_EXT_VIDEO, COLOR_RED, -1);
		init_pair(CP_EXT_AUDIO, COLOR_CYAN, -1);
		init_pair(CP_EXT_ARCHIVE, COLOR_YELLOW, -1);
		init_pair(CP_EXT_CODE, COLOR_GREEN, -1);
		init_pair(CP_EXT_DOC, COLOR_WHITE, -1);
		init_pair(CP_EXT_EXEC, COLOR_YELLOW, -1);
		init_pair(CP_EXT_DATA, COLOR_BLUE, -1);

		init_pair(CP_SEL_IMAGE, COLOR_BLACK, COLOR_MAGENTA);
		init_pair(CP_SEL_VIDEO, COLOR_BLACK, COLOR_RED);
		init_pair(CP_SEL_AUDIO, COLOR_BLACK, COLOR_CYAN);
		init_pair(CP_SEL_ARCHIVE, COLOR_BLACK, COLOR_YELLOW);
		init_pair(CP_SEL_CODE, COLOR_BLACK, COLOR_GREEN);
		init_pair(CP_SEL_DOC, COLOR_BLACK, COLOR_WHITE);
		init_pair(CP_SEL_EXEC, COLOR_BLACK, COLOR_YELLOW);
		init_pair(CP_SEL_DATA, COLOR_BLACK, COLOR_BLUE);
		init_pair(CP_SEL_DIR, COLOR_BLACK, COLOR_CYAN);
		init_pair(CP_SEL_FILE, COLOR_BLACK, COLOR_WHITE);
		init_pair(CP_MULTI_SEL, COLOR_BLACK, COLOR_MAGENTA);
		init_pair(CP_DELETE_CONFIRM, COLOR_BLACK, COLOR_RED);
		init_pair(CP_TRASH_CONFIRM, COLOR_BLACK, COLOR_YELLOW);
	}

	rebuild_windows(tui);
	if (!tui->files_win || !tui->shell_win) {
		endwin();
		free(tui);
		return NULL;
	}

	tui->active_buffer = BUFFER_FILES;
	tui->running = 1;
	tui->dirty = 1;
	tui->confirm = popup_confirm;

	dir = start_dir;
	if (!dir || dir[0] == '\0') {
		dir = getenv("HOME");
		if (!dir || dir[0] == '\0') {
			pw = getpwuid(getuid());
			dir = (pw && pw->pw_dir) ? pw->pw_dir : ".";
		}
	}
	if (realpath(dir, resolved)) { dir = resolved; }
	files_load_directory(&tui->files, dir);

	tui->shell.history_index = -1;

	signal(SIGWINCH, handle_resize);

	return tui;
}

/**
 * @brief Tears down ncurses and frees the TUI.
 *
 * Safe to call with a NULL pointer.
 *
 * @param tui The TUI instance to destroy.
 */
void tui_cleanup(TUI *tui) {
	if (!tui) { return; }
	if (tui->files_win) { delwin(tui->files_win); }
	if (tui->shell_win) { delwin(tui->shell_win); }
	if (tui->info_panel) { del_panel(tui->info_panel); }
	if (tui->info_win) { delwin(tui->info_win); }
	endwin();
	files_free(&tui->files);
	preview_free();
	free(tui);
}

/**
 * @brief Responds to a pending terminal resize.
 *
 * Should be called at the top of the main loop whenever needs_resize is set.
 * Resets the flag, rebuilds all windows, and forces a full redraw.
 *
 * @param tui The TUI instance to resize.
 */
void tui_resize_handler(TUI *tui) {
	if (!needs_resize) { return; }
	needs_resize = 0;
	tui->dirty = 1;

	endwin();
	refresh();
	clear();
	rebuild_windows(tui);

	clearok(stdscr, TRUE);
	if (tui->files_win) { clearok(tui->files_win, TRUE); touchwin(tui->files_win); }
	if (tui->shell_win) { clearok(tui->shell_win, TRUE); touchwin(tui->shell_win); }
	if (tui->info_win) { clearok(tui->info_win, TRUE); touchwin(tui->info_win); }

	wrefresh(stdscr);
}


static void render_visual_indicator(TUI *tui) {
	int llen;
	int lx;

	llen = (int)strlen(" VISUAL ");
	lx = (tui->max_x - llen) / 2;
	if (lx < 0) { lx = 0; }
	wattron(tui->files_win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	mvwprintw(tui->files_win, 0, lx, " VISUAL ");
	wattroff(tui->files_win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
	wnoutrefresh(tui->files_win);
}

/**
 * @brief Renders a complete frame of the TUI.
 *
 * Draws the horizontal divider, file buffer, shell buffer, and any active
 * overlay (preview, goto, rename, quickshell, info). Falls back to a
 * "terminal too small" message when dimensions are below the minimum.
 *
 * @param tui The TUI instance.
 */
void tui_render(TUI *tui) {
	int files_render_w;
	int preview_w;
	int cwdlen;
	int cwd_avail;
	int jump_x;
	char ghost[MAX_FILENAME];

	if (!tui) { return; }

	if (tui->max_x < MIN_COLS || tui->max_y < MIN_ROWS) {
		erase();
		attron(A_BOLD);
		mvprintw(0, 0, "Terminal size is too small!  Requires >= %dx%d", MIN_COLS, MIN_ROWS);
		attroff(A_BOLD);
		mvprintw(1, 0, "Current: %dx%d", tui->max_x, tui->max_y);
		refresh();
		return;
	}

	if (tui->active_buffer != BUFFER_SHELL &&
	    !tui->rename_mode && !tui->quickshell_mode && !tui->goto_mode) {
		curs_set(0);
	}

	erase();

	move(tui->divider_y, 0);
	for (int i = 0; i < tui->max_x; i++) { addch(ACS_HLINE); }

	move(tui->max_y - 1, 0);
	clrtoeol();
	wnoutrefresh(stdscr);

	files_render_w = tui->max_x;
	if (tui->preview_mode && tui->max_x > 8) {
		preview_w = tui->max_x / 2;
		files_render_w = tui->max_x - preview_w - 1;
	}

	files_render(&tui->files, tui->files_win, tui->files_height, files_render_w,
	             tui->active_buffer == BUFFER_FILES);

	if (tui->file_cmd_len > 0 && tui->files_win) {
		cwd_avail = files_render_w - 9;
		cwdlen = (int)strlen(tui->files.cwd);
		if (cwdlen > cwd_avail) { cwdlen = cwd_avail; }
		jump_x = 9 + cwdlen + 1;
		if (jump_x < files_render_w) {
			wattron(tui->files_win, COLOR_PAIR(CP_PROMPT) | A_BOLD);
			mvwprintw(tui->files_win, 0, jump_x, "%s", tui->file_cmd_buf);
			wattroff(tui->files_win, COLOR_PAIR(CP_PROMPT) | A_BOLD);
			wnoutrefresh(tui->files_win);
		}
	}

	if (tui->preview_mode && tui->files_win) { preview_render(tui, files_render_w); }
	if (tui->goto_mode && tui->files_win) { prompt_render_goto(tui); }

	ghost[0] = '\0';
	if (tui->active_buffer == BUFFER_SHELL) {
		compute_ghost(tui->shell.current_input, tui->shell.input_pos, tui->files.cwd, &tui->files, &tui->shell, ghost, sizeof(ghost));
	}
	shell_render(&tui->shell, tui->shell_win, tui->shell_height, tui->active_buffer == BUFFER_SHELL, ghost);

	if (tui->rename_mode && tui->files_win && tui->files.entry_count > 0) {
		prompt_render_rename(tui);
	}
	if (tui->quickshell_mode && tui->files_win) {
		prompt_render_quickshell(tui, files_render_w);
	}
	if (tui->info_mode && tui->info_win) {
		help_draw(tui);
		show_panel(tui->info_panel);
	}
	if (tui->visual_mode && tui->files_win) {
		render_visual_indicator(tui);
	}
	update_panels();

	if (tui->rename_mode && tui->files_win && tui->files.entry_count > 0) {
		wnoutrefresh(tui->files_win);
	} else if (tui->quickshell_mode && tui->files_win) {
		wnoutrefresh(tui->files_win);
	} else if (tui->goto_mode && tui->files_win) {
		wnoutrefresh(tui->files_win);
	} else if (tui->active_buffer == BUFFER_SHELL && tui->shell_win) {
		wnoutrefresh(tui->shell_win);
	}

	doupdate();
}


/* raw() disables the terminal's own Ctrl+Z, so suspend by hand.
   ncurses' SIGTSTP handler is bypassed so the terminal is only restored once. */
void tui_suspend(TUI *tui) {
	void (*old)(int);

	(void)tui;
	def_prog_mode();
	endwin();
	old = signal(SIGTSTP, SIG_DFL);
	raise(SIGTSTP);
	signal(SIGTSTP, old);
	reset_prog_mode();
	needs_resize = 1;
}

void tui_handle_preview_input(TUI *tui, int ch) {
	if (prompt_handle_input(tui, ch)) { return; }
	run_key(tui, ch, KEY_SCOPE_PREVIEW, 0);
}

void tui_handle_files_input(TUI *tui, int ch) {
	if (prompt_handle_input(tui, ch)) { return; }
	if (handle_file_cmd_input(tui, ch)) { return; }
	run_key(tui, ch, KEY_SCOPE_FILES, take_count(tui));
}

/* Keys bound in the shell scope (Esc, Ctrl combos, shifted arrows) are never
   printable, so everything else goes to the line editor. */
void tui_handle_shell_input(TUI *tui, int ch) {
	char line[SHELL_MAX_INPUT];

	if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
		memcpy(line, tui->shell.current_input, sizeof(line));
		memset(tui->shell.current_input, 0, SHELL_MAX_INPUT);
		tui->shell.input_pos = 0;
		tui->shell.history_index = -1;
		cmd_submit(tui, line, 1);
		return;
	}
	if (ch == '\t') {
		shell_tab_complete(&tui->shell, tui->files.cwd);
		return;
	}
	if (run_key(tui, ch, KEY_SCOPE_SHELL, 0)) { return; }
	shell_handle_char(&tui->shell, ch);
}

void tui_handle_input(TUI *tui, int ch) {
	if (tui->max_x < MIN_COLS || tui->max_y < MIN_ROWS) {
		if (ch == 'q' || ch == 'Q') { tui->running = 0; }
		return;
	}
	if (tui->active_buffer == BUFFER_INFO) {
		help_toggle(tui);
		return;
	}

	switch (tui->active_buffer) {
	case BUFFER_SHELL:
		tui_handle_shell_input(tui, ch);
		break;
	case BUFFER_PREVIEW:
		tui_handle_preview_input(tui, ch);
		break;
	default:
		tui_handle_files_input(tui, ch);
		break;
	}
}
