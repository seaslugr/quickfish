#pragma once

#include <stddef.h>

#define KEY_SCOPE_FILES 1
#define KEY_SCOPE_PREVIEW 2
#define KEY_SCOPE_SHELL 4
#define KEY_SCOPE_PANES (KEY_SCOPE_FILES | KEY_SCOPE_PREVIEW)
#define KEY_SCOPE_ALL (KEY_SCOPE_FILES | KEY_SCOPE_PREVIEW | KEY_SCOPE_SHELL)

#define KEY_DOTDOT -2 ///< Pseudo key for typing ".." in the files pane, never returned by getch()

/* A key bound to a command line. help is NULL for duplicate keys (like \r next
   to \n) that should not get their own row in the help overlay. */
typedef struct {
	int key;
	int scope;
	const char *line;
	const char *help;
} Keybind;

const Keybind *keymap_find(int key, int scope);
int keymap_count();
const Keybind *keymap_at(int i);
void keymap_key_name(int key, char *out, size_t out_size);
