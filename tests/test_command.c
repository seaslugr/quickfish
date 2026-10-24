#define _XOPEN_SOURCE 700

#include <ncurses.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fixture.h"
#include "../src/cmdline.h"
#include "../src/command.h"
#include "../src/keymap.h"
#include "../src/movereg.h"
#include "../src/targets.h"
#include "../src/util.h"

static void make_fixture(const char *root);
static int resolve_line(TUI *tui, const char *line, const char *fallback, int *out);
static int exists(const char *root, const char *name);

/* root/alpha/alchemy.txt, root/beta/, root/alpine.txt, root/it's, root/two words.txt
   which list as 1 alpha, 2 beta, 3 alpine.txt, 4 it's, 5 two words.txt */
static void make_fixture(const char *root) {
	char path[PATH_MAX];

	path_join(path, sizeof(path), root, "alpha");
	mkdir(path, 0755);
	path_join(path, sizeof(path), root, "alpha/alchemy.txt");
	fixture_write_file(path, NULL);
	path_join(path, sizeof(path), root, "beta");
	mkdir(path, 0755);
	path_join(path, sizeof(path), root, "alpine.txt");
	fixture_write_file(path, NULL);
	path_join(path, sizeof(path), root, "it's");
	fixture_write_file(path, NULL);
	path_join(path, sizeof(path), root, "two words.txt");
	fixture_write_file(path, NULL);
}

static int resolve_line(TUI *tui, const char *line, const char *fallback, int *out) {
	CmdArgs args;

	cmdline_split(line, &args);
	return targets_resolve(&tui->files, &tui->shell, &args, 0, args.argc, fallback, out);
}

static int exists(const char *root, const char *name) {
	char path[PATH_MAX];
	struct stat st;

	path_join(path, sizeof(path), root, name);
	return lstat(path, &st) == 0;
}

