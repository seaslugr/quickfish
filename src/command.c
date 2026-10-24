#include <dirent.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "command.h"
#include "fileinfo.h"
#include "fs.h"
#include "help.h"
#include "keymap.h"
#include "launch.h"
#include "movereg.h"
#include "ops.h"
#include "prompt.h"
#include "targets.h"
#include "util.h"

static int is_index(const char *s);
static int resolve(CmdCtx *ctx, const CmdArgs *args, int first, int last, const char *fallback, int **out);
static int need_one(TUI *tui, const char *cmd, int n);
static int ask(TUI *tui, const char *body, const char *warning, int danger);
static int describe_targets(FilesBuffer *files, const int *idx, int n, char *out, size_t out_size);
static int move_amount(CmdCtx *ctx, const CmdArgs *args);
static void visual_extend(TUI *tui);
static int parse_on_off(TUI *tui, const char *cmd, const CmdArgs *args, int current);
static int expand_home(TUI *tui, const char *arg, char *out, size_t out_size);
static void set_result_basename(TUI *tui, const char *path);
static int run_system(TUI *tui, const char *line);

static int cmd_down(CmdCtx *ctx, const CmdArgs *args);
static int cmd_up(CmdCtx *ctx, const CmdArgs *args);
static int cmd_page_down(CmdCtx *ctx, const CmdArgs *args);
static int cmd_page_up(CmdCtx *ctx, const CmdArgs *args);
static int cmd_parent(CmdCtx *ctx, const CmdArgs *args);
static int cmd_open(CmdCtx *ctx, const CmdArgs *args);
static int cmd_back(CmdCtx *ctx, const CmdArgs *args);
static int cmd_cd(CmdCtx *ctx, const CmdArgs *args);
static int cmd_goto(CmdCtx *ctx, const CmdArgs *args);
static int cmd_select(CmdCtx *ctx, const CmdArgs *args);
static int cmd_visual(CmdCtx *ctx, const CmdArgs *args);
static int cmd_rename(CmdCtx *ctx, const CmdArgs *args);
static int cmd_trash(CmdCtx *ctx, const CmdArgs *args);
static int cmd_delete(CmdCtx *ctx, const CmdArgs *args);
static int cmd_mark(CmdCtx *ctx, const CmdArgs *args);
static int cmd_clear_marks(CmdCtx *ctx, const CmdArgs *args);
static int cmd_paste(CmdCtx *ctx, const CmdArgs *args);
static int cmd_undo(CmdCtx *ctx, const CmdArgs *args);
static int cmd_redo(CmdCtx *ctx, const CmdArgs *args);
static int cmd_stat(CmdCtx *ctx, const CmdArgs *args);
static int cmd_quickshell(CmdCtx *ctx, const CmdArgs *args);
static int cmd_preview(CmdCtx *ctx, const CmdArgs *args);
static int cmd_scroll(CmdCtx *ctx, const CmdArgs *args);
static int cmd_focus(CmdCtx *ctx, const CmdArgs *args);
static int cmd_help(CmdCtx *ctx, const CmdArgs *args);
static int cmd_clear(CmdCtx *ctx, const CmdArgs *args);
static int cmd_suspend(CmdCtx *ctx, const CmdArgs *args);
static int cmd_quit(CmdCtx *ctx, const CmdArgs *args);
static int cmd_spawn(CmdCtx *ctx, const CmdArgs *args);
static int cmd_dbg_jumplist(CmdCtx *ctx, const CmdArgs *args);
static int cmd_dbg_move(CmdCtx *ctx, const CmdArgs *args);
static int cmd_dbg_undo(CmdCtx *ctx, const CmdArgs *args);
static int cmd_dbg_redo(CmdCtx *ctx, const CmdArgs *args);

