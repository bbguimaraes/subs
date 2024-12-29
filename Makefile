CPPFLAGS := -D _POSIX_C_SOURCE=200809L
CFLAGS := -std=c17 -O2 -g3 -Wall -Wextra -Wpedantic -Wconversion
OUTPUT_OPTION = -MMD -MP -o $@
LDLIBS = \
	$(shell pkg-config --libs libcjson) \
	$(shell pkg-config --libs libcurl) \
	$(shell pkg-config --libs sqlite3) \
	$(shell pkg-config --libs lua)

TESTS := \
	tests/buffer \
	tests/subs \
	tests/util
BIN := subs $(TESTS)

all: $(BIN)
subs: \
	src/buffer.o \
	src/db.o \
	src/http.o \
	src/log.o \
	src/lua.o \
	src/main.o \
	src/subs.o \
	src/update.o \
	src/update_lbry.o \
	src/util.o
	$(LINK.C) -o $@ $^ $(LDLIBS)
$(TESTS): CPPFLAGS := \
	$(CPPFLAGS) \
	-fsanitize=address,undefined -fstack-protector \
	-I src
tests/buffer: \
	src/buffer.o \
	src/log.o \
	tests/common.o
tests/subs: \
	src/buffer.o \
	src/db.o \
	src/http.o \
	src/http_fake.o \
	src/log.o \
	src/lua.o \
	src/os.o \
	src/subs.o \
	src/update.o \
	src/update_lbry.o \
	src/util.o \
	tests/common.o
tests/util: \
	src/log.o \
	tests/common.o

.PHONY: check clean
check: $(TESTS)
	for x in $(TESTS); do { echo "$$x" && ./"$$x"; } || exit; done
clean:
	rm -f $(BIN) src/*.[do] tests/*.[do]

-include $(wildcard src/*.d)
-include $(wildcard tests/*.d)
