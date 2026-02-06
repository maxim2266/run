BIN       := run
CFILES    := $(BIN).c
CFLAGS    := -s -Os -std=c11 -Wall -Wextra

.PHONY: local clean-local static image delete-image test clean

# local build
local: $(BIN)

$(BIN): $(CFILES) version
	$(CC) $(CFLAGS) -DVER=$(shell ./version) -o $@ $(CFILES)
	chmod 0711 $@

clean-local:
	$(RM) $(BIN)

# build with static linking (used inside containers)
static: CFLAGS := -static $(CFLAGS)
static: $(BIN)

# docker image
DOCKERFILE := $(BIN).dockerfile
IMAGE      := $(BIN)-image

image:
	docker build -f $(DOCKERFILE) -t $(IMAGE) .

delete-image:
	IDS="$$(docker ps -aqf "ancestor=$(IMAGE)")"; [ -z "$$IDS" ] || docker rm -f "$$IDS"
	docker rmi -f $(IMAGE)

# testing
test: image
	./test-run $(IMAGE)

# cleanup
clean: clean-local delete-image
