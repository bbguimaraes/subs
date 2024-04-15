OS ?= unix

CPPFLAGS := -D _POSIX_C_SOURCE=200809L
ifeq ($(OS),unix)
CPPFLAGS += -D SUBS_HAS_ALLOCA
endif
CFLAGS := -std=c17 -O2 -g3 -Wall -Wextra -Wpedantic -Wconversion
OUTPUT_OPTION = -MMD -MP -o $@
LDLIBS = \
	$(shell pkg-config --libs menu form panel ncurses) \
	$(shell pkg-config --libs libcjson) \
	$(shell pkg-config --libs libcurl) \
	$(shell pkg-config --libs sqlite3) \
	$(shell pkg-config --libs lua)

TESTS := \
	tests/buffer \
	tests/curses \
	tests/subs \
	tests/update \
	tests/util
BIN := subs $(TESTS)

SUBS_OBJ := \
	src/buffer.o \
	src/curses/curses.o \
	src/curses/form.o \
	src/curses/input.o \
	src/curses/lua/lua.o \
	src/curses/menu.o \
	src/curses/message.o \
	src/curses/search.o \
	src/curses/source.o \
	src/curses/subs.o \
	src/curses/videos.o \
	src/curses/window/list.o \
	src/curses/window/list_search.o \
	src/curses/window/window.o \
	src/db.o \
	src/http.o \
	src/log.o \
	src/lua.o \
	src/subs.o \
	src/task.o \
	src/update.o \
	src/update_lbry.o \
	src/update_youtube.o \
	src/util.o \
	src/unix.o

all: $(BIN)
subs: $(SUBS_OBJ) src/main.o
	$(LINK.C) -o $@ $^ $(LDLIBS)
$(TESTS): CPPFLAGS := \
	$(CPPFLAGS) \
	-fsanitize=address,undefined -fstack-protector \
	-I src
tests/buffer: \
	src/buffer.o \
	src/log.o \
	tests/common.o
tests/curses: \
	src/log.o \
	src/util.o \
	src/curses/window/list.o \
	src/curses/window/window.o
tests/subs: $(SUBS_OBJ) src/http_fake.o tests/common.o
tests/update: $(SUBS_OBJ) src/http_fake.o tests/common.o
tests/util: \
	src/log.o \
	tests/common.o

.PHONY: check clean
check: $(TESTS)
	for x in $(TESTS); do { echo "$$x" && ./"$$x"; } || exit; done
clean:
	rm -f \
		$(BIN) src/*.[do] \
		src/curses/*.[do] src/curses/lua/*.[do] src/curses/window/*.[do] \
		tests/*.[do]

-include $(wildcard src/*.d)
-include $(wildcard src/curses/*.d)
-include $(wildcard tests/*.d)
