// Net.h  WiFi 연결과 날씨·버스·지하철 API 조회
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#pragma once
#include <Arduino.h>
#include "config.h"

struct WeatherInfo {
  bool        ok;       // 조회에 성공했는지
  int8_t      tempC;    // 외부 기온
  uint8_t     sky;      // 앞으로 3시간 중 가장 흐린 SKY 코드 (1 맑음, 3 구름많음, 4 흐림)
  uint8_t     pty;      // 앞으로 3시간 중 처음 나온 강수형태 코드 (0 없음)
  WeatherKind kind;     // 다이얼이 가리킬 칸
};

bool     netBegin();                    // WiFi 연결 (최대 15초 기다림)
bool     netReady();                    // 연결돼 있으면 true
uint32_t netEpoch();                    // 현재 시각 (UTC 초). 모르면 0
bool     netBusy();                     // API 조회 중이면 true

bool fetchWeather(WeatherInfo& out);                         // 기상청 초단기예보
bool fetchArrival(const Stop& stop, TransitArrival& out);    // 버스 또는 지하철 도착 정보
