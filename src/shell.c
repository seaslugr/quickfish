#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fs.h"
#include "shell.h"
#include "util.h"

static void jumplist_push(ShellBuffer *shell, const char *path);
static void log_make_room(ShellBuffer *shell, size_t need);
static void log_write(ShellBuffer *shell, const char *data, size_t len);
static void log_begin(ShellBuffer *shell, int type);
static void log_end(ShellBuffer *shell);

static void jumplist_push(ShellBuffer *shell, const char *path) {
	if (shell->dir_jumplist_count >= DIR_JUMPLIST_MAX) {
		memmove(shell->dir_jumplist[0], shell->dir_jumplist[1], (size_t)(DIR_JUMPLIST_MAX - 1) * PATH_MAX);
		shell->dir_jumplist_count = DIR_JUMPLIST_MAX - 1;
	}
	str_copy(shell->dir_jumplist[shell->dir_jumplist_count], PATH_MAX, path);
	shell->dir_jumplist_count++;
}

/* Pops the most recent directory off the jumplist into out. Returns 0 when it is empty. */
int shell_pop_dir(ShellBuffer *shell, char *out, size_t out_size) {
	if (shell->dir_jumplist_count <= 0) { return 0; }
	shell->dir_jumplist_count--;
	str_copy(out, out_size, shell->dir_jumplist[shell->dir_jumplist_count]);
	return 1;
}

void shell_push_dir(ShellBuffer *shell, const char *path) {
	if (!path || path[0] == '\0') {
		return;
	}
	jumplist_push(shell, path);
}

void shell_add_history(ShellBuffer *shell, const char *cmd) {
	if (shell->history_count > 0 &&
	    strcmp(shell->history[shell->history_count - 1], cmd) == 0) {
		return;
	}
	if (shell->history_count >= SHELL_MAX_HIST) {
		memmove(shell->history[0], shell->history[1],
		        (size_t)(SHELL_MAX_HIST - 1) * SHELL_MAX_INPUT);
		shell->history_count = SHELL_MAX_HIST - 1;
	}
	str_copy(shell->history[shell->history_count], SHELL_MAX_INPUT, cmd);
	shell->history_count++;
}

void shell_handle_char(ShellBuffer *shell, int ch) {
	int len;

	len = (int)strlen(shell->current_input);

	if (ch == '\x7f' || ch == KEY_BACKSPACE) {
		if (shell->input_pos > 0) {
			memmove(shell->current_input + shell->input_pos - 1,
			        shell->current_input + shell->input_pos,
			        (size_t)(len - shell->input_pos + 1));
			shell->input_pos--;
		}
		return;
	}
	if (ch == KEY_DC) {
		if (shell->input_pos < len) {
			memmove(shell->current_input + shell->input_pos,
			        shell->current_input + shell->input_pos + 1,
			        (size_t)(len - shell->input_pos));
		}
		return;
	}
	if (ch == KEY_LEFT) {
		if (shell->input_pos > 0) { shell->input_pos--; }
		return;
	}
	if (ch == KEY_RIGHT) {
		if (shell->input_pos < len) { shell->input_pos++; }
		return;
	}
	if (ch == KEY_HOME) {
		shell->input_pos = 0;
		return;
	}
	if (ch == KEY_END) {
		shell->input_pos = len;
		return;
	}
	if (ch == KEY_UP) {
		if (shell->history_index < shell->history_count - 1) {
			shell->history_index++;
			strncpy(shell->current_input,
			        shell->history[shell->history_count - 1 - shell->history_index],
			        SHELL_MAX_INPUT - 1);
			shell->input_pos = (int)strlen(shell->current_input);
		}
		return;
	}
	if (ch == KEY_DOWN) {
		if (shell->history_index > 0) {
			shell->history_index--;
			strncpy(shell->current_input,
			        shell->history[shell->history_count - 1 - shell->history_index],
			        SHELL_MAX_INPUT - 1);
			shell->input_pos = (int)strlen(shell->current_input);
		} else if (shell->history_index == 0) {
			shell->history_index = -1;
			memset(shell->current_input, 0, SHELL_MAX_INPUT);
			shell->input_pos = 0;
		}
		return;
	}
	if (ch >= 32 && ch < 127 && len < SHELL_MAX_INPUT - 1) {
		memmove(shell->current_input + shell->input_pos + 1,
		        shell->current_input + shell->input_pos,
		        (size_t)(len - shell->input_pos + 1));
		shell->current_input[shell->input_pos++] = (unsigned char)ch;
	}
}

