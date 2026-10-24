#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fixture.h"
#include "../src/command.h"
#include "../src/fs.h"
#include "../src/ops.h"
#include "../src/util.h"

#define EVIL_NAME "x'; touch PWNED; '"

static int select_named(FilesBuffer *files, const char *name);

static FilesBuffer files;
static ShellBuffer shell;

static int select_named(FilesBuffer *files, const char *name) {
	int i;

	i = fixture_index_of(files, name);
	if (i >= 0) {
		files_toggle_select(files, i);
	}
	return i;
}

void test_safety() {
	char root[] = "/tmp/quickfish-test-safety-XXXXXX";
	char home[PATH_MAX];
	char trash_dir[PATH_MAX];
	char path[PATH_MAX];
	char other[PATH_MAX];
	char pwned[PATH_MAX];
	char old_cwd[PATH_MAX];
	char out[64];
	char long_name[201];
	char *old_home;
	char line[64];
	struct stat st;
	TUI *tui;
	int idx;
	int rc;

	SUITE("path_move");

	CHECK(mkdtemp(root) != NULL, "creates safety temp directory");
	if (!getcwd(old_cwd, sizeof(old_cwd))) {
		old_cwd[0] = '\0';
	}
	if (chdir(root) != 0) {
		return;
	}
	path_join(pwned, sizeof(pwned), root, "PWNED");

	path_join(path, sizeof(path), root, "a");
	path_join(other, sizeof(other), root, "b");
	fixture_write_file(path, "a");
	fixture_write_file(other, "bb");
	rc = path_move(path, other);
	CHECK(rc != 0, "refuses to overwrite an existing destination");
	CHECK(stat(other, &st) == 0 && st.st_size == 2, "existing destination is left intact");
	CHECK(stat(path, &st) == 0, "source is left in place when move is refused");

	unlink(other);
	CHECK(path_move(path, other) == 0, "moves when destination is free");
	CHECK(stat(other, &st) == 0 && stat(path, &st) != 0, "move relocates the entry");
	unlink(other);

	SUITE("files_build_sel_args quoting");

	path_join(path, sizeof(path), root, "it's");
	fixture_write_file(path, NULL);
	files_free(&files);
	memset(&files, 0, sizeof(files));
	files_load_directory(&files, root);
	select_named(&files, "it's");
	rc = files_build_sel_args(&files, out, sizeof(out));
	CHECK_STR(out, "'it'\\''s'", "escapes embedded single quotes");
	CHECK(rc == (int)strlen(out), "returns written length");
	CHECK(files_build_sel_args(&files, out, 6) == -1, "reports overflow instead of truncating");
	unlink(path);

	SUITE("trash with hostile filename");

	path_join(home, sizeof(home), root, "home");
	mkdir(home, 0755);
	old_home = getenv("HOME");
	old_home = old_home ? strdup(old_home) : NULL;
	setenv("HOME", home, 1);

	path_join(path, sizeof(path), root, EVIL_NAME);
	fixture_write_file(path, NULL);
	files_free(&files);
	memset(&files, 0, sizeof(files));
	memset(&shell, 0, sizeof(shell));
	files_load_directory(&files, root);
	idx = fixture_index_of(&files, EVIL_NAME);
	CHECK(idx >= 0, "loads hostile filename");
	files_trash_entries(&files, &shell, &idx, 1);
	CHECK(stat(pwned, &st) != 0, "trash does not execute filename as shell");
	CHECK(stat(path, &st) != 0, "trash removes the entry from its directory");
	path_join(trash_dir, sizeof(trash_dir), home, ".local/share/Trash/files");
	path_join(other, sizeof(other), trash_dir, EVIL_NAME);
	CHECK(stat(other, &st) == 0, "trash places the entry in the trash directory");
	CHECK(files.undo_top == 1 && files.undo_stack[0].type == OP_TRASH, "trash records an undo op");

	files_undo(&files, &shell);
	CHECK(stat(path, &st) == 0, "undo restores the trashed entry");
	CHECK(stat(pwned, &st) != 0, "undo does not execute filename as shell");

	fixture_write_file(other, NULL);
	files_load_directory(&files, root);
	idx = fixture_index_of(&files, EVIL_NAME);
	files_trash_entries(&files, &shell, &idx, 1);
	CHECK(stat(other, &st) == 0 && stat(path, &st) != 0, "name collision in trash keeps both entries");

	if (old_home) {
		setenv("HOME", old_home, 1);
		free(old_home);
	} else {
		unsetenv("HOME");
	}

	SUITE("permanent delete with hostile filename");

	path_join(path, sizeof(path), root, EVIL_NAME);
	mkdir(path, 0755);
	path_join(other, sizeof(other), path, "inner");
	fixture_write_file(other, NULL);
	path_join(other, sizeof(other), root, "keep");
	fixture_write_file(other, NULL);
	files_load_directory(&files, root);
	idx = fixture_index_of(&files, EVIL_NAME);
	files.undo_top = 0;
	files_delete_entries(&files, &shell, &idx, 1);
	CHECK(stat(path, &st) != 0, "deletes directory recursively");
	CHECK(stat(other, &st) == 0, "leaves sibling entries alone");
	CHECK(stat(pwned, &st) != 0, "delete does not execute filename as shell");

	SUITE("delete reports failure");

	path_join(path, sizeof(path), root, "locked");
	mkdir(path, 0755);
	path_join(other, sizeof(other), path, "inner");
	fixture_write_file(other, NULL);
	chmod(path, 0555);
	files_load_directory(&files, root);
	idx = fixture_index_of(&files, "locked");
	files.undo_top = 0;
	memset(&shell, 0, sizeof(shell));
	files_delete_entries(&files, &shell, &idx, 1);
	if (geteuid() != 0) {
		CHECK(files.undo_top == 0, "failed delete does not record an undo op");
		CHECK(strstr(shell.last_output + 1, "Delete failed") != NULL, "failed delete prints an error");
	}
	chmod(path, 0755);
	fixture_remove_tree(path);

	SUITE("shell rm with hostile filename");

	path_join(path, sizeof(path), root, EVIL_NAME);
	fixture_write_file(path, NULL);
	tui = fixture_tui_new(root);
	idx = fixture_index_of(&tui->files, EVIL_NAME);
	snprintf(line, sizeof(line), "rm %d", tui->files.entries[idx].index);
	cmd_run(tui, line, 0, 0);
	CHECK(stat(path, &st) != 0, "confirmed rm removes the indexed entry");
	CHECK(stat(pwned, &st) != 0, "rm does not execute filename as shell");
	CHECK(strstr(tui->shell.last_output, "Deleted 1 item(s)") != NULL, "rm reports what it removed");

	SUITE("sel and cur expansion");

	fixture_write_file(path, NULL);
	tui_refresh_files(tui);
	select_named(&tui->files, EVIL_NAME);
	cmd_run(tui, "ls sel", 0, 0);
	CHECK(stat(pwned, &st) != 0, "sel expansion does not execute filename as shell");
	tui->files.selected = fixture_index_of(&tui->files, EVIL_NAME);
	cmd_run(tui, "!ls cur", 0, 0);
	CHECK(stat(pwned, &st) != 0, "cur expansion does not execute filename as shell");
	unlink(path);

	memset(long_name, 'n', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	for (int i = 0; i < 12; i++) {
		long_name[0] = (char)('a' + i);
		path_join(path, sizeof(path), root, long_name);
		fixture_write_file(path, NULL);
	}
	tui_refresh_files(tui);
	for (int i = 0; i < 12; i++) {
		long_name[0] = (char)('a' + i);
		select_named(&tui->files, long_name);
	}
	cmd_run(tui, "touch MARKER sel", 0, 0);
	path_join(path, sizeof(path), root, "MARKER");
	CHECK(stat(path, &st) != 0, "oversized sel expansion is not run truncated");
	CHECK(strstr(tui->shell.last_output, "too large") != NULL, "oversized sel expansion prints an error");
	fixture_tui_free(tui);

	if (old_cwd[0]) {
		chdir(old_cwd);
	}
	files_free(&files);
	fixture_remove_tree(root);
}
