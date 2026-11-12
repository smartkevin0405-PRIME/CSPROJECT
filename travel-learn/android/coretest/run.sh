#!/usr/bin/env bash
# 생성형 AI(Claude)의 도움을 받아 작성함.
# core 패키지 단위 시험. Android SDK 없이 JDK만으로 돈다. (core에 android.* 의존이 섞이면 여기서 컴파일이 실패한다.)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/out"
mkdir -p "$OUT"
javac -nowarn -encoding UTF-8 -d "$OUT" "$HERE"/../app/src/main/java/kr/doorboard/travellearn/core/*.java "$HERE"/*.java 2>&1 | grep -v '^Picked up' || true
java -Dfile.encoding=UTF-8 -Dstdout.encoding=UTF-8 -cp "$OUT" "${1:-CoreSelfTest}" "${@:2}" 2>&1 | grep -v '^Picked up'
exit "${PIPESTATUS[0]}"
