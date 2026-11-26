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

// ---------- 핀 (entrance-board/docs/wiring.html, 2026-11-26판과 같다) ----------
// 화면: 3.5인치 8비트 병렬 실드 "3.5" TFT LCD for arduino uno" (빨간 기판, 화면 칩 HX8357B)
//       실드 기판에 전압 변환 칩이 있어 화면 신호 13개는 보드에 바로 연결한다
const int8_t PIN_LCD_RD  = A0;
const int8_t PIN_LCD_WR  = A1;
const int8_t PIN_LCD_RS  = A2;   // 명령/데이터 (DC)
const int8_t PIN_LCD_CS  = A3;
const int8_t PIN_LCD_RST = A4;
const int8_t PIN_LCD_D[8] = { 8, 9, 2, 3, 4, 5, 6, 7 };   // LCD_D0..D7
// 화면 실드의 RESET 핀은 아두이노 리셋이라 비워 둔다. 5V, 3.3V, GND는 모두 연결한다

// RFID RC522 (SPI): D10 SDA, D11 MOSI, D13 SCK 는 분압(470Ω + 1kΩ, 약 3.4V)을 거친다
//                   D12 MISO 는 바로 연결 (74HCT125N이 오면 그 칩을 거친다)
//                   RST는 보드에 연결하지 않고 3.3V에 올린다(10kΩ 풀업 또는 직결). 소프트웨어 리셋을 쓴다
const uint8_t PIN_RFID_SS  = 10;

// 센서
const uint8_t PIN_DHT      = 0;    // DHT11 DATA (D0)
const uint8_t PIN_DOOR     = 1;    // 문 열림 센서 MC-38 (D1, 내부 풀업)
const uint8_t PIN_PIR      = A5;   // PIR OUT. 3.3V 출력이라 아날로그로 읽는다 (74HCT125N을 거쳐도 그대로 동작)

// ---------- 화면 ----------
// 소프트웨어 병렬 버스라 화면 전체를 칠하면 몇 초 걸린다. 그래서 바뀐 글자 자리만 다시 그린다
const uint8_t  TFT_ROTATION = 1;        // 1 또는 3 (가로). 뒤집혀 보이면 바꾼다
#define TFT_INVERT 0                    // 색이 반전돼 보이면 1로
// RC522는 MFRC522 라이브러리가 정한 4MHz로 통신한다 (라이브러리 설정이라 여기서 바꾸지 않는다)

// ---------- 센서 ----------
const int      PIR_THRESHOLD    = 300;  // analogRead 0~1023. 이보다 크면 사람 감지 (약 1.5V)
const uint8_t  DOOR_OPEN_LEVEL  = HIGH; // 문이 열렸을 때 D1 값. 반대로 동작하면 LOW로

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
  { "USER1",   { 0xEB, 0x02, 0xEC, 0x05 }, 4, { 0, 1 }, 2 },   // 2026-11-26 실물에서 읽은 카드
  { "USER2",   { 0x0D, 0x77, 0x42, 0x06 }, 4, { 1, 0 }, 1 },   // 2026-11-26 실물에서 읽은 카드(또는 키링)
};
const uint8_t USER_COUNT = sizeof(USERS) / sizeof(USERS[0]);

// 배열을 앞에서부터 순차 탐색한다. 없으면 -1
inline int findUser(const uint8_t* uid, uint8_t len) {
  for (uint8_t i = 0; i < USER_COUNT; i++) {
    if (USERS[i].matches(uid, len)) return i;
  }
  return -1;
}
