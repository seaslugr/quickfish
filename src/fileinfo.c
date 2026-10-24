#define _GNU_SOURCE

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "fileinfo.h"
#include "shell.h"
#include "util.h"

static int dir_size_walk(const char *path, const struct stat *st, int type, struct FTW *ftw);
static off_t compute_dir_size(const char *path);
static void stat_print(struct ShellBuffer *shell, const char *full, const char *name);

static off_t dir_size_total;

static int dir_size_walk(const char *path, const struct stat *st, int type, struct FTW *ftw) {
	(void)path; (void)type; (void)ftw;
	dir_size_total += st->st_blocks * 512;
	return 0;
}

static off_t compute_dir_size(const char *path) {
	dir_size_total = 0;
	nftw(path, dir_size_walk, 64, FTW_PHYS);
	return dir_size_total;
}

void files_cmd_stat(FilesBuffer *files, struct ShellBuffer *shell, const char *arg) {
	char full[PATH_MAX];
	DirEntry *e;
	DirEntry synthetic;
	const char *base;
	char *end;
	long idx;
	char msg[MAX_FILENAME + 32];

	if (!arg || arg[0] == '\0') {
		print_to_shell(shell, "stat: no file specified\n", 3);
		return;
	}

	e = NULL;
	memset(&synthetic, 0, sizeof(synthetic));

	if (arg[0] == '~') {
		const char *home = getenv("HOME");
		if (!home) { home = "/"; }
		if (arg[1] == '/' || arg[1] == '\0') {
			snprintf(full, sizeof full, "%s%s", home, arg + 1);
		} else {
			snprintf(full, sizeof full, "%s", arg);
		}
		base = strrchr(full, '/');
		str_copy(synthetic.name, sizeof(synthetic.name), base ? base + 1 : full);
		e = &synthetic;
	} else if (strchr(arg, '/')) {
		if (arg[0] == '/') {
			snprintf(full, sizeof full, "%s", arg);
		} else {
			path_join(full, sizeof full, files->cwd, arg);
		}
		base = strrchr(full, '/');
		str_copy(synthetic.name, sizeof(synthetic.name), base ? base + 1 : full);
		e = &synthetic;
	} else {
		idx = strtol(arg, &end, 10);
		if (*end == '\0') {
			for (int i = 0; i < files->entry_count; i++) {
				if (files->entries[i].index == (int)idx) { e = &files->entries[i]; break; }
			}
		} else {
			for (int i = 0; i < files->entry_count; i++) {
				if (strcasecmp(files->entries[i].name, arg) == 0) { e = &files->entries[i]; break; }
			}
		}

		if (!e) {
			snprintf(msg, sizeof msg, "stat: '%s' not found in current directory\n", arg);
			print_to_shell(shell, msg, 3);
			return;
		}
		path_join(full, sizeof full, files->cwd, e->name);
	}

	stat_print(shell, full, e->name);
}

void files_stat_entry(FilesBuffer *files, struct ShellBuffer *shell, int idx) {
	char full[PATH_MAX];

	if (idx < 0 || idx >= files->entry_count) { return; }
	path_join(full, sizeof(full), files->cwd, files->entries[idx].name);
	stat_print(shell, full, files->entries[idx].name);
}

static void stat_print(struct ShellBuffer *shell, const char *full, const char *name) {
	struct stat st;
	char perms[11];
	off_t size;
	char size_str[32];
	char time_str[32];
	struct tm *tm;
	char out[512];
	char msg[MAX_FILENAME + 32];

	if (stat(full, &st) != 0) {
		snprintf(msg, sizeof msg, "stat: could not stat '%s'\n", name);
		print_to_shell(shell, msg, SHELL_MSG_ERROR);
		return;
	}

	perms[0] = S_ISDIR(st.st_mode) ? 'd' : '-';
	perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
	perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
	perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
	perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
	perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
	perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
	perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
	perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
	perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
	perms[10] = '\0';

	size = S_ISDIR(st.st_mode) ? compute_dir_size(full) : st.st_size;

	if (size >= 1024 * 1024 * 1024) {
		snprintf(size_str, sizeof size_str, "%.1fG", (double)size / (1024 * 1024 * 1024));
	} else {
		format_size(size, size_str, sizeof(size_str));
	}

	tm = localtime(&st.st_mtime);
	strftime(time_str, sizeof time_str, "%Y-%m-%d %H:%M", tm);

	snprintf(out, sizeof out,
	    "name: %s\n"
	    "type: %s\n"
	    "perms: %s\n"
	    "size: %s\n"
	    "modified: %s\n"
	    "inode: %lu\n"
	    "links: %lu\n",
	    name,
	    S_ISDIR(st.st_mode) ? "directory" : "file",
	    perms,
	    size_str,
	    time_str,
	    (unsigned long)st.st_ino,
	    (unsigned long)st.st_nlink
	);

	print_to_shell(shell, out, SHELL_MSG_NORMAL);
}
