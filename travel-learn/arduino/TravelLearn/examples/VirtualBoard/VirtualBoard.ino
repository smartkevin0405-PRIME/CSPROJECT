// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// VirtualBoard: 센서가 하나도 없는 UNO R4 WiFi 한 장으로 TravelLearn을 시험하는 예제.
//  - 시리얼 모니터(115200, 줄 끝 "새 줄")에서 명령을 입력해 "가상 출발"과 "가상 버스 도착 시간"을 만든다.
//  - 실제 폰 앱과 통신해 출발 신호 전달, 이동 시간 기록 수신, 여유 시간 계산을 확인할 수 있다.
//
// 준비
//  1) arduino_secrets.h.example 을 arduino_secrets.h 로 복사하고 값을 채운다.
//  2) 업로드 후 시리얼 모니터에 나오는 보드 IP를 폰 앱의 "현관판 주소"에 입력한다.
//     (공유기 설정에서 이 보드의 IP를 고정(DHCP 예약)해 두면 주소가 바뀌지 않는다.)
//  3) 폰 앱에도 같은 사용자 번호와 같은 32자리 키를 입력한다.

#include <WiFiS3.h>
#include <TravelLearn.h>
#include "arduino_secrets.h"

// ---- 함수 미리 선언(Arduino IDE는 자동으로 하지만, 일반 C++에서도 컴파일되도록) ----
static void connectWifi();
static void keepWifi();
static void onDepartResult(uint8_t user, uint32_t trip, bool delivered);
static void onTripStored(uint8_t user, uint8_t stop, uint32_t trip, bool applied);
static void printHelp();
static uint8_t currentTb();
static void readSerialLine();
static void handleLine(char* line);
static void cmdSlack(int user, int stop, long eta);
static void cmdPhone();

TravelLearn tl;

static uint8_t gWx = 0;               // 가상 날씨 구간: 0 강수 없음, 1 비, 2 눈
static char gLine[48];                // 시리얼 한 줄을 모으는 버퍼
static uint8_t gLen = 0;
static unsigned long gLastWifiTry = 0;

// ---------------------------------------------------------------- Wi-Fi

static void connectWifi() {
  Serial.print("Wi-Fi 연결 중");
  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
  }
  Serial.println();
  Serial.print("연결됨. 보드 IP(폰 앱에 입력): ");
  Serial.println(WiFi.localIP());
}

// 연결이 끊기면 10초에 한 번씩 다시 시도한다.
static void keepWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - gLastWifiTry < 10000UL) return;
  gLastWifiTry = millis();
  Serial.println("Wi-Fi 끊김. 다시 연결을 시도합니다.");
  WiFi.begin(SECRET_SSID, SECRET_PASS);
}

// ---------------------------------------------------------------- 콜백

static void onDepartResult(uint8_t user, uint32_t trip, bool delivered) {
  char s[96];
  snprintf(s, sizeof(s), "[출발 신호] 사용자 %d, 이동 %lu: %s", (int)user, (unsigned long)trip,
           delivered ? "폰에 전달됨" : "전달 실패(폰에 연결 안 됨)");
  Serial.println(s);
}

static void onTripStored(uint8_t user, uint8_t stop, uint32_t trip, bool applied) {
  char s[96];
  snprintf(s, sizeof(s), "[기록 수신] 사용자 %d, 정류장 %d, 이동 %lu: %s", (int)user, (int)stop,
           (unsigned long)trip, applied ? "통계에 반영" : "저장만 하고 반영 안 함");
  Serial.println(s);
}

// ---------------------------------------------------------------- 시간대

// 폰이 알려 준 시각으로 시간대(0~3)를 구한다. 시각을 모르면 1(낮)을 쓴다.
static uint8_t currentTb() {
  uint32_t epoch;
  int16_t tzMin;
  if (!tl.phoneTime(&epoch, &tzMin)) return 1;
  int32_t sec = (int32_t)(epoch % 86400UL) + (int32_t)tzMin * 60;
  sec = ((sec % 86400L) + 86400L) % 86400L;
  return TravelLearn::tbFromHour((int)(sec / 3600L));
}

// ---------------------------------------------------------------- 명령

static void printHelp() {
  Serial.println("명령어 (입력 후 Enter)");
  Serial.println("  d <사용자>              가상 출발(문이 열린 것으로 처리)");
  Serial.println("  w <0|1|2>               가상 날씨: 0 강수 없음, 1 비, 2 눈");
  Serial.println("  e <사용자> <정류장> <초>   버스가 <초> 뒤 도착한다고 보고 여유 시간 계산");
  Serial.println("  s                       통계 표 출력");
  Serial.println("  c <사용자>              출발 취소");
  Serial.println("  x                       통계 전부 지우기");
  Serial.println("  p                       폰 연결 상태와 보드 IP");
  Serial.println("  h                       도움말");
}

