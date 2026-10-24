/**
 * @file tui.h
 * @brief Public interface for the Quickfish TUI layer.
 *
 * Declares the TUI struct and all functions for initialisation, rendering,
 * input handling, and cleanup. Include this wherever the TUI needs to be
 * driven from outside tui.c.
 */
#pragma once

#include <ncurses.h>
#include <panel.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "shell.h"

#define GOTO_BUF_MAX 512 ///< Maximum length of the goto input buffer including null terminator
#define RENAME_BUF_MAX 512 ///< Maximum length of the rename input buffer including null terminator
#define FILE_CMD_BUF_MAX 32 ///< Maximum length of the file command preview buffer including null terminator

/**
 * @brief Identifies which pane currently has focus.
 */
typedef enum {
	BUFFER_FILES, ///< File list pane
	BUFFER_SHELL, ///< Shell pane
	BUFFER_PREVIEW, ///< Preview pane
	BUFFER_INFO, ///< F1 help overlay
} BufferType;

/**
 * @brief Top-level state for the quickfish TUI.
 *
 * Owns all ncurses windows, overlay buffers, and references to the files
 * and shell subsystems. Allocated and initialised by tui_init().
 */
typedef struct TUI {
	char file_cmd_buf[FILE_CMD_BUF_MAX];
	char goto_buf[GOTO_BUF_MAX];
	char rename_buf[RENAME_BUF_MAX];
	char quickshell_buf[SHELL_MAX_INPUT];
	int info_mode;
	int max_x, max_y;
	int files_height, shell_height, divider_y;
	int running;
	int dirty;
	int goto_mode;
	int goto_len;
	int rename_mode;
	int rename_len;
	int rename_cursor;
	int quickshell_mode;
	int quickshell_len;
	int quickshell_cursor;
	int preview_mode;
	int preview_scroll;
	int preview_last_selected;
	int preview_saved_col_offset;
	int file_cmd_len;
	int visual_mode;
	int rename_target;
	BufferType active_buffer;
	BufferType prev_buffer;
	WINDOW *files_win;
	WINDOW *shell_win;
	WINDOW *info_win;
	PANEL *info_panel;
	int (*confirm)(struct TUI *tui, const char *body, const char *warning, int danger); ///< Asks yes/no, NULL always answers no
	FilesBuffer files;
	ShellBuffer shell;
} TUI;

TUI *tui_init(const char *start_dir);
void tui_cleanup(TUI *tui);
void tui_resize_handler(TUI *tui);
void tui_render(TUI *tui);
void tui_handle_input(TUI *tui, int ch);
void tui_handle_files_input(TUI *tui, int ch);
void tui_handle_preview_input(TUI *tui, int ch);
void tui_handle_shell_input(TUI *tui, int ch);

void tui_load_dir(TUI *tui, const char *path);
void tui_change_dir(TUI *tui, const char *dirname);
void tui_refresh_files(TUI *tui);
void tui_select_name(TUI *tui, const char *name);
void tui_set_preview(TUI *tui, int on);
void tui_focus(TUI *tui, BufferType buffer);
void tui_suspend(TUI *tui);

extern volatile int needs_resize;
