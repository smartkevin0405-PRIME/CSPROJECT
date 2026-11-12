# SmartEntrance (스마트 현관판)

보드: Arduino UNO R4 WiFi

## 준비
1. Arduino IDE 보드 매니저에서 "Arduino UNO R4 Boards" 설치
2. 라이브러리 매니저에서 설치: Adafruit GFX Library(함께 묻는 Adafruit BusIO 포함), MFRC522, Servo
2-1. 이동 시간 모듈: 저장소의 `travel-learn/arduino/TravelLearn` 폴더를 통째로 Arduino 라이브러리 폴더(보통 `문서/Arduino/libraries/`)에 복사한다. 데모 모드(`DEMO_MODE` 1)만 쓸 때나 config.h의 `USE_TRAVEL_STUB`이 1이면 필요 없다
3. 이 폴더의 `secrets.h.example`을 같은 폴더에 `secrets.h`로 복사한다. 데모 모드에서도 이 파일이 있어야 컴파일된다. `secrets.h`는 `.gitignore`에 있어 git에 올라가지 않는다
4. 이 폴더(SmartEntrance)를 통째로 열고 SmartEntrance.ino를 업로드

## 처음에는 데모 모드
config.h의 `DEMO_MODE`가 1이면 WiFi와 API 키 없이 가짜 데이터로 동작한다.
화면, 매트릭스, 서보, RFID, PIR, 문 센서를 먼저 확인할 수 있다.

## 실제 조회로 바꾸기
1. secrets.h(준비 3에서 복사한 파일)에 WiFi 이름·비밀번호, API 키, 사용자별 폰 공유 키를 적는다. 예시 파일(`secrets.h.example`)에는 실제 값을 쓰지 않는다
2. config.h에서 `DEMO_MODE`를 0으로, 기상청 격자(KMA_NX, KMA_NY), 정류장(STOPS), 사용자(USERS)를 실제 값으로 바꾼다
3. 카드 UID는 시리얼 모니터(115200)에 찍히는 "UID: .." 값을 USERS에 옮겨 적는다

## Wi-Fi 재연결
- 연결이 끊긴 채로 약 30초가 지나면 `netLoop()`가 `WiFi.begin()`을 다시 부른다(이후에도 30초마다). 시리얼 모니터에 `WiFi lost, reconnecting`이 찍힌다
- `WiFi.setTimeout(0)`으로 `WiFi.begin()`이 연결 명령만 보내고 바로 돌아오게 했다(WiFiS3 기본값은 10초 동안 기다림). 연결은 보드의 Wi-Fi 모듈(ESP32-S3)이 이어서 한다
- 이동 시간 모듈의 서버(8080)는 재연결 뒤 다시 열지 않는다. 소스로 확인한 근거:
  - WiFiS3의 `WiFiServer`는 Wi-Fi 모듈 안 소켓 번호만 기억한다. 모듈의 서버를 모두 닫는 명령(`_SOFTRESETWIFI`)은 처음 한 번(`modem.begin()`)과 `WiFi.end()`에서만 나간다
  - Wi-Fi 모듈 펌웨어(uno-r4-wifi-usb-bridge)는 `WiFi.begin()` 명령을 받아도 서버 소켓을 건드리지 않는다
  - 서버 소켓은 주소 0.0.0.0에 묶여 있고, lwIP는 IP가 바뀔 때 특정 주소에 묶인 소켓만 바꾼다
- 실제 공유기를 껐다 켜는 시험은 아직 하지 않았다(미확인)

## 파일
- SmartEntrance.ino: 상태 흐름 (대기 → 태그 확인 → 조회 → 표시 → 복귀)
- config.h: 핀, 시간, User 클래스와 사용자·정류장 배열, 순차 탐색
- secrets.h.example: WiFi·API 키·폰 공유 키의 예시. `secrets.h`로 복사해서 쓴다
- Net.h/.cpp: WiFi, 날씨·버스·지하철 조회
- Screens.h/.cpp, Tft9488.h/.cpp: 화면
- Devices.h/.cpp: PIR, 문 센서, DHT11, LED 매트릭스, 서보
- travel_iface.h: 이동 시간 모듈과의 약속
- travel_link.cpp: TravelLearn 라이브러리 연결 (USE_TRAVEL_STUB 0)
- travel_stub.cpp: 가상 이동 시간 240초 (USE_TRAVEL_STUB 1)

## 확인된 것과 아닌 것
- 확인: UNO R4 WiFi 대상 컴파일 (데모 모드, 실제 조회 모드), 가짜 응답으로 한 응답 해석 시험
- 미확인: 실제 부품에서의 동작, 실제 API 응답 형식
