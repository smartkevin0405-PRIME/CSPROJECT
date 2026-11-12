// Net.cpp
// 응답을 통째로 저장하지 않고, 받는 대로 한 글자씩 읽으며 필요한 값만 골라낸다.
// 보드 RAM이 32KB라서 10KB가 넘는 응답을 버퍼에 담지 않기 위해서다.
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "Net.h"
#include "Devices.h"
#include "secrets.h"

static bool busy = false;
bool netBusy() { return busy; }

// "3분12초후[1번째 전]", "5분 후", "곧 도착" 같은 안내문에서 남은 초를 구한다
static uint16_t parseKoreanEta(const char* s) {
  if (strstr(s, "곧")) return 0;
  uint16_t total = 0, num = 0;
  bool any = false, hasNum = false;
  for (const char* p = s; *p;) {
    if (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); hasNum = true; p++; continue; }
    if (hasNum && strncmp(p, "분", 3) == 0) { total += num * 60; any = true; num = 0; hasNum = false; p += 3; continue; }
    if (hasNum && strncmp(p, "초", 3) == 0) { total += num;      any = true; num = 0; hasNum = false; p += 3; continue; }
    if (*p == '[' || *p == '(') break;     // "[1번째 전]"의 숫자는 시간이 아니다
    num = 0; hasNum = false; p++;
  }
  return any ? total : TRAVEL_UNKNOWN_SEC;
}

// 도착 시간을 작은 순서로 두 개까지 넣는다
static void insertEta(TransitArrival& a, uint16_t eta) {
  if (eta < a.etaSec[0]) { a.etaSec[1] = a.etaSec[0]; a.etaSec[0] = eta; }
  else if (eta < a.etaSec[1]) { a.etaSec[1] = eta; }
}

#if DEMO_MODE
// ---------------- 가짜 데이터 (WiFi 없이 화면과 흐름을 개발할 때) ----------------
bool netBegin() { return true; }
void netLoop() {}
bool netReady() { return true; }
uint32_t netEpoch() { return 0; }

bool fetchWeather(WeatherInfo& out) {
  static uint8_t n = 0;                   // 부를 때마다 날씨를 바꿔 다이얼 동작을 확인한다
  out.kind = (WeatherKind)(n++ % 4);
  out.ok = true;
  out.tempC = 12;
  out.sky = (out.kind == WX_SUNNY) ? 1 : 4;
  out.pty = (out.kind == WX_RAIN) ? 1 : (out.kind == WX_SNOW ? 3 : 0);
  out.nowPty = out.pty;
  return true;
}

bool fetchArrival(const Stop& stop, TransitArrival& out) {
  out.mode = stop.mode;
  strncpy(out.route, stop.route, sizeof(out.route) - 1);
  out.route[sizeof(out.route) - 1] = 0;
  out.etaSec[0] = (stop.mode == MODE_BUS) ? 200 : 95;
  out.etaSec[1] = (stop.mode == MODE_BUS) ? 640 : 400;
  (void)parseKoreanEta; (void)insertEta;
  return true;
}

#else
// ---------------- 실제 조회 ----------------
#include <WiFiS3.h>

