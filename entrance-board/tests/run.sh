#!/usr/bin/env bash
# 생성형 AI의 도움을 받아 작성함.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
for name in main_flow door_sensor json_str; do
  "${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$HERE/shim" "$HERE/$name.cpp" -o "$OUT/$name"
  "$OUT/$name"
done
