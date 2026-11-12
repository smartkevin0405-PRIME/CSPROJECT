# SmartEntrance (스마트 현관판)

보드: Arduino UNO R4 WiFi

## 준비
1. Arduino IDE 보드 매니저에서 "Arduino UNO R4 Boards" 설치
2. 라이브러리 매니저에서 설치: Adafruit GFX Library(함께 묻는 Adafruit BusIO 포함), MFRC522, Servo
2-1. 이동 시간 모듈: 저장소의 `travel-learn/arduino/TravelLearn` 폴더를 통째로 Arduino 라이브러리 폴더(보통 `문서/Arduino/libraries/`)에 복사한다. 쓰지 않으려면 config.h의 `USE_TRAVEL_STUB`을 1로
3. 이 폴더(SmartEntrance)를 통째로 열고 SmartEntrance.ino를 업로드

## 처음에는 데모 모드
config.h의 `DEMO_MODE`가 1이면 WiFi와 API 키 없이 가짜 데이터로 동작한다.
화면, 매트릭스, 서보, RFID, PIR, 문 센서를 먼저 확인할 수 있다.

## 실제 조회로 바꾸기
1. secrets.h에 WiFi 이름·비밀번호와 API 키를 적는다
2. config.h에서 `DEMO_MODE`를 0으로, 기상청 격자(KMA_NX, KMA_NY), 정류장(STOPS), 사용자(USERS)를 실제 값으로 바꾼다
3. 카드 UID는 시리얼 모니터(115200)에 찍히는 "UID: .." 값을 USERS에 옮겨 적는다

## 파일
- SmartEntrance.ino: 상태 흐름 (대기 → 태그 확인 → 조회 → 표시 → 복귀)
- config.h: 핀, 시간, User 클래스와 사용자·정류장 배열, 순차 탐색
- Net.h/.cpp: WiFi, 날씨·버스·지하철 조회
- Screens.h/.cpp, Tft9488.h/.cpp: 화면
- Devices.h/.cpp: PIR, 문 센서, DHT11, LED 매트릭스, 서보
- travel_iface.h: 이동 시간 모듈과의 약속
- travel_link.cpp: TravelLearn 라이브러리 연결 (USE_TRAVEL_STUB 0)
- travel_stub.cpp: 가상 이동 시간 240초 (USE_TRAVEL_STUB 1)

## 확인된 것과 아닌 것
- 확인: UNO R4 WiFi 대상 컴파일 (데모 모드, 실제 조회 모드), 가짜 응답으로 한 응답 해석 시험
- 미확인: 실제 부품에서의 동작, 실제 API 응답 형식
