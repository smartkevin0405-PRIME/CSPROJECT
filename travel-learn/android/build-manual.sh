#!/usr/bin/env bash
# 생성형 AI(Claude)의 도움을 받아 작성함.
# Gradle·Android Studio 없이 APK를 만드는 스크립트(우분투의 aapt2, dalvik-exchange, zipalign, apksigner 사용).
# 일반적인 빌드는 Android Studio에서 이 폴더를 열어 하면 된다. 이 스크립트는 구글 저장소에 접근할 수 없는
# 환경에서 APK를 만들기 위한 대체 경로다.
#
# 사용법: COMPILE_JAR=<API 37 android.jar> LINK_JAR=<API 34 android.jar> ./build-manual.sh <출력.apk>
#   COMPILE_JAR: 자바 컴파일용(최신 API 기호가 필요)
#   LINK_JAR   : 리소스 링크용(구버전 aapt2가 읽을 수 있는 API 34 이하)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT_APK="${1:-$HERE/build-manual/TravelLearn-debug.apk}"
: "${COMPILE_JAR:?COMPILE_JAR 필요}"
: "${LINK_JAR:?LINK_JAR 필요}"
PKG=kr.doorboard.travellearn
MIN_SDK=29
TARGET_SDK=37
W="$HERE/build-manual"
mkdir -p "$W/gen" "$W/classes" "$W/dex"
find "$W/classes" -name '*.class' -delete

# 1) 매니페스트에 package·uses-sdk를 넣은 사본을 만든다(Gradle은 build.gradle에서 넣어 준다).
python3 - "$HERE/app/src/main/AndroidManifest.xml" "$W/AndroidManifest.xml" "$PKG" "$MIN_SDK" "$TARGET_SDK" <<'PY'
import sys
src, dst, pkg, mn, tg = sys.argv[1:6]
s = open(src, encoding="utf-8").read()
s = s.replace('<manifest xmlns:android="http://schemas.android.com/apk/res/android">',
    '<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="%s" '
    'android:versionCode="1" android:versionName="1.0">\n    <uses-sdk android:minSdkVersion="%s" '
    'android:targetSdkVersion="%s" />' % (pkg, mn, tg), 1)
assert 'package="' in s
open(dst, "w", encoding="utf-8").write(s)
PY

# 2) 리소스 컴파일·링크
aapt2 compile --dir "$HERE/app/src/main/res" -o "$W/res.zip"
aapt2 link -o "$W/base.apk" -I "$LINK_JAR" --manifest "$W/AndroidManifest.xml" --java "$W/gen" \
  --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" "$W/res.zip"

# 3) 자바 컴파일(자바 8 문법, 람다 미사용)
find "$HERE/app/src/main/java" "$W/gen" -name '*.java' > "$W/sources.txt"
javac -nowarn -encoding UTF-8 -source 8 -target 8 -bootclasspath "$COMPILE_JAR" \
  -d "$W/classes" @"$W/sources.txt" 2>&1 | grep -v -e '^Picked up' -e '^warning:' -e '^Note:' -e '^[0-9]* warning' || true
test -f "$W/classes/kr/doorboard/travellearn/StandbyService.class"

# 4) dex 변환
dalvik-exchange --dex --min-sdk-version=26 --output="$W/dex/classes.dex" "$W/classes"

# 5) 묶기 → 정렬 → 서명(디버그 키)
cp "$W/base.apk" "$W/unaligned.apk"
( cd "$W/dex" && zip -q -j "$W/unaligned.apk" classes.dex )
zipalign -f -p 4 "$W/unaligned.apk" "$W/aligned.apk"
KS="$W/debug.jks"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias debug \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=TravelLearn Debug" >/dev/null 2>&1
fi
mkdir -p "$(dirname "$OUT_APK")"
apksigner sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true \
  --min-sdk-version "$MIN_SDK" --out "$OUT_APK" "$W/aligned.apk" 2>&1 | grep -v '^Picked up' || true
apksigner verify --min-sdk-version "$MIN_SDK" "$OUT_APK" 2>&1 | grep -v '^Picked up' || true
echo "APK: $OUT_APK"
