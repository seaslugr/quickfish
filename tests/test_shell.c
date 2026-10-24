#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fixture.h"
#include "../src/shell.h"
#include "../src/util.h"

static ShellBuffer shell;

void test_shell() {
	char root[] = "/tmp/quickfish-test-shell-XXXXXX";
	char path[PATH_MAX];
	char msg[64];

	SUITE("shell_add_history");

	memset(&shell, 0, sizeof(shell));
	shell_add_history(&shell, "ls");
	shell_add_history(&shell, "ls");
	shell_add_history(&shell, "pwd");
	CHECK(shell.history_count == 2, "deduplicates consecutive history entries");
	CHECK_STR(shell.history[0], "ls", "stores first history entry");
	CHECK_STR(shell.history[1], "pwd", "stores second history entry");

	SUITE("shell_handle_char");

	memset(&shell, 0, sizeof(shell));
	shell_handle_char(&shell, 'a');
	shell_handle_char(&shell, 'c');
	shell_handle_char(&shell, KEY_LEFT);
	shell_handle_char(&shell, 'b');
	CHECK_STR(shell.current_input, "abc", "inserts at cursor position");
	CHECK(shell.input_pos == 2, "tracks cursor after insertion");

	shell_handle_char(&shell, KEY_BACKSPACE);
	CHECK_STR(shell.current_input, "ac", "backspace removes previous character");
	CHECK(shell.input_pos == 1, "backspace updates cursor");

	SUITE("shell_tab_complete");

	CHECK(mkdtemp(root) != NULL, "creates shell temp directory");
	path_join(path, sizeof(path), root, "alpha");
	mkdir(path, 0755);
	path_join(path, sizeof(path), root, "alpine.txt");
	fixture_write_file(path, NULL);

	memset(&shell, 0, sizeof(shell));
	snprintf(shell.current_input, sizeof(shell.current_input), "al");
	shell.input_pos = 2;
	shell_tab_complete(&shell, root);
	CHECK_STR(shell.current_input, "alp", "tab-completes to common prefix");

	SUITE("jumplist");

	memset(&shell, 0, sizeof(shell));
	for (int i = 0; i < DIR_JUMPLIST_MAX + 3; i++) {
		snprintf(path, sizeof(path), "/dir%d", i);
		shell_push_dir(&shell, path);
	}
	snprintf(path, sizeof(path), "/dir%d", DIR_JUMPLIST_MAX + 2);
	CHECK(shell.dir_jumplist_count == DIR_JUMPLIST_MAX, "jumplist stays at its maximum size");
	CHECK_STR(shell.dir_jumplist[DIR_JUMPLIST_MAX - 1], path, "full jumplist keeps the newest entry");
	CHECK_STR(shell.dir_jumplist[0], "/dir3", "full jumplist drops the oldest entries");
	CHECK(shell_pop_dir(&shell, path, sizeof(path)) == 1, "pop returns the newest directory");
	snprintf(msg, sizeof(msg), "/dir%d", DIR_JUMPLIST_MAX + 2);
	CHECK_STR(path, msg, "pop hands back the most recent push");
	memset(&shell, 0, sizeof(shell));
	CHECK(shell_pop_dir(&shell, path, sizeof(path)) == 0, "pop on an empty jumplist reports nothing");

	SUITE("output log");

	memset(&shell, 0, sizeof(shell));
	print_to_shell(&shell, "first\n", SHELL_MSG_NORMAL);
	print_to_shell(&shell, "second\nline two", SHELL_MSG_ERROR);
	CHECK(strstr(shell.last_output, "first") != NULL, "earlier messages are kept");
	CHECK(strstr(shell.last_output, "\003ERROR: second\nline two\n") != NULL, "messages are typed and newline terminated");

	for (int i = 0; i < 5000; i++) {
		snprintf(msg, sizeof(msg), "message number %d\n", i);
		print_to_shell(&shell, msg, SHELL_MSG_NORMAL);
	}
	CHECK(shell.output_len < SHELL_LOG_MAX, "log stays within its capacity");
	CHECK(shell.output_len == (int)strlen(shell.last_output), "log length tracks its contents");
	CHECK(strstr(shell.last_output, "first") == NULL, "full log drops the oldest messages");
	CHECK(strstr(shell.last_output, "message number 4999\n") != NULL, "full log keeps the newest message");
	CHECK(shell.last_output[0] == SHELL_MSG_NORMAL || shell.last_output[0] == 'm', "full log is trimmed at a line boundary");

	shell_clear_output(&shell);
	CHECK(shell.output_len == 0 && shell.last_output[0] == '\0', "clear empties the log");

	shell_run(&shell, "echo hello", root);
	CHECK(strstr(shell.last_output, "\004$ echo hello\n\001hello\n") != NULL, "system commands are logged with their output");
	shell_run(&shell, "false", root);
	CHECK(strstr(shell.last_output, "WARN: exit status 1") != NULL, "failing commands log their exit status");

	fixture_remove_tree(root);
}
