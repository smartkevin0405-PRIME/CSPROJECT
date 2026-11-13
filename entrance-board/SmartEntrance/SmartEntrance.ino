// SmartEntrance.ino  스마트 현관판 메인 프로그램
// 보드: Arduino UNO R4 WiFi
// 흐름: 대기 → 태그 확인 → 조회 → 표시 → 복귀
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include <SPI.h>
#include <MFRC522.h>
#include "config.h"
#include "travel_iface.h"
#include "Devices.h"
#include "Net.h"
#include "Screens.h"

enum State : uint8_t { ST_IDLE, ST_PROMPT, ST_SHOW, ST_MESSAGE };

static MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

static State    state = ST_IDLE;
static uint32_t stateSince = 0;          // 지금 상태에 들어온 시각
static State    afterMessage = ST_IDLE;  // 안내 문구가 끝난 뒤 갈 상태
static uint32_t messageMs = 0;

static WeatherInfo weather = { false, -128, 0, 255, 255, WX_UNKNOWN };
static uint32_t    weatherAt = 0;
static bool        weatherTried = false;
static uint32_t    weatherOkAt = 0;          // 마지막으로 날씨를 받은 시각 (N5)

static bool     indoorOk = false;
static int8_t   indoorTemp = 0;
static uint8_t  indoorHum = 0;
static uint32_t indoorAt = 0;

// 지금 표시 중인 사용자와 도착 정보
static int            curUser = -1;
static TransitArrival arrivals[MAX_USER_STOPS];
static bool           arrivalOk[MAX_USER_STOPS];
static uint32_t       fetchedAt[MAX_USER_STOPS]; // 도착 정보마다 조회 완료 시각이 다르다
static uint32_t       lastTick = 0;

// 태그 후 문이 열리는지 기다리는 상태
static int      armedUser = -1;
static uint32_t armedAt = 0;
static uint32_t departSeq = 0;
static bool     armedDoorOpen = false;   // 문이 열린 채로 태그했는지 (N2)

// 외출 취소 판단용: 마지막으로 출발 신호를 보낸 사람과 시각
static int      departedUser = -1;
static uint32_t departedAt = 0;

static bool processDeparture();

// ---- 이동 모듈이 부르는 함수 ----
uint32_t hostEpochNow()  { return netEpoch(); }
bool     hostWifiReady() { return netReady(); }
bool     hostNetIdle()   { return !netBusy(); }
uint8_t  hostWeatherPty(){ return weather.ok ? weather.nowPty : 255; }   // 학습에는 지금 날씨를 쓴다

static void enter(State s) {
  state = s;
  stateSince = millis();
}

static void showMessage(const char* l1, const char* l2, uint16_t color, uint32_t ms, State next) {
  screenMessage(l1, l2, color);
  messageMs = ms;
  afterMessage = next;
  enter(ST_MESSAGE);
}

static void goIdle() {
  curUser = -1;
  screenIdle(indoorOk, indoorTemp, indoorHum);
  enter(ST_IDLE);
}

// 날씨를 새로 받아 매트릭스와 다이얼에 반영한다
static void refreshWeather() {
  weatherTried = true;
  weatherAt = millis();
  WeatherInfo fresh = weather;
  if (fetchWeather(fresh)) {
    weather = fresh;
    weatherOkAt = millis();
    matrixShowTemp(weather.tempC);
    dialSet(weather.kind);
  } else if (!weather.ok || millis() - weatherOkAt > WEATHER_KEEP_MS) {
    weather.ok = false;                      // 오래 실패했으면 이전 값도 버린다 (N5)
    matrixShowNone();
  }                                          // 그 밖에는 마지막 정상 값을 그대로 둔다 (N5)
}

// 카드가 새로 닿았으면 UID를 읽는다
static bool readCard(uint8_t* uid, uint8_t& len) {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return false;
  len = rfid.uid.size > 7 ? 7 : rfid.uid.size;
  memcpy(uid, rfid.uid.uidByte, len);
  rfid.PICC_HaltA();
  Serial.print(F("UID:"));
  for (uint8_t i = 0; i < len; i++) { Serial.print(' '); Serial.print(uid[i], HEX); }
  Serial.println();
  return true;
}

// 화면에 남은 시간을 줄여서 보여 준다 (조회한 뒤 지난 시간만큼 뺀다)
static uint16_t etaNow(uint16_t eta, uint32_t at) {
  if (eta == TRAVEL_UNKNOWN_SEC) return eta;
  uint32_t passed = (millis() - at) / 1000;
  return passed >= eta ? 0 : (uint16_t)(eta - passed);
}

