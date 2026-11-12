// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// BasicIntegration: 메인 프로그램이 TravelLearn 모듈을 어디에 연결하는지 보여 주는 뼈대.
//  - "[다른 세션 담당]" 표시가 붙은 stub 함수는 센서·API·화면을 맡은 사람이 진짜 코드로 바꾼다.
//  - 이 파일의 나머지(태그 → 문 열림 → 출발 신호 → 여유 시간 표시)가 모듈과 연결되는 부분이다.
//
// ---- 주의 1: 시간(타이밍) ----
//  tl.loop()는 보통 몇 ms 안에 돌아온다. 하지만 아래 두 경우에는 오래 걸릴 수 있다.
//   - 출발 신호를 폰에 다시 보내는 동안(문 열림 후 최대 45초): 한 번 호출이
//     최대 TL_CONNECT_TIMEOUT_MS + TL_IO_TIMEOUT_MS (약 2.7초, 최악의 경우)까지 멈출 수 있다.
//     폰이 아예 연결되지 않을 때는 약 1.2초다.
//   - 폰이 보낸 이동 기록을 저장할 때: EEPROM 쓰기에 대략 0.1~0.3초가 걸린다.
//  그래서 이런 순간에 loop() 호출 간격에 기대는 일(서보 펄스, 스위치 디바운싱 등)은 하지 않는다.
//
// ---- 주의 2: Wi-Fi와 EEPROM ----
//  - WiFi.begin()은 메인 프로그램이 한다. 모듈은 연결 상태만 확인한다.
//  - 모듈은 EEPROM의 TL_EE_BASE ~ TL_EE_BASE+TL_EE_SIZE-1 (기본 0~4095번지)을 쓴다.
//    다른 용도로 EEPROM을 쓸 때는 이 구간 바깥을 쓴다.
//    (UNO R4의 EEPROM은 1024바이트 블록 단위로 지우고 쓴다.)

#include <WiFiS3.h>
#include <TravelLearn.h>

// ---- 함수 미리 선언 ----
static void connectWifi();
static uint8_t readRfidUser();
static bool doorJustOpened();
static int currentPty();
static int currentHourOrMinus1();
static int32_t busEtaSeconds(uint8_t user, uint8_t stop);
static int16_t outdoorTempC();
static void showSlack(uint8_t user, const TlSlack& s);
static void showPhoneIcon(uint8_t user, bool online);
static void onDepartResult(uint8_t user, uint32_t trip, bool delivered);
static void onTripStored(uint8_t user, uint8_t stop, uint32_t trip, bool applied);
static void handleTag(uint8_t user);
static void handleDoor();
static void updateSlack();

// ======================================================================
// [다른 세션 담당] 아래 stub 함수들을 진짜 코드로 바꾼다.
// ======================================================================

// [다른 세션 담당] Wi-Fi 연결. 연결될 때까지 기다린다.
static void connectWifi() {
  // 예: while (WiFi.status() != WL_CONNECTED) { WiFi.begin(ssid, pass); delay(5000); }
}

// [다른 세션 담당] 방금 태그된 사용자 번호(1~4). 태그가 없으면 0.
static uint8_t readRfidUser() {
  return 0;
}

// [다른 세션 담당] 문이 닫혀 있다가 막 열렸으면 true (한 번만 true).
static bool doorJustOpened() {
  return false;
}

// [다른 세션 담당] 기상청 PTY 코드(0 없음, 1·2·5·6 비, 3·7 눈). 모르면 0.
static int currentPty() {
  return 0;
}

// [다른 세션 담당] 현지 시각의 시(0~23). NTP 시각이 없으면 -1.
static int currentHourOrMinus1() {
  return -1;
}

// [다른 세션 담당] 그 사용자·정류장의 버스가 도착하기까지 남은 초. 모르면 -1.
static int32_t busEtaSeconds(uint8_t user, uint8_t stop) {
  (void)user;
  (void)stop;
  return -1;
}

// [다른 세션 담당] 실외 기온(℃). 모르면 -128.
static int16_t outdoorTempC() {
  return -128;
}

// [다른 세션 담당] 화면에 여유 시간을 표시한다. s.slackS가 음수면 이번 버스는 어렵다는 뜻.
static void showSlack(uint8_t user, const TlSlack& s) {
  (void)user;
  (void)s;
}

// [다른 세션 담당] 화면의 폰 연결 아이콘을 켜거나 끈다.
static void showPhoneIcon(uint8_t user, bool online) {
  (void)user;
  (void)online;
}

// ======================================================================
// 모듈 연결 부분
// ======================================================================

TravelLearn tl;

