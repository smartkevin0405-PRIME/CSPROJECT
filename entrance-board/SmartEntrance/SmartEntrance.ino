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

static WeatherInfo weather = { false, -128, 0, 255, WX_UNKNOWN };
static uint32_t    weatherAt = 0;
static bool        weatherTried = false;

static bool     indoorOk = false;
static int8_t   indoorTemp = 0;
static uint8_t  indoorHum = 0;
static uint32_t indoorAt = 0;

// 지금 표시 중인 사용자와 도착 정보
static int            curUser = -1;
static TransitArrival arrivals[MAX_USER_STOPS];
static bool           arrivalOk[MAX_USER_STOPS];
static uint32_t       fetchedAt = 0;
static uint32_t       lastTick = 0;

// 태그 후 문이 열리는지 기다리는 상태
static int      armedUser = -1;
static uint32_t armedAt = 0;
static uint32_t departSeq = 0;

// 외출 취소 판단용: 마지막으로 출발 신호를 보낸 사람과 시각
static int      departedUser = -1;
static uint32_t departedAt = 0;

// ---- 이동 모듈이 부르는 함수 ----
uint32_t hostEpochNow()  { return netEpoch(); }
bool     hostWifiReady() { return netReady(); }
bool     hostNetIdle()   { return !netBusy(); }
uint8_t  hostWeatherPty(){ return weather.ok ? weather.pty : 255; }

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
  if (fetchWeather(weather)) {
    matrixShowTemp(weather.tempC);
    dialSet(weather.kind);
  } else {
    matrixShowNone();
  }
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
static uint16_t etaNow(uint16_t eta) {
  if (eta == TRAVEL_UNKNOWN_SEC) return eta;
  uint32_t passed = (millis() - fetchedAt) / 1000;
  return passed >= eta ? 0 : (uint16_t)(eta - passed);
}

static void drawRows() {
  const User& u = USERS[curUser];
  for (uint8_t i = 0; i < u.stopCount; i++) {
    const Stop& s = STOPS[u.stopIdx[i]];
    TransitArrival a = arrivals[i];
    a.etaSec[0] = etaNow(a.etaSec[0]);
    a.etaSec[1] = etaNow(a.etaSec[1]);
    int16_t slack = arrivalOk[i] ? travelGetSlackSec((uint8_t)curUser, s.key, a.etaSec[0]) : TRAVEL_NO_SLACK;
    screenInfoRow(i, s, a, arrivalOk[i], slack);
  }
}

// 등록된 사용자가 태그했을 때: 조회하고 표시한다
static void handleUser(int idx) {
  const User& u = USERS[idx];
  screenMessage("LOADING", u.name, C_CYAN);
  if (!weatherTried || millis() - weatherAt > 60000) refreshWeather();

  bool any = false;
  for (uint8_t i = 0; i < u.stopCount; i++) {
    arrivalOk[i] = fetchArrival(STOPS[u.stopIdx[i]], arrivals[i]);
    if (arrivalOk[i]) any = true;
  }
  if (!any && !weather.ok) {                 // 아무것도 못 받았으면 NO DATA
    showMessage("NO DATA", "check WiFi or API key", C_RED, 3000, ST_IDLE);
    return;
  }
  curUser = idx;
  fetchedAt = millis();
  armedUser = idx;                           // 이제부터 문이 열리면 외출로 본다
  armedAt = millis();
  screenInfo(u, weather);
  drawRows();
  screenInfoCountdown(SHOW_MS / 1000);
  lastTick = millis();
  enter(ST_SHOW);
}

static void handleCard() {
  uint8_t uid[7], len = 0;
  if (!readCard(uid, len)) return;
  int idx = findUser(uid, len);              // 배열 순차 탐색
  if (idx >= 0 && idx == departedUser && millis() - departedAt <= CANCEL_WINDOW_MS) {
    travelOnCancel((uint8_t)idx);            // 나갔다가 곧바로 다시 태그 → 외출 취소
    departedUser = -1;
    showMessage("TRIP CANCELED", USERS[idx].name, C_YELLOW, 2000, ST_IDLE);
    return;
  }
  if (idx < 0) showMessage("UNKNOWN CARD", "this card is not registered", C_RED, 2000, ST_PROMPT);
  else handleUser(idx);
}

// 태그한 사람이 문을 열고 나갔다: 이동 모듈에 알린다
static void sendDeparture() {
  const User& u = USERS[armedUser];
  DepartureEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.seq = ++departSeq;
  ev.epochSec = netEpoch();
  ev.uptimeMs = millis();
  ev.userId = (uint8_t)armedUser;
  strncpy(ev.stopId, STOPS[u.stopIdx[0]].key, sizeof(ev.stopId) - 1);
  ev.outdoorTempC = weather.ok ? weather.tempC : -128;
  ev.sky = weather.ok ? weather.sky : 0;
  ev.pty = weather.ok ? weather.pty : 255;
  ev.arrivalCount = 0;
  if (armedUser == curUser) {
    for (uint8_t i = 0; i < u.stopCount && i < TRAVEL_MAX_ARRIVALS; i++) {
      if (!arrivalOk[i]) continue;
      TransitArrival a = arrivals[i];
      a.etaSec[0] = etaNow(a.etaSec[0]);
      a.etaSec[1] = etaNow(a.etaSec[1]);
      ev.arrivals[ev.arrivalCount++] = a;
    }
  }
  travelOnDeparture(ev);
  departedUser = armedUser;
  departedAt = millis();
  armedUser = -1;
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
  travelLoop();
  uint32_t now = millis();

  // 태그 후 정해진 시간 안에 문이 열리면 외출
  bool opened = doorJustOpened();
  if (armedUser >= 0) {
    if (now - armedAt > DEPART_WINDOW_MS) armedUser = -1;
    else if (opened) {
      sendDeparture();
      showMessage("HAVE A NICE DAY", "", C_GREEN, 3000, ST_IDLE);
      return;
    }
  }

  switch (state) {
    case ST_IDLE:
      if (now - indoorAt >= IDLE_SENSOR_MS) {            // 실내 온습도 갱신
        indoorAt = now;
        int8_t t; uint8_t h;
        if (dhtRead(t, h)) { indoorOk = true; indoorTemp = t; indoorHum = h; }
        screenIdleUpdate(indoorOk, indoorTemp, indoorHum);
      }
      if (now - weatherAt >= WEATHER_MS) refreshWeather(); // 외부 기온 갱신
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
      if (now - lastTick >= 1000) {                      // 1초마다 남은 시간 갱신
        lastTick = now;
        drawRows();
        screenInfoCountdown((uint16_t)((SHOW_MS - (now - stateSince)) / 1000));
      }
      break;

    case ST_MESSAGE:
      if (now - stateSince >= messageMs) {
        if (afterMessage == ST_PROMPT) { screenPrompt(); enter(ST_PROMPT); }
        else goIdle();
      }
      break;
  }
}
