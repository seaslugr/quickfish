CC ?= gcc
override CFLAGS_DEV = -Wall -Wextra -g -std=gnu11 -MMD -MP
override CFLAGS_REL = -Wall -Wextra -O3 -std=gnu11 -MMD -MP
PKG_CONFIG ?= pkg-config
LDLIBS = $(shell $(PKG_CONFIG) --libs ncursesw panelw 2>/dev/null || echo -lpanelw -lncursesw -ltinfo)
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DOCDIR = $(PREFIX)/share/doc/quickfish
DEV_BUILDDIR = build
REL_BUILDDIR = build/release
TEST_BUILDDIR = build/test
SRC = $(wildcard src/*.c)
OBJ_DEV = $(SRC:src/%.c=$(DEV_BUILDDIR)/%.o)
OBJ_REL = $(SRC:src/%.c=$(REL_BUILDDIR)/%.o)
DEP_DEV = $(OBJ_DEV:.o=.d)
DEP_REL = $(OBJ_REL:.o=.d)
TEST_SRC = $(wildcard tests/*.c)
TEST_OBJ = $(TEST_SRC:tests/%.c=$(TEST_BUILDDIR)/%.o)
TEST_OBJ_SRC = $(filter-out $(DEV_BUILDDIR)/main.o, $(OBJ_DEV))
DEP_TEST = $(TEST_OBJ:.o=.d)
TOTAL = $(shell echo $$(($(words $(SRC)) + 2)))
WIDTH = $(shell echo $$(($(TOTAL)<10?1:$(TOTAL)<100?2:3)))

.PHONY: dev rel test clean install uninstall

dev: $(DEV_BUILDDIR)/quickfish

$(DEV_BUILDDIR):
	@mkdir -p $(DEV_BUILDDIR)

$(DEV_BUILDDIR)/%.o: src/%.c | $(DEV_BUILDDIR)
	@$(eval COUNT_DEV=$(shell echo $$(($(COUNT_DEV)+1))))
	@printf "[%*d/%d] Compiling %s (dev)\n" $(WIDTH) $(COUNT_DEV) $(TOTAL) $<
	@$(CC) $(CPPFLAGS) $(CFLAGS_DEV) $(CFLAGS) -c $< -o $@

$(DEV_BUILDDIR)/quickfish: $(OBJ_DEV)
	@$(eval COUNT_DEV=$(shell echo $$(($(COUNT_DEV)+1))))
	@printf "[%*d/%d] Linking %s (dev)\n" $(WIDTH) $(COUNT_DEV) $(TOTAL) $@
	@$(CC) $(OBJ_DEV) $(LDFLAGS) $(LDLIBS) -o $@
	@echo "Dev build complete. Binary located in $(DEV_BUILDDIR)/"

-include $(DEP_DEV)

rel: test $(REL_BUILDDIR)/quickfish

$(REL_BUILDDIR):
	@mkdir -p $(REL_BUILDDIR)

$(REL_BUILDDIR)/%.o: src/%.c | $(REL_BUILDDIR)
	@$(eval COUNT_REL=$(shell echo $$(($(COUNT_REL)+1))))
	@printf "[%*d/%d] Compiling %s (release)\n" $(WIDTH) $(COUNT_REL) $(TOTAL) $<
	@$(CC) $(CPPFLAGS) $(CFLAGS_REL) $(CFLAGS) -c $< -o $@

$(REL_BUILDDIR)/quickfish: $(OBJ_REL)
	@$(eval COUNT_REL=$(shell echo $$(($(COUNT_REL)+1))))
	@printf "[%*d/%d] Linking %s (release)\n" $(WIDTH) $(COUNT_REL) $(TOTAL) $@
	@$(CC) $(OBJ_REL) $(LDFLAGS) $(LDLIBS) -o $@
	@echo "Release build complete. Binary located in $(REL_BUILDDIR)/"

-include $(DEP_REL)

test: $(OBJ_DEV) $(TEST_BUILDDIR)/test_runner
	@$(TEST_BUILDDIR)/test_runner

$(TEST_BUILDDIR):
	@mkdir -p $(TEST_BUILDDIR)

$(TEST_BUILDDIR)/%.o: tests/%.c | $(TEST_BUILDDIR)
	@printf "[test] Compiling %s\n" $<
	@$(CC) $(CPPFLAGS) $(CFLAGS_DEV) $(CFLAGS) -Isrc -Itests -c $< -o $@

$(TEST_BUILDDIR)/test_runner: $(TEST_OBJ) $(TEST_OBJ_SRC)
	@printf "[test] Linking test runner\n"
	@$(CC) $(TEST_OBJ) $(TEST_OBJ_SRC) $(LDFLAGS) $(LDLIBS) -o $@

-include $(DEP_TEST)

install:
	install -Dm755 $(REL_BUILDDIR)/quickfish $(DESTDIR)$(BINDIR)/quickfish
	install -Dm644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -Dm644 docs/keybinds.md $(DESTDIR)$(DOCDIR)/keybinds.md

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/quickfish
	rm -rf $(DESTDIR)$(DOCDIR)

clean:
	rm -rf $(DEV_BUILDDIR) $(REL_BUILDDIR) $(TEST_BUILDDIR)
