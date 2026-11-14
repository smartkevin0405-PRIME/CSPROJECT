#!/usr/bin/env bash
# 생성형 AI(Claude)의 도움을 받아 작성함.
# PC에서 돌릴 수 있는 자동 시험 전부를 한 번에 실행한다(Linux/macOS/WSL, g++·JDK·Python 3 필요).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export LC_ALL=C.UTF-8
FAIL=0
step() { echo; echo "=== $1 ==="; }

step "1/7 아두이노 모듈 단위 시험(호스트 빌드)"
( cd "$HERE/arduino/TravelLearn/extras/host" && ./build.sh >/dev/null && ./build/unit_tests | tail -4 ) || FAIL=1

step "2/7 앱 핵심 로직 단위 시험(JVM)"
( cd "$HERE/android/coretest" && ./run.sh CoreSelfTest | tail -2 ) || FAIL=1

step "3/7 종단 시험: 앱 통신 코드 ↔ 아두이노 모듈(호스트 빌드)"
( cd "$HERE/android/coretest" && ./run.sh E2eTest "아두이노 모듈" "$HERE/arduino/TravelLearn/extras/host/build/virtual_board" --port {port} --eeprom {state} --key 1={key} | tail -2 ) || FAIL=1

step "4/7 종단 시험: 앱 통신 코드 ↔ 파이썬 가상 현관판"
( cd "$HERE/android/coretest" && ./run.sh E2eTest "파이썬 가상 현관판" python3 -u "$HERE/tools/virtual_board.py" --port {port} --state {state} --key 1={key} | tail -2 ) || FAIL=1

step "5/7 시뮬레이터 자체 시험(파이썬 가상 폰 ↔ 가상 현관판)"
( cd "$HERE/tools" && python3 selftest.py | tail -2 ) || FAIL=1

step "6/7 현관판 조회·문 센서 통합 시험(호스트 빌드)"
"$HERE/../entrance-board/tests/run.sh" || FAIL=1

step "7/7 시험 스크립트의 컴파일 실패 처리"
"$HERE/android/coretest/test-runner.sh" || FAIL=1

echo
if [ "$FAIL" = 0 ]; then echo "전체 통과"; else echo "실패한 단계가 있음"; fi
exit "$FAIL"