void shell_tab_complete(ShellBuffer *shell, const char *cwd) {
	int tok_start;
	char token[SHELL_MAX_INPUT];
	int tok_len;
	char dir_part[PATH_MAX];
	char name_part[MAX_FILENAME];
	const char *last_slash;
	char search_dir[PATH_MAX];
	char completed[MAX_FILENAME];
	int count;
	char new_token[PATH_MAX];
	char new_input[SHELL_MAX_INPUT];

	tok_start = shell->input_pos;
	while (tok_start > 0 && shell->current_input[tok_start - 1] != ' ') { tok_start--; }

	tok_len = shell->input_pos - tok_start;
	strncpy(token, shell->current_input + tok_start, tok_len);
	token[tok_len] = '\0';

	last_slash = strrchr(token, '/');

	if (last_slash) {
		int dlen = (int)(last_slash - token);
		strncpy(dir_part, token, dlen);
		dir_part[dlen] = '\0';
		strncpy(name_part, last_slash + 1, MAX_FILENAME - 1);
		name_part[MAX_FILENAME - 1] = '\0';
	} else {
		dir_part[0] = '\0';
		strncpy(name_part, token, MAX_FILENAME - 1);
		name_part[MAX_FILENAME - 1] = '\0';
	}

	if (dir_part[0] == '\0') {
		strncpy(search_dir, cwd, PATH_MAX - 1);
		search_dir[PATH_MAX - 1] = '\0';
	} else if (dir_part[0] == '/') {
		strncpy(search_dir, dir_part, PATH_MAX - 1);
		search_dir[PATH_MAX - 1] = '\0';
	} else {
		path_join(search_dir, PATH_MAX, cwd, dir_part);
	}

	count = complete_in_dir(search_dir, name_part, completed, sizeof(completed));
	if (count == 0 || completed[0] == '\0') { return; }

	if (dir_part[0] == '\0') {
		strncpy(new_token, completed, sizeof(new_token) - 1);
		new_token[sizeof(new_token) - 1] = '\0';
	} else {
		path_join(new_token, sizeof(new_token), dir_part, completed);
	}

	strncpy(new_input, shell->current_input, tok_start);
	new_input[tok_start] = '\0';
	strncat(new_input, new_token, SHELL_MAX_INPUT - 1 - tok_start);

	strncpy(shell->current_input, new_input, SHELL_MAX_INPUT - 1);
	shell->current_input[SHELL_MAX_INPUT - 1] = '\0';
	shell->input_pos = (int)strlen(shell->current_input);
}

void shell_restore_ncurses(WINDOW *files_win, WINDOW *shell_win) {
	reset_prog_mode();
	keypad(stdscr, TRUE);
	timeout(INPUT_TIMEOUT_MS);

	if (files_win) { keypad(files_win, TRUE); }
	if (shell_win) { keypad(shell_win, TRUE); }

	curs_set(0);
	clearok(stdscr, TRUE);
	refresh();

	if (files_win) { clearok(files_win, TRUE); touchwin(files_win); }
	if (shell_win) { clearok(shell_win, TRUE); touchwin(shell_win); }
}

void shell_print_jumplist(ShellBuffer *shell) {
	char display[SHELL_OUTPUT_MAX];
	int pos;
	int n;
	int wrote_all;
	int suffix_len;
	const char *suffix;

	if (shell->dir_jumplist_count <= 0) {
		print_to_shell(shell, "[jumplist] (empty)\n", SHELL_MSG_NORMAL);
		return;
	}

	suffix = "... truncated ...\n";
	suffix_len = (int)strlen(suffix);
	pos = snprintf(display, sizeof(display), "[jumplist]\n");
	wrote_all = 1;
	for (int i = shell->dir_jumplist_count - 1; i >= 0 && pos < (int)sizeof(display) - 1; i--) {
		n = snprintf(display + pos, sizeof(display) - pos, "%2d. %s\n",
		             shell->dir_jumplist_count - i, shell->dir_jumplist[i]);
		if (n < 0 || n >= (int)sizeof(display) - pos) {
			wrote_all = 0;
			break;
		}
		pos += n;
	}

	if (!wrote_all) {
		if (pos > (int)sizeof(display) - 1 - suffix_len) {
			pos = (int)sizeof(display) - 1 - suffix_len;
		}
		snprintf(display + pos, sizeof(display) - pos, "%s", suffix);
	}

	print_to_shell(shell, display, SHELL_MSG_NORMAL);
}

