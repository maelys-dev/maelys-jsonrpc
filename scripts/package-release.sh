#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
target=${1:?TARGET}
version=$(sed -n '1p' VERSION)
case "$(uname -s):$(uname -m):$target" in
 Linux:x86_64:linux-x86_64|Linux:aarch64:linux-arm64|Linux:arm64:linux-arm64|Darwin:arm64:macos-arm64) ;;
 *) echo "unsupported target or host mismatch: $target" >&2; exit 64 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/maelys-jsonrpc-package.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
make check BUILD="$work/check"
cmake -S . -B "$work/package" -DMAELYS_JSON_DIR="${MAELYS_JSON_DIR:-${MAELYS_DEPENDENCIES_DIR:-..}/maelys-json}" \
 -DMAELYS_JSONRPC_BUILD_TESTS=OFF -DMAELYS_JSONRPC_WERROR=ON -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build "$work/package"
DESTDIR="$work/stage" cmake --install "$work/package"
install -d "$work/stage/usr/local/share/licenses/maelys-jsonrpc"
install -m 0644 LICENSE LICENSING.md "$work/stage/usr/local/share/licenses/maelys-jsonrpc/"
mkdir -p dist
name="libmaelys-jsonrpc-$version-$target.tar.gz"
tar -czf "dist/$name" -C "$work/stage" .
if command -v sha256sum >/dev/null 2>&1; then (cd dist && sha256sum "$name" >"$name.sha256")
else (cd dist && shasum -a 256 "$name" >"$name.sha256"); fi
printf '%s\n' "dist/$name"
