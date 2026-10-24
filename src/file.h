#pragma once

#include <limits.h>
#include <stddef.h>
#include <sys/stat.h>

#include "util.h"

#define UNDO_MAX 50

typedef struct {
	int count;
	int cap;
	char **paths;
} MoveRegister;

typedef enum {
	ENTRY_FILE,
	ENTRY_DIR,
} EntryType;

typedef enum {
	OP_RENAME,
	OP_TRASH,
	OP_DELETE_PERM,
	OP_MOVE,
} OpType;

typedef struct {
	char marked;
	char in_move_reg;
	int index;
	int is_empty;
	EntryType type;
	mode_t mode;
	char name[MAX_FILENAME];
} DirEntry;

typedef struct {
	OpType type;
	int batch_id;
	char old_name[MAX_FILENAME];
	char new_name[MAX_FILENAME];
	char cwd[PATH_MAX];
	char trash_path[PATH_MAX];
} UndoOp;

typedef struct {
	int entry_count;
	int selected;
	int col_offset;
	int sel_count;
	int undo_top;
	int redo_top;
	int batch_seq;
	int entry_cap;
	unsigned load_gen;
	DirEntry *entries;
	MoveRegister move_reg;
	char cwd[PATH_MAX];
	UndoOp undo_stack[UNDO_MAX];
	UndoOp redo_stack[UNDO_MAX];
} FilesBuffer;

struct ShellBuffer;

void files_load_directory(FilesBuffer *files, const char *path);
void files_free(FilesBuffer *files);
void files_sort_entries(FilesBuffer *files);
void files_select_next(FilesBuffer *files);
void files_select_next_n(FilesBuffer *files, int n);
void files_select_prev(FilesBuffer *files);
void files_select_prev_n(FilesBuffer *files, int n);
void files_change_dir(FilesBuffer *files, const char *dirname);
int files_get_selected_path(FilesBuffer *files, char *out_path, size_t out_size);

void refresh_files_buffer(FilesBuffer *files);

void files_toggle_select(FilesBuffer *files, int idx);
void files_select_add(FilesBuffer *files, int idx);
void files_clear_selection(FilesBuffer *files);
int files_build_sel_args(FilesBuffer *files, char *out, int out_size);

