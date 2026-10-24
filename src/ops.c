#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs.h"
#include "ops.h"
#include "shell.h"
#include "util.h"

static int undo_op_describe(const UndoOp *op, char *out, size_t out_size);
static void files_print_op_stack(const char *title, const UndoOp *stack, int top,
                                 struct ShellBuffer *shell);
static int do_trash_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx, int batch);
static int do_delete_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx, int batch);

void push_undo(FilesBuffer *files, UndoOp op) {
	if (files->undo_top < UNDO_MAX) {
		files->undo_stack[files->undo_top++] = op;
	} else {
		memmove(&files->undo_stack[0], &files->undo_stack[1],
		        (UNDO_MAX - 1) * sizeof(UndoOp));
		files->undo_stack[UNDO_MAX - 1] = op;
	}
	files->redo_top = 0;
}

static int undo_op_describe(const UndoOp *op, char *out, size_t out_size) {
	char src[PATH_MAX];
	char dst[PATH_MAX];

	switch (op->type) {
	case OP_RENAME:
		path_join(src, sizeof(src), op->cwd, op->old_name);
		path_join(dst, sizeof(dst), op->cwd, op->new_name);
		return snprintf(out, out_size, "rename %s -> %s", src, dst);
	case OP_TRASH:
		path_join(src, sizeof(src), op->cwd, op->old_name);
		return snprintf(out, out_size, "trash %s -> %s", src,
		                op->trash_path[0] ? op->trash_path : "(unknown)");
	case OP_DELETE_PERM:
		path_join(src, sizeof(src), op->cwd, op->old_name);
		return snprintf(out, out_size, "delete %s", src);
	case OP_MOVE:
		path_join(dst, sizeof(dst), op->cwd, op->old_name);
		return snprintf(out, out_size, "move %s -> %s", op->trash_path, dst);
	}

	return snprintf(out, out_size, "unknown");
}

static void files_print_op_stack(const char *title, const UndoOp *stack, int top,
                                 struct ShellBuffer *shell) {
	char display[SHELL_OUTPUT_MAX];
	char line[PATH_MAX * 2 + 64];
	int pos;
	int n;
	int wrote_all;
	int suffix_len;
	const char *suffix;

	if (top <= 0) {
		snprintf(display, sizeof(display), "[%s] (empty)\n", title);
		print_to_shell(shell, display, SHELL_MSG_NORMAL);
		return;
	}

	suffix = "... truncated ...\n";
	suffix_len = (int)strlen(suffix);
	pos = snprintf(display, sizeof(display), "[%s]\n", title);
	wrote_all = 1;

	for (int i = top - 1; i >= 0 && pos < (int)sizeof(display) - 1; i--) {
		undo_op_describe(&stack[i], line, sizeof(line));
		n = snprintf(display + pos, sizeof(display) - pos, "%2d. %s\n",
		             top - i, line);
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

void files_debug_print_undo_stack(FilesBuffer *files, struct ShellBuffer *shell) {
	files_print_op_stack("undo stack", files->undo_stack, files->undo_top, shell);
}

void files_debug_print_redo_stack(FilesBuffer *files, struct ShellBuffer *shell) {
	files_print_op_stack("redo stack", files->redo_stack, files->redo_top, shell);
}

static int do_trash_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx, int batch) {
	char entry_path[PATH_MAX];
	char trash_dir[PATH_MAX];
	char trash_dest[PATH_MAX];
	char msg[MAX_FILENAME + 128];
	const char *home;
	const char *name;
	long now;
	UndoOp op;

	if (idx < 0 || idx >= files->entry_count) { return -1; }

	name = files->entries[idx].name;
	path_join(entry_path, sizeof(entry_path), files->cwd, name);

	home = getenv("HOME");
	if (!home || home[0] == '\0') { home = "/tmp"; }
	snprintf(trash_dir, sizeof(trash_dir), "%s/.local/share/Trash/files", home);
	if (mkdir_p(trash_dir) != 0) {
		snprintf(msg, sizeof(msg), "Trash failed: cannot create trash directory (%s).\n", strerror(errno));
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return -1;
	}

	path_join(trash_dest, sizeof(trash_dest), trash_dir, name);
	now = (long)time(NULL);
	for (int n = 1; path_exists(trash_dest); n++) {
		if (snprintf(trash_dest, sizeof(trash_dest), "%s/%.200s.%ld.%d", trash_dir, name, now, n) >= (int)sizeof(trash_dest)) {
			print_to_shell(shell, "Trash failed: trash path is too long.\n", SHELL_MSG_ERROR);
			return -1;
		}
	}

	if (path_move(entry_path, trash_dest) != 0) {
		snprintf(msg, sizeof(msg), "Trash failed for '%s': %s\n", name, strerror(errno));
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return -1;
	}

	op = (UndoOp){ .type = OP_TRASH, .batch_id = batch };
	str_copy(op.cwd, sizeof(op.cwd), files->cwd);
	str_copy(op.old_name, sizeof(op.old_name), name);
	str_copy(op.trash_path, sizeof(op.trash_path), trash_dest);
	push_undo(files, op);
	return 0;
}

static int do_delete_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx, int batch) {
	char entry_path[PATH_MAX];
	char msg[MAX_FILENAME + 128];
	const char *name;
	UndoOp op;

	if (idx < 0 || idx >= files->entry_count) { return -1; }

	name = files->entries[idx].name;
	path_join(entry_path, sizeof(entry_path), files->cwd, name);

	if (path_remove_tree(entry_path) != 0) {
		snprintf(msg, sizeof(msg), "Delete failed for '%s': %s\n", name, strerror(errno));
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return -1;
	}

	op = (UndoOp){ .type = OP_DELETE_PERM, .batch_id = batch };
	str_copy(op.cwd, sizeof(op.cwd), files->cwd);
	str_copy(op.old_name, sizeof(op.old_name), name);
	push_undo(files, op);
	return 0;
}

