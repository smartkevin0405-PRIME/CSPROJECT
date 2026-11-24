// config.h  핀, 동작 시간, 사용자와 정류장 설정
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#pragma once
#include <Arduino.h>
#include "travel_iface.h"

// ---------- 동작 방식 ----------
#define DEMO_MODE        1   // 1: WiFi와 API 없이 가짜 데이터로 동작 (부품·키가 오기 전 개발용)
#define USE_TRAVEL_STUB  0   // 0: 실제 이동 시간 모듈(TravelLearn 라이브러리) 사용, 1: 240초 고정 스텁

// 데모 모드는 WiFi를 연결하지 않으므로 폰과 통신할 수 없다. 그래서 이동 시간 모듈도 스텁을 쓴다.
// (데모만 돌릴 때는 TravelLearn 라이브러리를 설치하지 않아도 된다)
#if DEMO_MODE
#undef  USE_TRAVEL_STUB
#define USE_TRAVEL_STUB  1
#endif

// ---------- 핀 (entrance-board/docs/hardware.md와 같다) ----------
// 분압: 보드 → 화면·RC522로 나가는 6개(D7, D8, D9, D10, D11, D13)는 1kΩ + 2kΩ로 약 3.3V
// 74HCT125: RC522 MISO → D12, PIR OUT → D2 를 5V 신호로 바꾼다
const uint8_t PIN_PIR      = 2;   // PIR 출력 (74HCT125 2번 채널을 거쳐 5V)
const uint8_t PIN_DHT      = 4;   // DHT11
const uint8_t PIN_DOOR     = 5;   // 문 열림 센서 MC-38 (내부 풀업)
const uint8_t PIN_TFT_RST  = 7;   // 화면 RESET (분압)
const uint8_t PIN_TFT_DC   = 8;   // 화면 DC/RS (분압)
const uint8_t PIN_TFT_CS   = 9;   // 화면 CS (분압)
const uint8_t PIN_RFID_SS  = 10;  // RC522 SDA/SS (분압). 분압 전 신호가 74HCT125 1번 채널의 OE도 켠다
// RC522 RST는 보드에 연결하지 않는다. 10kΩ으로 3.3V에 풀업하고 소프트웨어 리셋을 쓴다
// SPI: D11 MOSI, D12 MISO(74HCT125를 거침), D13 SCK

// ---------- 화면 ----------
// 분압 상태에서 안정성을 먼저 보려고 500kHz로 시작한다. 이 속도에서는 화면 전체 칠하기가 약 7초 걸린다
// (480 x 320 x 3바이트). 깨짐이 없으면 4MHz, 8MHz 순서로 올려 본다
const uint32_t TFT_SPI_HZ   = 500000;
const uint8_t  TFT_ROTATION = 1;        // 1 또는 3 (가로). 뒤집혀 보이면 바꾼다
#define TFT_INVERT 1                    // IPS 패널은 보통 1. 색이 반전돼 보이면 0으로
// RC522는 MFRC522 라이브러리가 정한 4MHz로 통신한다 (라이브러리 설정이라 여기서 바꾸지 않는다)

// ---------- 센서 ----------
const uint8_t  PIR_ACTIVE_LEVEL = HIGH; // 사람을 감지했을 때 D2 값
const uint8_t  DOOR_OPEN_LEVEL  = HIGH; // 문이 열렸을 때 D5 값. 반대로 동작하면 LOW로

// ---------- 시간 (ms) ----------
const uint32_t SHOW_MS          = 25000;   // 정보 표시 시간
const uint32_t PROMPT_MS        = 20000;   // 태그 안내 유지 시간
const uint32_t DEPART_WINDOW_MS = 120000;  // 태그 후 이 안에 문이 열리면 외출
const uint32_t IDLE_SENSOR_MS   = 5000;    // 실내 온습도 측정 간격
const uint32_t WEATHER_MS       = 600000;  // 날씨 갱신 간격
const uint32_t CANCEL_WINDOW_MS = 90000;   // 출발 후 이 안에 같은 사람이 다시 태그하면 외출 취소
const uint32_t HTTP_TIMEOUT_MS  = 8000;    // API 요청 1건(연결부터 응답 끝까지) 전체 제한 시간

