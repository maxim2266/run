BIN       := run
CFILES    := $(BIN).c
CFLAGS    := -s -Os -std=c11 -Wall -Wextra
THIS_FILE := $(lastword $(MAKEFILE_LIST))

# CC := musl-gcc -static
# CC := clang

.PHONY: all clean static

.DELETE_ON_ERROR:

# local build
all: .gitignore $(BIN)

$(BIN): $(CFILES) version
	$(CC) $(CFLAGS) -DVER=$(shell ./version) -o $@ $(CFILES)
	chmod 0711 $@

clean:
	$(RM) $(BIN)

# build with static linking
static: CFLAGS := -static $(CFLAGS)
static: $(BIN)

# other bits
.gitignore: $(THIS_FILE)
	echo '$(BIN)' > .gitignore