void test_command() {
	char root[] = "/tmp/quickfish-test-command-XXXXXX";
	char path[PATH_MAX];
	char home[PATH_MAX];
	char out[256];
	char ghost[128];
	char expected[PATH_MAX * 3];
	char *old_home;
	const Keybind *kb;
	const Command *c;
	CmdArgs args;
	TUI *tui;
	int idx[16];
	size_t wlen;
	int n;
	int all_found;

	SUITE("cmdline_split");

	n = cmdline_split("trash 3 'two words.txt' \"a \\\"q\\\"\" b\\ c", &args);
	CHECK(n == 5, "splits words on whitespace");
	CHECK_STR(args.argv[1], "3", "keeps plain words");
	CHECK(!args.quoted[1], "plain words are not flagged as quoted");
	CHECK_STR(args.argv[2], "two words.txt", "single quotes keep spaces");
	CHECK(args.quoted[2], "quoted words are flagged");
	CHECK_STR(args.argv[3], "a \"q\"", "double quotes allow escaped quotes");
	CHECK_STR(args.argv[4], "b c", "backslash escapes a space");
	CHECK(cmdline_split("echo 'oops", &args) == -1, "unterminated quote is an error");
	cmdline_split("rename 3 x", &args);
	cmdline_shift(&args);
	CHECK(args.argc == 2 && strcmp(args.argv[0], "3") == 0, "shift drops the command word");

	SUITE("cmdline_expand");

	n = cmdline_expand("ls sel cur", "'a' 'b'", "'c'", out, sizeof(out));
	CHECK_STR(out, "ls 'a' 'b' 'c'", "expands sel and cur");
	cmdline_expand("echo 'sel' xsel \\sel", "'a'", "'c'", out, sizeof(out));
	CHECK_STR(out, "echo 'sel' xsel \\sel", "leaves quoted, escaped and embedded words alone");
	CHECK(cmdline_expand("ls sel", NULL, "'c'", out, sizeof(out)) == -1, "sel without a selection is an error");
	CHECK(cmdline_expand("ls cur", "'a'", "'a very long name'", out, 8) == -1, "reports overflow instead of truncating");

	CHECK(mkdtemp(root) != NULL, "creates command temp directory");
	make_fixture(root);
	tui = fixture_tui_new(root);
	path_join(home, sizeof(home), root, ".home");
	mkdir(home, 0755);
	old_home = getenv("HOME");
	old_home = old_home ? strdup(old_home) : NULL;
	setenv("HOME", home, 1);

	SUITE("targets_resolve");

	n = resolve_line(tui, "3", NULL, idx);
	CHECK(n == 1 && strcmp(tui->files.entries[idx[0]].name, "alpine.txt") == 0, "an index names that entry");
	n = resolve_line(tui, "beta 'two words.txt'", NULL, idx);
	CHECK(n == 2 && idx[0] == 1 && idx[1] == 4, "names resolve in listing order");
	CHECK(resolve_line(tui, "'3'", NULL, idx) == -1, "a quoted number is a name, not an index");
	CHECK(resolve_line(tui, "9", NULL, idx) == -1, "a missing index is an error");
	CHECK(strstr(tui->shell.last_output, "no entry '9'") != NULL, "a missing target is reported");
	CHECK(resolve_line(tui, "all", NULL, idx) == 5, "all names every entry");
	CHECK(resolve_line(tui, "3 3 alpine.txt", NULL, idx) == 1, "duplicates collapse");
	tui->files.selected = 3;
	n = resolve_line(tui, "", TARGETS_DEFAULT, idx);
	CHECK(n == 1 && idx[0] == 3, "default falls back to the cursor without a selection");
	files_toggle_select(&tui->files, 0);
	files_toggle_select(&tui->files, 2);
	n = resolve_line(tui, "", TARGETS_DEFAULT, idx);
	CHECK(n == 2 && idx[0] == 0 && idx[1] == 2, "default prefers the selection");
	CHECK(resolve_line(tui, "sel|all", NULL, idx) == 2, "an alternative stops at the first non-empty set");
	files_clear_selection(&tui->files);
	CHECK(resolve_line(tui, "sel|all", NULL, idx) == 5, "an empty alternative falls through");

	SUITE("command table");

	c = cmd_find("rm");
	CHECK(c && strcmp(c->name, "delete") == 0, "rm is delete");
	c = cmd_find("r");
	CHECK(c && strcmp(c->name, "rename") == 0, "r is rename");
	c = cmd_find("..");
	CHECK(c && strcmp(c->name, "back") == 0, ".. is back");
	CHECK(cmd_find("nope") == NULL, "unknown words are not commands");

	SUITE("keymap");

	all_found = 1;
	for (int i = 0; i < keymap_count(); i++) {
		wlen = strcspn(keymap_at(i)->line, " ");
		memcpy(out, keymap_at(i)->line, wlen);
		out[wlen] = '\0';
		if (!cmd_find(out)) {
			all_found = 0;
			printf("    unknown command in keymap: %s\n", keymap_at(i)->line);
		}
	}
	CHECK(all_found, "every binding runs a real command");
	kb = keymap_find('j', KEY_SCOPE_FILES);
	CHECK(kb && strcmp(kb->line, "down") == 0, "j moves down in the files pane");
	CHECK(keymap_find('J', KEY_SCOPE_SHELL) == NULL, "J types in the shell pane");
	kb = keymap_find(KEY_SR, KEY_SCOPE_SHELL);
	CHECK(kb && strcmp(kb->line, "focus files") == 0, "shift+up leaves the shell pane");
	keymap_key_name(4, out, sizeof(out));
	CHECK_STR(out, "Ctrl+D", "names control keys");

	SUITE("navigation commands");

	cmd_run(tui, "cd alpha", 0, 0);
	path_join(path, sizeof(path), root, "alpha");
	CHECK_STR(tui->files.cwd, path, "cd enters a directory");
	cmd_run(tui, "back", 0, 0);
	CHECK_STR(tui->files.cwd, root, "back returns to the previous directory");
	cmd_run(tui, "cd /quickfish/does/not/exist", 0, 0);
	CHECK_STR(tui->files.cwd, root, "cd to a missing directory stays put");
	CHECK(strstr(tui->shell.last_output, "no such directory") != NULL, "cd to a missing directory is reported");
	cmd_run(tui, "cd 3", 0, 0);
	CHECK(strstr(tui->shell.last_output, "is not a directory") != NULL, "cd to a file index is reported");
	cmd_run(tui, "cd", 0, 0);
	CHECK_STR(tui->files.cwd, home, "cd with no argument goes home");
	cmd_run(tui, "cd -", 0, 0);
	CHECK_STR(tui->files.cwd, root, "cd - goes back");
	cmd_run(tui, "1", 0, 0);
	CHECK_STR(tui->files.cwd, path, "a bare index opens that directory");
	cmd_run(tui, "parent", 0, 0);
	CHECK_STR(tui->files.cwd, root, "parent goes up a level");
	tui->files.selected = 0;
	cmd_run(tui, "down 2", 0, 0);
	CHECK(tui->files.selected == 2, "down takes a count argument");
	cmd_run(tui, "goto", 5, 1);
	CHECK(tui->files.selected == 4, "a count before goto jumps to that index");
	cmd_run(tui, "goto", 99, 1);
	CHECK(tui->files.selected == 4 && strstr(tui->shell.last_output, "no entry 99") != NULL, "goto to a missing index is reported");

	SUITE("selection commands");

	cmd_run(tui, "select 1 3", 0, 0);
	CHECK(tui->files.sel_count == 2, "select toggles the given targets");
	cmd_run(tui, "select 1", 0, 0);
	CHECK(tui->files.sel_count == 1 && tui->files.entries[2].marked, "select toggles off again");
	files_clear_selection(&tui->files);
	tui->files.selected = 0;
	cmd_run(tui, "visual on", 0, 0);
	cmd_run(tui, "down", 0, 0);
	CHECK(tui->visual_mode && tui->files.sel_count == 2, "visual mode extends the selection while moving");
	cmd_run(tui, "visual off", 0, 0);
	files_clear_selection(&tui->files);

	SUITE("file commands");

	cmd_run(tui, "stat 3", 0, 0);
	CHECK(strstr(tui->shell.last_output, "name: alpine.txt") != NULL, "stat prints the target");
	cmd_run(tui, "r 3 renamed.txt", 0, 0);
	CHECK(exists(root, "renamed.txt") && !exists(root, "alpine.txt"), "r renames by index");
	CHECK_STR(tui->files.entries[tui->files.selected].name, "renamed.txt", "the cursor follows the renamed entry");
	cmd_run(tui, "rename renamed.txt beta", 0, 0);
	CHECK(exists(root, "renamed.txt") && exists(root, "beta"), "rename never replaces an existing entry");
	cmd_run(tui, "rename renamed.txt alpine.txt", 0, 0);
	CHECK(exists(root, "alpine.txt"), "rename by name works");

	cmd_run(tui, "trash alpine.txt \"it's\"", 0, 0);
	CHECK(!exists(root, "alpine.txt") && !exists(root, "it's"), "trash takes several targets");
	CHECK(strstr(tui->shell.last_output, "Trashed 2 item(s)") != NULL, "trash reports what it did");
	cmd_run(tui, "undo", 0, 0);
	CHECK(exists(root, "alpine.txt") && exists(root, "it's"), "one undo restores the whole batch");

	tui->confirm = fixture_confirm_no;
	cmd_run(tui, "rm 'two words.txt'", 0, 0);
	CHECK(exists(root, "two words.txt"), "delete does nothing when not confirmed");
	CHECK(strstr(tui->shell.last_output, "cancelled") != NULL, "a declined delete says so");
	tui->confirm = NULL;
	cmd_run(tui, "rm 'two words.txt'", 0, 0);
	CHECK(exists(root, "two words.txt"), "delete refuses without a way to confirm");
	tui->confirm = fixture_confirm_yes;
	cmd_run(tui, "rm 'two words.txt'", 0, 0);
	CHECK(!exists(root, "two words.txt"), "confirmed delete removes the target");
	path_join(path, sizeof(path), root, "two words.txt");
	fixture_write_file(path, NULL);
	tui_refresh_files(tui);

	cmd_run(tui, "mark sel|all", 0, 1);
	CHECK(tui->files.move_reg.count == tui->files.entry_count, "M marks everything without a selection");
	cmd_run(tui, "mark sel|all", 0, 1);
	CHECK(tui->files.move_reg.count == 0, "marking an all-marked set unmarks it");
	cmd_run(tui, "paste", 0, 0);
	CHECK(strstr(tui->shell.last_output, "nothing is marked") != NULL, "paste with nothing marked warns");
	cmd_run(tui, "mark alpine.txt", 0, 0);
	cmd_run(tui, "paste beta", 0, 0);
	CHECK(exists(root, "beta/alpine.txt") && !exists(root, "alpine.txt"), "paste moves marked items into a directory");
	cmd_run(tui, "undo", 0, 0);
	CHECK(exists(root, "alpine.txt"), "undo brings the moved item back");
	cmd_run(tui, "mark 3", 0, 0);
	cmd_run(tui, "paste 3", 0, 0);
	CHECK(strstr(tui->shell.last_output, "is not a directory") != NULL, "paste into a file is refused");
	cmd_run(tui, "clear-marks", 0, 0);

	SUITE("system commands");

	cmd_run(tui, "!echo bang", 0, 0);
	CHECK(strstr(tui->shell.last_output, "\001bang\n") != NULL, "! runs the rest in the system shell");
	files_toggle_select(&tui->files, 3);
	cmd_run(tui, "echo sel", 0, 0);
	CHECK(strstr(tui->shell.last_output, "\001it's\n") != NULL, "unknown words run in the system shell with sel expanded");
	files_clear_selection(&tui->files);
	cmd_run(tui, "!stat --version", 0, 0);
	CHECK(strstr(tui->shell.last_output, "$ stat --version") != NULL, "! reaches a system command a quickfish command shadows");
	cmd_run(tui, "quickfish-no-such-command-xyz", 0, 0);
	CHECK(strstr(tui->shell.last_output, "exit status 127") != NULL, "a missing system command reports its exit status");

	SUITE("cmd_submit");

	cmd_submit(tui, "  q  ", 0);
	CHECK(tui->running == 0, "q quits");
	CHECK_STR(tui->shell.last_cmd, "q", "the result row shows the trimmed line");
	CHECK_STR(tui->shell.last_result, "quit", "the result row names the aliased command");
	CHECK_STR(tui->shell.history[tui->shell.history_count - 1], "q", "submitted lines go to history");
	tui->running = 1;
	cmd_submit(tui, "help", 0);
	CHECK(strstr(tui->shell.last_output, "trash") != NULL && strstr(tui->shell.last_output, "Targets:") != NULL, "help lists the commands");

	SUITE("compute_ghost");

	compute_ghost("q", 1, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, " > quit", "shows what an alias runs");
	compute_ghost("s", 1, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, " > stat [targets|path]", "shows usage after an alias");
	compute_ghost("trash", 5, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, " > [targets]", "shows usage after a full name");
	compute_ghost("alp", 3, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, "", "does not show a file ghost for a bare first word");
	compute_ghost("alpha/al", 8, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, "chemy.txt", "shows the rest of a path completion");
	compute_ghost("3", 1, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, " > alpine.txt", "shows the entry a bare index opens");
	tui->shell.dir_jumplist_count = 0;
	shell_push_dir(&tui->shell, "/tmp/origin");
	compute_ghost("..", 2, root, &tui->files, &tui->shell, ghost, sizeof(ghost));
	CHECK_STR(ghost, " > origin", "shows where .. goes");

	SUITE("debug commands");

	shell_clear_output(&tui->shell);
	files_move_mark(&tui->files, &tui->shell, fixture_index_of(&tui->files, "alpine.txt"));
	shell_clear_output(&tui->shell);
	cmd_run(tui, "$move", 0, 0);
	snprintf(expected, sizeof(expected), "\001[move register]\n  %s/alpine.txt\n", root);
	CHECK_STR(tui->shell.last_output, expected, "$move prints the move register");
	cmd_run(tui, "clear-marks", 0, 0);

	tui->files.undo_top = 0;
	cmd_run(tui, "r alpine.txt moved.txt", 0, 0);
	shell_clear_output(&tui->shell);
	cmd_run(tui, "$undo", 0, 0);
	snprintf(expected, sizeof(expected), "\001[undo stack]\n 1. rename %s/alpine.txt -> %s/moved.txt\n", root, root);
	CHECK_STR(tui->shell.last_output, expected, "$undo prints top-to-bottom");
	cmd_run(tui, "undo", 0, 0);
	shell_clear_output(&tui->shell);
	cmd_run(tui, "$redo", 0, 0);
	snprintf(expected, sizeof(expected), "\001[redo stack]\n 1. rename %s/alpine.txt -> %s/moved.txt\n", root, root);
	CHECK_STR(tui->shell.last_output, expected, "$redo prints top-to-bottom");

	if (old_home) {
		setenv("HOME", old_home, 1);
		free(old_home);
	} else {
		unsetenv("HOME");
	}
	fixture_tui_free(tui);
	fixture_remove_tree(root);
}
