<!-- 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다. -->
# TravelLearn

"외출 준비 스마트 현관판"(Arduino UNO R4 WiFi)의 **폰 통신 + 이동 시간 학습** 모듈이다.

## 하는 일 / 하지 않는 일

하는 일
- 메인 프로그램이 알려 준 "출발"을 집 Wi-Fi로 사용자 폰에 보낸다(수신 확인, 최대 45초 재전송).
- 폰이 잰 "문 열림 ~ 정류장 도착" 시간을 귀가 후 받아, 조건별(사용자 × 정류장 × 날씨 × 시간대) 평균·편차를 갱신하고 EEPROM에 저장한다.
- 여유 시간 = 버스 도착까지 남은 시간 - 예상 이동 시간 - 안전 여유 를 계산한다.

하지 않는 일
- RFID, 문 열림 센서, 화면, 날씨·버스 API, Wi-Fi 연결과 재연결(`WiFi.begin`), 메인 프로그램은 현관판 메인 코드(`entrance-board/SmartEntrance`)가 맡는다. 이 모듈은 센서를 읽지 않는다.

통신 형식은 [통신규격](../../docs/통신규격.md), 메모리 사용량은 [메모리_사용량](../../docs/메모리_사용량.md)을 본다.

## 설치

1. `TravelLearn` 폴더를 `Documents/Arduino/libraries/`에 복사한다.
2. 보드 패키지 "Arduino UNO R4 Boards"를 설치하고 보드는 "Arduino UNO R4 WiFi"(`arduino:renesas_uno:unor4wifi`)를 고른다.
3. (권장) 보드의 ESP32-S3 Wi-Fi 펌웨어를 0.4.1 이상으로 올린다. `setConnectionTimeout`과 서버 관련 수정이 필요하다.

## 빠른 시작: VirtualBoard

센서 없이 보드 한 장으로 실제 폰 앱과 통신해 볼 수 있다.

1. `examples/VirtualBoard/arduino_secrets.h.example`을 `arduino_secrets.h`로 복사하고 Wi-Fi 정보와 32자리 키를 넣는다(git에 올리지 않는다).
   키 만들기: `python -c "import secrets; print(secrets.token_hex(16))"`
2. 업로드 후 시리얼 모니터(115200, 줄 끝 "새 줄")에 나온 **보드 IP**를 폰 앱에 입력한다. 같은 키도 앱에 넣는다.
   공유기에서 이 보드의 IP를 고정(DHCP 예약)해 두는 것을 권장한다.
3. 명령: `d 1` 가상 출발 / `w 1` 날씨(0 없음, 1 비, 2 눈) / `e 1 1 420` 버스 420초 뒤 도착일 때 여유 계산 /
   `s` 통계 표 / `c 1` 취소 / `x` 통계 삭제 / `p` 폰 상태·IP / `h` 도움말

## 현관판 메인 프로그램과의 연결

현관판 메인 프로그램(`entrance-board/SmartEntrance`)에는 이미 연결되어 있다(2026-11-13). `travel_link.cpp`가 메인 코드의 약속(`travel_iface.h` v0.2)을 이 모듈의 함수로 바꿔 부른다.

| 메인 코드 | 이 모듈 |
|---|---|
| `travelBegin()` (`setup()`, Wi-Fi 연결 뒤) | `setUserKey`, `setDefaultWalk`, `onDepartResult`, `onTripStored`, `begin` |
| `travelLoop()` (`loop()`, 태그 후 API 조회 한 건이 끝날 때마다) | `loop` |
| `travelOnDeparture(ev)` (태그 후 10분 안에 문 열림) | `notifyDeparture(user, 0, wx, tb, etaS, tempC)` |
| `travelOnCancel(userId)` (출발 후 90초 안에 다시 태그) | `cancelDeparture` |
| `travelGetSlackSec(...)` (정보 화면 1초마다) | `slack` |
| `travelPhoneOnline(userId)` (정보 화면의 `PHONE OK` / `PHONE --`) | `phoneOnline` |

