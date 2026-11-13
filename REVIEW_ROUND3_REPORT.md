<!-- 생성형 AI(Claude)의 도움을 받아 작성함. -->
# 3차 수정 결과 보고

기준 `fix/review-departure-sync-arrival` @ `ea8aaf9`(PR #5 머지 커밋). 작업 브랜치 `fix/review-round3`(기준 커밋에서 새로 만듦). 지시서대로 push한 뒤 `fix/review-departure-sync-arrival`을 대상으로 PR을 연다(`main`이 아님).

## 기준 상태
- 수정 전 `run-all-tests.sh` 결과: **전체 통과**, 실패한 단계 없음.
- 수정 전 시험 수: 아두이노 모듈 163, 앱 core 66, 종단 27 + 27, 시뮬레이터 자체 시험 통과, 현관판 통합 2개 프로그램(`main_flow`, `door_sensor`). 지시서의 수와 같았다.

## 항목별 결과
7개 항목 모두 적용했다. 미적용·부분 적용은 없다.

| ID | 결과 | 바꾼 파일 | 추가한 시험 | 비고 |
|---|---|---|---|---|
| C01 | 적용 | `TlStore.h`, `TlStore.cpp`, `TravelLearn.h`, `TravelLearn.cpp`, `tools/virtual_board.py` | `unit_tests.cpp`: C01 묶음 5건, `selftest.py`: (l) 1건 | 저장 형식은 안 바꿨다(`TL_LAYOUT_VER` 2 그대로). `selftest.py`의 `trip=5`는 지시서 그대로 통과했다 |
| C02 | 적용 | `SmartEntrance.ino` | `main_flow.cpp`: C02 | `handleUser`는 한 줄짜리로 남기고 본문을 `startUser(idx, openDoorCounts)`로 옮겼다. 기본 인자 없음 |
| C03 | 적용 | `config.h`, `SmartEntrance.ino` | `main_flow.cpp`: C03 | `reset()`에 `lastTagUser`, `lastTagMs` 초기화를 추가했다(기존 시험 기대값은 그대로) |
| C04 | 적용 | `SmartEntrance.ino` | 없음(지시서대로) | 주석을 실제 역할로 고치고 `sendDeparture(at)`로 맞췄다 |
| C05 | 적용 | `SmartEntrance.ino` | `main_flow.cpp`: C05 | |
| C06 | 적용 | `JsonStr.h`(새 파일), `Net.cpp`, `tests/json_str.cpp`(새 파일), `tests/run.sh` | `json_str.cpp`: 표의 8건 | **`Net.cpp`는 PC에서 컴파일되지 않음 → 실제 보드 빌드 확인 필요** |
| C07 | 적용 | `메모리_사용량.md`, `entrance-board/README.md`, `통신규격.md`, `시험목록.md`, `travel-learn/README.md` | 없음 | 수치에는 "계산값, 보드 재측정 전"을 붙였다. 측정값 표는 지우지 않았다 |

지시서에 없는 작은 차이 하나: `JsonStr.h`의 `finish()`에 `len == 0`이면 끝의 0을 쓰지 않고 돌아가는 한 줄을 넣었다(`out[n] = 0`이 크기 0 버퍼를 넘어 쓰는 것을 막기 위해). 지시서가 정한 동작에는 영향이 없고, 호출하는 쪽은 항상 `len`이 1 이상이다.

## 멈춤 규칙에 걸린 것
없다. 기존 시험의 기대값을 바꾼 것도, 지운 것도 없다(시험은 추가만 했다). 지시서가 가리킨 코드는 모두 설명과 같았다.

## 최종 시험 결과
`bash travel-learn/run-all-tests.sh` 마지막 30줄(환경 변수 `JAVA_TOOL_OPTIONS`가 찍는 `Picked up ...` 줄은 뺐다). 종료 코드 0.

```
   기록 100건: 블록당 지우기 최대 50회 (블록 0~3: 50 50 49 49)
-- 소켓 수준: 서버 견고성, 출발 신호 전송
----------------------------------------
통과 168 / 실패 0

=== 2/7 앱 핵심 로직 단위 시험(JVM) ===
----------------------------------------
통과 66 / 실패 0

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
JSON 문자열 이스케이프 시험 통과

=== 7/7 시험 스크립트의 컴파일 실패 처리 ===
컴파일 실패 시 즉시 중단·이전 클래스 실행 방지 시험 통과

전체 통과
```

종료 코드: 0

시험 수: 아두이노 모듈 163 → **168**(C01 5건), 앱 core 66(변화 없음), 종단 27 + 27(변화 없음). 현관판 통합은 프로그램이 2개에서 3개가 됐다(`json_str` 추가). `entrance-board/tests/run.sh`는 `-Wall -Wextra -Werror`로 경고 없이 통과했다.

## 새 시험이 실제로 실패를 잡는지 되돌려 본 결과 (규칙 7)
시험마다 한 번씩, 고친 코드를 잠시 되돌려 실패하는지 확인하고 복원했다.

| 시험 | 되돌린 방법 | 결과 |
|---|---|---|
| C01 `unit_tests.cpp` | `boardLimit()`가 "제한 없음"(`0xFFFFFFFF`)을 돌려주게 함(수정 전 동작) | "다른 계보 ID가 최고값을 올리지 않아 새 ID는 dup이 아님" 1건 실패 |
| C01 `selftest.py` | 가상 현관판의 `foreign`·`stale_hw` 판정을 끔 | `(l) 다른 계보 ... (C01)` 실패, 응답 `why":"dup"` |
| C02 | 취소 경로를 `startUser(idx, true)`로 되돌림 | `assert(cancels==1 && armedUser==0 && !armedDoorOpen)` 실패 |
| C03 | 재태그 무시 조건 제거 | `assert(requests==r1 && state==ST_SHOW)` 실패 |
| C05 | `expireWeather()`의 만료 조건을 꺼 둠 | `assert(!weather.ok && requests==0)` 실패 |
| C06 | `putBytes`의 `full` 고정을 풀어 버퍼가 차도 뒤의 짧은 글자가 붙게 함 | `strcmp(small, "ab") == 0 && js.full` 실패 |

정직하게 적어 둘 점: C02와 C05는 처음에 호출 한 줄을 지우는 방식으로 되돌렸는데, `-Werror`가 "쓰지 않는 매개변수/함수" 경고로 컴파일부터 막아서 의미 있는 확인이 아니었다. 경고가 나지 않는 방식(C02는 호출 인자, C05는 조건 끄기)으로 다시 해서 위 표의 결과를 얻었다. C04는 시험을 추가하지 않는 항목이라 기존 시험(`uptimeMs==2000`, `UINT32_MAX` 경계, "태그 직전 50ms")이 그대로 통과하는 것으로 확인했다.

`Net.cpp`는 PC에서 컴파일되지 않으므로, 저장소 밖 임시 폴더에서 `Net.cpp`의 `readString` 본문을 그대로 뽑아 가짜 `WiFiClient`와 함께 `-Wall -Wextra -Werror`로 컴파일·실행해 봤다. 7건(`내선` 디코딩, 버퍼가 차면 뒤 글자도 안 붙음, 이모지, 앞쪽 서로게이트 뒤 일반 글자, `\n \t`, 잘못된 16진 거절, 닫는 따옴표 없이 끝남)이 통과했다. 이 하네스는 저장소에 넣지 않았다. 실제 보드 빌드를 대신하지는 않는다.

## 사람에게 알릴 사항
- **C01 한계**: 다른 계보(초기화 전 현관판·가상 현관판)의 ID는 최근 8개 목록으로만 중복을 막는다. 폰은 `stored:1`을 받으면 다시 보내지 않으므로 실제로는 문제가 없다. 부팅 횟수가 32768을 넘어 `& 0x7FFF`가 한 바퀴 도는 경우는 다루지 않는다.
- **`Net.cpp`(C06)는 Arduino IDE로 실제 조회 모드(`DEMO_MODE` 0) 빌드 확인이 필요하다.** 이번에도 PC에서 컴파일되지 않는다. (`.ino`는 기본 인자를 쓰지 않았다. 규칙 4.)
- **C03의 부수 효과(참고)**: 같은 카드를 5초 안에 다시 찍으면 무시된다. 지시서가 "취소 판정보다 앞"에 넣으라고 해서, 조회가 끝난 뒤 5초 안에 문을 열고 나갔다가 되돌아와 다시 찍는 경우에는 취소가 아니라 무시로 처리된다. 정상 사용에서는 드물지만 시험(H-4, H-13) 때 알아 두면 좋다.
- **문서 잔여**: `entrance-board/README.md` 70행의 "힙 구역은 17,240바이트"는 지시서 C07의 목록에 없어 그대로 뒀다. 같은 파일의 표(17,208)와 어긋난다. 보드 재측정 때 같이 고칠 것. 그 밖에 `메모리_사용량.md` 2.1 표의 값(+3,592, 19,780 등)은 배치 v1에서 잰 값이라 그대로 두었다.
- 2차 보고서의 사람 확인 항목 V01~V08과 `DEMO_MODE` 0 전환은 그대로 남아 있다.

## 커밋 목록
`git log --oneline` (보고서 커밋 2개를 빼고, 코드·문서 커밋만)

```
ae979bf [C] 문서: C07
2d1983a [A] 현관판: C02 C03 C04 C05 C06
2eebf15 [B] TravelLearn: C01
ea8aaf9 Merge pull request #5 from smartkevin0405-PRIME/claude/new-session-4frzea
```

PR 주소: https://github.com/smartkevin0405-PRIME/CSPROJECT/pull/6 (`fix/review-round3` → `fix/review-departure-sync-arrival`)
