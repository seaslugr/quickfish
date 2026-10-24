#pragma once

#include "file.h"

void files_move_reindex(FilesBuffer *files);
void files_move_mark(FilesBuffer *files, struct ShellBuffer *shell, int idx);
void files_move_mark_entries(FilesBuffer *files, struct ShellBuffer *shell, const int *idx, int n);
void files_move_clear(FilesBuffer *files, struct ShellBuffer *shell);
void files_move_paste(FilesBuffer *files, struct ShellBuffer *shell, const char *dest);
void files_debug_print_move_register(FilesBuffer *files, struct ShellBuffer *shell);
