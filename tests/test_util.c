#include <locale.h>

#include "test.h"
#include "../src/util.h"

void test_util() {
	char path[64];
	char size[32];
	char out[64];

	SUITE("path_join");

	path_join(path, sizeof(path), "/tmp", "alpha");
	CHECK_STR(path, "/tmp/alpha", "joins directory and name");

	path_join(path, 8, "/tmp", "alphabet");
	CHECK_STR(path, "/tmp/al", "truncates to fit destination");

	path_join(path, sizeof(path), "/", "etc");
	CHECK_STR(path, "/etc", "does not double the slash after the root");

	SUITE("format_size");

	format_size(999, size, sizeof(size));
	CHECK_STR(size, "999B", "formats byte-sized values");

	format_size(1536, size, sizeof(size));
	CHECK_STR(size, "1.5K", "formats kibibyte-sized values");

	format_size(2 * 1024 * 1024, size, sizeof(size));
	CHECK_STR(size, "2.0M", "formats mebibyte-sized values");

	SUITE("str_fit_cols");

	str_fit_cols(out, sizeof(out), "abc", 6);
	CHECK_STR(out, "abc   ", "pads short names to the column count");
	str_fit_cols(out, sizeof(out), "abcdefgh", 6);
	CHECK_STR(out, "abcd..", "ends overlong names with dots");
	str_fit_cols(out, sizeof(out), "a\nb", 4);
	CHECK_STR(out, "a?b ", "replaces control characters");
	if (setlocale(LC_CTYPE, "C.UTF-8")) {
		str_fit_cols(out, sizeof(out), "a\xc3\xa7\xc3\xa3o.txt", 9);
		CHECK_STR(out, "a\xc3\xa7\xc3\xa3o.txt ", "counts multibyte characters as one column");
		str_fit_cols(out, sizeof(out), "\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7", 4);
		CHECK_STR(out, "\xc3\xa7\xc3\xa7..", "never splits a multibyte character");
		str_fit_cols(out, sizeof(out), "\xe6\x97\xa5\xe6\x97\xa5x", 4);
		CHECK_STR(out, "\xe6\x97\xa5..", "counts wide characters as two columns");
		str_fit_cols(out, sizeof(out), "a\xff" "b", 4);
		CHECK_STR(out, "a?b ", "replaces invalid bytes");
		setlocale(LC_CTYPE, "C");
	}
}