static const Command commands[] = {
	{ CMD_CAT_NAV, cmd_down, "down", NULL, "[n]", "move the cursor down" },
	{ CMD_CAT_NAV, cmd_up, "up", NULL, "[n]", "move the cursor up" },
	{ CMD_CAT_NAV, cmd_page_down, "page-down", NULL, NULL, "move half a page down" },
	{ CMD_CAT_NAV, cmd_page_up, "page-up", NULL, NULL, "move half a page up" },
	{ CMD_CAT_NAV, cmd_parent, "parent", NULL, NULL, "go to the parent directory" },
	{ CMD_CAT_NAV, cmd_open, "open", NULL, "[target]", "enter a directory or open a file (a bare index does this too)" },
	{ CMD_CAT_NAV, cmd_back, "back", "..", NULL, "go back to the previous directory" },
	{ CMD_CAT_NAV, cmd_cd, "cd", NULL, "[path|index]", "change directory" },
	{ CMD_CAT_NAV, cmd_goto, "goto", NULL, "[path|name]", "go to a path, or the first name that matches" },
	{ CMD_CAT_FILES, cmd_select, "select", NULL, "[targets]", "toggle selection" },
	{ CMD_CAT_FILES, cmd_visual, "visual", NULL, "[on|off]", "sweep select mode" },
	{ CMD_CAT_FILES, cmd_rename, "rename", "r", "[target] [new name]", "rename, prompting when no new name is given" },
	{ CMD_CAT_FILES, cmd_trash, "trash", NULL, "[targets]", "move to the trash (undoable)" },
	{ CMD_CAT_FILES, cmd_delete, "delete", "rm", "[targets]", "permanently delete, always asks first" },
	{ CMD_CAT_FILES, cmd_mark, "mark", NULL, "[targets]", "toggle move marks" },
	{ CMD_CAT_FILES, cmd_clear_marks, "clear-marks", NULL, NULL, "clear every move mark" },
	{ CMD_CAT_FILES, cmd_paste, "paste", NULL, "[dir]", "move marked items here, or into dir" },
	{ CMD_CAT_FILES, cmd_undo, "undo", NULL, NULL, "undo the last file operation" },
	{ CMD_CAT_FILES, cmd_redo, "redo", NULL, NULL, "redo the last undone operation" },
	{ CMD_CAT_FILES, cmd_stat, "stat", "s", "[targets|path]", "show file details" },
	{ CMD_CAT_FILES, cmd_quickshell, "quickshell", NULL, NULL, "open the inline command prompt" },
	{ CMD_CAT_PANES, cmd_preview, "preview", NULL, "[on|off]", "toggle the preview pane" },
	{ CMD_CAT_PANES, cmd_scroll, "scroll", NULL, "[n|half|-half]", "scroll the preview" },
	{ CMD_CAT_PANES, cmd_focus, "focus", NULL, "files|shell|preview|next|prev", "move focus between panes" },
	{ CMD_CAT_PANES, cmd_help, "help", NULL, NULL, "list commands (F1 shows keybinds)" },
	{ CMD_CAT_PANES, cmd_clear, "clear", NULL, NULL, "clear the shell output" },
	{ CMD_CAT_PANES, cmd_suspend, "suspend", NULL, NULL, "suspend quickfish (fg resumes)" },
	{ CMD_CAT_SHELL, cmd_quit, "quit", "q", NULL, "quit" },
	{ CMD_CAT_SHELL, cmd_spawn, "spawn", NULL, NULL, "open $SHELL here (or press Enter on an empty line)" },
	{ CMD_CAT_SHELL, NULL, "swordfish", NULL, NULL, "it's my brother, no way!" },
	{ CMD_CAT_DEBUG, cmd_dbg_jumplist, "$jumplist", NULL, NULL, "print the directory jumplist" },
	{ CMD_CAT_DEBUG, cmd_dbg_move, "$move", NULL, NULL, "print the move register" },
	{ CMD_CAT_DEBUG, cmd_dbg_undo, "$undo", NULL, NULL, "print the undo stack" },
	{ CMD_CAT_DEBUG, cmd_dbg_redo, "$redo", NULL, NULL, "print the redo stack" },
};

const Command *cmd_find(const char *word) {
	size_t wlen;
	const char *a;
	const char *end;

	wlen = strlen(word);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		if (strcmp(commands[i].name, word) == 0) { return &commands[i]; }
		for (a = commands[i].aliases; a && *a; a = end) {
			while (*a == ' ') { a++; }
			end = strchr(a, ' ');
			if (!end) { end = a + strlen(a); }
			if ((size_t)(end - a) == wlen && strncmp(a, word, wlen) == 0) { return &commands[i]; }
		}
	}
	return NULL;
}

int cmd_count() {
	return (int)(sizeof(commands) / sizeof(commands[0]));
}

const Command *cmd_at(int i) {
	return &commands[i];
}

const char *cmd_category_name(CmdCategory category) {
	switch (category) {
	case CMD_CAT_NAV: return "Navigation";
	case CMD_CAT_FILES: return "Files";
	case CMD_CAT_PANES: return "Panes";
	case CMD_CAT_SHELL: return "Shell";
	case CMD_CAT_DEBUG: return "Debug";
	}
	return "";
}

static int is_index(const char *s) {
	if (*s == '\0') { return 0; }
	for (; *s; s++) {
		if (*s < '0' || *s > '9') { return 0; }
	}
	return 1;
}

/* Resolves a target range into a freshly allocated index array the caller frees.
   Returns the count, or -1 after an error was printed (nothing to free). */
static int resolve(CmdCtx *ctx, const CmdArgs *args, int first, int last, const char *fallback, int **out) {
	FilesBuffer *files;
	int n;

	files = &ctx->tui->files;
	*out = malloc(((size_t)files->entry_count + 1) * sizeof(int));
	if (!*out) {
		print_to_shell(&ctx->tui->shell, "out of memory\n", SHELL_MSG_ERROR);
		return -1;
	}
	n = targets_resolve(files, &ctx->tui->shell, args, first, last, fallback, *out);
	if (n < 0) {
		free(*out);
		*out = NULL;
	}
	return n;
}

