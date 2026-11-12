#!/usr/bin/env bash
# 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
# arduino-cli 없이, 공식 UNO R4 코어 소스와 ARM GCC로 스케치를 컴파일·링크해 크기를 재는 스크립트.
# (개발 환경에서 arduino-cli와 보드 패키지를 내려받을 수 없어서 만든 대체 경로다.
#  평소에는 Arduino IDE나 `arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi`를 쓰면 된다.)
#
# 준비물(우분투 기준):
#   sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
#   git clone https://github.com/arduino/ArduinoCore-renesas core
#   git clone https://github.com/arduino/ArduinoCore-API                   # core/cores/arduino/api 링크가 가리키는 곳
#   git clone https://github.com/arduino/tinyusb
#   git clone https://github.com/arduino/uno-r4-wifi-usb-bridge bridge
#   mkdir -p core/extras/uno-r4-wifi-usb-bridge/UNOR4USBBridge
#   cp bridge/UNOR4USBBridge/commands.h core/extras/uno-r4-wifi-usb-bridge/UNOR4USBBridge/
# 위 네 폴더가 놓인 곳을 R4ROOT로 넘긴다.
#
# 사용: R4ROOT=/path build_real.sh <스케치.cpp> <이름> [1|0: TravelLearn 포함 여부]
#   스케치는 .ino 맨 앞에 `#include <Arduino.h>`를 붙여 .cpp로 만든 것.
# 주의: 공식 빌드는 GCC 7.2.1을 쓰고 여기서는 배포판의 GCC(13.x)를 쓴다. 크기는 조금 다를 수 있다.
#       USB 직렬(cores/arduino/usb)과 WiFiSSLClient.cpp는 뺀다(GCC 13에서 공식 소스가 컴파일되지 않음).
set -u
R="${R4ROOT:?R4ROOT 필요}"; P="$R/core"; B="$R/b_$2"; mkdir -p "$B/core" "$B/lib" "$B/sk"
L="$(cd "$(dirname "$0")/../../src" && pwd)"
DEFS="-DF_CPU=48000000 -DNO_USB -DBACKTRACE_SUPPORT -DARDUINO_UNOR4_WIFI -DARDUINO=10607 -DARDUINO_UNOWIFIR4 -DARDUINO_ARCH_RENESAS_UNO -DARDUINO_ARCH_RENESAS -DARDUINO_FSP -D_XOPEN_SOURCE=700 -mthumb -DCFG_TUSB_MCU=OPT_MCU_RAXXX"
FSPD=$(grep -v '^-W' "$P/variants/UNOWIFIR4/defines.txt" | tr '\n' ' ')
MC="-mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16"
INC="${EXTRA_INC:-} -I$P/cores/arduino/tinyusb -I$R/tinyusb/src -I$P/cores/arduino/api/deprecated -I$P/cores/arduino/api/deprecated-avr-comp -I$P/cores/arduino -I$P/variants/UNOWIFIR4 -I$P/libraries/WiFiS3/src -I$P/libraries/EEPROM/src -I$P/libraries/BlockDevices -I$L -iprefix$P @$P/variants/UNOWIFIR4/includes.txt"
CXXF="-c -w -Os -g3 -fno-use-cxa-atexit -fno-rtti -fno-exceptions -nostdlib -std=gnu++17 $MC -fsigned-char -ffunction-sections -fdata-sections -fmessage-length=0 -fno-builtin $DEFS $FSPD $INC"
CF="-c -w -Os -g3 -nostdlib -std=gnu11 $MC -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-builtin $DEFS $FSPD $INC"
SF="-c -g -x assembler-with-cpp -Os $MC -fsigned-char -ffunction-sections -fdata-sections $DEFS $INC"
export CXXF CF SF
cc1() { src="$1"; out="$2/$(echo "$src" | md5sum | cut -c1-10)_$(basename "$src").o"; [ -f "$out" ] && return 0
  case "$src" in *.cpp) arm-none-eabi-g++ $CXXF "$src" -o "$out";; *.c) arm-none-eabi-gcc $CF "$src" -o "$out";; *.S) arm-none-eabi-gcc $SF "$src" -o "$out";; esac 2>>"$2/err.log" || echo "FAIL $src" >> "$2/fail.log"; }
export -f cc1
rm -f "$B"/*/fail.log "$B"/*/err.log
( find -L "$P/cores/arduino" -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) | grep -v "/usb/" ; find "$P/variants/UNOWIFIR4" -maxdepth 2 -not -path "*/includes/*" -type f \( -name '*.c' -o -name '*.cpp' \) ) | xargs -P 8 -I{} bash -c 'cc1 "$1" "$2"' _ {} "$B/core"
( ls "$P"/libraries/WiFiS3/src/*.cpp "$P"/libraries/BlockDevices/*.cpp | grep -v -e QSPIFlash -e SDCardBlock -e WiFiSSLClient ; [ "${3:-0}" = 1 ] && ls "$L"/*.cpp ) | xargs -P 8 -I{} bash -c 'cc1 "$1" "$2"' _ {} "$B/lib"
rm -f "$B"/sk/*.o; cc1 "$1" "$B/sk"
cat "$B"/*/fail.log 2>/dev/null | head -20
rm -f "$B/core.a"; arm-none-eabi-ar rcs "$B/core.a" "$B"/core/*.o
arm-none-eabi-g++ -Wl,--gc-sections --specs=nosys.specs -w $MC -o "$B/out.elf" -L"$B" -L"$P/variants/UNOWIFIR4" -T"$P/variants/UNOWIFIR4/fsp.ld" "$B"/sk/*.o "$B"/lib/*.o \
  -Wl,--whole-archive -Wl,--start-group "$P/variants/UNOWIFIR4/libs/libfsp.a" "$B/core.a" -Wl,--no-whole-archive --specs=nano.specs -lstdc++ -lsupc++ -lm -lc -lgcc -lnosys -Wl,--end-group "-Wl,-Map,$B/out.map" 2>&1 | grep -v -e "warning" -e "libc_nano" -e "not implemented" | head -30
[ -f "$B/out.elf" ] && arm-none-eabi-size "$B/out.elf"
