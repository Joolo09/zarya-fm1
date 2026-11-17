# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
CORE := $(wildcard core/*.c)
HDRS := $(wildcard core/*.h)
WARN := -std=c99 -Wall -Wextra -Wshadow -Werror
.PHONY: all test wasm schema clean
all: test wasm schema
core/zr_tables.c: tools/gen_tables.py
	python3 tools/gen_tables.py
build/test: $(CORE) $(HDRS) host/test.c
	@mkdir -p build
	gcc $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(CORE) host/test.c -lm -o $@
test: build/test
	./build/test
schema: build/test
	./build/test --schema web/src/core/schema.json
# Freestanding WebAssembly build of the same core (the web editor's simulator).
wasm: $(CORE) $(HDRS) wasm/glue.c
	@mkdir -p web/src/assets
	clang --target=wasm32 -std=c99 -O2 -ffreestanding -fno-builtin -nostdlib -Wall -Wextra \
	  -Wl,--no-entry -fvisibility=hidden -Wl,--export-dynamic -Wl,-z,stack-size=65536 -Wl,--initial-memory=2097152 \
	  $(CORE) wasm/glue.c -o web/src/assets/zarya.wasm
clean:
	rm -rf build