static int need_one(TUI *tui, const char *cmd, int n) {
	char msg[128];

	if (n == 1) { return 1; }
	snprintf(msg, sizeof(msg), "%s: %s\n", cmd, n == 0 ? "nothing to act on" : "needs exactly one target");
	print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
	return 0;
}

static int ask(TUI *tui, const char *body, const char *warning, int danger) {
	return tui->confirm ? tui->confirm(tui, body, warning, danger) : 0;
}

/* Writes "'name' (file)" or "N items" for a confirmation prompt. Returns 1 if any target is a directory. */
static int describe_targets(FilesBuffer *files, const int *idx, int n, char *out, size_t out_size) {
	char name[128];
	int has_dir;

	has_dir = 0;
	for (int i = 0; i < n; i++) {
		if (files->entries[idx[i]].type == ENTRY_DIR) { has_dir = 1; }
	}
	if (n == 1) {
		str_fit_trimmed(name, sizeof(name), files->entries[idx[0]].name, 30);
		snprintf(out, out_size, "'%s' (%s)", name, has_dir ? "directory" : "file");
	} else {
		snprintf(out, out_size, "%d items", n);
	}
	return has_dir;
}

static int move_amount(CmdCtx *ctx, const CmdArgs *args) {
	if (ctx->count > 0) { return ctx->count; }
	if (args->argc > 0 && atoi(args->argv[0]) > 0) { return atoi(args->argv[0]); }
	return 1;
}

static void visual_extend(TUI *tui) {
	if (tui->visual_mode) { files_select_add(&tui->files, tui->files.selected); }
}

/* Reads an optional on/off argument. Returns the new state, or -1 on bad input. */
static int parse_on_off(TUI *tui, const char *cmd, const CmdArgs *args, int current) {
	char msg[128];

	if (args->argc == 0) { return !current; }
	if (strcmp(args->argv[0], "on") == 0) { return 1; }
	if (strcmp(args->argv[0], "off") == 0) { return 0; }
	snprintf(msg, sizeof(msg), "%s: expected on or off\n", cmd);
	print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
	return -1;
}

/* Expands ~ and ~user at the start of arg. */
static int expand_home(TUI *tui, const char *arg, char *out, size_t out_size) {
	char user[256];
	char msg[300];
	const char *rest;
	const char *home;
	struct passwd *pw;
	size_t ulen;

	rest = strchr(arg, '/');
	if (!rest) { rest = arg + strlen(arg); }
	ulen = (size_t)(rest - arg - 1);
	if (ulen == 0) {
		home = getenv("HOME");
		if (!home || home[0] == '\0') { home = "/"; }
	} else {
		if (ulen >= sizeof(user)) { ulen = sizeof(user) - 1; }
		memcpy(user, arg + 1, ulen);
		user[ulen] = '\0';
		pw = getpwnam(user);
		if (!pw) {
			snprintf(msg, sizeof(msg), "no such user: %s\n", user);
			print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
			return -1;
		}
		home = pw->pw_dir;
	}
	if (snprintf(out, out_size, "%s%s", home, rest) >= (int)out_size) {
		print_to_shell(&tui->shell, "path is too long\n", SHELL_MSG_ERROR);
		return -1;
	}
	return 0;
}

static void set_result_basename(TUI *tui, const char *path) {
	const char *slash;

	slash = strrchr(path, '/');
	str_copy(tui->shell.last_result, sizeof(tui->shell.last_result), (slash && slash[1]) ? slash + 1 : path);
}

static int cmd_down(CmdCtx *ctx, const CmdArgs *args) {
	if (ctx->tui->files.entry_count == 0) { return 0; }
	files_select_next_n(&ctx->tui->files, move_amount(ctx, args));
	visual_extend(ctx->tui);
	return 0;
}

static int cmd_up(CmdCtx *ctx, const CmdArgs *args) {
	if (ctx->tui->files.entry_count == 0) { return 0; }
	files_select_prev_n(&ctx->tui->files, move_amount(ctx, args));
	visual_extend(ctx->tui);
	return 0;
}

static int cmd_page_down(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	if (ctx->tui->files.entry_count == 0) { return 0; }
	files_select_next_n(&ctx->tui->files, ctx->tui->files_height > 2 ? ctx->tui->files_height / 2 : 1);
	visual_extend(ctx->tui);
	return 0;
}

static int cmd_page_up(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	if (ctx->tui->files.entry_count == 0) { return 0; }
	files_select_prev_n(&ctx->tui->files, ctx->tui->files_height > 2 ? ctx->tui->files_height / 2 : 1);
	visual_extend(ctx->tui);
	return 0;
}