- 사용자 번호 = 메인 코드 `USERS` 배열 위치+1, 정류장 번호 = `STOPS` 배열 위치+1. 폰 앱에 넣는 번호도 같다.
- `wx`에는 가장 가까운 예보 시각의 강수형태를, `tb`에는 보드 시각(없으면 폰이 hello로 알려 준 시각)을 쓴다.
- `DEMO_MODE` 1이거나 `USE_TRAVEL_STUB` 1이면 이 모듈 대신 240초 스텁이 쓰여 라이브러리를 설치하지 않아도 된다.

다른 스케치에 새로 붙일 때의 최소 예는 `examples/BasicIntegration`이다.

## 공개 함수

| 함수 | 언제 | 뜻 |
|---|---|---|
| `bool begin(uint16_t port = TL_BOARD_PORT)` | `setup()`, Wi-Fi 연결 뒤 | EEPROM을 읽고 서버를 연다. 반환값은 저장소 정상 여부 |
| `void loop()` | `loop()`에서 매번 | 들어온 요청 1건, 내보낼 신호 1건까지 처리한다 |
| `void setUserKey(uint8_t user, const char* hex32)` | `setup()` | 사용자별 공유 키(16진수 32글자). 형식이 틀리면 그 사용자의 인증은 모두 실패한다 |
| `void setDefaultWalk(uint8_t user, uint8_t stop, uint16_t seconds)` | `setup()` | 표본이 없을 때 쓸 초기 이동 시간(초). EEPROM에 저장하지 않는다 |
| `uint32_t notifyDeparture(user, stopHint, wx, tb, etaS = -1, tempC = -128)` | RFID 확인 뒤 문 열림 감지 | 출발 신호를 보내기 시작한다. 반환: 이동 ID(0이면 거절) |
| `void cancelDeparture(uint8_t user)` | 외출 취소로 판단했을 때 | 폰에 취소를 알린다 |
| `TlSlack slack(user, stop, wx, tb, etaS)` | 버스 정보를 받을 때마다 | 여유 시간 = etaS - 예상 이동 시간 - 안전 여유 |
| `bool phoneOnline(uint8_t user)` | 화면 아이콘 갱신 | 폰과 연락이 되는 상태인지(마지막 출발 신호 전달 성공, 또는 그 뒤 hello 수신) |
| `void onTripStored(cb(user, stop, trip, applied))` | `setup()` | 이동 기록이 저장될 때 부르는 함수 등록 |
| `void onDepartResult(cb(user, trip, delivered))` | `setup()` | 출발 신호의 전달 성공/실패 결과를 받을 함수 등록 |
| `void resetStats()` | 점검 | 통계를 모두 지운다 |
| `void dumpStats(Print& out)` | 점검 | 통계 표를 출력한다 |
| `bool phoneTime(uint32_t* epoch, int16_t* tzMin)` | NTP 시각이 없을 때 | 마지막 hello에서 받은 폰 시각(흐른 시간 반영). 모르면 false |
| `static uint8_t wxFromPty(int pty)` | 날씨 변환 | 기상청 PTY 코드 → 날씨 구간(0 없음, 1 비, 2 눈) |
| `static uint8_t tbFromHour(int hour)` | 시간대 변환 | 현지 시(0~23) → 시간대(0 아침, 1 낮, 2 저녁, 3 밤) |

값의 범위: `user` 1~4, `stop` 1~4(`stopHint`는 모르면 0), `wx` 0~2, `tb` 0~3(모르면 `TL_TB_UNKNOWN`=255), `etaS` 모르면 -1, `tempC` 모르면 -128.
점검·시험용으로 `store()`, `stats()`, `handleRequest()`도 공개되어 있다.

### TlSlack

| 필드 | 뜻 |
|---|---|
| `slackS` | 여유 시간(초). 음수면 늦음 |
| `walkS` | 예상 이동 시간(초) |
| `safetyS` | 안전 여유(초) |
| `level` | 근거: 0 해당 칸, 1 같은 시간대(또는 같은 날씨) 묶음, 2 정류장 전체 묶음, 3 초기 설정값 |
| `n` | 근거로 쓴 표본 수 |

## 설정 상수 (`src/TlConfig.h`)

