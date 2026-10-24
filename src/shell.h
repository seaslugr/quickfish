#pragma once

#include <limits.h>
#include <ncurses.h>
#include <stddef.h>

#include "colors.h"

#define SHELL_MAX_INPUT 512
#define SHELL_MAX_HIST 100
#define SHELL_OUTPUT_MAX 8192
#define SHELL_LOG_MAX 65536
#define SHELL_RESULT_MAX 128
#define DIR_JUMPLIST_MAX 64

#define SHELL_MSG_NORMAL 1
#define SHELL_MSG_WARN 2
#define SHELL_MSG_ERROR 3
#define SHELL_MSG_CMD 4

typedef struct ShellBuffer {
	int history_count;
	int history_index;
	int input_pos;
	int dir_jumplist_count;
	int output_len;
	char last_result[SHELL_RESULT_MAX];
	char last_cmd[SHELL_MAX_INPUT];
	char current_input[SHELL_MAX_INPUT];
	char last_output[SHELL_LOG_MAX];
	char history[SHELL_MAX_HIST][SHELL_MAX_INPUT];
	char dir_jumplist[DIR_JUMPLIST_MAX][PATH_MAX];
} ShellBuffer;

void shell_add_history(ShellBuffer *shell, const char *cmd);
void shell_handle_char(ShellBuffer *shell, int ch);
void shell_tab_complete(ShellBuffer *shell, const char *cwd);
void shell_push_dir(ShellBuffer *shell, const char *path);
int shell_pop_dir(ShellBuffer *shell, char *out, size_t out_size);
void shell_print_jumplist(ShellBuffer *shell);
void shell_restore_ncurses(WINDOW *files_win, WINDOW *shell_win);
void shell_spawn(const char *cwd, WINDOW *files_win, WINDOW *shell_win);
void shell_run(ShellBuffer *shell, const char *cmd, const char *cwd);
void shell_render(ShellBuffer *shell, WINDOW *win, int height, int focused, const char *ghost);
int print_to_shell(ShellBuffer *shell, const char *text, int type);
void shell_clear_output(ShellBuffer *shell);
