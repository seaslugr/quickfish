#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "movereg.h"
#include "ops.h"
#include "shell.h"
#include "util.h"

static int move_reg_find(const MoveRegister *reg, const char *path);
static int move_reg_append(MoveRegister *reg, const char *path);
static void move_reg_remove(MoveRegister *reg, int j);
static void files_move_print_register(FilesBuffer *files, struct ShellBuffer *shell);
static int move_paste_one(FilesBuffer *files, struct ShellBuffer *shell, const char *src, const char *dest, int *moved);

/* Flags entries of the current directory that are in the move register.
   Only register paths whose parent is cwd can match, so the rest are skipped cheaply. */
void files_move_reindex(FilesBuffer *files) {
	size_t cwd_len;
	const char *name;
	const char *p;

	for (int i = 0; i < files->entry_count; i++) {
		files->entries[i].in_move_reg = 0;
	}

	cwd_len = strlen(files->cwd);
	for (int j = 0; j < files->move_reg.count; j++) {
		p = files->move_reg.paths[j];
		if (strncmp(p, files->cwd, cwd_len) != 0) { continue; }
		name = p + cwd_len;
		if (cwd_len > 1) {
			if (*name != '/') { continue; }
			name++;
		}
		if (strchr(name, '/')) { continue; }
		for (int i = 0; i < files->entry_count; i++) {
			if (strcmp(files->entries[i].name, name) == 0) {
				files->entries[i].in_move_reg = 1;
				break;
			}
		}
	}
}

static int move_reg_find(const MoveRegister *reg, const char *path) {
	for (int j = 0; j < reg->count; j++) {
		if (strcmp(reg->paths[j], path) == 0) { return j; }
	}
	return -1;
}

static int move_reg_append(MoveRegister *reg, const char *path) {
	char **grown;
	char *copy;
	int cap;

	if (reg->count == reg->cap) {
		cap = reg->cap ? reg->cap * 2 : 64;
		grown = realloc(reg->paths, (size_t)cap * sizeof(char *));
		if (!grown) { return -1; }
		reg->paths = grown;
		reg->cap = cap;
	}
	copy = strdup(path);
	if (!copy) { return -1; }
	reg->paths[reg->count++] = copy;
	return 0;
}

static void move_reg_remove(MoveRegister *reg, int j) {
	free(reg->paths[j]);
	memmove(&reg->paths[j], &reg->paths[j + 1], (size_t)(reg->count - j - 1) * sizeof(char *));
	reg->count--;
}

static void files_move_print_register(FilesBuffer *files, struct ShellBuffer *shell) {
	char buf[SHELL_OUTPUT_MAX];
	int pos;
	int n;

	if (files->move_reg.count == 0) {
		print_to_shell(shell, "[move register] (empty)\n", SHELL_MSG_NORMAL);
		return;
	}
	pos = snprintf(buf, sizeof(buf), "[move register]\n");
	for (int i = 0; i < files->move_reg.count; i++) {
		n = snprintf(buf + pos, sizeof(buf) - pos, "  %s\n", files->move_reg.paths[i]);
		if (n < 0 || n >= (int)sizeof(buf) - pos) { break; }
		pos += n;
	}
	print_to_shell(shell, buf, SHELL_MSG_NORMAL);
}

void files_debug_print_move_register(FilesBuffer *files, struct ShellBuffer *shell) {
	files_move_print_register(files, shell);
}

/* Toggles the given entries in the move register: if every one is already
   marked they are all unmarked, otherwise the unmarked ones are added. */
