<!-- 이 문서는 생성형 AI(Claude)의 도움을 받아 작성했습니다. -->
# 기본 현관판 (센서·화면·API·메인 프로그램)

카드를 태그하면 사람별 버스·지하철 도착 정보와 날씨를 보여 주는 벽걸이 현관판.
보드는 Arduino UNO R4 WiFi.

## 현재 상황 (2026-11-12)

| 항목 | 상태 |
| --- | --- |
| 부품 신청표 | 작성함. 필수 구매와 배송비 41,880원, 보유 확인 후 구매까지 43,780원 (예산 45,000원). 디바이스마트 부가세·배송비는 미확인 |
| 전원·핀 배치·케이스 설계 | 초안 작성함. 부품 도착 후 실물 확인 필요 |
| 배선도 | `docs/wiring.html` (브라우저로 열기) |
| 메인 코드 | `SmartEntrance/`. UNO R4 WiFi 대상 컴파일 통과, 실물 동작은 미확인 |
| 이동 시간 모듈 연결 | 연결함(`SmartEntrance/travel_link.cpp`, 2026-11-13). PC 호스트 빌드로 출발 신호·여유 시간 계산 호출만 확인. 보드 컴파일·실물은 미확인. `config.h`의 `USE_TRAVEL_STUB 1`이면 예전 240초 스텁으로 돌아감 |

## 폴더

- `SmartEntrance/` 아두이노 스케치. 여는 방법과 설정은 폴더 안 `README.md`
- `docs/wiring.html` 모듈 핀별 점퍼선 배선도와 선 목록
- `docs/hardware.md` 전원, 핀 배치, 분압, 구매 품목 요약

## 확인한 것

- 컴파일 (데모 모드): 플래시 77,640바이트(29%), RAM 10,028바이트(30%)
- 컴파일 (실제 조회 모드): 플래시 88,776바이트(33%), RAM 10,804바이트(32%)
- 날씨·버스·지하철 응답 해석: 직접 만든 가짜 응답으로 PC에서 시험해 통과

## 아직 확인하지 못한 것

- 실제 부품에서의 동작 (화면 초기화, 색 반전, SPI 속도, DHT11 타이밍)
- 저항 분압으로 SPI가 안정적인지, RC522 MISO(3.3V)를 보드가 읽는지
- 버스 API(`getStationByUid`)의 응답 항목 이름. 키를 받은 뒤 실제 응답과 맞춰 봐야 함
- 화면 글자는 영문·숫자만 나옴. 한글 글꼴은 용량을 재 본 뒤 결정

## 다음 할 일

1. 부품 주문 (화면 THL-CD07은 해외 상품이라 약 일주일)
2. `secrets.h`에 WiFi와 API 키, `config.h`에 실제 정류장·사용자 UID·기상청 격자 입력
3. 부품 도착 후 화면 단독 시험 → 전체 배선 → 데모 모드 → 실제 조회 순서로 확인
4. 이동 시간 모듈과 합친 상태로 보드 컴파일, RAM 사용량 재측정, `secrets.h`의 `TL_KEY_USER*`에 실제 키 입력

## 이동 시간 모듈과의 연결

`../travel-learn/arduino/TravelLearn`이 실제 모듈이고, `SmartEntrance/travel_link.cpp`가 메인 코드의 약속(`travel_iface.h` v0.2)을 이 모듈로 바꿔 부른다. 값은 아래처럼 바꾼다.

추가로 바뀐 점

- `travel_iface.h`에 `travelOnCancel`(출발 후 90초 안에 같은 사람이 다시 태그하면 외출 취소, 화면에 TRIP CANCELED), `travelPhoneOnline`, `hostWeatherPty`를 더했다.
- `config.h`의 `Stop`에 `walkSec`(학습 기록이 없을 때의 예상 이동 시간)을 더했다. 정류장·사용자 배열 순서가 곧 모듈의 번호이므로 학습을 시작한 뒤에는 순서를 바꾸지 않는다.
- 출발 신호의 정류장은 0(모름)으로 보낸다. 폰이 그 사용자의 정류장을 모두 감시한다.
- 시간대는 보드 시각(`WiFi.getTime()`)으로 정하고, 없으면 폰이 hello로 알려 준 시각을 쓴다.
- 전달 결과와 기록 저장은 시리얼 모니터에 `[travel]`로 찍힌다. 화면의 폰 연결 아이콘은 아직 없다.

| 메인 코드의 값 | TravelLearn에 넘길 값 |
| --- | --- |
| `userId` (0부터) | `user` = `userId + 1` (1~4) |
| 정류장 키 (`STOPS` 배열의 위치) | `stop` = 배열 위치 + 1 (1~4) |
| 기상청 PTY 코드 | `wx` = `TravelLearn::wxFromPty(pty)` |
| 현재 시각 (한국 시) | `tb` = `TravelLearn::tbFromHour(hour)`, 시각을 모르면 255 |
| 첫 차량 도착까지 남은 초 | `etaS`, 모르면 -1 |
| 출발 당시 외부 기온 | `tempC`, 모르면 -128 |

| 메인 코드의 함수 | 부를 함수 |
| --- | --- |
| `travelBegin()` | `tl.begin()`, `tl.setUserKey()`, `tl.setDefaultWalk()` |
| `travelLoop()` | `tl.loop()` |
| `travelOnDeparture(ev)` | `tl.notifyDeparture(user, stop, wx, tb, etaS, tempC)` |
| `travelGetSlackSec(...)` | `tl.slack(user, stop, wx, tb, etaS)`의 여유 시간 |

합칠 때 확인할 것

- 통신규격 5장에 따르면 출발 신호를 보내는 동안 `tl.loop()` 한 번이 최대 약 2.7초 걸릴 수 있다. 그동안 화면의 남은 시간 갱신과 서보 신호 끊기가 늦어질 수 있다.
- EEPROM 0~4095번지는 이동 시간 모듈이 쓴다. 메인 코드는 EEPROM을 쓰지 않는다.
- Wi-Fi 연결은 메인 코드(`netBegin`)가 한다. 모듈이 요구하는 조건과 같다.
- 사용자별 공유 키를 `secrets.h`에 추가해야 한다.
- 두 코드를 합친 상태의 RAM 사용량을 다시 잰다. 메인 코드만 10,804바이트(코어가 잡는 힙 8,192와 스택 1,024 포함), 모듈 고정 사용량은 문서 기준 3,640바이트다.

## 주의

- `secrets.h`에 실제 키를 넣은 뒤에는 그 파일을 올리지 않는다.
- 코드 파일마다 생성형 AI(Claude) 활용 사실을 주석으로 적어 두었다.
