#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
root=$(pwd)
base=$1
mkdir -p "$base"
work=$(mktemp -d "$base/run.XXXXXX")
case "$work" in /*) ;; *) work="$root/$work" ;; esac
mkdir -p "$work/consumer"
cmake -S "$MAELYS_JSON_DIR" -B "$work/json" -DMAELYS_JSON_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/prefix" >/dev/null
cmake --build "$work/json" >/dev/null
cmake --install "$work/json" >/dev/null
cmake -S "$root" -B "$work/rpc" -DMAELYS_JSON_DIR="$MAELYS_JSON_DIR" \
    -DMAELYS_JSONRPC_WERROR=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/prefix" >/dev/null
cmake --build "$work/rpc" >/dev/null
ctest --test-dir "$work/rpc" --output-on-failure
cmake --install "$work/rpc" >/dev/null
cat >"$work/consumer/main.c" <<'C'
/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/jsonrpc.h>
#include <string.h>
int main(void) {
    maelys_json_document_t *doc = NULL;
    maelys_jsonrpc_message_t message;
    const char text[] = "{\"jsonrpc\":\"2.0\",\"method\":\"probe\"}";
    if (maelys_jsonrpc_parse(text, sizeof text - 1, NULL, &doc, NULL) != MAELYS_JSONRPC_OK) return 1;
    int failed = maelys_jsonrpc_classify(doc, NULL, &message) != MAELYS_JSONRPC_OK ||
        message.kind != MAELYS_JSONRPC_NOTIFICATION || strcmp(message.method, "probe");
    maelys_json_document_release(doc);
    return failed || strcmp(maelys_jsonrpc_version(), MAELYS_JSONRPC_VERSION_STRING);
}
C
cat >"$work/consumer/CMakeLists.txt" <<'CMAKE'
# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.16)
project(consumer C)
set(CMAKE_C_STANDARD 11)
find_package(maelys-jsonrpc CONFIG REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE maelys::jsonrpc)
CMAKE
cmake -S "$work/consumer" -B "$work/consumer/build" -DCMAKE_PREFIX_PATH="$work/prefix" >/dev/null
cmake --build "$work/consumer/build" >/dev/null
"$work/consumer/build/consumer"
pcdir="$work/prefix/lib/pkgconfig"
if test -d "$work/prefix/lib64/pkgconfig"; then pcdir="$pcdir:$work/prefix/lib64/pkgconfig"; fi
flags=$(PKG_CONFIG_PATH="$pcdir" pkg-config --cflags --libs --static maelys-jsonrpc)
# shellcheck disable=SC2086
${CC:-cc} -std=c11 "$work/consumer/main.c" $flags -o "$work/consumer/pc-consumer"
"$work/consumer/pc-consumer"
# maelys-json's Make/Homebrew installation supplies pkg-config without CMake.
make -C "$MAELYS_JSON_DIR" install BUILD="$work/json-make" PREFIX="$work/pkg-prefix" >/dev/null
PKG_CONFIG_PATH="$work/pkg-prefix/lib/pkgconfig" cmake -S "$root" -B "$work/rpc-pkg" \
    -DMAELYS_JSON_DIR="$work/not-a-checkout" -DCMAKE_INSTALL_PREFIX="$work/pkg-prefix" \
    -DMAELYS_JSONRPC_WERROR=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$work/rpc-pkg" >/dev/null
ctest --test-dir "$work/rpc-pkg" --output-on-failure
cmake --install "$work/rpc-pkg" >/dev/null
PKG_CONFIG_PATH="$work/pkg-prefix/lib/pkgconfig" cmake -S "$work/consumer" -B "$work/consumer/pkg-build" \
    -DCMAKE_PREFIX_PATH="$work/pkg-prefix" >/dev/null
cmake --build "$work/consumer/pkg-build" >/dev/null
"$work/consumer/pkg-build/consumer"
echo "cmake-check: Release/NDEBUG, find_package and pkg-config-only dependency/consumers OK"