void shell_spawn(const char *cwd, WINDOW *files_win, WINDOW *shell_win) {
	const char *sh;
	pid_t pid;
	int status;

	def_prog_mode();
	endwin();

	sh = getenv("SHELL");
	if (!sh || sh[0] == '\0') { sh = "/bin/sh"; }

	pid = fork();
	if (pid == 0) {
		if (cwd && cwd[0]) { chdir(cwd); }
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		execlp(sh, sh, (char *)NULL);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, &status, 0);
	}

	shell_restore_ncurses(files_win, shell_win);
}

void shell_run(ShellBuffer *shell, const char *cmd, const char *cwd) {
	int pfd[2];
	const char *sh;
	pid_t pid;
	char buf[4096];
	char msg[64];
	ssize_t n;
	int status;
	int started;

	log_begin(shell, SHELL_MSG_CMD);
	log_write(shell, "$ ", 2);
	log_write(shell, cmd, strlen(cmd));
	log_end(shell);

	if (pipe(pfd) != 0) { return; }

	sh = getenv("SHELL");
	if (!sh || sh[0] == '\0') { sh = "/bin/sh"; }

	pid = fork();
	if (pid == 0) {
		close(pfd[0]);
		dup2(pfd[1], STDOUT_FILENO);
		dup2(pfd[1], STDERR_FILENO);
		close(pfd[1]);
		if (cwd && cwd[0]) { chdir(cwd); }
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		execlp(sh, sh, "-c", cmd, (char *)NULL);
		_exit(127);
	}

	close(pfd[1]);

	if (pid > 0) {
		started = 0;
		while ((n = read(pfd[0], buf, sizeof(buf))) > 0) {
			if (!started) {
				log_begin(shell, SHELL_MSG_NORMAL);
				started = 1;
			}
			log_write(shell, buf, (size_t)n);
		}
		if (started) { log_end(shell); }
		close(pfd[0]);
		if (waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			snprintf(msg, sizeof(msg), "exit status %d\n", WEXITSTATUS(status));
			print_to_shell(shell, msg, SHELL_MSG_WARN);
		}
	} else {
		close(pfd[0]);
	}
}

/* Drops whole lines from the front of the log until need more bytes fit. */
static void log_make_room(ShellBuffer *shell, size_t need) {
	size_t drop;
	const char *nl;

	if ((size_t)shell->output_len + need + 1 <= SHELL_LOG_MAX) { return; }
	drop = (size_t)shell->output_len + need + 1 - SHELL_LOG_MAX;
	nl = (drop < (size_t)shell->output_len) ? memchr(shell->last_output + drop, '\n', (size_t)shell->output_len - drop) : NULL;
	if (!nl) {
		shell_clear_output(shell);
		return;
	}
	drop = (size_t)(nl + 1 - shell->last_output);
	memmove(shell->last_output, shell->last_output + drop, (size_t)shell->output_len - drop + 1);
	shell->output_len -= (int)drop;
}

static void log_write(ShellBuffer *shell, const char *data, size_t len) {
	if (len > SHELL_LOG_MAX - 2) {
		data += len - (SHELL_LOG_MAX - 2);
		len = SHELL_LOG_MAX - 2;
	}
	log_make_room(shell, len);
	memcpy(shell->last_output + shell->output_len, data, len);
	shell->output_len += (int)len;
	shell->last_output[shell->output_len] = '\0';
}

/* Starts a new message. Its type byte colors every line until the next message. */
static void log_begin(ShellBuffer *shell, int type) {
	char t;

	log_end(shell);
	t = (char)type;
	log_write(shell, &t, 1);
}

static void log_end(ShellBuffer *shell) {
	if (shell->output_len > 0 && shell->last_output[shell->output_len - 1] != '\n') {
		log_write(shell, "\n", 1);
	}
}

void shell_clear_output(ShellBuffer *shell) {
	shell->output_len = 0;
	shell->last_output[0] = '\0';
}

int print_to_shell(ShellBuffer *shell, const char *text, int type) {
	log_begin(shell, type);
	if (type == SHELL_MSG_ERROR) {
		log_write(shell, "ERROR: ", 7);
	} else if (type == SHELL_MSG_WARN) {
		log_write(shell, "WARN: ", 6);
	}
	log_write(shell, text, strlen(text));
	log_end(shell);
	return 0;
}

