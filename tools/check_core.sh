#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
code="$root/code"
build="$root/build/core_check"
mkdir -p "$build"
"${CC:-cc}" -c "$root/third_party/cjson/cJSON.c" -o "$build/cjson.o"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -pthread -I "$code" -I "$root" \
    "$root/tools/check_core.cpp" \
    "$code/platform/files.cpp" "$code/platform/bmp.cpp" "$code/platform/process.cpp" \
    "$code/platform/crypto.cpp" "$code/services/user_store.cpp" "$code/services/pin_store.cpp" \
    "$build/cjson.o" -lcrypto -o "$build/check_core"
"$build/check_core"