static const unsigned long TAG_TO_DOOR_MS = 120000UL;     // 태그 후 이 시간 안에 문이 열려야 출발로 본다
static const unsigned long CANCEL_WINDOW_MS = 90000UL;    // 출발 후 같은 사용자가 다시 태그하면 취소로 본다
static const unsigned long SLACK_PERIOD_MS = 5000UL;      // 여유 시간을 다시 계산하는 간격

static uint8_t gTagUser = 0;            // 마지막으로 태그한 사용자(0이면 없음)
static unsigned long gTagMs = 0;        // 그 태그 시각
static uint8_t gDepartUser = 0;         // 마지막으로 출발 신호를 낸 사용자(0이면 없음)
static unsigned long gDepartMs = 0;
static uint8_t gShownUser = 0;          // 화면에 지금 보여 주는 사용자
static uint8_t gShownStop = 1;          // 화면에 보여 주는 정류장 슬롯(예: 가장 가까운 정류장)
static unsigned long gLastSlackMs = 0;

// 이 사용자의 폰 아이콘과 여유 시간 표시를 새로 고친다.
static void refreshScreen(uint8_t user) {
  showPhoneIcon(user, tl.phoneOnline(user));
  int32_t eta = busEtaSeconds(user, gShownStop);
  if (eta < 0) return;
  int hour = currentHourOrMinus1();
  uint8_t wx = TravelLearn::wxFromPty(currentPty());
  uint8_t tb = hour >= 0 ? TravelLearn::tbFromHour(hour) : (uint8_t)TL_TB_UNKNOWN;
  TlSlack s = tl.slack(user, gShownStop, wx, tb, eta);
  showSlack(user, s);
}

// 폰에서 출발 신호 전달 결과가 나왔을 때(성공/실패)
static void onDepartResult(uint8_t user, uint32_t trip, bool delivered) {
  (void)trip;
  (void)delivered;
  showPhoneIcon(user, tl.phoneOnline(user));
}

// 폰이 보낸 이동 기록이 저장됐을 때 -> 예상 이동 시간이 바뀌었을 수 있으니 화면을 새로 고친다.
static void onTripStored(uint8_t user, uint8_t stop, uint32_t trip, bool applied) {
  (void)stop;
  (void)trip;
  (void)applied;
  refreshScreen(user);
}

// RFID 태그가 읽혔을 때
static void handleTag(uint8_t user) {
  unsigned long now = millis();
  // 출발한 지 얼마 안 됐는데 같은 사용자가 다시 태그했다 -> 외출 취소
  if (gDepartUser == user && now - gDepartMs <= CANCEL_WINDOW_MS) {
    tl.cancelDeparture(user);
    gDepartUser = 0;
    gTagUser = 0;
  } else {
    gTagUser = user;
    gTagMs = now;
  }
  gShownUser = user;
  refreshScreen(user);
}

// 문이 열렸을 때
static void handleDoor() {
  unsigned long now = millis();
  if (gTagUser == 0 || now - gTagMs > TAG_TO_DOOR_MS) return;   // 태그 없이 열린 문은 무시

  uint8_t user = gTagUser;
  int hour = currentHourOrMinus1();
  uint8_t wx = TravelLearn::wxFromPty(currentPty());
  uint8_t tb = hour >= 0 ? TravelLearn::tbFromHour(hour) : (uint8_t)TL_TB_UNKNOWN;
  int32_t eta = busEtaSeconds(user, gShownStop);
  int16_t temp = outdoorTempC();

  // 정류장은 아직 모르므로 0을 넘긴다(폰이 모든 정류장을 감시한다).
  tl.notifyDeparture(user, 0, wx, tb, eta, temp);
  gDepartUser = user;
  gDepartMs = now;
  gTagUser = 0;     // 이 태그는 사용했다
}

// 5초마다 지금 보여 주는 사용자의 여유 시간을 다시 계산한다.
static void updateSlack() {
  if (gShownUser == 0) return;
  if (millis() - gLastSlackMs < SLACK_PERIOD_MS) return;
  gLastSlackMs = millis();
  refreshScreen(gShownUser);
}

void setup() {
  Serial.begin(115200);
  connectWifi();

  // 사용자 키: 16진수 32글자. 폰 앱에도 같은 값을 넣는다. (실제 값은 별도 비밀 파일에 둔다.)
  tl.setUserKey(1, "00000000000000000000000000000000");
  tl.setDefaultWalk(1, 1, 300);      // 사용자 1, 정류장 1: 처음에는 5분으로 가정

  tl.onDepartResult(onDepartResult);
  tl.onTripStored(onTripStored);
  tl.begin();
}

void loop() {
  tl.loop();                        // 매번 부른다(위 "주의 1" 참고)

  uint8_t tag = readRfidUser();
  if (tag >= 1 && tag <= TL_MAX_USERS) handleTag(tag);
  if (doorJustOpened()) handleDoor();

  updateSlack();
}
