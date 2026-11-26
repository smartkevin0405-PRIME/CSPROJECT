// Screens.cpp  화면 라이브러리: Arduino_GFX (GFX Library for Arduino)
// 화면: 3.5인치 8비트 병렬 실드, 칩 HX8357B (2026-11-26 실물 시험에서 확인)
// UNO R4는 MCUFRIEND_kbv를 지원하지 않아 Arduino_GFX의 소프트웨어 8비트 병렬 버스(Arduino_SWPAR8)를 쓴다.
// 이 방식은 화면 전체를 칠하는 데 몇 초가 걸려서 fillScreen은 부팅할 때 한 번만 쓰고, 그 뒤로는
//  1) 글자를 쓸 때마다 그 자리(상자)를 기억해 두고, 화면이 바뀔 때는 기억한 상자만 검은색으로 지운다
//  2) 같은 자리에 같은 글자·색을 다시 쓰라고 하면 건너뛴다 (매초 부르는 카운트다운·도착 시간)
//  3) 새 글자가 이전보다 짧으면 남은 꼬리만 지운다
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "Screens.h"
#include <Arduino_GFX_Library.h>

static Arduino_SWPAR8 bus(PIN_LCD_RS, PIN_LCD_CS, PIN_LCD_WR, PIN_LCD_RD,
                          PIN_LCD_D[0], PIN_LCD_D[1], PIN_LCD_D[2], PIN_LCD_D[3],
                          PIN_LCD_D[4], PIN_LCD_D[5], PIN_LCD_D[6], PIN_LCD_D[7]);
static Arduino_HX8357B tft(&bus, PIN_LCD_RST, TFT_ROTATION, TFT_INVERT);
static const char* const WX_NAME[5] = { "SUNNY", "CLOUDY", "RAIN", "SNOW", "--" };

// ---------- 그린 자리 기억 ----------
struct Box { int16_t x, y, w, h; uint32_t sig; };
static const uint8_t MAX_BOXES = 20;
static Box boxes[MAX_BOXES];
static uint8_t boxCount = 0;

static uint32_t signature(const char* s, uint16_t color) {   // FNV-1a
  uint32_t h = 2166136261u ^ color;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

// 지금 화면에 그려 둔 것만 지운다
static void clearDrawn() {
  for (uint8_t i = 0; i < boxCount; i++)
    if (boxes[i].w > 0) tft.fillRect(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, C_BLACK);
  boxCount = 0;
}

static Box* findBox(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < boxCount; i++)
    if (boxes[i].x == x && boxes[i].y == y) return &boxes[i];
  return nullptr;
}

static void remember(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t sig) {
  if (boxCount < MAX_BOXES) boxes[boxCount++] = { x, y, w, h, sig };
  // 넘치면 기억하지 못한다. 지금 화면 구성은 많아야 11개라 여기까지 오지 않는다
}

void screensBegin() {
  tft.begin();
  tft.setTextWrap(false);
  tft.fillScreen(C_BLACK);       // 전체 칠하기는 여기서 한 번만 (500kHz에서 약 7초)
  boxCount = 0;
}

// 글자 크기 size로 (x, y)에 쓴다. 기본 글꼴은 글자 하나가 가로 6 x size, 세로 8 x size 픽셀
static void text(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* s) {
  int16_t w = (int16_t)strlen(s) * 6 * size, h = 8 * size;
  uint32_t sig = signature(s, color);
  Box* b = findBox(x, y);
  if (b && b->sig == sig) return;                       // 같은 글자라 다시 그릴 필요 없음
  tft.setTextSize(size);
  tft.setTextColor(color, C_BLACK);
  tft.setCursor(x, y);
  tft.print(s);
  if (b) {
    if (b->w > w) tft.fillRect(x + w, y, b->w - w, b->h, C_BLACK);   // 이전 글자의 꼬리
    if (b->h > h) h = b->h;
    b->w = w; b->h = h; b->sig = sig;
  } else {
    remember(x, y, w, h, sig);
  }
}

// 가운데 정렬. 가운데 정렬 글자는 길이에 따라 x가 바뀌므로 y만 같으면 같은 자리로 본다
static void textCenter(int16_t y, uint8_t size, uint16_t color, const char* s) {
  int16_t w = (int16_t)strlen(s) * 6 * size;
  int16_t x = (tft.width() - w) / 2;
  for (uint8_t i = 0; i < boxCount; i++) {
    Box& b = boxes[i];
    if (b.y == y && b.x != x && b.h == 8 * size) {       // 이전 가운데 글자가 다른 폭이었다
      if (b.sig == signature(s, color)) return;
      tft.fillRect(b.x, b.y, b.w, b.h, C_BLACK);
      b.x = x; b.w = 0; b.sig = 0;                       // 아래 text()가 새 폭으로 채운다
      break;
    }
  }
  text(x, y, size, color, s);
}