// ---------- 날씨 종류 (화면 NEXT 3H 표시) ----------
enum WeatherKind : uint8_t { WX_SUNNY = 0, WX_CLOUDY = 1, WX_RAIN = 2, WX_SNOW = 3, WX_UNKNOWN = 4 };

// ---------- 기상청 격자 좌표 (사는 동네 값으로 바꾼다) ----------
const int KMA_NX = 59;
const int KMA_NY = 125;

// ---------- 정류장 ----------
struct Stop {
  const char* key;     // 메인 코드 안에서 정류장을 가리키는 이름(정류장마다 다르게). 이동 모듈에는 넘기지 않는다
  TransitMode mode;
  const char* label;   // 화면에 보일 이름 (영문·숫자)
  const char* query;   // 버스: 정류소 번호(ARS 5자리) / 지하철: URL 인코딩한 역 이름
  const char* route;   // 버스: 노선 번호 / 지하철: 호선 ID (2호선 = "1002")
  const char* dir;     // 버스: 방향 글자 일부, 비우면 전체 / 지하철: "상행" "하행" "내선" "외선"
  uint16_t walkSec;    // 학습 기록이 없을 때 쓰는 집→정류장 예상 이동 시간(초)
};

// 아래 정류장과 사용자 값은 예시다. 실제 값으로 바꾼다.
// 이동 시간 모듈은 정류장 4곳, 사용자 4명까지 다룬다. 모듈과 폰 앱의 번호는 배열 위치+1이다(앞에서부터 1번).
//  - STOPS의 n번째 = 폰 앱 "3. 정류장"의 정류장 n,  USERS의 n번째 = 폰 앱 "2. 현관판 연결"의 사용자 번호 n
//  - 학습을 시작한 뒤 순서를 바꾸면 이미 쌓인 기록이 다른 정류장·사람의 기록과 섞인다. 새 항목은 맨 뒤에 붙인다.
const Stop STOPS[] = {
  { "21252", MODE_BUS,    "BUS 5511",  "21252",                 "5511", "",     300 },
  { "S0230", MODE_SUBWAY, "SILLIM L2", "%EC%8B%A0%EB%A6%BC",    "1002", "내선", 420 },
};
const uint8_t STOP_COUNT = sizeof(STOPS) / sizeof(STOPS[0]);

// ---------- 사용자 ----------
const uint8_t MAX_USER_STOPS = 2;

class User {
public:
  const char* name;                    // 화면에 보일 이름 (영문)
  uint8_t uid[7];                      // 카드 UID
  uint8_t uidLen;                      // UID 길이 (보통 4)
  uint8_t stopIdx[MAX_USER_STOPS];     // STOPS 배열의 인덱스
  uint8_t stopCount;

  bool matches(const uint8_t* other, uint8_t len) const {
    if (len != uidLen) return false;
    for (uint8_t i = 0; i < len; i++) {
      if (uid[i] != other[i]) return false;
    }
    return true;
  }
};

// UID는 시리얼 모니터에 찍히는 값을 보고 바꾼다
const User USERS[] = {
  { "USER1",   { 0xDE, 0xAD, 0xBE, 0xEF }, 4, { 0, 1 }, 2 },
  { "USER2",   { 0x12, 0x34, 0x56, 0x78 }, 4, { 1, 0 }, 1 },
};
const uint8_t USER_COUNT = sizeof(USERS) / sizeof(USERS[0]);

// 배열을 앞에서부터 순차 탐색한다. 없으면 -1
inline int findUser(const uint8_t* uid, uint8_t len) {
  for (uint8_t i = 0; i < USER_COUNT; i++) {
    if (USERS[i].matches(uid, len)) return i;
  }
  return -1;
}
