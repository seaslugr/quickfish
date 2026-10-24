#pragma once

#include "file.h"

void files_cmd_stat(FilesBuffer *files, struct ShellBuffer *shell, const char *arg);
void files_stat_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx);