static int cmd_parent(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	tui_change_dir(ctx->tui, "..");
	return 0;
}

static int cmd_open(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	char name[MAX_FILENAME];
	int *idx;
	int n;
	int is_dir;

	tui = ctx->tui;
	n = resolve(ctx, args, 0, args->argc, "cur", &idx);
	if (n < 0) { return -1; }
	if (!need_one(tui, "open", n)) {
		free(idx);
		return -1;
	}
	// Copy the name out, loading a directory can move the entries array
	str_copy(name, sizeof(name), tui->files.entries[idx[0]].name);
	is_dir = (tui->files.entries[idx[0]].type == ENTRY_DIR);
	str_copy(tui->shell.last_result, sizeof(tui->shell.last_result), name);
	if (is_dir) {
		tui_change_dir(tui, name);
	} else {
		tui->files.selected = idx[0];
		files_open_selected(&tui->files, &tui->shell, tui->files_win, tui->shell_win);
		// xdg-open can write to the terminal, so redraw everything
		needs_resize = 1;
	}
	free(idx);
	return 0;
}

static int cmd_back(CmdCtx *ctx, const CmdArgs *args) {
	char prev[PATH_MAX];

	(void)args;
	if (!shell_pop_dir(&ctx->tui->shell, prev, sizeof(prev))) {
		print_to_shell(&ctx->tui->shell, "back: no previous directory\n", SHELL_MSG_WARN);
		return -1;
	}
	files_load_directory(&ctx->tui->files, prev);
	set_result_basename(ctx->tui, prev);
	return 0;
}

static int cmd_cd(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	char target[PATH_MAX];
	char msg[PATH_MAX + 64];
	const char *arg;
	struct stat st;
	DIR *d;
	int *idx;
	int n;

	tui = ctx->tui;
	if (args->argc > 1) {
		print_to_shell(&tui->shell, "cd: too many arguments (quote paths with spaces)\n", SHELL_MSG_ERROR);
		return -1;
	}
	if (args->argc == 0) {
		if (expand_home(tui, "~", target, sizeof(target)) != 0) { return -1; }
	} else {
		arg = args->argv[0];
		if (!args->quoted[0] && strcmp(arg, "-") == 0) { return cmd_back(ctx, args); }
		if (!args->quoted[0] && is_index(arg)) {
			n = resolve(ctx, args, 0, 1, NULL, &idx);
			if (n < 0) { return -1; }
			if (tui->files.entries[idx[0]].type != ENTRY_DIR) {
				snprintf(msg, sizeof(msg), "cd: '%s' is not a directory\n", tui->files.entries[idx[0]].name);
				print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
				free(idx);
				return -1;
			}
			path_join(target, sizeof(target), tui->files.cwd, tui->files.entries[idx[0]].name);
			free(idx);
		} else if (!args->quoted[0] && arg[0] == '~') {
			if (expand_home(tui, arg, target, sizeof(target)) != 0) { return -1; }
		} else if (arg[0] == '/') {
			str_copy(target, sizeof(target), arg);
		} else {
			path_join(target, sizeof(target), tui->files.cwd, arg);
		}
	}

	if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) {
		snprintf(msg, sizeof(msg), "cd: no such directory: %s\n", target);
		print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
		return -1;
	}
	d = opendir(target);
	if (!d) {
		snprintf(msg, sizeof(msg), "cd: cannot open %s\n", target);
		print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
		return -1;
	}
	closedir(d);

	tui_load_dir(tui, target);
	set_result_basename(tui, tui->files.cwd);
	return 0;
}

static int cmd_goto(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	char msg[64];

	tui = ctx->tui;
	if (args->argc > 1) {
		print_to_shell(&tui->shell, "goto: takes one path (quote paths with spaces)\n", SHELL_MSG_ERROR);
		return -1;
	}
	if (args->argc == 1) {
		prompt_goto(tui, args->argv[0]);
		return 0;
	}
	if (ctx->count > 0) {
		for (int i = 0; i < tui->files.entry_count; i++) {
			if (tui->files.entries[i].index == ctx->count) {
				tui->files.selected = i;
				return 0;
			}
		}
		snprintf(msg, sizeof(msg), "goto: no entry %d\n", ctx->count);
		print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
		return -1;
	}
	prompt_goto_begin(tui);
	return 0;
}

static int cmd_select(CmdCtx *ctx, const CmdArgs *args) {
	int *idx;
	int n;

	n = resolve(ctx, args, 0, args->argc, "cur", &idx);
	if (n < 0) { return -1; }
	for (int i = 0; i < n; i++) {
		files_toggle_select(&ctx->tui->files, idx[i]);
	}
	free(idx);
	return 0;
}

