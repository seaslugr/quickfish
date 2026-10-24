#define _XOPEN_SOURCE 700

#include "test.h"
#include "../src/file.h"
#include "../src/main.h"
#include "../src/shell.h"

#include <fcntl.h>
#include <ftw.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define EVIL_NAME "x'; touch PWNED; '"

static int unlink_cb(const char *path, const struct stat *st, int type, struct FTW *ftwbuf);
static void remove_tree(const char *path);
static void write_file(const char *path, const char *data);
static int select_named(FilesBuffer *files, const char *name);
static int index_of(FilesBuffer *files, const char *name);

static FilesBuffer files;
static ShellBuffer shell;

static int unlink_cb(const char *path, const struct stat *st, int type, struct FTW *ftwbuf) {
	(void)st;
	(void)type;
	(void)ftwbuf;
	return remove(path);
}

static void remove_tree(const char *path) {
	nftw(path, unlink_cb, 16, FTW_DEPTH | FTW_PHYS);
}

static void write_file(const char *path, const char *data) {
	int fd;

	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd >= 0) {
		if (data) {
			write(fd, data, strlen(data));
		}
		close(fd);
	}
}

static int index_of(FilesBuffer *files, const char *name) {
	for (int i = 0; i < files->entry_count; i++) {
		if (strcmp(files->entries[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

static int select_named(FilesBuffer *files, const char *name) {
	int i;

	i = index_of(files, name);
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
	struct stat st;
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
	write_file(path, "a");
	write_file(other, "bb");
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
	write_file(path, NULL);
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
	write_file(path, NULL);
	memset(&files, 0, sizeof(files));
	memset(&shell, 0, sizeof(shell));
	files_load_directory(&files, root);
	files.selected = index_of(&files, EVIL_NAME);
	CHECK(files.selected >= 0, "loads hostile filename");
	files_delete_to_trash(&files, &shell);
	CHECK(stat(pwned, &st) != 0, "trash does not execute filename as shell");
	CHECK(stat(path, &st) != 0, "trash removes the entry from its directory");
	path_join(trash_dir, sizeof(trash_dir), home, ".local/share/Trash/files");
	path_join(other, sizeof(other), trash_dir, EVIL_NAME);
	CHECK(stat(other, &st) == 0, "trash places the entry in the trash directory");
	CHECK(files.undo_top == 1 && files.undo_stack[0].type == OP_TRASH, "trash records an undo op");

	files_undo(&files, &shell);
	CHECK(stat(path, &st) == 0, "undo restores the trashed entry");
	CHECK(stat(pwned, &st) != 0, "undo does not execute filename as shell");

	write_file(other, NULL);
	files_load_directory(&files, root);
	files.selected = index_of(&files, EVIL_NAME);
	files_delete_to_trash(&files, &shell);
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
	write_file(other, NULL);
	path_join(other, sizeof(other), root, "keep");
	write_file(other, NULL);
	files_load_directory(&files, root);
	files.selected = index_of(&files, EVIL_NAME);
	files.undo_top = 0;
	files_delete_permanent(&files, &shell);
	CHECK(stat(path, &st) != 0, "deletes directory recursively");
	CHECK(stat(other, &st) == 0, "leaves sibling entries alone");
	CHECK(stat(pwned, &st) != 0, "delete does not execute filename as shell");

	SUITE("delete reports failure");

	path_join(path, sizeof(path), root, "locked");
	mkdir(path, 0755);
	path_join(other, sizeof(other), path, "inner");
	write_file(other, NULL);
	chmod(path, 0555);
	files_load_directory(&files, root);
	files.selected = index_of(&files, "locked");
	files.undo_top = 0;
	memset(&shell, 0, sizeof(shell));
	files_delete_permanent(&files, &shell);
	if (geteuid() != 0) {
		CHECK(files.undo_top == 0, "failed delete does not record an undo op");
		CHECK(strstr(shell.last_output + 1, "Delete failed") != NULL, "failed delete prints an error");
	}
	chmod(path, 0755);
	remove_tree(path);

	SUITE("shell rm with hostile filename");

	path_join(path, sizeof(path), root, EVIL_NAME);
	write_file(path, NULL);
	files_load_directory(&files, root);
	idx = index_of(&files, EVIL_NAME);
	memset(&shell, 0, sizeof(shell));
	shell.rm_confirm_mode = 1;
	snprintf(shell.current_input, sizeof(shell.current_input), "rm %d", files.entries[idx].index);
	execute_given(&shell, &files, NULL, NULL);
	CHECK(stat(path, &st) != 0, "confirmed rm removes the indexed entry");
	CHECK(stat(pwned, &st) != 0, "rm does not execute filename as shell");
	CHECK(strstr(shell.last_output + 1, "removed 1") != NULL, "rm reports what it removed");

	SUITE("sel expansion");

	path_join(path, sizeof(path), root, EVIL_NAME);
	write_file(path, NULL);
	files_load_directory(&files, root);
	select_named(&files, EVIL_NAME);
	memset(&shell, 0, sizeof(shell));
	snprintf(shell.current_input, sizeof(shell.current_input), "ls sel");
	execute_given(&shell, &files, NULL, NULL);
	CHECK(stat(pwned, &st) != 0, "sel expansion does not execute filename as shell");
	unlink(path);

	memset(long_name, 'n', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	for (int i = 0; i < 3; i++) {
		long_name[0] = (char)('a' + i);
		path_join(path, sizeof(path), root, long_name);
		write_file(path, NULL);
	}
	files_load_directory(&files, root);
	for (int i = 0; i < 3; i++) {
		long_name[0] = (char)('a' + i);
		select_named(&files, long_name);
	}
	memset(&shell, 0, sizeof(shell));
	snprintf(shell.current_input, sizeof(shell.current_input), "touch MARKER sel");
	execute_given(&shell, &files, NULL, NULL);
	path_join(path, sizeof(path), root, "MARKER");
	CHECK(stat(path, &st) != 0, "oversized sel expansion is not run truncated");
	CHECK(strstr(shell.last_output + 1, "too large") != NULL, "oversized sel expansion prints an error");

	if (old_cwd[0]) {
		chdir(old_cwd);
	}
	remove_tree(root);
}