bool netBegin() {
  if (WiFi.status() == WL_NO_MODULE) return false;
  WiFi.setTimeout(0);                 // WiFi.begin()이 연결을 기다리지 않고 바로 돌아오게 한다 (기본은 10초 대기)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

// 공유기가 다시 켜지는 등으로 연결이 끊기면, 끊긴 채로 30초가 지날 때마다 연결을 다시 시작한다.
// WiFi.begin()은 연결 명령만 보내고 바로 돌아오므로 loop()를 오래 막지 않는다.
// 이동 시간 모듈의 서버(8080)는 재연결 뒤 다시 열 필요가 없다 (README의 "Wi-Fi 재연결" 참고).
void netLoop() {
  static uint32_t checkedAt = 0;      // 마지막으로 상태를 확인한 시각
  static uint32_t waitFrom = 0;       // 연결돼 있던 마지막 시각 또는 마지막 재연결 시도 시각
  uint32_t now = millis();
  if (now - checkedAt < 1000) return; // 상태 확인은 1초에 한 번만
  checkedAt = now;
  if (netReady()) { waitFrom = now; return; }
  if (now - waitFrom < 30000) return; // 아직 30초가 안 됐다
  waitFrom = now;
  Serial.println(F("WiFi lost, reconnecting"));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool netReady() { return WiFi.status() == WL_CONNECTED; }
uint32_t netEpoch() { return netReady() ? (uint32_t)WiFi.getTime() : 0; }

static uint32_t reqStart = 0;            // 지금 요청을 시작한 시각 (httpOpen에서 정한다)

// ---- 세 조회 함수가 같이 쓰는 버퍼 ----
// 메인 스택은 1KB뿐이다(UNO R4 코어 설정). 그런데 WiFiClient 연결과 snprintf가 그 아래에서
// 약 600바이트를 쓰므로, 큰 버퍼는 함수 안(스택)이 아니라 여기(전역 영역)에 하나씩만 둔다.
// 조회는 한 번에 한 건씩만 하므로 같이 써도 겹치지 않는다.
static char reqPath[320];                // 요청 경로(URL)
static char fieldName[16];               // JSON 키 또는 XML 태그 이름
static char fieldText[48];               // JSON 값 또는 XML 글자

// 한 글자를 읽는다. 요청 전체 제한 시간(HTTP_TIMEOUT_MS)이 지났거나 연결이 끝나면 -1
// 기다리는 동안에도 문 센서를 읽어서, 조회 중에 문이 열려도 놓치지 않게 한다
static int readCh(WiFiClient& c) {
  for (;;) {
    devicesLoop();
    if (millis() - reqStart > HTTP_TIMEOUT_MS) return -1;
    if (c.available()) return c.read();
    if (!c.connected()) return -1;
    delay(1);
  }
}

// GET 요청을 보내고 응답 헤더를 건너뛴다. 상태 코드가 200이면 true
// HTTP/1.0으로 요청해서 본문이 조각(chunked)으로 나뉘어 오지 않게 한다
static bool httpOpen(WiFiClient& c, const char* host, const char* path) {
  reqStart = millis();                   // 여기서부터 HTTP_TIMEOUT_MS 안에 끝나야 한다
  if (!c.connect(host, 80)) return false;
  c.print(F("GET "));
  c.print(path);
  c.print(F(" HTTP/1.0\r\nHost: "));
  c.print(host);
  c.print(F("\r\nConnection: close\r\n\r\n"));
  int ch;
  while ((ch = readCh(c)) >= 0 && ch != ' ') {}          // "HTTP/1.x " 건너뛰기
  char code[4] = { 0, 0, 0, 0 };
  for (uint8_t i = 0; i < 3; i++) { ch = readCh(c); if (ch < 0) return false; code[i] = (char)ch; }
  if (strcmp(code, "200") != 0) return false;
  uint8_t match = 0;                                     // 빈 줄(\r\n\r\n)까지가 헤더
  while (match < 4) {
    ch = readCh(c);
    if (ch < 0) return false;
    if ((match % 2 == 0 && ch == '\r') || (match % 2 == 1 && ch == '\n')) match++;
    else match = (ch == '\r') ? 1 : 0;
  }
  return true;
}

enum ScanResult : uint8_t { SCAN_PAIR, SCAN_END, SCAN_EOF };

// JSON에서 "키":값 쌍을 차례로 꺼낸다. 객체가 닫히면('}') SCAN_END
class JsonScan {
public:
  explicit JsonScan(WiFiClient& client) : c(client), pendingEnd(false) {}

  ScanResult next(char* key, uint8_t klen, char* val, uint8_t vlen) {
    if (pendingEnd) { pendingEnd = false; return SCAN_END; }
    for (;;) {
      int ch = readCh(c);
      if (ch < 0) return SCAN_EOF;
      if (ch == '}') return SCAN_END;
      if (ch != '"') continue;
      if (!readString(key, klen)) return SCAN_EOF;
      ch = skipSpace();
      if (ch < 0) return SCAN_EOF;
      if (ch == '}') return SCAN_END;
      if (ch != ':') continue;                           // 배열 안의 문자열 값이었다
      ch = skipSpace();
      if (ch < 0) return SCAN_EOF;
      if (ch == '"') return readString(val, vlen) ? SCAN_PAIR : SCAN_EOF;
      if (ch == '{' || ch == '[') continue;              // 안쪽 객체나 배열이 시작됨
      uint8_t n = 0;                                     // 숫자, true, null 같은 값
      while (ch >= 0 && ch != ',' && ch != '}' && ch != ']') {
        if (n < vlen - 1 && ch > ' ') val[n++] = (char)ch;
        ch = readCh(c);
      }
      val[n] = 0;
      if (ch == '}') pendingEnd = true;
      return SCAN_PAIR;
    }
  }

private:
  WiFiClient& c;
  bool pendingEnd;

  int skipSpace() {
    int ch;
    do { ch = readCh(c); } while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t');
    return ch;
  }
  // 여는 따옴표 다음부터 닫는 따옴표까지 읽는다
  bool readString(char* out, uint8_t len) {
    uint8_t n = 0;
    for (;;) {
      int ch = readCh(c);
      if (ch < 0) return false;
      if (ch == '"') break;
      if (ch == '\\') { ch = readCh(c); if (ch < 0) return false; }
      if (n < len - 1) out[n++] = (char)ch;
    }
    out[n] = 0;
    return true;
  }
};

// XML에서 <태그>글자</태그> 쌍을 차례로 꺼낸다. </endTag>를 만나면 SCAN_END
class XmlScan {
public:
  explicit XmlScan(WiFiClient& client) : c(client), held(-1) {}

  ScanResult next(const char* endTag, char* tag, uint8_t tlen, char* text, uint8_t xlen) {
    for (;;) {
      int ch;
      if (held >= 0) { ch = held; held = -1; }           // 이미 읽어 둔 태그 첫 글자
      else {
        do { ch = readCh(c); if (ch < 0) return SCAN_EOF; } while (ch != '<');
        ch = readCh(c);
        if (ch < 0) return SCAN_EOF;
      }
      bool closing = (ch == '/');
      uint8_t n = 0;
      if (ch == '?' || ch == '!') {                       // <?xml ...?> 는 건너뛴다
        while ((ch = readCh(c)) >= 0 && ch != '>') {}
        if (ch < 0) return SCAN_EOF;
        continue;
      }
      if (!closing) tag[n++] = (char)ch;
      while ((ch = readCh(c)) >= 0 && ch != '>') {
        if (n < tlen - 1) tag[n++] = (char)ch;
      }
      if (ch < 0) return SCAN_EOF;
      tag[n] = 0;
      if (closing) { if (strcmp(tag, endTag) == 0) return SCAN_END; continue; }
      if (n > 0 && tag[n - 1] == '/') continue;          // <빈태그/>
      uint8_t m = 0;
      while ((ch = readCh(c)) >= 0 && ch != '<') {
        if (m < xlen - 1) text[m++] = (char)ch;
      }
      if (ch < 0) return SCAN_EOF;
      text[m] = 0;
      ch = readCh(c);
      if (ch < 0) return SCAN_EOF;
      if (ch == '/') {                                   // 글자만 든 요소가 닫혔다
        while ((ch = readCh(c)) >= 0 && ch != '>') {}
        if (ch < 0) return SCAN_EOF;
        return SCAN_PAIR;
      }
      held = ch;                                         // 안쪽에 다른 요소가 시작됐다
    }
  }

private:
  WiFiClient& c;
  int held;
};

// UTC 초를 한국 날짜와 시로 바꾼다
static void kstDate(uint32_t epoch, int& y, int& m, int& d, int& hour) {
  uint32_t t = epoch + 9UL * 3600UL;
  hour = (int)((t % 86400UL) / 3600UL);
  int32_t z = (int32_t)(t / 86400UL) + 719468;           // 날짜 계산 (그레고리력)
  int32_t era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

bool fetchWeather(WeatherInfo& out) {
  out.ok = false; out.tempC = -128; out.sky = 0; out.pty = 255; out.nowPty = 255; out.kind = WX_UNKNOWN;
  uint32_t now = netEpoch();
  if (now < 1600000000UL) return false;                  // 시각을 아직 못 받았다

  // 초단기예보는 매시 30분에 만들어져 45분쯤부터 조회된다
  int y, m, d, hour;
  kstDate(now - 45UL * 60UL, y, m, d, hour);
  snprintf(reqPath, sizeof(reqPath),
           "/1360000/VilageFcstInfoService_2.0/getUltraSrtFcst?serviceKey=%s&numOfRows=60&pageNo=1"
           "&dataType=JSON&base_date=%04d%02d%02d&base_time=%02d30&nx=%d&ny=%d",
           KEY_DATA_GO_KR, y, m, d, hour, KMA_NX, KMA_NY);

  busy = true;
  WiFiClient client;
  bool gotTemp = false, gotSky = false, rain = false, snow = false;
  uint8_t nPty = 0, nSky = 0, nT1h = 0, maxSky = 0, firstPty = 0, nowPty = 255;
  if (httpOpen(client, "apis.data.go.kr", reqPath)) {
    JsonScan scan(client);
    char* key = fieldName;
    char* val = fieldText;
    char cat[4] = "", fval[8] = "";
    for (;;) {
      ScanResult r = scan.next(key, sizeof(fieldName), val, sizeof(fieldText));
      if (r == SCAN_EOF) break;
      if (r == SCAN_PAIR) {
        if (strcmp(key, "category") == 0) { strncpy(cat, val, 3); cat[3] = 0; }
        else if (strcmp(key, "fcstValue") == 0) { strncpy(fval, val, 7); fval[7] = 0; }
        continue;
      }
      // 예보 한 건이 끝났다. 같은 항목은 가까운 시각부터 차례로 온다
      if (cat[0] != 0) {
        int v = atoi(fval);
        if (strcmp(cat, "PTY") == 0 && nPty < 3) {
          if (nPty == 0) nowPty = (uint8_t)v;               // 첫 PTY = 가장 가까운 예보 시각 (학습용)
          nPty++;
          if (v == 1 || v == 2 || v == 5 || v == 6) rain = true;
          if (v == 3 || v == 7) snow = true;
          if (firstPty == 0) firstPty = (uint8_t)v;
        } else if (strcmp(cat, "SKY") == 0 && nSky++ < 3) {
          if (v > maxSky) maxSky = (uint8_t)v;
          gotSky = true;
        } else if (strcmp(cat, "T1H") == 0 && nT1h++ == 0) {
          out.tempC = (int8_t)v;
          gotTemp = true;
        }
      }
      cat[0] = 0; fval[0] = 0;
    }
  }
  client.stop();
  busy = false;

  if (!gotTemp || !gotSky) return false;
  out.sky = maxSky;
  out.pty = firstPty;
  out.nowPty = nowPty;
  if (rain) out.kind = WX_RAIN;
  else if (snow) out.kind = WX_SNOW;
  else if (maxSky >= 3) out.kind = WX_CLOUDY;
  else out.kind = WX_SUNNY;
  out.ok = true;
  return true;
}

static bool fetchBus(const Stop& stop, TransitArrival& out) {
  snprintf(reqPath, sizeof(reqPath), "/api/rest/stationinfo/getStationByUid?serviceKey=%s&arsId=%s",
           KEY_DATA_GO_KR, stop.query);
  WiFiClient client;
  bool found = false;
  if (httpOpen(client, "ws.bus.go.kr", reqPath)) {
    XmlScan scan(client);
    char* tag = fieldName;
    char* text = fieldText;
    // 노선 하나의 값. static이라 스택이 아니라 전역 영역에 있고, 지난 값이 남아 있으므로 먼저 비운다
    static char rt[12], adir[40], msg1[48], msg2[48];
    rt[0] = 0; adir[0] = 0; msg1[0] = 0; msg2[0] = 0;
    for (;;) {
      ScanResult r = scan.next("itemList", tag, sizeof(fieldName), text, sizeof(fieldText));
      if (r == SCAN_EOF) break;
      if (r == SCAN_PAIR) {
        if (strcmp(tag, "rtNm") == 0)            { strncpy(rt, text, sizeof(rt) - 1);     rt[sizeof(rt) - 1] = 0; }
        else if (strcmp(tag, "adirection") == 0) { strncpy(adir, text, sizeof(adir) - 1); adir[sizeof(adir) - 1] = 0; }
        else if (strcmp(tag, "arrmsg1") == 0)    { strncpy(msg1, text, sizeof(msg1) - 1); msg1[sizeof(msg1) - 1] = 0; }
        else if (strcmp(tag, "arrmsg2") == 0)    { strncpy(msg2, text, sizeof(msg2) - 1); msg2[sizeof(msg2) - 1] = 0; }
        continue;
      }
      // 노선 하나가 끝났다. 원하는 노선과 방향인지 본다
      bool routeOk = (strcmp(rt, stop.route) == 0);
      bool dirOk = (stop.dir[0] == 0) || (strstr(adir, stop.dir) != nullptr);
      if (routeOk && dirOk) {
        out.etaSec[0] = parseKoreanEta(msg1);
        out.etaSec[1] = parseKoreanEta(msg2);
        found = true;
        break;
      }
      rt[0] = 0; adir[0] = 0; msg1[0] = 0; msg2[0] = 0;
    }
  }
  client.stop();
  return found;
}

static bool fetchSubway(const Stop& stop, TransitArrival& out) {
  snprintf(reqPath, sizeof(reqPath), "/api/subway/%s/json/realtimeStationArrival/0/8/%s",
           KEY_SEOUL_SUBWAY, stop.query);
  WiFiClient client;
  bool found = false;
  if (httpOpen(client, "swopenapi.seoul.go.kr", reqPath)) {
    JsonScan scan(client);
    char* key = fieldName;
    char* val = fieldText;
    // 열차 한 대의 값. static이라 스택이 아니라 전역 영역에 있고, 지난 값이 남아 있으므로 먼저 비운다
    static char line[8], updn[16], barvl[8], msg[48], cd[4];
    line[0] = 0; updn[0] = 0; barvl[0] = 0; msg[0] = 0; cd[0] = 0;
    for (;;) {
      ScanResult r = scan.next(key, sizeof(fieldName), val, sizeof(fieldText));
      if (r == SCAN_EOF) break;
      if (r == SCAN_PAIR) {
        if (strcmp(key, "subwayId") == 0)      { strncpy(line, val, sizeof(line) - 1);   line[sizeof(line) - 1] = 0; }
        else if (strcmp(key, "updnLine") == 0) { strncpy(updn, val, sizeof(updn) - 1);   updn[sizeof(updn) - 1] = 0; }
        else if (strcmp(key, "barvlDt") == 0)  { strncpy(barvl, val, sizeof(barvl) - 1); barvl[sizeof(barvl) - 1] = 0; }
        else if (strcmp(key, "arvlMsg2") == 0) { strncpy(msg, val, sizeof(msg) - 1);     msg[sizeof(msg) - 1] = 0; }
        else if (strcmp(key, "arvlCd") == 0)   { strncpy(cd, val, sizeof(cd) - 1);       cd[sizeof(cd) - 1] = 0; }
        continue;
      }
      // 열차 한 대가 끝났다. 호선과 방향이 맞으면 도착 시간을 넣는다
      if (line[0] != 0 && strcmp(line, stop.route) == 0 && strcmp(updn, stop.dir) == 0) {
        uint16_t eta = TRAVEL_UNKNOWN_SEC;
        int sec = atoi(barvl);
        if (sec > 0) eta = (uint16_t)sec;                 // 남은 초가 오면 그대로 쓴다
        else eta = parseKoreanEta(msg);                   // 아니면 안내문에서 구한다
        if (eta == TRAVEL_UNKNOWN_SEC) {
          int code = atoi(cd);                            // 0 진입, 1 도착, 3~5 전역
          if (cd[0] != 0 && (code == 0 || code == 1)) eta = 0;
          else if (code >= 3 && code <= 5) eta = 120;
        }
        if (eta != TRAVEL_UNKNOWN_SEC) { insertEta(out, eta); found = true; }
      }
      line[0] = 0; updn[0] = 0; barvl[0] = 0; msg[0] = 0; cd[0] = 0;
    }
  }
  client.stop();
  return found;
}

bool fetchArrival(const Stop& stop, TransitArrival& out) {
  out.mode = stop.mode;
  strncpy(out.route, stop.route, sizeof(out.route) - 1);
  out.route[sizeof(out.route) - 1] = 0;
  out.etaSec[0] = out.etaSec[1] = TRAVEL_UNKNOWN_SEC;
  if (!netReady()) return false;
  busy = true;
  bool ok = (stop.mode == MODE_BUS) ? fetchBus(stop, out) : fetchSubway(stop, out);
  busy = false;
  return ok;
}
#endif