static int cmd_visual(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	int on;

	tui = ctx->tui;
	on = parse_on_off(tui, "visual", args, tui->visual_mode);
	if (on < 0) { return -1; }
	if (on && tui->files.entry_count == 0) { return 0; }
	tui->visual_mode = on;
	visual_extend(tui);
	return 0;
}

static int cmd_rename(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	int *idx;
	int n;

	tui = ctx->tui;
	if (args->argc > 2) {
		print_to_shell(&tui->shell, "rename: quote names that contain spaces\n", SHELL_MSG_ERROR);
		return -1;
	}
	n = resolve(ctx, args, 0, args->argc > 0 ? 1 : 0, "cur", &idx);
	if (n < 0) { return -1; }
	if (!need_one(tui, "rename", n)) {
		free(idx);
		return -1;
	}
	if (args->argc < 2) {
		prompt_rename_begin(tui, idx[0]);
	} else if (files_rename_entry(&tui->files, &tui->shell, idx[0], args->argv[1]) == 0) {
		tui_refresh_files(tui);
		tui_select_name(tui, args->argv[1]);
	}
	free(idx);
	return 0;
}

static int cmd_trash(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	char what[192];
	char body[256];
	char msg[64];
	int *idx;
	int n;
	int done;

	tui = ctx->tui;
	n = resolve(ctx, args, 0, args->argc, TARGETS_DEFAULT, &idx);
	if (n < 0) { return -1; }
	if (n == 0) {
		free(idx);
		return 0;
	}
	if (ctx->from_key) {
		describe_targets(&tui->files, idx, n, what, sizeof(what));
		snprintf(body, sizeof(body), "Trash %s?", what);
		if (!ask(tui, body, NULL, 0)) {
			free(idx);
			return 0;
		}
	}
	done = files_trash_entries(&tui->files, &tui->shell, idx, n);
	free(idx);
	tui_refresh_files(tui);
	if (done > 0) {
		snprintf(msg, sizeof(msg), "Trashed %d item(s), u to undo.\n", done);
		print_to_shell(&tui->shell, msg, SHELL_MSG_NORMAL);
	}
	return 0;
}

static int cmd_delete(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	char what[192];
	char body[256];
	char msg[64];
	int *idx;
	int n;
	int done;
	int has_dir;

	tui = ctx->tui;
	n = resolve(ctx, args, 0, args->argc, TARGETS_DEFAULT, &idx);
	if (n < 0) { return -1; }
	if (n == 0) {
		free(idx);
		return 0;
	}
	has_dir = describe_targets(&tui->files, idx, n, what, sizeof(what));
	snprintf(body, sizeof(body), "Permanently delete %s?", what);
	if (!ask(tui, body, has_dir ? "Recursive! This cannot be undone." : "This cannot be undone.", 1)) {
		free(idx);
		print_to_shell(&tui->shell, "delete: cancelled\n", SHELL_MSG_NORMAL);
		return 0;
	}
	done = files_delete_entries(&tui->files, &tui->shell, idx, n);
	free(idx);
	tui_refresh_files(tui);
	if (done > 0) {
		snprintf(msg, sizeof(msg), "Deleted %d item(s).\n", done);
		print_to_shell(&tui->shell, msg, SHELL_MSG_NORMAL);
	}
	return 0;
}

static int cmd_mark(CmdCtx *ctx, const CmdArgs *args) {
	int *idx;
	int n;

	n = resolve(ctx, args, 0, args->argc, TARGETS_DEFAULT, &idx);
	if (n < 0) { return -1; }
	if (n > 0) { files_move_mark_entries(&ctx->tui->files, &ctx->tui->shell, idx, n); }
	free(idx);
	return 0;
}

static int cmd_clear_marks(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	if (ctx->tui->files.move_reg.count > 0 || !ctx->from_key) {
		files_move_clear(&ctx->tui->files, &ctx->tui->shell);
	}
	return 0;
}

static int cmd_paste(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	char dest[PATH_MAX];
	char dest_fit[192];
	char body[256];
	char msg[MAX_FILENAME + 64];
	int *idx;
	int n;

	tui = ctx->tui;
	if (tui->files.move_reg.count == 0) {
		print_to_shell(&tui->shell, "paste: nothing is marked for move\n", SHELL_MSG_WARN);
		return -1;
	}
	if (args->argc > 1) {
		print_to_shell(&tui->shell, "paste: takes one destination directory\n", SHELL_MSG_ERROR);
		return -1;
	}
	if (args->argc == 0) {
		str_copy(dest, sizeof(dest), tui->files.cwd);
	} else {
		n = resolve(ctx, args, 0, 1, NULL, &idx);
		if (n < 0) { return -1; }
		if (tui->files.entries[idx[0]].type != ENTRY_DIR) {
			snprintf(msg, sizeof(msg), "paste: '%s' is not a directory\n", tui->files.entries[idx[0]].name);
			print_to_shell(&tui->shell, msg, SHELL_MSG_ERROR);
			free(idx);
			return -1;
		}
		path_join(dest, sizeof(dest), tui->files.cwd, tui->files.entries[idx[0]].name);
		free(idx);
	}
	if (ctx->from_key) {
		str_fit_trimmed(dest_fit, sizeof(dest_fit), dest, 40);
		snprintf(body, sizeof(body), "Move %d item(s) to '%s'?", tui->files.move_reg.count, dest_fit);
		if (!ask(tui, body, NULL, 0)) { return 0; }
	}
	files_move_paste(&tui->files, &tui->shell, dest);
	tui_refresh_files(tui);
	return 0;
}