void files_move_mark_entries(FilesBuffer *files, struct ShellBuffer *shell, const int *idx, int n) {
	char full[PATH_MAX];
	int all_marked;
	int j;

	all_marked = (n > 0);
	for (int i = 0; i < n; i++) {
		if (idx[i] < 0 || idx[i] >= files->entry_count) { return; }
		if (!files->entries[idx[i]].in_move_reg) { all_marked = 0; }
	}

	for (int i = 0; i < n; i++) {
		path_join(full, sizeof(full), files->cwd, files->entries[idx[i]].name);
		if (all_marked) {
			j = move_reg_find(&files->move_reg, full);
			if (j >= 0) { move_reg_remove(&files->move_reg, j); }
			continue;
		}
		if (files->entries[idx[i]].in_move_reg) { continue; }
		if (move_reg_append(&files->move_reg, full) != 0) {
			print_to_shell(shell, "Move register: out of memory.\n", SHELL_MSG_ERROR);
			break;
		}
	}
	files_move_reindex(files);
	files_move_print_register(files, shell);
}

void files_move_mark(FilesBuffer *files, struct ShellBuffer *shell, int idx) {
	files_move_mark_entries(files, shell, &idx, 1);
}

void files_move_clear(FilesBuffer *files, struct ShellBuffer *shell) {
	for (int i = 0; i < files->move_reg.count; i++) {
		free(files->move_reg.paths[i]);
	}
	files->move_reg.count = 0;
	for (int i = 0; i < files->entry_count; i++) {
		files->entries[i].in_move_reg = 0;
	}
	print_to_shell(shell, "[move register] cleared\n", SHELL_MSG_NORMAL);
}

/* Moves one register entry into dest. Returns 1 if it should stay in the
   register (the move failed or was refused), 0 if it is done with. */
static int move_paste_one(FilesBuffer *files, struct ShellBuffer *shell, const char *src, const char *dest, int *moved) {
	char dst[PATH_MAX];
	char msg[PATH_MAX + 128];
	const char *base;
	UndoOp op;
	int rv;

	base = strrchr(src, '/');
	base = base ? base + 1 : src;

	if (strcmp(dest, "/") == 0) {
		rv = snprintf(dst, sizeof(dst), "/%s", base);
	} else {
		rv = snprintf(dst, sizeof(dst), "%s/%s", dest, base);
	}
	if (rv < 0 || (size_t)rv >= sizeof(dst)) {
		snprintf(msg, sizeof(msg), "Move failed: destination path too long for '%s'.\n", base);
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return 1;
	}

	if (strcmp(src, dst) == 0) {
		snprintf(msg, sizeof(msg), "Move: '%s' is already here.\n", base);
		print_to_shell(shell, msg, SHELL_MSG_WARN);
		return 0;
	}

	if (path_is_at_or_below(dest, src)) {
		snprintf(msg, sizeof(msg), "Move: can't move '%s' into itself.\n", base);
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return 1;
	}

	if (path_move(src, dst) != 0) {
		if (errno == EEXIST) {
			snprintf(msg, sizeof(msg), "Move: '%s' already exists at destination.\n", base);
		} else {
			snprintf(msg, sizeof(msg), "Move failed for '%s': %s\n", base, strerror(errno));
		}
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return 1;
	}

	op = (UndoOp){ .type = OP_MOVE, .batch_id = files->batch_seq };
	str_copy(op.old_name, sizeof(op.old_name), base);
	str_copy(op.cwd, sizeof(op.cwd), dest);
	str_copy(op.trash_path, sizeof(op.trash_path), src);
	push_undo(files, op);
	snprintf(msg, sizeof(msg), "Moved '%s'\n", base);
	print_to_shell(shell, msg, SHELL_MSG_NORMAL);
	*moved = 1;
	return 0;
}

void files_move_paste(FilesBuffer *files, struct ShellBuffer *shell, const char *dest) {
	MoveRegister *reg;
	int any_moved;
	int kept;

	reg = &files->move_reg;
	if (reg->count == 0) { return; }

	files->batch_seq++;
	any_moved = 0;
	kept = 0;

	for (int i = 0; i < reg->count; i++) {
		if (move_paste_one(files, shell, reg->paths[i], dest, &any_moved)) {
			reg->paths[kept++] = reg->paths[i];
		} else {
			free(reg->paths[i]);
		}
	}
	reg->count = kept;

	if (!any_moved) {
		files->batch_seq--;
	}
	if (kept > 0 && any_moved) {
		files_move_print_register(files, shell);
	}

	files_move_reindex(files);
}
