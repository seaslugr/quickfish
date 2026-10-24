#pragma once

int dir_is_empty(const char *path);
int complete_in_dir(const char *dir, const char *prefix, char *out, int out_size);
int path_exists(const char *path);
int path_is_at_or_below(const char *path, const char *root);
int path_remove_tree(const char *path);
int path_move(const char *src, const char *dst);
int mkdir_p(const char *path);
