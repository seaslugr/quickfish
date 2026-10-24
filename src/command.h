#pragma once

#include "cmdline.h"
#include "tui.h"

typedef enum {
	CMD_CAT_NAV,
	CMD_CAT_FILES,
	CMD_CAT_PANES,
	CMD_CAT_SHELL,
	CMD_CAT_DEBUG,
} CmdCategory;

/* count is the numeric prefix typed before a key (0 when none). from_key is
   set when a keybind ran the command, which is when undoable actions ask for
   confirmation. */
typedef struct {
	int count;
	int from_key;
	TUI *tui;
} CmdCtx;

typedef int (*CmdFn)(CmdCtx *ctx, const CmdArgs *args);

/* aliases is a space-separated list of extra names for the shell. A command
   with a NULL fn only provides a ghost hint and still runs in the system shell. */
typedef struct {
	CmdCategory category;
	CmdFn fn;
	const char *name;
	const char *aliases;
	const char *usage;
	const char *help;
} Command;

const Command *cmd_find(const char *word);
int cmd_count();
const Command *cmd_at(int i);
const char *cmd_category_name(CmdCategory category);
int cmd_run(TUI *tui, const char *line, int count, int from_key);
void cmd_submit(TUI *tui, const char *line, int from_shell_pane);
void compute_ghost(const char *input, int input_pos, const char *cwd,
                   const FilesBuffer *files, const ShellBuffer *shell,
                   char *ghost_out, int ghost_max);
