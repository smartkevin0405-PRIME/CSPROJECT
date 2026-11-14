#!/usr/bin/env bash
# 생성형 AI의 도움을 받아 작성함. 이전 클래스가 있어도 컴파일 실패를 성공으로 오인하지 않아야 한다.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/coretest"
cp "$HERE/run.sh" "$HERE/CoreSelfTest.java" "$TMP/coretest/"
cp -R "$HERE/../app" "$TMP/app"
"$TMP/coretest/run.sh" > "$TMP/warm.log" 2>&1
test -f "$TMP/coretest/out/CoreSelfTest.class"
printf 'deliberately invalid Java;\n' > "$TMP/coretest/Broken.java"
if "$TMP/coretest/run.sh" > "$TMP/broken.log" 2>&1; then
  cat "$TMP/broken.log"
  echo "컴파일 실패를 성공으로 처리함" >&2
  exit 1
fi
if grep -q 'PASS ' "$TMP/broken.log"; then
  echo "컴파일 실패 후 이전 시험을 실행함" >&2
  exit 1
fi
echo "컴파일 실패 시 즉시 중단·이전 클래스 실행 방지 시험 통과"
