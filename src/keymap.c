#include <ncurses.h>
#include <stdio.h>

#include "keymap.h"

static const Keybind keymap[] = {
	{ 'j', KEY_SCOPE_FILES, "down", "move down" },
	{ KEY_DOWN, KEY_SCOPE_FILES, "down", "move down" },
	{ 'k', KEY_SCOPE_FILES, "up", "move up" },
	{ KEY_UP, KEY_SCOPE_FILES, "up", "move up" },
	{ 4, KEY_SCOPE_FILES, "page-down", "half page down" },
	{ 21, KEY_SCOPE_FILES, "page-up", "half page up" },
	{ 'h', KEY_SCOPE_FILES, "parent", "go to parent dir" },
	{ KEY_LEFT, KEY_SCOPE_FILES, "parent", "go to parent dir" },
	{ 'l', KEY_SCOPE_FILES, "open", "enter dir or open file" },
	{ KEY_RIGHT, KEY_SCOPE_FILES, "open", "enter dir or open file" },
	{ '\n', KEY_SCOPE_FILES, "open", "enter dir or open file" },
	{ '\r', KEY_SCOPE_FILES, "open", NULL },
	{ KEY_ENTER, KEY_SCOPE_FILES, "open", NULL },
	{ 'g', KEY_SCOPE_FILES, "goto", "go to path, <num>g jumps to index" },
	{ KEY_DOTDOT, KEY_SCOPE_FILES, "back", "previous directory" },

	{ 'r', KEY_SCOPE_FILES, "rename", "rename item" },
	{ 's', KEY_SCOPE_FILES, "select cur", "toggle-select item" },
	{ 'v', KEY_SCOPE_FILES, "visual", "sweep select mode" },
	{ 27, KEY_SCOPE_FILES, "visual off", NULL },
	{ 'd', KEY_SCOPE_FILES, "trash", "trash selection or item" },
	{ 'D', KEY_SCOPE_FILES, "delete", "permanently delete" },
	{ 'm', KEY_SCOPE_FILES, "mark cur", "mark item for move" },
	{ 'M', KEY_SCOPE_FILES, "mark sel|all", "mark selection (or all)" },
	{ 'p', KEY_SCOPE_FILES, "paste", "move marked items here" },
	{ 'P', KEY_SCOPE_FILES, "paste cur", "move into selected dir" },
	{ 3, KEY_SCOPE_ALL, "clear-marks", "clear move marks" },
	{ 'u', KEY_SCOPE_FILES, "undo", "undo" },
	{ 'U', KEY_SCOPE_FILES, "redo", "redo" },
	{ ':', KEY_SCOPE_PANES, "quickshell", "quickshell (inline command)" },

	{ 'j', KEY_SCOPE_PREVIEW, "scroll 1", NULL },
	{ KEY_DOWN, KEY_SCOPE_PREVIEW, "scroll 1", NULL },
	{ 'k', KEY_SCOPE_PREVIEW, "scroll -1", NULL },
	{ KEY_UP, KEY_SCOPE_PREVIEW, "scroll -1", NULL },
	{ 4, KEY_SCOPE_PREVIEW, "scroll half", NULL },
	{ 21, KEY_SCOPE_PREVIEW, "scroll -half", NULL },
	{ 0x10, KEY_SCOPE_PANES, "preview", "toggle preview pane" },
	{ 'p', KEY_SCOPE_PREVIEW, "preview off", "close preview (in preview)" },
	{ 'K', KEY_SCOPE_PANES, "focus files", "focus files pane" },
	{ KEY_SR, KEY_SCOPE_ALL, "focus files", "focus files pane" },
	{ 27, KEY_SCOPE_SHELL, "focus files", "focus files pane" },
	{ 'J', KEY_SCOPE_PANES, "focus shell", "focus shell pane" },
	{ KEY_SF, KEY_SCOPE_ALL, "focus shell", "focus shell pane" },
	{ 'L', KEY_SCOPE_PANES, "focus next", "focus next pane" },
	{ KEY_SRIGHT, KEY_SCOPE_ALL, "focus next", "focus next pane" },
	{ 'H', KEY_SCOPE_PANES, "focus prev", "focus previous pane" },
	{ KEY_SLEFT, KEY_SCOPE_ALL, "focus prev", "focus previous pane" },
	{ KEY_F(1), KEY_SCOPE_ALL, "help", "toggle this help" },
	{ '\f', KEY_SCOPE_ALL, "clear", "clear shell output" },
	{ 26, KEY_SCOPE_ALL, "suspend", "suspend (fg resumes)" },
};

const Keybind *keymap_find(int key, int scope) {
	for (size_t i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++) {
		if (keymap[i].key == key && (keymap[i].scope & scope)) { return &keymap[i]; }
	}
	return NULL;
}

int keymap_count() {
	return (int)(sizeof(keymap) / sizeof(keymap[0]));
}

const Keybind *keymap_at(int i) {
	return &keymap[i];
}

void keymap_key_name(int key, char *out, size_t out_size) {
	switch (key) {
	case KEY_DOTDOT: snprintf(out, out_size, ".."); return;
	case '\n': snprintf(out, out_size, "Enter"); return;
	case 27: snprintf(out, out_size, "Esc"); return;
	case KEY_UP: snprintf(out, out_size, "Up"); return;
	case KEY_DOWN: snprintf(out, out_size, "Down"); return;
	case KEY_LEFT: snprintf(out, out_size, "Left"); return;
	case KEY_RIGHT: snprintf(out, out_size, "Right"); return;
	case KEY_SR: snprintf(out, out_size, "S-Up"); return;
	case KEY_SF: snprintf(out, out_size, "S-Down"); return;
	case KEY_SLEFT: snprintf(out, out_size, "S-Left"); return;
	case KEY_SRIGHT: snprintf(out, out_size, "S-Right"); return;
	default: break;
	}
	if (key >= KEY_F0 && key <= KEY_F(63)) {
		snprintf(out, out_size, "F%d", key - KEY_F0);
	} else if (key >= 1 && key <= 26) {
		snprintf(out, out_size, "Ctrl+%c", 'A' + key - 1);
	} else if (key > ' ' && key < 127) {
		snprintf(out, out_size, "%c", key);
	} else {
		snprintf(out, out_size, "#%d", key);
	}
}
