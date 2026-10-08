# SPDX-License-Identifier: MPL-2.0
CC ?= cc
CXX ?= c++
AR ?= ar
BUILD ?= build
PREFIX ?= /usr/local
DESTDIR ?=
MAELYS_JSON_DIR ?= $(if $(MAELYS_DEPENDENCIES_DIR),$(MAELYS_DEPENDENCIES_DIR)/maelys-json,../maelys-json)
MAELYS_RELEASE_DIR ?= $(if $(MAELYS_DEPENDENCIES_DIR),$(MAELYS_DEPENDENCIES_DIR)/maelys-release,../maelys-release)
PYTHON ?= python3
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CPPFLAGS ?=
WERROR ?=
VERSION := $(shell cat VERSION)
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Wsign-conversion -Wcast-qual -Wformat=2 -Wvla -Wundef
INCLUDES := -Iinclude -I$(MAELYS_JSON_DIR)/include
ALL_CFLAGS = $(CPPFLAGS) $(INCLUDES) -std=c11 $(WARNINGS) $(WERROR) $(CFLAGS)
AUDIT_FLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror $(CFLAGS)
JANSSON_CFLAGS = $(shell $(PKG_CONFIG) --cflags jansson)
JANSSON_LIBS = $(shell $(PKG_CONFIG) --libs jansson)
SOURCES := src/reader.c src/message.c src/calls.c src/result.c
HEADERS := $(wildcard include/maelys/*.h include/maelys/jsonrpc/*.h) src/internal.h
TEST_SOURCES := tests/main.c tests/test_reader.c tests/test_message.c tests/test_calls.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD)/obj/%.o)
LIBRARY := $(BUILD)/lib/libmaelys-jsonrpc.a
JSON_LIBRARY := $(BUILD)/dependency/libmaelys-json.a
TEST := $(BUILD)/bin/test-jsonrpc
CORPUS_READER := $(BUILD)/bin/test-corpus-reader
DIFF := $(BUILD)/bin/test-differential
PC := $(BUILD)/lib/pkgconfig/maelys-jsonrpc.pc
LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin/
FUZZ_CC ?= $(shell if test -x $(LLVM_PREFIX)clang; then printf '%s' $(LLVM_PREFIX)clang; else printf '%s' clang; fi)
FUZZ_TIME ?= 30
FUZZ_FLAGS := -O1 -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
FLAGS_STAMP := $(BUILD)/cflags.stamp
.DEFAULT_GOAL := all
.PHONY: all test check lint cmake-check asan ubsan asan-ubsan coverage fuzz fuzz-smoke audit corpus-audit install clean force

all: $(LIBRARY) $(PC)

$(FLAGS_STAMP): force
	@mkdir -p $(@D)
	@printf '%s\n' '$(ALL_CFLAGS)' | cmp -s - $@ 2>/dev/null || printf '%s\n' '$(ALL_CFLAGS)' >$@

$(BUILD)/obj/%.o: src/%.c $(HEADERS) $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c $< -o $@

$(LIBRARY): $(OBJECTS)
	@mkdir -p $(@D)
	ZERO_AR_DATE=1 $(AR) rcs $@ $^

$(JSON_LIBRARY): force
	cmake -S $(MAELYS_JSON_DIR) -B $(BUILD)/dependency -DMAELYS_JSON_BUILD_TESTS=OFF -DMAELYS_JSON_WERROR=ON -DCMAKE_C_COMPILER=$(CC) -DCMAKE_C_FLAGS='$(CFLAGS)'
	cmake --build $(BUILD)/dependency

$(TEST): $(TEST_SOURCES) tests/framework.h $(LIBRARY) $(JSON_LIBRARY) $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) $(TEST_SOURCES) $(LIBRARY) $(JSON_LIBRARY) -o $@

$(DIFF): tests/differential.c $(LIBRARY) $(JSON_LIBRARY) $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) $(JANSSON_CFLAGS) $< $(LIBRARY) $(JSON_LIBRARY) $(JANSSON_LIBS) -o $@

$(CORPUS_READER): tests/corpus_reader.c $(LIBRARY) $(JSON_LIBRARY) $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) $< $(LIBRARY) $(JSON_LIBRARY) -o $@

$(PC): pkgconfig/maelys-jsonrpc.pc.in VERSION
	@mkdir -p $(@D)
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' $< >$@

test: $(TEST) $(DIFF) $(CORPUS_READER)
	$(TEST)
	$(PYTHON) tests/test_capture.py
	$(PYTHON) tools/run-differential.py --executable $(DIFF) --reader $(CORPUS_READER) --corpus tests/corpus --report $(BUILD)/differential.json

check:
	$(MAKE) test WERROR=-Werror
	$(MAKE) lint
	$(MAKE) cmake-check

lint:
	sh tools/check-version.sh
	sh tools/check-spdx.sh
	sh tools/check-cmake-sources.sh $(SOURCES)
	$(PYTHON) tools/check-boundaries.py
	$(CC) $(ALL_CFLAGS) -Werror -fsyntax-only $(SOURCES)
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror $(INCLUDES) -fsyntax-only tests/header.cpp

cmake-check:
	MAELYS_JSON_DIR=$(MAELYS_JSON_DIR) sh tools/check-cmake-install.sh $(BUILD)/cmake-check

asan:
	$(MAKE) test BUILD=$(BUILD)-asan WERROR=-Werror CFLAGS='-O1 -g -fsanitize=address -fno-omit-frame-pointer'
ubsan:
	$(MAKE) test BUILD=$(BUILD)-ubsan WERROR=-Werror CFLAGS='-O1 -g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
asan-ubsan:
	$(MAKE) test BUILD=$(BUILD)-asan-ubsan WERROR=-Werror CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
coverage:
	$(MAKE) test BUILD=$(BUILD)-coverage WERROR=-Werror CFLAGS='-O0 -g -fprofile-instr-generate -fcoverage-mapping'
	sh tools/coverage-report.sh $(BUILD)-coverage

fuzz:
	@mkdir -p $(BUILD)/bin
	$(FUZZ_CC) $(INCLUDES) -std=c11 $(FUZZ_FLAGS) fuzz/fuzz_json_lines.c $(SOURCES) $(wildcard $(MAELYS_JSON_DIR)/src/*.c) -o $(BUILD)/bin/fuzz-json-lines
	$(FUZZ_CC) $(INCLUDES) -std=c11 $(FUZZ_FLAGS) fuzz/fuzz_calls.c $(SOURCES) $(wildcard $(MAELYS_JSON_DIR)/src/*.c) -o $(BUILD)/bin/fuzz-calls
fuzz-smoke: fuzz
	@mkdir -p $(BUILD)/fuzz-lines $(BUILD)/fuzz-calls
	$(BUILD)/bin/fuzz-json-lines -max_total_time=$(FUZZ_TIME) -timeout=2 -rss_limit_mb=1024 -artifact_prefix=$(BUILD)/ $(BUILD)/fuzz-lines fuzz/seeds/lines
	$(BUILD)/bin/fuzz-calls -max_total_time=$(FUZZ_TIME) -timeout=2 -rss_limit_mb=1024 -artifact_prefix=$(BUILD)/ $(BUILD)/fuzz-calls fuzz/seeds/calls

install: all
	install -d $(DESTDIR)$(PREFIX)/include/maelys/jsonrpc $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 0644 include/maelys/jsonrpc.h $(DESTDIR)$(PREFIX)/include/maelys/
	install -m 0644 include/maelys/jsonrpc/*.h $(DESTDIR)$(PREFIX)/include/maelys/jsonrpc/
	install -m 0644 $(LIBRARY) $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 $(PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/

clean:
	$(PYTHON) tools/clean-build.py $(BUILD)

$(BUILD)/audit-dependency: tests/audit/dependency.c $(JSON_LIBRARY) force
	$(CC) $(AUDIT_FLAGS) -I$(MAELYS_JSON_DIR)/include $(JANSSON_CFLAGS) \
		$< $(JSON_LIBRARY) $(JANSSON_LIBS) -o $@

$(BUILD)/audit-line: tests/audit/line.c $(JSON_LIBRARY) force
	$(CC) $(AUDIT_FLAGS) -I$(MAELYS_JSON_DIR)/include $(JANSSON_CFLAGS) \
		$< $(JSON_LIBRARY) $(JANSSON_LIBS) -o $@

audit: $(BUILD)/audit-dependency $(BUILD)/audit-line
	$(BUILD)/audit-dependency
	$(PYTHON) tests/audit/test_corpus.py --probe $(BUILD)/audit-line

corpus-audit: $(BUILD)/audit-line
	$(PYTHON) tools/audit-corpus.py --probe $(BUILD)/audit-line \
		--manifest tests/corpus/manifest.json --report $(BUILD)/corpus-audit.json

-include $(OBJECTS:.o=.d)
