#pragma once

#include "tui.h"

void prompt_goto_begin(TUI *tui);
void prompt_goto(TUI *tui, const char *text);
void prompt_rename_begin(TUI *tui, int idx);
void prompt_quickshell_begin(TUI *tui);
int prompt_handle_input(TUI *tui, int ch);
void prompt_render_goto(TUI *tui);
void prompt_render_rename(TUI *tui);
void prompt_render_quickshell(TUI *tui, int files_render_w);