/* Trashes the given entries as one undo batch. Returns how many succeeded. */
int files_trash_entries(FilesBuffer *files, struct ShellBuffer *shell, const int *idx, int n) {
	int done;

	done = 0;
	files->batch_seq++;
	for (int i = 0; i < n; i++) {
		if (do_trash_entry(files, shell, idx[i], files->batch_seq) == 0) { done++; }
	}
	return done;
}

/* Permanently deletes the given entries. Returns how many succeeded. */
int files_delete_entries(FilesBuffer *files, struct ShellBuffer *shell, const int *idx, int n) {
	int done;

	done = 0;
	files->batch_seq++;
	for (int i = 0; i < n; i++) {
		if (do_delete_entry(files, shell, idx[i], files->batch_seq) == 0) { done++; }
	}
	return done;
}

/* Renames one entry in place, refusing names that would leave the directory or replace something. */
int files_rename_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx, const char *new_name) {
	char old_path[PATH_MAX];
	char new_path[PATH_MAX];
	char msg[MAX_FILENAME * 2 + 128];
	size_t len;
	UndoOp op;

	if (idx < 0 || idx >= files->entry_count) { return -1; }
	len = strlen(new_name);
	if (len == 0 || strchr(new_name, '/') || strcmp(new_name, ".") == 0 || strcmp(new_name, "..") == 0) {
		print_to_shell(shell, "rename: invalid name.\n", SHELL_MSG_ERROR);
		return -1;
	}
	if (len >= MAX_FILENAME) {
		print_to_shell(shell, "rename: name is too long.\n", SHELL_MSG_ERROR);
		return -1;
	}
	if (strcmp(files->entries[idx].name, new_name) == 0) { return 0; }

	path_join(old_path, sizeof(old_path), files->cwd, files->entries[idx].name);
	path_join(new_path, sizeof(new_path), files->cwd, new_name);
	if (path_move(old_path, new_path) != 0) {
		snprintf(msg, sizeof(msg), "rename failed for '%s': %s\n", new_name, strerror(errno));
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return -1;
	}

	op = (UndoOp){ .type = OP_RENAME };
	str_copy(op.cwd, sizeof(op.cwd), files->cwd);
	str_copy(op.old_name, sizeof(op.old_name), files->entries[idx].name);
	memcpy(op.new_name, new_name, len + 1);
	push_undo(files, op);
	return 0;
}

