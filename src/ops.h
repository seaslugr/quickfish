#pragma once

#include "file.h"

int files_trash_entries(FilesBuffer *files, struct ShellBuffer *shell, const int *idx, int n);
int files_delete_entries(FilesBuffer *files, struct ShellBuffer *shell, const int *idx, int n);
int files_rename_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx, const char *new_name);
void files_undo(FilesBuffer *files, struct ShellBuffer *shell);
void files_redo(FilesBuffer *files, struct ShellBuffer *shell);
void files_debug_print_undo_stack(FilesBuffer *files, struct ShellBuffer *shell);
void files_debug_print_redo_stack(FilesBuffer *files, struct ShellBuffer *shell);
void push_undo(FilesBuffer *files, UndoOp op);