void shell_render(ShellBuffer *shell, WINDOW *win, int height, int focused, const char *ghost) {
	int width;
	int out_rows;
	const char *line_ptr[256];
	int line_len[256];
	char line_type[256];
	int nlines;
	int head;
	int cur_type;
	int len;
	int pair;
	int slot;
	attr_t at;
	const char *p;
	const char *end;
	const char *nl;
	int result_row;
	int cmd_cols;
	int x;
	int input_row;
	int cx;

	width = getmaxx(win);
	werase(win);

	if (focused) {
		wattron(win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
		mvwprintw(win, 0, 0, " SHELL ");
		wattroff(win, COLOR_PAIR(CP_TITLE_F) | A_BOLD);
		mvwprintw(win, 0, 8, " Enter: spawn shell q: quit");
	} else {
		wattron(win, COLOR_PAIR(CP_TITLE_UF) | A_BOLD);
		mvwprintw(win, 0, 0, " SHELL ");
		wattroff(win, COLOR_PAIR(CP_TITLE_UF) | A_BOLD);
	}
	wclrtoeol(win);

	// Keep the last out_rows lines in a ring while walking the log, carrying each message's type forward
	out_rows = height - 3;
	if (out_rows > 256) { out_rows = 256; }
	if (out_rows > 0 && shell->output_len > 0) {
		nlines = 0;
		head = 0;
		cur_type = SHELL_MSG_NORMAL;
		p = shell->last_output;
		end = shell->last_output + shell->output_len;
		while (p < end) {
			nl = memchr(p, '\n', (size_t)(end - p));
			if (!nl) { nl = end; }
			if (*p >= SHELL_MSG_NORMAL && *p <= SHELL_MSG_CMD) {
				cur_type = *p;
				p++;
			}
			line_ptr[head] = p;
			line_len[head] = (int)(nl - p);
			line_type[head] = (char)cur_type;
			head = (head + 1) % out_rows;
			if (nlines < out_rows) { nlines++; }
			p = nl + 1;
		}

		for (int i = 0; i < nlines; i++) {
			slot = (head - nlines + i + out_rows) % out_rows;
			at = A_NORMAL;
			len = line_len[slot];
			if (len > width - 2) {
				len = width - 2;
				while (len > 0 && (line_ptr[slot][len] & 0xC0) == 0x80) { len--; }
			}
			pair = 0;
			switch (line_type[slot]) {
			case SHELL_MSG_ERROR: pair = CP_EXT_VIDEO; at = A_BOLD; break;
			case SHELL_MSG_WARN: pair = CP_EXT_ARCHIVE; at = A_BOLD; break;
			case SHELL_MSG_CMD: at = A_DIM; break;
			default: break;
			}
			wattron(win, COLOR_PAIR(pair) | at);
			mvwaddnstr(win, 1 + i, 1, line_ptr[slot], len);
			wattroff(win, COLOR_PAIR(pair) | at);
		}
	}

	result_row = height - 2;
	if (result_row >= 1 && shell->last_cmd[0] != '\0') {
		wattron(win, A_DIM);
		mvwprintw(win, result_row, 1, "%.*s", width / 2, shell->last_cmd);
		if (shell->last_result[0] != '\0') {
			cmd_cols = (int)strlen(shell->last_cmd);
			if (cmd_cols > width / 2) { cmd_cols = width / 2; }
			x = 1 + cmd_cols;
			if (x + 3 < width) {
				mvwprintw(win, result_row, x, " > %.*s", width - x - 4, shell->last_result);
			}
		}
		wattroff(win, A_DIM);
		wclrtoeol(win);
	}

	input_row = height - 1;
	wattron(win, COLOR_PAIR(CP_PROMPT) | A_BOLD);
	mvwaddstr(win, input_row, 1, "> ");
	wattroff(win, COLOR_PAIR(CP_PROMPT) | A_BOLD);

	waddstr(win, shell->current_input);

	if (focused && ghost && ghost[0] != '\0') {
		wclrtoeol(win);
		wattron(win, A_DIM);
		waddstr(win, ghost);
		wattroff(win, A_DIM);
	}

	wclrtoeol(win);

	if (focused) {
		cx = 3 + shell->input_pos;
		wmove(win, input_row, cx);
		leaveok(win, FALSE);
		curs_set(2);
	} else {
		leaveok(win, TRUE);
	}

	wnoutrefresh(win);
}
