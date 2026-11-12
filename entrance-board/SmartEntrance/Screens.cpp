// Screens.cpp
// 화면 전체를 지우는 일은 화면이 바뀔 때 한 번만 하고,
// 그 뒤에는 글자를 배경색과 함께 덮어써서 바뀐 부분만 다시 그린다.
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "Screens.h"
#include "Tft9488.h"

static Tft9488 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, TFT_SPI_HZ);
static const char* const WX_NAME[5] = { "SUNNY", "CLOUDY", "RAIN", "SNOW", "--" };

void screensBegin() {
  tft.begin(TFT_ROTATION, TFT_INVERT);
  tft.setTextWrap(false);
  tft.fillScreen(C_BLACK);
}

// 글자 크기 size로 (x, y)에 쓴다. 배경색까지 칠해서 이전 글자를 덮는다
static void text(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* s) {
  tft.setTextSize(size);
  tft.setTextColor(color, C_BLACK);
  tft.setCursor(x, y);
  tft.print(s);
}

// 가운데 정렬. 기본 글꼴은 글자 하나가 가로 6 x size 픽셀이다
static void textCenter(int16_t y, uint8_t size, uint16_t color, const char* s) {
  int16_t w = (int16_t)strlen(s) * 6 * size;
  text((tft.width() - w) / 2, y, size, color, s);
}

// 남은 초를 "3m20s" 꼴로 바꾼다
static void formatEta(uint16_t sec, char* out, size_t len) {
  if (sec == TRAVEL_UNKNOWN_SEC) snprintf(out, len, "--");
  else if (sec < 30) snprintf(out, len, "NOW");
  else snprintf(out, len, "%um%02us", sec / 60, sec % 60);
}

void screenIdleUpdate(bool haveIndoor, int8_t tempC, uint8_t humidity) {
  char buf[24];
  if (haveIndoor) snprintf(buf, sizeof(buf), " %3dC  %3u%% ", tempC, humidity);
  else snprintf(buf, sizeof(buf), "  --C   --%% ");
  textCenter(130, 5, C_WHITE, buf);
}

void screenIdle(bool haveIndoor, int8_t tempC, uint8_t humidity) {
  tft.fillScreen(C_BLACK);
  textCenter(24, 3, C_CYAN, "SMART ENTRANCE");
  textCenter(84, 2, C_GRAY, "INDOOR TEMP / HUMIDITY");
  screenIdleUpdate(haveIndoor, tempC, humidity);
  textCenter(270, 2, C_GRAY, "Come closer and tag your card");
}

void screenPrompt() {
  tft.fillScreen(C_BLACK);
  textCenter(100, 5, C_YELLOW, "TAG YOUR CARD");
  textCenter(190, 2, C_GRAY, "Hold the card on the mark below");
}

void screenMessage(const char* line1, const char* line2, uint16_t color) {
  tft.fillScreen(C_BLACK);
  textCenter(110, 4, color, line1);
  if (line2 && line2[0]) textCenter(180, 2, C_GRAY, line2);
}

void screenInfo(const User& user, const WeatherInfo& wx) {
  char buf[40];
  tft.fillScreen(C_BLACK);
  snprintf(buf, sizeof(buf), "HELLO, %s", user.name);
  text(12, 10, 3, C_WHITE, buf);
  if (wx.ok) snprintf(buf, sizeof(buf), "OUT %dC  NEXT 3H: %s", wx.tempC, WX_NAME[wx.kind]);
  else snprintf(buf, sizeof(buf), "WEATHER: NO DATA");
  text(12, 46, 2, C_CYAN, buf);
  tft.drawFastHLine(0, 72, tft.width(), C_GRAY);
}

void screenInfoRow(uint8_t row, const Stop& stop, const TransitArrival& a, bool ok, int16_t slackSec) {
  int16_t y = 84 + row * 100;
  char e1[10], e2[10], buf[40];
  text(12, y, 3, C_YELLOW, stop.label);
  if (!ok) {
    text(12, y + 34, 3, C_RED, "NO DATA             ");
    text(12, y + 68, 2, C_GRAY, "                              ");
    return;
  }
  formatEta(a.etaSec[0], e1, sizeof(e1));
  formatEta(a.etaSec[1], e2, sizeof(e2));
  snprintf(buf, sizeof(buf), "%-7s next %-7s", e1, e2);
  text(12, y + 34, 3, C_WHITE, buf);
  if (slackSec == TRAVEL_NO_SLACK) {
    text(12, y + 68, 2, C_GRAY, "                              ");
  } else if (slackSec >= 0) {
    snprintf(buf, sizeof(buf), "spare %dm%02ds before leaving    ", slackSec / 60, slackSec % 60);
    text(12, y + 68, 2, C_GREEN, buf);
  } else {
    int late = -slackSec;
    snprintf(buf, sizeof(buf), "too late by %dm%02ds            ", late / 60, late % 60);
    text(12, y + 68, 2, C_RED, buf);
  }
}

void screenInfoCountdown(uint16_t secLeft) {
  char buf[24];
  snprintf(buf, sizeof(buf), "back in %2us ", secLeft);
  text(12, 296, 2, C_GRAY, buf);
}

// 오른쪽 아래에 폰 연결 여부를 작게 쓴다. OK: 최근에 폰과 연락됨(hello 또는 출발 신호 전달 성공)
// --: 아직 연락을 확인하지 못했거나 마지막 출발 신호 전달에 실패함
void screenInfoPhone(bool online) {
  int16_t x = tft.width() - 12 - 8 * 12;     // 글자 크기 2(한 글자 12픽셀)로 8글자, 오른쪽 맞춤
  if (online) text(x, 296, 2, C_GREEN, "PHONE OK");
  else        text(x, 296, 2, C_GRAY,  "PHONE --");
}
