#!/bin/sh
# 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
# 호스트(PC)에서 라이브러리를 그대로 컴파일해 시험 프로그램 3개를 만든다.
#   ./build.sh            -> build/unit_tests, build/virtual_board, build/size_report
#   ./build.sh test       -> 빌드 후 단위 시험 실행
set -e
cd "$(dirname "$0")"
SRC=../../src
OUT=build
mkdir -p "$OUT"
CXX=${CXX:-g++}
FLAGS="-std=c++17 -Wall -Wextra -O1 -g -fsigned-char -Ishim -I$SRC $EXTRA"
LIB="$SRC/TravelLearn.cpp $SRC/TlStore.cpp $SRC/TlStats.cpp $SRC/TlHttp.cpp $SRC/TlJson.cpp $SRC/TlSha256.cpp shim/host.cpp"
$CXX $FLAGS -o "$OUT/unit_tests" tests/unit_tests.cpp $LIB -lpthread
$CXX $FLAGS -o "$OUT/virtual_board" tests/virtual_board.cpp $LIB
$CXX $FLAGS -o "$OUT/size_report" tests/size_report.cpp $LIB
echo "build ok: $OUT/unit_tests $OUT/virtual_board $OUT/size_report"
if [ "$1" = "test" ]; then
  "$OUT/unit_tests"
fi
