#pragma once

#include "cmdline.h"
#include "file.h"

#define TARGETS_DEFAULT "sel|cur"

int targets_resolve(FilesBuffer *files, struct ShellBuffer *shell, const CmdArgs *args, int first, int last, const char *fallback, int *out);
