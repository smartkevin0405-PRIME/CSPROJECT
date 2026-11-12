// Screens.h  TFT 화면 그리기 (가로 480 x 세로 320)
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "Net.h"

// 자주 쓰는 색 (RGB565)
const uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_GRAY = 0x8410, C_RED = 0xF800,
               C_GREEN = 0x07E0, C_BLUE = 0x001F, C_YELLOW = 0xFFE0, C_CYAN = 0x07FF,
               C_ORANGE = 0xFD20;

void screensBegin();
void screenIdle(bool haveIndoor, int8_t tempC, uint8_t humidity);        // 대기: 실내 온습도
void screenIdleUpdate(bool haveIndoor, int8_t tempC, uint8_t humidity);  // 숫자만 다시 그림
void screenPrompt();                                                     // 태그 안내
void screenMessage(const char* line1, const char* line2, uint16_t color);
void screenInfo(const User& user, const WeatherInfo& wx);                // 정보 화면의 틀
void screenInfoRow(uint8_t row, const Stop& stop, const TransitArrival& a, bool ok, int16_t slackSec);
void screenInfoCountdown(uint16_t secLeft);
void screenInfoPhone(bool online);                                       // 이 사용자의 폰 연결 여부
