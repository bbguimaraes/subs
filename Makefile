CPPFLAGS := -D _POSIX_C_SOURCE=200809L
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
	tests/util
BIN := subs $(TESTS)

all: $(BIN)
subs: \
	src/buffer.o \
	src/curses/curses.o \
	src/curses/lua/lua.o \
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
	src/main.o \
	src/subs.o \
	src/update.o \
	src/update_lbry.o \
	src/update_youtube.o \
	src/util.o \
	src/unix.o
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
tests/subs: \
	src/buffer.o \
	src/curses/curses.o \
	src/curses/lua/lua.o \
	src/curses/search.o \
	src/curses/source.o \
	src/curses/subs.o \
	src/curses/videos.o \
	src/curses/window/list.o \
	src/curses/window/list_search.o \
	src/curses/window/window.o \
	src/db.o \
	src/http.o \
	src/http_fake.o \
	src/log.o \
	src/lua.o \
	src/os.o \
	src/subs.o \
	src/update.o \
	src/update_lbry.o \
	src/update_youtube.o \
	src/util.o \
	src/unix.o \
	tests/common.o
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