void files_undo(FilesBuffer *files, struct ShellBuffer *shell) {
	UndoOp op;
	char old_path[PATH_MAX];
	char new_path[PATH_MAX];
	char msg[MAX_FILENAME * 2 + 128];
	int bid;

	if (files->undo_top == 0) {
		print_to_shell(shell, "Nothing to undo.\n", SHELL_MSG_WARN);
		return;
	}

	bid = files->undo_stack[files->undo_top - 1].batch_id;

	do {
		op = files->undo_stack[--files->undo_top];
		if (files->redo_top < UNDO_MAX) {
			files->redo_stack[files->redo_top++] = op;
		}

		switch (op.type) {
		case OP_RENAME:
			if (strcmp(op.cwd, "/") == 0) {
				snprintf(old_path, PATH_MAX, "/%s", op.new_name);
				snprintf(new_path, PATH_MAX, "/%s", op.old_name);
			} else {
				path_join(old_path, PATH_MAX, op.cwd, op.new_name);
				path_join(new_path, PATH_MAX, op.cwd, op.old_name);
			}
			if (path_move(old_path, new_path) == 0) {
				snprintf(msg, sizeof(msg), "Undo rename: '%.255s' -> '%.255s'\n", op.new_name, op.old_name);
				print_to_shell(shell, msg, SHELL_MSG_NORMAL);
			} else {
				snprintf(msg, sizeof(msg), "Undo rename failed for '%.255s': %s\n", op.new_name, strerror(errno));
				print_to_shell(shell, msg, SHELL_MSG_ERROR);
			}
			break;
		case OP_TRASH:
			if (op.trash_path[0] == '\0') {
				print_to_shell(shell, "Undo trash: path unknown.\n", SHELL_MSG_WARN);
				break;
			}
			if (strcmp(op.cwd, "/") == 0) {
				snprintf(old_path, PATH_MAX, "/%s", op.old_name);
			} else {
				path_join(old_path, PATH_MAX, op.cwd, op.old_name);
			}
			if (path_move(op.trash_path, old_path) == 0) {
				snprintf(msg, sizeof(msg), "Restored '%s' from trash.\n", op.old_name);
				print_to_shell(shell, msg, SHELL_MSG_NORMAL);
			} else {
				snprintf(msg, sizeof(msg), "Restore failed for '%s': %s\n", op.old_name, strerror(errno));
				print_to_shell(shell, msg, SHELL_MSG_ERROR);
			}
			break;
		case OP_DELETE_PERM:
			snprintf(msg, sizeof(msg), "Undo perm-delete: '%s' is gone forever.\n", op.old_name);
			print_to_shell(shell, msg, SHELL_MSG_ERROR);
			break;
		case OP_MOVE:
			if (strcmp(op.cwd, "/") == 0) {
				snprintf(old_path, PATH_MAX, "/%s", op.old_name);
			} else {
				path_join(old_path, PATH_MAX, op.cwd, op.old_name);
			}
			if (path_move(old_path, op.trash_path) == 0) {
				snprintf(msg, sizeof(msg), "Undo move: restored '%s'.\n", op.old_name);
				print_to_shell(shell, msg, SHELL_MSG_NORMAL);
			} else {
				snprintf(msg, sizeof(msg), "Undo move failed for '%s': %s\n", op.old_name, strerror(errno));
				print_to_shell(shell, msg, SHELL_MSG_ERROR);
			}
			break;
		}
	} while (bid > 0 && files->undo_top > 0 && files->undo_stack[files->undo_top - 1].batch_id == bid);
}

void files_redo(FilesBuffer *files, struct ShellBuffer *shell) {
	UndoOp op;
	char old_path[PATH_MAX];
	char new_path[PATH_MAX];
	char msg[MAX_FILENAME * 2 + 128];
	int bid;

	if (files->redo_top == 0) {
		print_to_shell(shell, "Nothing to redo.\n", SHELL_MSG_WARN);
		return;
	}

	bid = files->redo_stack[files->redo_top - 1].batch_id;

	do {
		op = files->redo_stack[--files->redo_top];
		if (files->undo_top < UNDO_MAX) {
			files->undo_stack[files->undo_top++] = op;
		}

		switch (op.type) {
		case OP_RENAME:
			if (strcmp(op.cwd, "/") == 0) {
				snprintf(old_path, PATH_MAX, "/%s", op.old_name);
				snprintf(new_path, PATH_MAX, "/%s", op.new_name);
			} else {
				path_join(old_path, PATH_MAX, op.cwd, op.old_name);
				path_join(new_path, PATH_MAX, op.cwd, op.new_name);
			}
			if (path_move(old_path, new_path) == 0) {
				snprintf(msg, sizeof(msg), "Redo rename: '%.255s' -> '%.255s'\n", op.old_name, op.new_name);
				print_to_shell(shell, msg, SHELL_MSG_NORMAL);
			} else {
				snprintf(msg, sizeof(msg), "Redo rename failed for '%.255s': %s\n", op.old_name, strerror(errno));
				print_to_shell(shell, msg, SHELL_MSG_ERROR);
			}
			break;
		case OP_TRASH:
			snprintf(msg, sizeof(msg), "Redo trash not supported.\n");
			print_to_shell(shell, msg, SHELL_MSG_WARN);
			break;
		case OP_DELETE_PERM:
			snprintf(msg, sizeof(msg), "Redo perm-delete not supported.\n");
			print_to_shell(shell, msg, SHELL_MSG_WARN);
			break;
		case OP_MOVE:
			if (strcmp(op.cwd, "/") == 0) {
				snprintf(new_path, PATH_MAX, "/%s", op.old_name);
			} else {
				path_join(new_path, PATH_MAX, op.cwd, op.old_name);
			}
			if (path_move(op.trash_path, new_path) == 0) {
				snprintf(msg, sizeof(msg), "Redo move: '%s' moved.\n", op.old_name);
				print_to_shell(shell, msg, SHELL_MSG_NORMAL);
			} else {
				snprintf(msg, sizeof(msg), "Redo move failed for '%s': %s\n", op.old_name, strerror(errno));
				print_to_shell(shell, msg, SHELL_MSG_ERROR);
			}
			break;
		}
	} while (bid > 0 && files->redo_top > 0 && files->redo_stack[files->redo_top - 1].batch_id == bid);
}
