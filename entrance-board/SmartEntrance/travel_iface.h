// travel_iface.h  v0.2 (TravelLearn 연결: 취소·폰 연결 상태·날씨 PTY 추가)
// 메인 프로그램과 이동 시간 모듈(다른 담당) 사이의 약속.
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#pragma once
#include <Arduino.h>

const uint8_t  TRAVEL_MAX_ARRIVALS = 2;
const uint16_t TRAVEL_UNKNOWN_SEC  = 0xFFFF;
const int16_t  TRAVEL_NO_SLACK     = -32768;

enum TransitMode : uint8_t { MODE_BUS = 0, MODE_SUBWAY = 1 };

struct TransitArrival {
  TransitMode mode;
  char     route[12];     // 노선 표시명. 예: "5511"
  uint16_t etaSec[2];     // 첫째, 둘째 차량까지 남은 초. 없으면 TRAVEL_UNKNOWN_SEC
};

struct DepartureEvent {
  uint32_t seq;           // 부팅 후 1부터 증가
  uint32_t epochSec;      // 문 열림 시각 (UTC). 시각을 못 받았으면 0
  uint32_t uptimeMs;      // 문 열림 시점의 millis()
  uint8_t  userId;        // 사용자 배열 인덱스 (0부터)
  char     stopId[12];    // 설정표의 정류장 키
  int8_t   outdoorTempC;  // 출발 당시 외부 기온. 모르면 -128
  uint8_t  sky;           // 기상청 SKY 코드. 모르면 0
  uint8_t  pty;           // 출발 시점의 기상청 PTY 코드(가장 가까운 예보 시각). 모르면 255
  uint8_t  arrivalCount;  // 0~TRAVEL_MAX_ARRIVALS
  TransitArrival arrivals[TRAVEL_MAX_ARRIVALS];
};

// 이동 모듈이 구현하고 메인이 호출한다
void     travelBegin();
void     travelLoop();
void     travelOnDeparture(const DepartureEvent& ev);
uint16_t travelGetWalkSec(uint8_t userId, const char* stopId);
int16_t  travelGetSlackSec(uint8_t userId, const char* stopId, uint16_t etaSec);
void     travelOnCancel(uint8_t userId);        // 출발 직후 같은 사람이 다시 태그함
bool     travelPhoneOnline(uint8_t userId);     // 그 사람 폰과 최근 연락이 됐는지

// 메인이 구현하고 이동 모듈이 호출한다
uint32_t hostEpochNow();
bool     hostWifiReady();
bool     hostNetIdle();
uint8_t  hostWeatherPty();   // 지금 날씨(가장 가까운 예보 시각)의 기상청 PTY 코드. 모르면 255
