#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fs.h"
#include "util.h"

static int remove_walk(const char *path, const struct stat *st, int type, struct FTW *ftw);

int dir_is_empty(const char *path) {
	int found;
	struct dirent *de;
	DIR *d;

	found = 0;
	d = opendir(path);

	if (!d) { return 1; }

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') { continue; }
		found = 1;
		break;
	}

	closedir(d);
	return !found;
}

int complete_in_dir(const char *dir, const char *prefix, char *out, int out_size) {
	DIR *d;
	int plen;
	int found;
	struct dirent *de;

	d = opendir(dir);
	if (!d) { return 0; }

	plen = (int)strlen(prefix);
	found = 0;
	out[0] = '\0';

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') { continue; }
		if (plen > 0 && strncasecmp(de->d_name, prefix, plen) != 0) { continue; }
		if (found == 0) {
			strncpy(out, de->d_name, out_size - 1);
			out[out_size - 1] = '\0';
		} else {
			int i = 0;
			while (out[i] && de->d_name[i] &&
			       tolower((unsigned char)out[i]) == tolower((unsigned char)de->d_name[i])) {
				i++;
			}
			out[i] = '\0';
		}
		found++;
	}

	closedir(d);
	return found;
}

int path_is_at_or_below(const char *path, const char *root) {
	size_t root_len;

	root_len = strlen(root);
	if (strncmp(path, root, root_len) != 0) {
		return 0;
	}
	if (path[root_len] == '\0') {
		return 1;
	}
	if (strcmp(root, "/") == 0) {
		return 1;
	}
	return path[root_len] == '/';
}

int path_exists(const char *path) {
	struct stat st;

	return lstat(path, &st) == 0;
}

static int remove_walk(const char *path, const struct stat *st, int type, struct FTW *ftw) {
	(void)st; (void)type; (void)ftw;
	return remove(path);
}

int path_remove_tree(const char *path) {
	return nftw(path, remove_walk, 64, FTW_DEPTH | FTW_PHYS);
}

/* Moves src to dst without ever replacing an existing dst. Falls back to
   mv(1) when src and dst are on different filesystems. Sets errno on failure. */
int path_move(const char *src, const char *dst) {
	pid_t pid;
	int status;
	int fd;

	if (path_exists(dst)) {
		errno = EEXIST;
		return -1;
	}
	if (rename(src, dst) == 0) { return 0; }
	if (errno != EXDEV) { return -1; }

	pid = fork();
	if (pid < 0) { return -1; }
	if (pid == 0) {
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
		}
		execlp("mv", "mv", "--", src, dst, (char *)NULL);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0) { return -1; }
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		errno = EIO;
		return -1;
	}
	return 0;
}

int mkdir_p(const char *path) {
	char buf[PATH_MAX];

	str_copy(buf, sizeof(buf), path);
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/') { continue; }
		*p = '\0';
		if (mkdir(buf, 0700) != 0 && errno != EEXIST) { return -1; }
		*p = '/';
	}
	if (mkdir(buf, 0700) != 0 && errno != EEXIST) { return -1; }
	return 0;
}