static void drawRows() {
  const User& u = USERS[curUser];
  for (uint8_t i = 0; i < u.stopCount; i++) {
    const Stop& s = STOPS[u.stopIdx[i]];
    TransitArrival a = arrivals[i];
    a.etaSec[0] = etaNow(a.etaSec[0], fetchedAt[i]);
    a.etaSec[1] = etaNow(a.etaSec[1], fetchedAt[i]);
    int16_t slack = arrivalOk[i] ? travelGetSlackSec((uint8_t)curUser, s.key, a.etaSec[0]) : TRAVEL_NO_SLACK;
    screenInfoRow(i, s, a, arrivalOk[i], slack);
  }
}

// 조회 한 건이 끝날 때마다 부른다. 조회하는 동안에도 문 센서와 폰 통신이 멈추지 않게 한다.
// tl.loop()는 스택을 많이 쓰므로 조회 함수(Net.cpp) 안쪽이 아니라 여기, 조회와 조회 사이에서만 부른다.
static bool betweenFetches() {
  devicesLoop();
  bool departed = processDeparture();
  travelLoop();
  return departed;
}

// 등록된 사용자가 태그했을 때: 조회하고 표시한다
// 조회 중 문이 열리면 현재 요청까지만 마치고 출발을 전달한다
static void handleUser(int idx) {
  const User& u = USERS[idx];
  if (armedUser >= 0 && armedUser != idx) {  // 마지막으로 태그한 사람이 출발 대상이다 (B01)
    Serial.println(F("[main] armed user replaced"));
  }
  doorJustOpened();                          // 태그 전에 열린 문은 이번 출발이 아니다
  armedUser = idx;                           // API 실패와 무관하게 출발을 감시한다
  armedAt = millis();
  armedDoorOpen = doorIsOpen();              // 문이 열린 채 태그했으면 닫힐 때를 출발로 본다 (N2)
  curUser = idx;
  memset(arrivalOk, 0, sizeof(arrivalOk));
  screenMessage("LOADING", u.name, C_CYAN);
  if (!weatherTried || millis() - weatherAt > (weather.ok ? 60000UL : 15000UL)) {   // N5
    refreshWeather();
    if (betweenFetches()) return;
  }

  bool any = false;
  for (uint8_t i = 0; i < u.stopCount; i++) {
    arrivalOk[i] = fetchArrival(STOPS[u.stopIdx[i]], arrivals[i]);
    fetchedAt[i] = millis();
    if (arrivalOk[i]) any = true;
    if (betweenFetches()) return;
  }
  if (!any && !weather.ok) {                 // 아무것도 못 받았으면 NO DATA
    showMessage("NO DATA", "check WiFi or API key", C_RED, 3000, ST_IDLE);
    return;
  }
  screenInfo(u, weather);
  drawRows();
  screenInfoCountdown(SHOW_MS / 1000);
  screenInfoPhone(travelPhoneOnline((uint8_t)idx));
  lastTick = millis();
  enter(ST_SHOW);
}

// 카드가 닿았으면 처리하고 true, 카드가 없으면 false
static bool handleCard() {
  uint8_t uid[7], len = 0;
  if (!readCard(uid, len)) return false;
  int idx = findUser(uid, len);              // 배열 순차 탐색
  if (idx >= 0 && idx == departedUser && millis() - departedAt <= CANCEL_WINDOW_MS) {
    travelOnCancel((uint8_t)idx);            // 나갔다가 곧바로 다시 태그 → 앞 외출 취소
    departedUser = -1;
    Serial.println(F("[main] trip canceled by re-tag"));
    handleUser(idx);                         // 취소한 뒤 정보를 다시 보여 주고 새 출발을 감시한다 (N3)
    return true;
  }
  if (idx < 0) showMessage("UNKNOWN CARD", "this card is not registered", C_RED, 2000, ST_PROMPT);
  else handleUser(idx);
  return true;
}

