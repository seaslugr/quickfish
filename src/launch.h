#pragma once

#include <ncurses.h>

#include "file.h"

void files_open_selected(FilesBuffer *files, struct ShellBuffer *shell, WINDOW *files_win, WINDOW *shell_win);
