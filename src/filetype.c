#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "colors.h"
#include "filetype.h"

static int ext_color_normal(const char *name);
static int to_selected_pair(int normal_pair);

static int ext_color_normal(const char *name) {
	const char *base;
	const char *dot;
	const char *ext;

	base = strrchr(name, '/');
	base = base ? base + 1 : name;

	if (!strcasecmp(base, "Makefile") || !strcasecmp(base, "makefile") ||
	    !strcasecmp(base, "GNUmakefile") || !strcasecmp(base, "Rakefile") ||
	    !strcasecmp(base, "Justfile") || !strcasecmp(base, "justfile") ||
	    !strcasecmp(base, "Dockerfile") || !strcasecmp(base, "dockerfile") ||
	    !strcasecmp(base, "Containerfile") ||
	    !strcasecmp(base, "CMakeLists.txt") ||
	    !strcasecmp(base, "meson.build") || !strcasecmp(base, "BUILD") ||
	    !strcasecmp(base, "BUILD.bazel") || !strcasecmp(base, "WORKSPACE") ||
	    !strcasecmp(base, "Vagrantfile") || !strcasecmp(base, "Guardfile") ||
	    !strcasecmp(base, "Gemfile") || !strcasecmp(base, "Podfile")) {
		return CP_EXT_CODE;
	}

	if (!strcasecmp(base, "README") || !strcasecmp(base, "CHANGELOG") ||
	    !strcasecmp(base, "CHANGES") || !strcasecmp(base, "AUTHORS") ||
	    !strcasecmp(base, "CONTRIBUTORS") || !strcasecmp(base, "COPYING") ||
	    !strcasecmp(base, "LICENSE") || !strcasecmp(base, "LICENCE") ||
	    !strcasecmp(base, "NOTICE") || !strcasecmp(base, "PATENTS") ||
	    !strcasecmp(base, "TODO") || !strcasecmp(base, "HACKING")) {
		return CP_EXT_DOC;
	}

	if (!strcasecmp(base, ".gitignore") || !strcasecmp(base, ".gitattributes") ||
	    !strcasecmp(base, ".gitmodules") || !strcasecmp(base, ".editorconfig") ||
	    !strcasecmp(base, ".env") || !strcasecmp(base, ".envrc") ||
	    !strcasecmp(base, ".npmrc") || !strcasecmp(base, ".yarnrc") ||
	    !strcasecmp(base, ".babelrc") || !strcasecmp(base, ".eslintrc") ||
	    !strcasecmp(base, ".prettierrc") || !strcasecmp(base, ".stylelintrc") ||
	    !strcasecmp(base, ".dockerignore") || !strcasecmp(base, ".htaccess") ||
	    !strcasecmp(base, "Pipfile") || !strcasecmp(base, "Procfile") ||
	    !strcasecmp(base, "requirements.txt") || !strcasecmp(base, "go.sum") ||
	    !strcasecmp(base, "go.mod") || !strcasecmp(base, "Cargo.lock") ||
	    !strcasecmp(base, "package-lock.json") || !strcasecmp(base, "yarn.lock") ||
	    !strcasecmp(base, "composer.lock") || !strcasecmp(base, "Gemfile.lock")) {
		return CP_EXT_DATA;
	}

	dot = strrchr(base, '.');
	if (!dot || dot == base) { return CP_FILE; }
	ext = dot + 1;

	if (!strcasecmp(ext, "png") || !strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg") ||
	    !strcasecmp(ext, "gif") || !strcasecmp(ext, "bmp") || !strcasecmp(ext, "tiff") ||
	    !strcasecmp(ext, "tif") || !strcasecmp(ext, "webp") || !strcasecmp(ext, "svg") ||
	    !strcasecmp(ext, "ico") || !strcasecmp(ext, "heic") || !strcasecmp(ext, "avif")) {
		return CP_EXT_IMAGE;
	}

	if (!strcasecmp(ext, "mp4") || !strcasecmp(ext, "mkv") || !strcasecmp(ext, "avi") ||
	    !strcasecmp(ext, "mov") || !strcasecmp(ext, "wmv") || !strcasecmp(ext, "flv") ||
	    !strcasecmp(ext, "webm") || !strcasecmp(ext, "m4v") || !strcasecmp(ext, "ts") ||
	    !strcasecmp(ext, "mpg") || !strcasecmp(ext, "mpeg")) {
		return CP_EXT_VIDEO;
	}

	if (!strcasecmp(ext, "mp3") || !strcasecmp(ext, "flac") || !strcasecmp(ext, "ogg") ||
	    !strcasecmp(ext, "wav") || !strcasecmp(ext, "aac") || !strcasecmp(ext, "m4a") ||
	    !strcasecmp(ext, "opus") || !strcasecmp(ext, "wma") || !strcasecmp(ext, "aiff")) {
		return CP_EXT_AUDIO;
	}

	if (!strcasecmp(ext, "zip") || !strcasecmp(ext, "tar") || !strcasecmp(ext, "gz") ||
	    !strcasecmp(ext, "bz2") || !strcasecmp(ext, "xz") || !strcasecmp(ext, "zst") ||
	    !strcasecmp(ext, "7z") || !strcasecmp(ext, "rar") || !strcasecmp(ext, "deb") ||
	    !strcasecmp(ext, "rpm") || !strcasecmp(ext, "pkg") || !strcasecmp(ext, "dmg") ||
	    !strcasecmp(ext, "iso") || !strcasecmp(ext, "tgz") || !strcasecmp(ext, "tbz2")) {
		return CP_EXT_ARCHIVE;
	}

	if (!strcasecmp(ext, "c") || !strcasecmp(ext, "h") || !strcasecmp(ext, "cc") ||
	    !strcasecmp(ext, "cpp") || !strcasecmp(ext, "cxx") || !strcasecmp(ext, "hh") ||
	    !strcasecmp(ext, "hpp") || !strcasecmp(ext, "rs") || !strcasecmp(ext, "go") ||
	    !strcasecmp(ext, "py") || !strcasecmp(ext, "rb") || !strcasecmp(ext, "java") ||
	    !strcasecmp(ext, "js") || !strcasecmp(ext, "ts") || !strcasecmp(ext, "jsx") ||
	    !strcasecmp(ext, "tsx") || !strcasecmp(ext, "lua") || !strcasecmp(ext, "zig") ||
	    !strcasecmp(ext, "swift") || !strcasecmp(ext, "kt") || !strcasecmp(ext, "cs") ||
	    !strcasecmp(ext, "php") || !strcasecmp(ext, "pl") || !strcasecmp(ext, "hs") ||
	    !strcasecmp(ext, "el") || !strcasecmp(ext, "vim") || !strcasecmp(ext, "S") ||
	    !strcasecmp(ext, "asm") || !strcasecmp(ext, "ex") || !strcasecmp(ext, "exs") ||
	    !strcasecmp(ext, "clj") || !strcasecmp(ext, "scala") || !strcasecmp(ext, "ml")) {
		return CP_EXT_CODE;
	}

	if (!strcasecmp(ext, "sh") || !strcasecmp(ext, "bash") || !strcasecmp(ext, "zsh") ||
	    !strcasecmp(ext, "fish") || !strcasecmp(ext, "ksh") || !strcasecmp(ext, "ps1") ||
	    !strcasecmp(ext, "bat") || !strcasecmp(ext, "cmd") || !strcasecmp(ext, "out") ||
	    !strcasecmp(ext, "run") || !strcasecmp(ext, "AppImage")) {
		return CP_EXT_EXEC;
	}

	if (!strcasecmp(ext, "pdf") || !strcasecmp(ext, "doc") || !strcasecmp(ext, "docx") ||
	    !strcasecmp(ext, "odt") || !strcasecmp(ext, "rtf") || !strcasecmp(ext, "tex") ||
	    !strcasecmp(ext, "md") || !strcasecmp(ext, "txt") || !strcasecmp(ext, "rst") ||
	    !strcasecmp(ext, "org") || !strcasecmp(ext, "epub") || !strcasecmp(ext, "mobi") ||
	    !strcasecmp(ext, "xls") || !strcasecmp(ext, "xlsx") || !strcasecmp(ext, "ods") ||
	    !strcasecmp(ext, "ppt") || !strcasecmp(ext, "pptx") || !strcasecmp(ext, "odp")) {
		return CP_EXT_DOC;
	}

	if (!strcasecmp(ext, "json") || !strcasecmp(ext, "yaml") || !strcasecmp(ext, "yml") ||
	    !strcasecmp(ext, "toml") || !strcasecmp(ext, "ini") || !strcasecmp(ext, "cfg") ||
	    !strcasecmp(ext, "conf") || !strcasecmp(ext, "xml") || !strcasecmp(ext, "csv") ||
	    !strcasecmp(ext, "tsv") || !strcasecmp(ext, "sql") || !strcasecmp(ext, "db") ||
	    !strcasecmp(ext, "sqlite") || !strcasecmp(ext, "env") || !strcasecmp(ext, "lock") ||
	    !strcasecmp(ext, "log") || !strcasecmp(ext, "pid")) {
		return CP_EXT_DATA;
	}

	return CP_FILE;
}

static int to_selected_pair(int normal_pair) {
	switch (normal_pair) {
	case CP_EXT_IMAGE:   return CP_SEL_IMAGE;
	case CP_EXT_VIDEO:   return CP_SEL_VIDEO;
	case CP_EXT_AUDIO:   return CP_SEL_AUDIO;
	case CP_EXT_ARCHIVE: return CP_SEL_ARCHIVE;
	case CP_EXT_CODE:    return CP_SEL_CODE;
	case CP_EXT_DOC:     return CP_SEL_DOC;
	case CP_EXT_EXEC:    return CP_SEL_EXEC;
	case CP_EXT_DATA:    return CP_SEL_DATA;
	default:             return CP_SEL_FILE;
	}
}

int entry_color_pair(const DirEntry *e, int selected) {
	int normal;

	if (e->type == ENTRY_DIR) {
		return selected ? CP_SEL_DIR : (e->is_empty ? CP_DIR_EMPTY : CP_DIR_FULL);
	}
	normal = ext_color_normal(e->name);
	if (normal == CP_FILE && (e->mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
		return selected ? CP_SEL_EXEC : CP_EXT_EXEC;
	}
	return selected ? to_selected_pair(normal) : normal;
}
