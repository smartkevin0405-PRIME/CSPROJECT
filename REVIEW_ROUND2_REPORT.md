<!-- 생성형 AI(Claude)의 도움을 받아 작성함. -->
# 2차 수정 결과 보고

기준 커밋 `fdcbab4`(PR #4). 작업 브랜치 `claude/new-session-4frzea`(지시서의 `fix/review-round2` 대신 세션이 정한 개발 브랜치를 `fdcbab4`에서 다시 만들어 썼다). `git push`는 하지 않았다(규칙 10).

## 기준 상태
- 수정 전 `run-all-tests.sh` 결과: **전체 통과**. 단계는 지시서의 5단계가 아니라 7단계(6단계 현관판 통합 시험, 7단계 시험 스크립트 실패 처리가 더 있음)였고 7단계 모두 통과했다. 실패한 단계는 없었다.
- 수정 전 시험 개수: 아두이노 모듈 157, 앱 core 53, 종단 27+27.

## 항목별 결과
25개 항목 모두 적용했다. 미적용·부분 적용은 없다.

| ID | 결과 | 바꾼 파일 | 추가·변경한 시험 | 비고 |
|---|---|---|---|---|
| B01 | 적용 | `SmartEntrance.ino` | `main_flow.cpp`: B01 | A-0 대역(`MFRC522.h`, `main_flow.cpp`)도 지시대로 교체 |
| B02 | 적용 | `TripDb.java`, `StandbyService.java`, `MainActivity.java` | 없음(Android DB) | Android 컴파일 확인만 |
| B03 | 적용 | `TravelLearn.cpp` | `unit_tests.cpp`: B03 2건 | 가짜 폰에 `cancelHits`(/v1/cancel 수신 수) 추가. 응답 MAC을 틀리게(mode 1) 해 "폰은 받았지만 전달 포기" 상황을 만듦 |
| B04 | 적용 | `TripTracker.java` | `CoreSelfTest.java`: B04 2건 | |
| B05 | 적용 | `MainActivity.java` | 없음 | 검사 후 한꺼번에 저장, 실패하면 서비스 시작 안 함 |
| B06 | 적용 | `TripTracker.java`, `Prefs.java`, `StandbyService.java` | `CoreSelfTest.java`: B06 2건 | core는 그룹 C, 서비스는 그룹 D |
| B07 | 적용 | `StandbyService.java` | 없음 | |
| B08 | 적용 | `TripTracker.java` | `CoreSelfTest.java`: B08 2건 | 기존 "모의 보행" 시험은 그대로 통과(이미 30초에 `onWifiLost`를 부름) |
| B09 | 적용 | `TripTracker.java` | `CoreSelfTest.java`: B09 1건 | |
| B10 | 적용 | `StandbyService.java` | 없음 | 안쪽 `for` 본문은 원래 코드 그대로 |
| B11 | 적용 | `StandbyService.java` | 없음 | |
| B12 | 적용 | `config.h`, `Net.cpp` | 없음 | **실제 보드 빌드 확인 필요** (PC 시험은 `DEMO_MODE`라 컴파일 안 됨) |
| B13 | 적용 | `Net.cpp` | 없음 | **실제 보드 빌드 확인 필요** |
| B14 | 적용 | `Net.cpp` | 없음 | **실제 보드 빌드 확인 필요** |
| B15 | 적용 | `TlStore.h`, `TlStore.cpp`, `virtual_board.py` | `unit_tests.cpp`: 기존 1건 변경 + 신규 3건, `selftest.py`: 기존 1건 변경 | `TL_LAYOUT_VER` 1→2 |
| B16 | 적용 | `HttpMini.java`, `StandbyService.java` | `CoreSelfTest.java`: B16 2건 | |
| B17 | 적용 | `build-manual.sh` | 없음(`bash -n` 통과) | **실제 빌드 미실행**(aapt2, apksigner 등이 이 환경에 없음) |
| B18 | 적용 | `PhoneServerLogic.java` | `CoreSelfTest.java`: B18 4건 | 보드 쪽 depart·cancel 본문이 모두 `"v":1`을 보내는 것을 확인했다 |
| B19 | 적용 | `MainActivity.java`, `Prefs.java` | 없음 | "5~120" 안내 문구는 검사 실패 토스트 한 곳뿐이었고 입력란 라벨("최대 측정 시간(분)")에는 범위 문구가 없어 바꿀 곳이 없었다 |
| N1 | 적용 | `StandbyService.java` | 없음 | |
| N2 | 적용 | `SmartEntrance.ino` | `main_flow.cpp`: N2 2건 | |
| N3 | 적용 | `SmartEntrance.ino` | `main_flow.cpp`: N3 | |
| N4 | 적용 | `TripRecord.java`, `TripDb.java`, `StandbyService.java` | 없음(Android DB) | Android 컴파일 확인만 |
| N5 | 적용 | `config.h`, `SmartEntrance.ino` | `main_flow.cpp`: N5 2건 | |
| N6 | 적용 | `SmartEntrance.ino` | `main_flow.cpp`: N6 | |

문서(그룹 F)는 지시서 표의 항목을 모두 고쳤다. `README.md`의 시험 개수는 마지막 전체 시험 출력대로 아두이노 모듈 163개, 앱 core 66개로 바꿨다.

## 정책 때문에 바꾼 기존 시험
- `unit_tests.cpp` "9번째가 들어가면 첫째가 빠짐" — 기대를 "목록에선 빠져도 최고값 이하라 여전히 중복"으로 바꾸고 확인 2건 추가 — 지시서 B15가 허용한 유일한 기존 시험 수정
- `selftest.py` "(l) 중복 목록은 최근 8개만 보관(9번째 전 기록은 dup 아님)" → "(l) 목록(8개)에서 밀려난 옛 기록도 최고값 이하라 dup (B15)" — 기대값을 `status`에서 `dup`으로 바꿈. 이 시험이 "목록 8개에서 밀려나면 dup이 아니다"라는 옛 가정에 기대고 있어 B15 정책과 정면으로 충돌했다(멈춤 규칙 2). 보내는 입력은 그대로이고 확인하려던 대상(밀려난 옛 ID의 판정)도 그대로다.

그 밖에 기존 시험을 지우거나 약하게 만든 것은 없다.

## 최종 시험 결과
`bash travel-learn/run-all-tests.sh` 마지막 30줄(환경 변수 `JAVA_TOOL_OPTIONS`가 찍는 `Picked up ...` 줄은 뺐다). 종료 코드 0.

```
=== 3/7 종단 시험: 앱 통신 코드 ↔ 아두이노 모듈(호스트 빌드) ===
----------------------------------------
통과 27 / 실패 0

=== 4/7 종단 시험: 앱 통신 코드 ↔ 파이썬 가상 현관판 ===
----------------------------------------
통과 27 / 실패 0

=== 5/7 시뮬레이터 자체 시험(파이썬 가상 폰 ↔ 가상 현관판) ===
----------------------------------------
모든 시험 통과

=== 6/7 현관판 조회·문 센서 통합 시험(호스트 빌드) ===
현관판 통합 흐름 회귀 시험 통과
문 센서 시각·디바운스 시험 통과

=== 7/7 시험 스크립트의 컴파일 실패 처리 ===
컴파일 실패 시 즉시 중단·이전 클래스 실행 방지 시험 통과

전체 통과
```

1~2단계 결과: 아두이노 모듈 단위 시험 163 / 실패 0, 앱 핵심 로직 단위 시험 66 / 실패 0. `entrance-board/tests/run.sh`는 `-Wall -Wextra -Werror`로 경고 없이 통과했다.

시험이 실제로 잡는지 확인하려고, 고친 코드를 잠시 되돌려 새 시험이 실패하는지 봤다. B01(표시 중 태그 처리 제거), B03·B15(라이브러리 `src` 되돌림), B18(`PhoneServerLogic` 되돌림) 모두 해당 새 시험이 실패했고, 되돌린 코드를 복원한 뒤 통과했다.

Android 컴파일 확인:
- Android SDK가 없어 지시서의 명령(`javac ... | grep -v "package android" | grep error`)은 쓸모가 없었다. 일반 `javac`는 `android.*` 패키지가 없으면 타입 검사 단계에 가지 않아 `kr.doorboard` 쪽 오류가 보이지 않기 때문이다.
- 그래서 `javac -XDshould-stop.ifError=FLOW -source 8 -target 8`로 타입 검사를 강제로 돌리고, **그룹 D 적용 전 소스와 적용 후 소스의 오류 목록을 줄 번호를 뺀 채 비교**했다.
- 결과: 새로 생긴 오류는 `SystemClock`, `getWritableDatabase`, `Context`처럼 Android 클래스가 없어서 생기는 것뿐이다. `kr.doorboard.travellearn`의 이름·타입 오류(호출한 메서드 없음, 인자 개수 불일치, 타입 불일치 등)는 없었다. 적용 전에 있던 `restoreLeftHome(boolean)` 오류는 사라졌다. Java 8 문법만 썼다(추가한 줄에 람다·`var`·메서드 참조 없음).
- 임시 출력(`/tmp/tlcheck*`)은 지웠다.
- 한계: Android 클래스가 걸린 식의 타입 오류는 이 방법으로 걸러지지 않는다. 실제 SDK 빌드(Android Studio 또는 `build-manual.sh`)가 필요하다.

## 사람에게 알릴 사항
- 현관판 EEPROM 배치 버전이 2로 올라가 기존 학습 통계가 초기화된다(B15). **새 펌웨어를 올리기 전에 폰의 미전송 기록을 모두 동기화(미전송 0건)**할 것.
- 같은 이유로 폰은 사용자별로 `trip` 오름차순으로 보내야 한다. 현재 앱은 그렇게 한다(`unsynced(user)`가 `trip ASC`). 순서를 바꾸는 변경을 하면 작은 ID가 `dup`으로 버려진다.
- `Net.cpp`(B12~B14)는 PC 시험으로 컴파일되지 않는다. **Arduino IDE로 실제 조회 모드(`DEMO_MODE` 0) 빌드를 확인**할 것. 특히 `WiFiClient::setConnectionTimeout(int)`이 쓰는 코어 버전에서 되는지 확인한다.
- 시연 전 `config.h`의 `DEMO_MODE`를 0으로, `secrets.h`에 실제 키 입력.
- 사용자 번호를 바꾸면(B02) 이전 번호의 미전송 기록은 지워지지 않고 남아 전송되지 않는다. 번호를 되돌리면 다시 전송된다.
- 문서 정합성 잔여(지시서 그룹 F 표에 없어서 건드리지 않음):
  - `entrance-board/README.md`에 `tl` 객체 3,468·모듈 RAM 3,640이 그대로 남아 있다.
  - `travel-learn/docs/메모리_사용량.md`의 요약 표(RAM 3,640바이트)와 2.1 링크 측정 표는 실제로 잰 값이라 두었다. 2.2의 U=2, S=2 줄(이미지 390, 객체 2,416)과 "1,052바이트가 줄어든다" 문장도 새 식(`20 + 49·U + 72·U·S`)으로 다시 계산하지 않았다.
  - 둘 다 보드 재측정 때 같이 고칠 것.
- 사람이 확인할 항목(지시서 5장 그대로):
  - V01 UNO R4 메인 스택 여유(1KB 예약 대비 통신 경로 약 1.2KB) — 보드에서 실측
  - V02 화면 갱신·폰 통신·EEPROM 기록 중 문 센서 관측 공백 — 실측 후 필요하면 인터럽트 방식 검토
  - V03 앱 화면의 "연결 시험"은 기본 망, 실제 서비스는 Wi-Fi 망을 씀 — 모바일 데이터가 기본 망일 때 실기기 확인
  - V04 지하철 API 0~8번 범위·기상청 응답 정렬·버스 실제 응답 필드 — 실제 키로 대조
  - V05 Android 생명주기·권한 회수·GPS 꺼짐/켜짐 — 실기기 확인
  - V06 RC522·TFT 전압 분압, DHT·서보 타이밍, 전원 — 실물 측정
  - V07 새 APK·보드 바이너리 정식 빌드
  - V08 강제 종료 후 복구 불가 등 설계상 한계 — 사용자 안내 확인
  - `config.h`의 `DEMO_MODE`를 0으로 바꾸는 일 — 시연 직전 사람이 한다

## 커밋 목록
`git log --oneline` (이 보고서를 넣는 커밋 직전 기준)

```
70781a0 [F] 문서: 2차 수정 반영
f2a399d [E] 빌드: B17
0a60c02 [D] 앱 서비스: B02 B05 B06 B07 B10 B11 B19 N1 N4
285b140 [C] 앱 core: B04 B08 B09 B06(일부) B16 B18
1d3aa26 [B] TravelLearn: B03 B15
75a242d [A] 현관판: B01 B12 B13 B14 N2 N3 N5 N6
fdcbab4 Fix departure timing, sync registration and manual arrival recording
```