// 남은 초를 "3m20s" 꼴로 바꾼다
static void formatEta(uint16_t sec, char* out, size_t len) {
  if (sec == TRAVEL_UNKNOWN_SEC) snprintf(out, len, "--");
  else if (sec < 30) snprintf(out, len, "NOW");
  else snprintf(out, len, "%um%02us", sec / 60, sec % 60);
}

void screenIdleUpdate(bool haveIndoor, int8_t tempC, uint8_t humidity) {
  char buf[24];
  if (haveIndoor) snprintf(buf, sizeof(buf), "%dC  %u%%", tempC, humidity);
  else snprintf(buf, sizeof(buf), "--C  --%%");
  textCenter(130, 5, C_WHITE, buf);
}

void screenIdle(bool haveIndoor, int8_t tempC, uint8_t humidity) {
  clearDrawn();
  textCenter(24, 3, C_CYAN, "SMART ENTRANCE");
  textCenter(84, 2, C_GRAY, "INDOOR TEMP / HUMIDITY");
  screenIdleUpdate(haveIndoor, tempC, humidity);
  textCenter(270, 2, C_GRAY, "Come closer and tag your card");
}

void screenPrompt() {
  clearDrawn();
  textCenter(100, 5, C_YELLOW, "TAG YOUR CARD");
  textCenter(190, 2, C_GRAY, "Hold the card on the mark below");
}

void screenMessage(const char* line1, const char* line2, uint16_t color) {
  clearDrawn();
  textCenter(110, 4, color, line1);
  if (line2 && line2[0]) textCenter(180, 2, C_GRAY, line2);
}

void screenInfo(const User& user, const WeatherInfo& wx) {
  char buf[40];
  clearDrawn();
  snprintf(buf, sizeof(buf), "HELLO, %s", user.name);
  text(12, 10, 3, C_WHITE, buf);
  if (wx.ok) snprintf(buf, sizeof(buf), "OUT %dC  NEXT 3H: %s", wx.tempC, WX_NAME[wx.kind]);
  else snprintf(buf, sizeof(buf), "WEATHER: NO DATA");
  text(12, 46, 2, C_CYAN, buf);
  tft.drawFastHLine(0, 72, tft.width(), C_GRAY);
  remember(0, 72, tft.width(), 1, 0);
}

void screenInfoRow(uint8_t row, const Stop& stop, const TransitArrival& a, bool ok, int16_t slackSec) {
  int16_t y = 84 + row * 100;
  char e1[10], e2[10], buf[40];
  text(12, y, 3, C_YELLOW, stop.label);
  if (!ok) {
    text(12, y + 34, 3, C_RED, "NO DATA");
    text(12, y + 68, 2, C_GRAY, "");
    return;
  }
  formatEta(a.etaSec[0], e1, sizeof(e1));
  formatEta(a.etaSec[1], e2, sizeof(e2));
  snprintf(buf, sizeof(buf), "%-7s next %-7s", e1, e2);
  text(12, y + 34, 3, C_WHITE, buf);
  if (slackSec == TRAVEL_NO_SLACK) {
    text(12, y + 68, 2, C_GRAY, "");
  } else if (slackSec >= 0) {
    snprintf(buf, sizeof(buf), "spare %dm%02ds before leaving", slackSec / 60, slackSec % 60);
    text(12, y + 68, 2, C_GREEN, buf);
  } else {
    int late = -slackSec;
    snprintf(buf, sizeof(buf), "too late by %dm%02ds", late / 60, late % 60);
    text(12, y + 68, 2, C_RED, buf);
  }
}

void screenInfoCountdown(uint16_t secLeft) {
  char buf[24];
  snprintf(buf, sizeof(buf), "back in %us", secLeft);
  text(12, 296, 2, C_GRAY, buf);
}

// 오른쪽 아래에 폰 연결 여부를 작게 쓴다. OK: 최근에 폰과 연락됨(hello 또는 출발 신호 전달 성공)
// --: 아직 연락을 확인하지 못했거나 마지막 출발 신호 전달에 실패함
void screenInfoPhone(bool online) {
  int16_t x = tft.width() - 12 - 8 * 12;     // 글자 크기 2(한 글자 12픽셀)로 8글자, 오른쪽 맞춤
  if (online) text(x, 296, 2, C_GREEN, "PHONE OK");
  else        text(x, 296, 2, C_GRAY,  "PHONE --");
}
