#define _XOPEN_SOURCE 700

#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fixture.h"

static int unlink_cb(const char *path, const struct stat *st, int type, struct FTW *ftwbuf);

static int unlink_cb(const char *path, const struct stat *st, int type, struct FTW *ftwbuf) {
	(void)st;
	(void)type;
	(void)ftwbuf;
	return remove(path);
}

void fixture_remove_tree(const char *path) {
	nftw(path, unlink_cb, 16, FTW_DEPTH | FTW_PHYS);
}

void fixture_write_file(const char *path, const char *data) {
	int fd;

	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0) { return; }
	if (data && write(fd, data, strlen(data)) < 0) {
		perror("write");
	}
	close(fd);
}

int fixture_index_of(FilesBuffer *files, const char *name) {
	for (int i = 0; i < files->entry_count; i++) {
		if (strcmp(files->entries[i].name, name) == 0) { return i; }
	}
	return -1;
}

/* A TUI with no curses windows, good for driving commands directly. */
TUI *fixture_tui_new(const char *dir) {
	TUI *tui;

	tui = calloc(1, sizeof(TUI));
	if (!tui) { return NULL; }
	tui->running = 1;
	tui->files_height = 20;
	tui->shell.history_index = -1;
	tui->confirm = fixture_confirm_yes;
	if (dir) { files_load_directory(&tui->files, dir); }
	return tui;
}

void fixture_tui_free(TUI *tui) {
	if (!tui) { return; }
	files_free(&tui->files);
	free(tui);
}

int fixture_confirm_yes(TUI *tui, const char *body, const char *warning, int danger) {
	(void)tui; (void)body; (void)warning; (void)danger;
	return 1;
}

int fixture_confirm_no(TUI *tui, const char *body, const char *warning, int danger) {
	(void)tui; (void)body; (void)warning; (void)danger;
	return 0;
}
