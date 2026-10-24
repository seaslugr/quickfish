#pragma once

#define CMD_MAX_ARGS 64
#define CMD_LINE_MAX 2048

typedef struct {
	int argc;
	char quoted[CMD_MAX_ARGS];
	char *argv[CMD_MAX_ARGS];
	char buf[CMD_LINE_MAX];
} CmdArgs;

int cmdline_split(const char *line, CmdArgs *args);
void cmdline_shift(CmdArgs *args);
int cmdline_expand(const char *line, const char *sel, const char *cur, char *out, int out_size);