static int cmd_undo(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	files_undo(&ctx->tui->files, &ctx->tui->shell);
	tui_refresh_files(ctx->tui);
	return 0;
}

static int cmd_redo(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	files_redo(&ctx->tui->files, &ctx->tui->shell);
	tui_refresh_files(ctx->tui);
	return 0;
}

static int cmd_stat(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	int *idx;
	int n;

	tui = ctx->tui;
	if (args->argc == 0) {
		n = resolve(ctx, args, 0, 0, "cur", &idx);
		if (n < 0) { return -1; }
		if (n == 1) { files_stat_entry(&tui->files, &tui->shell, idx[0]); }
		free(idx);
		return 0;
	}
	for (int i = 0; i < args->argc; i++) {
		if (args->argv[i][0] == '~' || strchr(args->argv[i], '/')) {
			files_cmd_stat(&tui->files, &tui->shell, args->argv[i]);
			continue;
		}
		n = resolve(ctx, args, i, i + 1, NULL, &idx);
		if (n < 0) { return -1; }
		for (int j = 0; j < n; j++) {
			files_stat_entry(&tui->files, &tui->shell, idx[j]);
		}
		free(idx);
	}
	return 0;
}

static int cmd_quickshell(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	prompt_quickshell_begin(ctx->tui);
	return 0;
}

static int cmd_preview(CmdCtx *ctx, const CmdArgs *args) {
	int on;

	on = parse_on_off(ctx->tui, "preview", args, ctx->tui->preview_mode);
	if (on < 0) { return -1; }
	tui_set_preview(ctx->tui, on);
	return 0;
}

static int cmd_scroll(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	int half;
	int n;

	tui = ctx->tui;
	half = tui->files_height > 2 ? tui->files_height / 2 : 1;
	n = 1;
	if (args->argc > 0) {
		if (strcmp(args->argv[0], "half") == 0) {
			n = half;
		} else if (strcmp(args->argv[0], "-half") == 0) {
			n = -half;
		} else {
			n = atoi(args->argv[0]);
		}
	}
	tui->preview_scroll += n;
	if (tui->preview_scroll < 0) { tui->preview_scroll = 0; }
	return 0;
}

static int cmd_focus(CmdCtx *ctx, const CmdArgs *args) {
	TUI *tui;
	const char *where;

	tui = ctx->tui;
	where = args->argc > 0 ? args->argv[0] : "";
	if (strcmp(where, "files") == 0) {
		tui_focus(tui, BUFFER_FILES);
	} else if (strcmp(where, "shell") == 0) {
		tui_focus(tui, BUFFER_SHELL);
	} else if (strcmp(where, "preview") == 0) {
		if (tui->preview_mode) { tui_focus(tui, BUFFER_PREVIEW); }
	} else if (strcmp(where, "next") == 0) {
		if (tui->active_buffer == BUFFER_FILES && tui->preview_mode) {
			tui_focus(tui, BUFFER_PREVIEW);
		} else if (tui->active_buffer == BUFFER_PREVIEW) {
			tui_focus(tui, BUFFER_SHELL);
		}
	} else if (strcmp(where, "prev") == 0) {
		if (tui->active_buffer == BUFFER_PREVIEW || tui->active_buffer == BUFFER_SHELL) {
			tui_focus(tui, BUFFER_FILES);
		}
	} else {
		print_to_shell(&tui->shell, "focus: expected files, shell, preview, next or prev\n", SHELL_MSG_ERROR);
		return -1;
	}
	return 0;
}

/* From a key this toggles the overlay. Typed, it prints every command with its
   aliases and keys, which is the full reference the overlay has no room for. */
