#define _GNU_SOURCE

#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "launch.h"
#include "util.h"

void files_open_selected(FilesBuffer *files, struct ShellBuffer *shell, WINDOW *files_win, WINDOW *shell_win) {
	char path[PATH_MAX];
	int pfd[2];
	pid_t pid;
	char mime[256];
	char desktop_id[256];
	char desktop_path[PATH_MAX];
	char line[512];
	char exec_cmd[512];
	ssize_t n;
	int status;
	int is_terminal_app;
	int found;
	const char *editor;
	const char *home;
	FILE *f;
	const char *search_dirs[] = {
		"/usr/share/applications",
		"/usr/local/share/applications",
		NULL,
	};
	char home_apps[PATH_MAX];

	(void)shell;

	if (files_get_selected_path(files, path, sizeof(path)) != 1) { return; }

	mime[0] = '\0';
	if (pipe(pfd) == 0) {
		pid = fork();
		if (pid == 0) {
			close(pfd[0]);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
			execlp("xdg-mime", "xdg-mime", "query", "filetype", path, (char *)NULL);
			_exit(1);
		}
		close(pfd[1]);
		n = (pid > 0) ? read(pfd[0], mime, sizeof(mime) - 1) : 0;
		close(pfd[0]);
		if (pid > 0) { waitpid(pid, &status, 0); }
		if (n > 0) {
			mime[n] = '\0';
			if (mime[n - 1] == '\n') { mime[--n] = '\0'; }
		}
	}

	desktop_id[0] = '\0';
	if (mime[0] != '\0' && pipe(pfd) == 0) {
		pid = fork();
		if (pid == 0) {
			close(pfd[0]);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
			execlp("xdg-mime", "xdg-mime", "query", "default", mime, (char *)NULL);
			_exit(1);
		}
		close(pfd[1]);
		n = (pid > 0) ? read(pfd[0], desktop_id, sizeof(desktop_id) - 1) : 0;
		close(pfd[0]);
		if (pid > 0) { waitpid(pid, &status, 0); }
		if (n > 0) {
			desktop_id[n] = '\0';
			if (desktop_id[n - 1] == '\n') { desktop_id[--n] = '\0'; }
		}
	}

	is_terminal_app = 0;
	exec_cmd[0] = '\0';
	if (desktop_id[0] != '\0') {
		home = getenv("HOME");
		if (home) {
			snprintf(home_apps, sizeof(home_apps), "%s/.local/share/applications", home);
		} else {
			home_apps[0] = '\0';
		}

		found = 0;
		for (int i = 0; !found; i++) {
			const char *dir;
			if (search_dirs[i] != NULL) {
				dir = search_dirs[i];
			} else if (home_apps[0] != '\0') {
				dir = home_apps;
				home_apps[0] = '\0';
			} else {
				break;
			}
			path_join(desktop_path, sizeof(desktop_path), dir, desktop_id);
			f = fopen(desktop_path, "r");
			if (!f) { continue; }
			found = 1;
			while (fgets(line, sizeof(line), f)) {
				if (strncmp(line, "Terminal=true", 13) == 0) {
					is_terminal_app = 1;
				}
				if (strncmp(line, "Exec=", 5) == 0 && exec_cmd[0] == '\0') {
					strncpy(exec_cmd, line + 5, sizeof(exec_cmd) - 1);
					exec_cmd[sizeof(exec_cmd) - 1] = '\0';
					n = (ssize_t)strlen(exec_cmd);
					if (n > 0 && exec_cmd[n - 1] == '\n') { exec_cmd[--n] = '\0'; }
					char *pct = strchr(exec_cmd, ' ');
					if (pct) { *pct = '\0'; }
					pct = strchr(exec_cmd, '%');
					if (pct) { *pct = '\0'; }
				}
			}
			fclose(f);
		}
	}

	if (is_terminal_app) {
		if (exec_cmd[0] == '\0') {
			editor = getenv("VISUAL");
			if (!editor || editor[0] == '\0') { editor = getenv("EDITOR"); }
			if (!editor || editor[0] == '\0') { editor = "vi"; }
			strncpy(exec_cmd, editor, sizeof(exec_cmd) - 1);
		}
		def_prog_mode();
		endwin();
		pid = fork();
		if (pid == 0) {
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);
			execlp(exec_cmd, exec_cmd, path, (char *)NULL);
			_exit(1);
		} else if (pid > 0) {
			waitpid(pid, &status, 0);
		}
		reset_prog_mode();
		keypad(stdscr, TRUE);
		timeout(INPUT_TIMEOUT_MS);
		curs_set(0);
		if (files_win) { keypad(files_win, TRUE); touchwin(files_win); }
		if (shell_win) { keypad(shell_win, TRUE); touchwin(shell_win); }
		refresh();
	} else {
		pid = fork();
		if (pid == 0) {
			setsid();
			if (fork() != 0) { _exit(0); }
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			execlp("xdg-open", "xdg-open", path, (char *)NULL);
			_exit(1);
		} else if (pid > 0) {
			waitpid(pid, NULL, WNOHANG);
		}
	}
}