static void cmdSlack(int user, int stop, long eta) {
  uint8_t tb = currentTb();
  TlSlack r = tl.slack((uint8_t)user, (uint8_t)stop, gWx, tb, (int32_t)eta);
  const char* verdict;
  if (r.slackS >= 60) verdict = "여유 있음";
  else if (r.slackS >= 0) verdict = "지금 출발";
  else verdict = "이번 버스는 어려움";
  char s[160];
  snprintf(s, sizeof(s), "여유 %ld초 (이동 %u초 + 안전 %u초, 근거 level %u, 표본 %u개) -> %s",
           (long)r.slackS, (unsigned)r.walkS, (unsigned)r.safetyS, (unsigned)r.level, (unsigned)r.n, verdict);
  Serial.println(s);
}

static void cmdPhone() {
  for (int u = 1; u <= TL_MAX_USERS; u++) {
    char s[64];
    snprintf(s, sizeof(s), "사용자 %d 폰: %s", u, tl.phoneOnline((uint8_t)u) ? "연결됨" : "연락 안 됨");
    Serial.println(s);
  }
  Serial.print("보드 IP: ");
  Serial.println(WiFi.localIP());
}

static void handleLine(char* line) {
  char cmd = line[0];
  int a = 0, b = 0;
  long c = 0;
  switch (cmd) {
    case 'd':
      if (sscanf(line + 1, "%d", &a) == 1 && a >= 1 && a <= TL_MAX_USERS) {
        uint32_t trip = tl.notifyDeparture((uint8_t)a, 0, gWx, TL_TB_UNKNOWN);
        char s[64];
        snprintf(s, sizeof(s), "가상 출발: 사용자 %d, 이동 ID %lu", a, (unsigned long)trip);
        Serial.println(trip ? s : "출발 거절(사용자 번호나 begin() 확인)");
      } else {
        Serial.println("사용법: d <사용자 1~4>");
      }
      break;
    case 'w':
      if (sscanf(line + 1, "%d", &a) == 1 && a >= 0 && a < TL_NUM_WX) {
        gWx = (uint8_t)a;
        Serial.println(a == 0 ? "날씨: 강수 없음" : (a == 1 ? "날씨: 비" : "날씨: 눈"));
      } else {
        Serial.println("사용법: w <0|1|2>");
      }
      break;
    case 'e':
      if (sscanf(line + 1, "%d %d %ld", &a, &b, &c) == 3 && a >= 1 && a <= TL_MAX_USERS &&
          b >= 1 && b <= TL_MAX_STOPS) {
        cmdSlack(a, b, c);
      } else {
        Serial.println("사용법: e <사용자> <정류장> <버스까지 남은 초>");
      }
      break;
    case 's':
      tl.dumpStats(Serial);
      break;
    case 'c':
      if (sscanf(line + 1, "%d", &a) == 1 && a >= 1 && a <= TL_MAX_USERS) {
        tl.cancelDeparture((uint8_t)a);
        Serial.println("취소 신호를 보냅니다.");
      } else {
        Serial.println("사용법: c <사용자 1~4>");
      }
      break;
    case 'x':
      tl.resetStats();
      Serial.println("통계를 모두 지웠습니다.");
      break;
    case 'p':
      cmdPhone();
      break;
    case 'h':
    case '?':
      printHelp();
      break;
    default:
      Serial.println("모르는 명령입니다. h 를 입력하면 도움말이 나옵니다.");
      break;
  }
}

// 시리얼에서 글자를 모아 한 줄이 완성되면 처리한다(기다리지 않는다).
static void readSerialLine() {
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      gLine[gLen] = '\0';
      if (gLen > 0) handleLine(gLine);
      gLen = 0;
    } else if (gLen < sizeof(gLine) - 1) {
      gLine[gLen++] = ch;
    }
  }
}

// ---------------------------------------------------------------- 시작·반복

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}   // USB 시리얼이 열릴 때까지 최대 3초

  connectWifi();

  tl.setUserKey(1, SECRET_KEY_USER1);
#ifdef SECRET_KEY_USER2
  tl.setUserKey(2, SECRET_KEY_USER2);
#endif

  // 표본이 쌓이기 전에 쓸 초기 이동 시간(초). 정류장 1은 5분, 2는 7분으로 가정.
  tl.setDefaultWalk(1, 1, 300);
  tl.setDefaultWalk(1, 2, 420);
#ifdef SECRET_KEY_USER2
  tl.setDefaultWalk(2, 1, 300);
  tl.setDefaultWalk(2, 2, 420);
#endif

  tl.onDepartResult(onDepartResult);
  tl.onTripStored(onTripStored);

  if (tl.begin()) Serial.println("TravelLearn 시작 (EEPROM 정상)");
  else Serial.println("TravelLearn 시작했지만 EEPROM 저장소에 문제가 있습니다.");
  printHelp();
}

void loop() {
  keepWifi();
  tl.loop();
  readSerialLine();
}
