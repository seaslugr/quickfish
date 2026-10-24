#pragma once

#include "../src/tui.h"

void fixture_remove_tree(const char *path);
void fixture_write_file(const char *path, const char *data);
int fixture_index_of(FilesBuffer *files, const char *name);
TUI *fixture_tui_new(const char *dir);
void fixture_tui_free(TUI *tui);
int fixture_confirm_yes(TUI *tui, const char *body, const char *warning, int danger);
int fixture_confirm_no(TUI *tui, const char *body, const char *warning, int danger);