| 이름 | 기본값 | 뜻 |
|---|---|---|
| `TL_MAX_USERS` / `TL_MAX_STOPS` | 4 / 4 | 사용자 수 / 정류장 슬롯 수 |
| `TL_BOARD_PORT` / `TL_PHONE_PORT` | 8080 / 8765 | 현관판 서버 포트 / 폰 서버 기본 포트 |
| `TL_DEPART_WINDOW_MS` | 45000 | 문 열림 후 출발 신호 재전송을 포기하는 시간 |
| `TL_DEPART_RETRY_MS` | 1000 | 출발 신호 재전송 간격 |
| `TL_CONNECT_TIMEOUT_MS` | 1200 | 폰 연결 제한 시간 |
| `TL_IO_TIMEOUT_MS` | 1500 | 요청/응답 읽기 제한 시간 |
| `TL_CANCEL_TRIES` | 2 | 취소 신호 시도 횟수 |
| `TL_EE_BASE` / `TL_EE_SIZE` | 0 / 4096 | EEPROM 시작 주소(1024의 배수) / 사용 크기 |
| `TL_MIN_SAMPLES` | 3 | 이 표본 수 이상이어야 그 칸을 믿는다 |
| `TL_EW_CAP` | 20 | 지수 가중 평균의 최대 유효 표본 수 |
| `TL_SAFETY_BASE_S` / `TL_SAFETY_K_X10` | 30 / 10 | 안전 여유 = 30초 + (10/10) × 표준편차 |
| `TL_DEFAULT_WALK_S` | 300 | 초기 설정이 없을 때의 예상 이동 시간(초) |
| `TL_DEFAULT_SD_PCT` / `TL_DEFAULT_SD_MIN_S` | 15 / 20 | 표본 부족 시 표준편차 = 평균의 15%, 최소 20초 |
| `TL_MIN_DUR_S` / `TL_MAX_DUR_S` | 20 / 3600 | 이 범위를 벗어난 기록은 버린다(초) |
| `TL_OUTLIER_MIN_N` | 5 | 이상값 판정을 시작하는 표본 수 |
| `TL_OUTLIER_SIGMA` / `TL_OUTLIER_FLOOR_S` | 3 / 120 | 이상값 기준: 평균에서 3 표준편차, 최소 120초 |
| `TL_OUTLIER_STRIKES` | 2 | 연속 이상값이 이만큼 쌓인 뒤의 값은 받아들인다 |
| `TL_ACCEPT_MANUAL` | 1 | 수동 기록도 통계에 반영(0이면 제외) |

대부분 `TlConfig.h`를 고치거나 컴파일 옵션(`-D`)으로 바꿀 수 있다.

### 사용자·정류장 수 바꾸기

`TlConfig.h`의 `TL_MAX_USERS`, `TL_MAX_STOPS`를 고친다. **표의 크기가 바뀌면 저장된 통계가 초기화(재포맷)된다.**
모듈은 EEPROM `TL_EE_BASE`부터 `TL_EE_SIZE`바이트(기본 0~4095번지)를 쓰므로 다른 용도는 이 구간 밖에 둔다.

## PC에서 시험 (호스트 테스트)

Linux/macOS/WSL에서 g++로 실제 라이브러리 코드를 그대로 컴파일해 시험한다.

```
cd extras/host && ./build.sh test
```

`unit_tests`, `virtual_board`, `size_report` 세 프로그램이 `extras/host/build/`에 만들어지고 단위 시험이 실행된다.

## 실제 보드에서 아직 확인하지 못한 것

- Arduino IDE/arduino-cli로 컴파일해 보지 못함. 대신 공식 코어 소스(ArduinoCore-renesas)와 ARM GCC 13.2로 라이브러리와 예제 두 개를 컴파일·링크하는 데까지 확인(`extras/realcore/build_real.sh`). 공식 도구는 GCC 7.2.1이라 IDE에서 한 번 더 확인 필요
- 보드에 올려 실행해 보지 못함 — 동작 검증은 호스트(g++) 빌드의 단위 시험과 종단 시험으로만 함
- WiFiS3 connect()의 실제 차단 시간
- EEPROM 블록 쓰기에 걸리는 실제 시간
- 메인 프로그램의 HTTP 요청(WiFiClient, 포트 80)과 함께 쓸 때의 소켓 수 한계
- 메인 스택(1 KB)을 넘는 스택 사용(`tl.loop()` 한 번 최악 약 1,200바이트, `docs/메모리_사용량.md` 2.3). 코어가 스택 감시를 꺼서 멈추지는 않지만 실제 최고 사용량은 재지 않음
- 며칠 연속 가동
