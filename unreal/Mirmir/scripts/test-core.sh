#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp /tmp/mirmir-core-test.XXXXXX)
trap 'rm -f -- "$test_binary"' EXIT
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$project_dir/Source/Mirmir/Public" \
  "$project_dir/Tests/behavior_test.cpp" -o "$test_binary"
"$test_binary"