static int cmd_help(CmdCtx *ctx, const CmdArgs *args) {
	char out[SHELL_OUTPUT_MAX];
	char keys[64];
	char key[16];
	char word[64];
	const Command *c;
	const Keybind *kb;
	size_t wlen;
	int pos;
	int n;
	int cat;

	(void)args;
	if (ctx->from_key) {
		help_toggle(ctx->tui);
		return 0;
	}

	pos = snprintf(out, sizeof(out), "Commands (prefix with ! to run the system command instead):\n");
	cat = -1;
	for (int i = 0; i < cmd_count() && pos < (int)sizeof(out); i++) {
		c = cmd_at(i);
		if ((int)c->category != cat) {
			cat = (int)c->category;
			pos += snprintf(out + pos, sizeof(out) - pos, "%s\n", cmd_category_name(c->category));
			if (pos >= (int)sizeof(out)) { break; }
		}
		keys[0] = '\0';
		for (int k = 0; k < keymap_count(); k++) {
			kb = keymap_at(k);
			if (!kb->help) { continue; }
			wlen = strcspn(kb->line, " ");
			if (wlen >= sizeof(word)) { continue; }
			memcpy(word, kb->line, wlen);
			word[wlen] = '\0';
			if (strcmp(word, c->name) != 0) { continue; }
			keymap_key_name(kb->key, key, sizeof(key));
			if (strlen(keys) + strlen(key) + 2 < sizeof(keys)) {
				if (keys[0]) { strcat(keys, " "); }
				strcat(keys, key);
			}
		}
		n = snprintf(out + pos, sizeof(out) - pos, "  %-12s %-4s %-22s %-10s %s\n",
		             c->name, c->aliases ? c->aliases : "", c->usage ? c->usage : "", keys, c->help);
		if (n < 0) { break; }
		pos += n;
	}
	if (pos >= (int)sizeof(out)) { pos = (int)sizeof(out) - 1; }
	out[pos] = '\0';
	print_to_shell(&ctx->tui->shell, out, SHELL_MSG_NORMAL);
	print_to_shell(&ctx->tui->shell, "Targets: an index, a name, sel, cur, all, or a|b for the first that isn't empty.\n", SHELL_MSG_NORMAL);
	return 0;
}

static int cmd_clear(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	shell_clear_output(&ctx->tui->shell);
	ctx->tui->shell.last_cmd[0] = '\0';
	ctx->tui->shell.last_result[0] = '\0';
	return 0;
}

static int cmd_suspend(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	tui_suspend(ctx->tui);
	return 0;
}

static int cmd_quit(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	ctx->tui->running = 0;
	return 0;
}

static int cmd_spawn(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	shell_spawn(ctx->tui->files.cwd, ctx->tui->files_win, ctx->tui->shell_win);
	tui_refresh_files(ctx->tui);
	return 0;
}

static int cmd_dbg_jumplist(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	shell_print_jumplist(&ctx->tui->shell);
	return 0;
}

static int cmd_dbg_move(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	files_debug_print_move_register(&ctx->tui->files, &ctx->tui->shell);
	return 0;
}

static int cmd_dbg_undo(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	files_debug_print_undo_stack(&ctx->tui->files, &ctx->tui->shell);
	return 0;
}

static int cmd_dbg_redo(CmdCtx *ctx, const CmdArgs *args) {
	(void)args;
	files_debug_print_redo_stack(&ctx->tui->files, &ctx->tui->shell);
	return 0;
}

/* Runs line in the system shell after expanding sel and cur to quoted names. */
static int run_system(TUI *tui, const char *line) {
	char sel[SHELL_MAX_INPUT * 3];
	char cur[MAX_FILENAME * 4 + 3];
	char expanded[CMD_LINE_MAX];
	const char *sel_ok;
	const char *cur_ok;

	sel_ok = NULL;
	cur_ok = NULL;
	if (tui->files.sel_count > 0) {
		if (files_build_sel_args(&tui->files, sel, sizeof(sel)) < 0) {
			print_to_shell(&tui->shell, "selection is too large to expand into the command line\n", SHELL_MSG_ERROR);
			return -1;
		}
		sel_ok = sel;
	}
	if (tui->files.entry_count > 0 && str_shell_quote(cur, sizeof(cur), 0, tui->files.entries[tui->files.selected].name) >= 0) {
		cur_ok = cur;
	}
	// Never run a command that was cut short, it could act on the wrong files
	if (cmdline_expand(line, sel_ok, cur_ok, expanded, sizeof(expanded)) < 0) {
		print_to_shell(&tui->shell, "can't expand sel/cur: nothing is selected or the command is too long (\\sel keeps the word)\n", SHELL_MSG_ERROR);
		return -1;
	}
	shell_run(&tui->shell, expanded, tui->files.cwd);
	tui_refresh_files(tui);
	return 0;
}

/* Runs one command line. Unknown words and lines starting with ! go to the
   system shell, and a bare index opens that entry. */
