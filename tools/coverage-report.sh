#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
work=$1
llvm_prefix=${LLVM_PREFIX:-/opt/homebrew/opt/llvm/bin/}
if ! test -x "${llvm_prefix}llvm-profdata"; then llvm_prefix=; fi
LLVM_PROFILE_FILE="$work/test.profraw" "$work/bin/test-jsonrpc" >/dev/null
"${llvm_prefix}llvm-profdata" merge -sparse "$work/test.profraw" -o "$work/test.profdata"
"${llvm_prefix}llvm-cov" report "$work/bin/test-jsonrpc" -instr-profile="$work/test.profdata" src/*.c
"${llvm_prefix}llvm-cov" export "$work/bin/test-jsonrpc" -instr-profile="$work/test.profdata" src/*.c >"$work/coverage.json"
