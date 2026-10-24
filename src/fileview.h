#pragma once

#include <ncurses.h>

#include "file.h"

void files_render(FilesBuffer *files, WINDOW *win, int height, int width, int focused);