// 태그한 사람이 문을 열고 나갔다: 이동 모듈에 알린다
static void sendDeparture(uint32_t openedAtMs) {
  const User& u = USERS[armedUser];
  static DepartureEvent ev;                  // 크기가 커서(약 70바이트) 1KB 메인 스택 대신 전역 영역에 둔다
  memset(&ev, 0, sizeof(ev));
  ev.seq = ++departSeq;
  ev.epochSec = netEpoch();
  if (ev.epochSec != 0) ev.epochSec -= (uint32_t)(millis() - openedAtMs) / 1000;
  ev.uptimeMs = openedAtMs;
  ev.userId = (uint8_t)armedUser;
  strncpy(ev.stopId, STOPS[u.stopIdx[0]].key, sizeof(ev.stopId) - 1);
  ev.outdoorTempC = weather.ok ? weather.tempC : -128;
  ev.sky = weather.ok ? weather.sky : 0;
  ev.pty = weather.ok ? weather.nowPty : 255;    // 출발 시점 날씨 (다이얼의 '앞으로 3시간'이 아님)
  ev.arrivalCount = 0;
  if (armedUser == curUser) {
    for (uint8_t i = 0; i < u.stopCount && i < TRAVEL_MAX_ARRIVALS; i++) {
      if (!arrivalOk[i]) continue;
      TransitArrival& a = ev.arrivals[ev.arrivalCount++];
      a = arrivals[i];
      a.etaSec[0] = etaNow(a.etaSec[0], fetchedAt[i]);
      a.etaSec[1] = etaNow(a.etaSec[1], fetchedAt[i]);
    }
  }
  travelOnDeparture(ev);
  departedUser = armedUser;
  departedAt = openedAtMs;
  armedUser = -1;
}

// loop와 조회 한 건 직후에 처리한다. 보류된 문 열림도 원래 시각으로 판정한다.
static bool processDeparture() {
  uint32_t openedAt = 0;
  bool opened = doorJustOpened(&openedAt);
  if (armedUser < 0) return false;
  if (!opened && armedDoorOpen && !doorIsOpen()) {   // 열린 채 태그했고 이제 닫혔다 → 그 순간을 출발로 본다 (N2)
    opened = true;
    openedAt = millis();
  }
  uint32_t at = opened ? openedAt : (uint32_t)millis();
  if ((int32_t)(at - armedAt) < 0) at = armedAt;     // 태그 직전에 열린 문은 태그 순간으로 본다 (N2)
  if (at - armedAt > DEPART_WINDOW_MS) { armedUser = -1; armedDoorOpen = false; return false; }
  if (!opened) return false;
  armedDoorOpen = false;
  sendDeparture(openedAt);
  showMessage("HAVE A NICE DAY", "", C_GREEN, 3000, ST_IDLE);
  return true;
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  devicesBegin();
  screensBegin();
  screenMessage("STARTING", DEMO_MODE ? "demo mode" : "connecting WiFi", C_CYAN);
  rfid.PCD_Init();
  matrixShowNone();
  if (!netBegin()) Serial.println(F("WiFi connect failed"));
  travelBegin();
  indoorOk = dhtRead(indoorTemp, indoorHum);
  refreshWeather();
  goIdle();
}

void loop() {
  devicesLoop();
  netLoop();                                   // WiFi가 끊겼으면 다시 연결
  travelLoop();
  if (processDeparture()) return;
  uint32_t now = millis();

  switch (state) {
    case ST_IDLE:
      if (now - indoorAt >= IDLE_SENSOR_MS) {            // 실내 온습도 갱신
        indoorAt = now;
        int8_t t; uint8_t h;
        if (dhtRead(t, h)) { indoorOk = true; indoorTemp = t; indoorHum = h; }
        screenIdleUpdate(indoorOk, indoorTemp, indoorHum);
      }
      {
        uint32_t wInterval = weather.ok ? WEATHER_MS : WEATHER_RETRY_MS;       // 실패하면 30초 뒤 다시 (N5)
        if (now - weatherAt >= wInterval && !pirActive()) refreshWeather();    // 사람이 있으면 미룬다 (N6)
      }
      handleCard();
      if (state == ST_IDLE && pirActive()) { screenPrompt(); enter(ST_PROMPT); }
      break;

    case ST_PROMPT:
      handleCard();
      if (state == ST_PROMPT) {
        if (pirActive()) stateSince = now;               // 사람이 있으면 안내를 유지
        else if (now - stateSince >= PROMPT_MS) goIdle();
      }
      break;

    case ST_SHOW:
      if (now - stateSince >= SHOW_MS) { goIdle(); break; }
      if (handleCard()) break;                           // 표시 중에도 새 태그를 받는다 (B01)
      if (now - lastTick >= 1000) {                      // 1초마다 남은 시간 갱신
        lastTick = now;
        drawRows();
        screenInfoCountdown((uint16_t)((SHOW_MS - (now - stateSince)) / 1000));
        screenInfoPhone(travelPhoneOnline((uint8_t)curUser));   // 보는 동안 폰이 연결되면 바뀐다
      }
      break;

    case ST_MESSAGE:
      if (handleCard()) break;                           // 안내 문구 중에도 태그를 받는다 (B01)
      if (now - stateSince >= messageMs) {
        if (afterMessage == ST_PROMPT) { screenPrompt(); enter(ST_PROMPT); }
        else goIdle();
      }
      break;
  }
}