int cmd_run(TUI *tui, const char *line, int count, int from_key) {
	CmdArgs args;
	CmdCtx ctx;
	const Command *cmd;

	while (*line == ' ' || *line == '\t') { line++; }
	if (*line == '!') { return run_system(tui, line + 1); }
	if (cmdline_split(line, &args) < 0) {
		print_to_shell(&tui->shell, "unterminated quote, or too many arguments\n", SHELL_MSG_ERROR);
		return -1;
	}
	if (args.argc == 0) { return 0; }

	ctx = (CmdCtx){ .count = count, .from_key = from_key, .tui = tui };
	if (!args.quoted[0] && is_index(args.argv[0])) { return cmd_open(&ctx, &args); }

	cmd = args.quoted[0] ? NULL : cmd_find(args.argv[0]);
	if (!cmd || !cmd->fn) { return run_system(tui, line); }
	cmdline_shift(&args);
	return cmd->fn(&ctx, &args);
}

/* Entry point for typed lines from the shell pane and quickshell: records
   history and the result row, then runs the line. */
void cmd_submit(TUI *tui, const char *line, int from_shell_pane) {
	char cmd[SHELL_MAX_INPUT];
	CmdArgs args;
	const Command *c;
	int len;

	while (*line == ' ' || *line == '\t') { line++; }
	str_copy(cmd, sizeof(cmd), line);
	len = (int)strlen(cmd);
	while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) { cmd[--len] = '\0'; }

	if (len == 0) {
		if (from_shell_pane) { cmd_spawn(&(CmdCtx){ .tui = tui }, NULL); }
		return;
	}

	str_copy(tui->shell.last_cmd, sizeof(tui->shell.last_cmd), cmd);
	tui->shell.last_result[0] = '\0';
	shell_add_history(&tui->shell, cmd);

	if (cmd[0] != '!' && cmdline_split(cmd, &args) > 0 && !args.quoted[0]) {
		c = cmd_find(args.argv[0]);
		if (c && c->fn && strcmp(c->name, args.argv[0]) != 0) {
			str_copy(tui->shell.last_result, sizeof(tui->shell.last_result), c->name);
		}
	}
	cmd_run(tui, cmd, 0, 0);
}

void compute_ghost(const char *input, int input_pos, const char *cwd,
                   const FilesBuffer *files, const ShellBuffer *shell,
                   char *ghost_out, int ghost_max) {
	int tok_start;
	int tok_len;
	char token[SHELL_MAX_INPUT];
	int name_len;
	char dir_part[PATH_MAX];
	char name_part[MAX_FILENAME];
	const char *last_slash;
	const char *slash;
	const char *prev;
	char search_dir[PATH_MAX];
	char completed[MAX_FILENAME];
	const Command *c;
	int count;
	int comp_len;
	int idx;

	ghost_out[0] = '\0';
	if (input_pos <= 0 || !cwd) { return; }

	tok_start = input_pos;
	while (tok_start > 0 && input[tok_start - 1] != ' ') { tok_start--; }
	if (tok_start != 0) { return; }

	tok_len = input_pos;
	memcpy(token, input, (size_t)tok_len);
	token[tok_len] = '\0';

	if (!strchr(token, '/')) {
		if (strcmp(token, "..") == 0) {
			if (shell && shell->dir_jumplist_count > 0) {
				prev = shell->dir_jumplist[shell->dir_jumplist_count - 1];
				slash = strrchr(prev, '/');
				snprintf(ghost_out, ghost_max, " > %s", (slash && slash[1] != '\0') ? slash + 1 : prev);
			}
			return;
		}
		if (is_index(token) && files) {
			idx = atoi(token);
			for (int i = 0; i < files->entry_count; i++) {
				if (files->entries[i].index == idx) {
					snprintf(ghost_out, ghost_max, " > %s", files->entries[i].name);
					return;
				}
			}
			return;
		}
		c = cmd_find(token);
		if (!c) { return; }
		if (strcmp(c->name, token) != 0) {
			snprintf(ghost_out, ghost_max, " > %s%s%s", c->name, c->usage ? " " : "", c->usage ? c->usage : "");
		} else if (c->usage) {
			snprintf(ghost_out, ghost_max, " > %s", c->usage);
		} else if (!c->fn) {
			snprintf(ghost_out, ghost_max, " > %s", c->help);
		}
		return;
	}

	last_slash = strrchr(token, '/');
	memcpy(dir_part, token, (size_t)(last_slash - token));
	dir_part[last_slash - token] = '\0';
	str_copy(name_part, sizeof(name_part), last_slash + 1);
	name_len = (int)strlen(name_part);
	if (name_len <= 0) { return; }

	if (dir_part[0] == '/') {
		str_copy(search_dir, sizeof(search_dir), dir_part);
	} else {
		path_join(search_dir, PATH_MAX, cwd, dir_part);
	}

	count = complete_in_dir(search_dir, name_part, completed, sizeof(completed));
	if (count == 0 || completed[0] == '\0') { return; }

	comp_len = (int)strlen(completed);
	if (comp_len <= name_len) { return; }

	str_copy(ghost_out, (size_t)ghost_max, completed + name_len);
}
