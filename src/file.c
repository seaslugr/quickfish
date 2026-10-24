#define _GNU_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "file.h"
#include "fs.h"
#include "movereg.h"
#include "util.h"

static int entries_grow(FilesBuffer *files);
static int entry_cmp(const void *a, const void *b);


static int entries_grow(FilesBuffer *files) {
	DirEntry *grown;
	int cap;

	cap = files->entry_cap ? files->entry_cap * 2 : 256;
	grown = realloc(files->entries, (size_t)cap * sizeof(DirEntry));
	if (!grown) { return -1; }
	files->entries = grown;
	files->entry_cap = cap;
	return 0;
}

void files_load_directory(FilesBuffer *files, const char *path) {
	char resolved[PATH_MAX];
	const char *use;
	DIR *dir;
	struct dirent *de;
	char full[PATH_MAX];
	struct stat st;
	DirEntry *e;

	use = realpath(path, resolved) ? resolved : path;

	strncpy(files->cwd, use, PATH_MAX - 1);
	files->cwd[PATH_MAX - 1] = '\0';

	files->entry_count = 0;
	files->selected = 0;
	files->col_offset = 0;
	files->sel_count = 0;
	files->load_gen++;

	dir = opendir(files->cwd);
	if (!dir) { return; }

	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.') { continue; }

		path_join(full, sizeof(full), files->cwd, de->d_name);

		if (stat(full, &st) == -1) { continue; }
		if (files->entry_count == files->entry_cap && entries_grow(files) != 0) { break; }

		e = &files->entries[files->entry_count];
		snprintf(e->name, sizeof(e->name), "%s", de->d_name);
		e->marked = 0;
		e->in_move_reg = 0;
		e->type = S_ISDIR(st.st_mode) ? ENTRY_DIR : ENTRY_FILE;
		e->mode = st.st_mode;
		e->is_empty = (e->type == ENTRY_DIR) ? dir_is_empty(full) : 0;
		e->index = files->entry_count + 1;
		files->entry_count++;
	}
	closedir(dir);
	files_sort_entries(files);
	files_move_reindex(files);
}

void files_free(FilesBuffer *files) {
	for (int i = 0; i < files->move_reg.count; i++) {
		free(files->move_reg.paths[i]);
	}
	free(files->move_reg.paths);
	free(files->entries);
	files->move_reg = (MoveRegister){ 0 };
	files->entries = NULL;
	files->entry_cap = 0;
	files->entry_count = 0;
}

static int entry_cmp(const void *a, const void *b) {
	const DirEntry *ea;
	const DirEntry *eb;

	ea = (const DirEntry *)a;
	eb = (const DirEntry *)b;
	if (ea->type != eb->type) {
		return (ea->type == ENTRY_DIR) ? -1 : 1;
	}
	return strcasecmp(ea->name, eb->name);
}

void files_sort_entries(FilesBuffer *files) {
	qsort(files->entries, (size_t)files->entry_count, sizeof(DirEntry), entry_cmp);
	for (int i = 0; i < files->entry_count; i++) {
		files->entries[i].index = i + 1;
	}
}

void files_select_next(FilesBuffer *files) {
	if (files->selected < files->entry_count - 1) { files->selected++; }
}

void files_select_next_n(FilesBuffer *files, int n) {
	files->selected += n;
	if (files->selected >= files->entry_count) { files->selected = files->entry_count - 1; }
}

void files_select_prev(FilesBuffer *files) {
	if (files->selected > 0) { files->selected--; }
}

void files_select_prev_n(FilesBuffer *files, int n) {
	files->selected -= n;
	if (files->selected < 0) { files->selected = 0; }
}

void files_change_dir(FilesBuffer *files, const char *dirname) {
	char new_path[PATH_MAX];
	char return_to[MAX_FILENAME];
	char *slash;

	return_to[0] = '\0';

	if (strcmp(dirname, "..") == 0) {
		snprintf(new_path, sizeof(new_path), "%s", files->cwd);
		slash = strrchr(new_path, '/');
		if (slash && slash != new_path) {
			str_copy(return_to, sizeof(return_to), slash + 1);
			*slash = '\0';
		} else {
			str_copy(return_to, sizeof(return_to), new_path + 1);
			strcpy(new_path, "/");
		}
	} else if (strcmp(files->cwd, "/") == 0) {
		snprintf(new_path, PATH_MAX, "/%.*s", (int)(PATH_MAX - 2), dirname);
	} else {
		path_join(new_path, PATH_MAX, files->cwd, dirname);
	}

	files_load_directory(files, new_path);

	if (return_to[0] != '\0') {
		for (int i = 0; i < files->entry_count; i++) {
			if (strcmp(files->entries[i].name, return_to) == 0) {
				files->selected = i;
				break;
			}
		}
	}
}

int files_get_selected_path(FilesBuffer *files, char *out_path, size_t out_size) {
	DirEntry *e;
	int n;

	if (files->selected < 0 || files->selected >= files->entry_count) { return -1; }

	e = &files->entries[files->selected];

	if (strcmp(files->cwd, "/") == 0) {
		n = snprintf(out_path, out_size, "/%s", e->name);
	} else {
		n = snprintf(out_path, out_size, "%s/%s", files->cwd, e->name);
	}
	if (n < 0 || (size_t)n >= out_size) { return -1; }

	return (e->type == ENTRY_DIR) ? 0 : 1;
}

void refresh_files_buffer(FilesBuffer *files) {
	int saved_sel;
	int saved_offset;

	saved_sel = files->selected;
	saved_offset = files->col_offset;
	files_load_directory(files, files->cwd);
	files->selected = (saved_sel < files->entry_count) ? saved_sel : 0;
	files->col_offset = saved_offset;
}

void files_toggle_select(FilesBuffer *files, int idx) {
	if (idx < 0 || idx >= files->entry_count) { return; }
	if (files->entries[idx].marked) {
		files->entries[idx].marked = 0;
		files->sel_count--;
	} else {
		files->entries[idx].marked = 1;
		files->sel_count++;
	}
}

void files_select_add(FilesBuffer *files, int idx) {
	if (idx < 0 || idx >= files->entry_count || files->entries[idx].marked) { return; }
	files->entries[idx].marked = 1;
	files->sel_count++;
}

void files_clear_selection(FilesBuffer *files) {
	for (int i = 0; i < files->entry_count; i++) {
		files->entries[i].marked = 0;
	}
	files->sel_count = 0;
}

/* Writes the multi-selected names to out as shell-quoted, space-separated args.
   Returns the length written, or -1 if the selection does not fit. */
int files_build_sel_args(FilesBuffer *files, char *out, int out_size) {
	int written;

	if (out_size <= 0) { return -1; }
	written = 0;
	out[0] = '\0';
	for (int i = 0; i < files->entry_count; i++) {
		if (!files->entries[i].marked) { continue; }
		if (written > 0) {
			if (written + 1 >= out_size) { return -1; }
			out[written++] = ' ';
		}
		written = str_shell_quote(out, out_size, written, files->entries[i].name);
		if (written < 0) {
			out[0] = '\0';
			return -1;
		}
	}
	return written;
}

