// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// TravelLearn 라이브러리 단위 시험(PC에서 실행). 실제 보드에 올리는 src/ 코드를 그대로 컴파일해 시험한다.
//   빌드·실행: extras/host/build.sh test
#include <Arduino.h>
#include <EEPROM.h>
#include <TravelLearn.h>
#include "TlJson.h"
#include "TlSha256.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int gPass = 0, gFail = 0;
#define CHECK(name, cond) do { if (cond) { gPass++; } else { gFail++; printf("FAIL  %s  (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)
static void section(const char* s) { printf("-- %s\n", s); }

static const char* KEY_HEX = "000102030405060708090a0b0c0d0e0f";
static uint8_t KEY[16];

static std::string hexOf(const uint8_t* d, size_t n) {
  std::vector<char> b(n * 2 + 1);
  tlHexEncode(d, n, b.data());
  return std::string(b.data());
}

// ---------------------------------------------------------------- 요청 만들기
static TlHttpReq mkReq(const char* method, const char* path, const char* body, uint8_t user, const uint8_t* key) {
  TlHttpReq r;
  memset(&r, 0, sizeof(r));
  snprintf(r.method, sizeof(r.method), "%s", method);
  snprintf(r.path, sizeof(r.path), "%s", path);
  snprintf(r.body, sizeof(r.body), "%s", body);
  r.bodyLen = (uint16_t)strlen(r.body);
  r.user = user;
  if (key) tlMacRequest(key, method, path, body, r.mac);
  return r;
}

static std::string tripBody(uint32_t trip, int user, int stop, int wx, int tb, const char* st, unsigned long durMs, int q = 0, int src = 0) {
  char b[256];
  snprintf(b, sizeof(b), "{\"v\":1,\"trip\":%lu,\"user\":%d,\"stop\":%d,\"wx\":%d,\"tb\":%d,\"st\":\"%s\",\"dur_ms\":%lu,\"q\":%d,\"src\":%d,\"epoch\":1763004600}",
           (unsigned long)trip, user, stop, wx, tb, st, durMs, q, src);
  return std::string(b);
}

struct Resp { int status; std::string body; std::string mac; };

static Resp call(TravelLearn& tl, const TlHttpReq& rq) {
  static char out[TL_BODY_MAX + 1];
  char mac[TL_MAC_HEX + 1];
  Resp r;
  r.status = tl.handleRequest(&rq, out, sizeof(out), mac);
  r.body = out;
  r.mac = mac;
  return r;
}

static Resp postTrip(TravelLearn& tl, uint32_t trip, int stop, int wx, int tb, const char* st, unsigned long durMs) {
  std::string b = tripBody(trip, 1, stop, wx, tb, st, durMs);
  return call(tl, mkReq("POST", "/v1/trips", b.c_str(), 1, KEY));
}

static uint32_t jU(const std::string& body, const char* key, uint32_t def = 0xFFFFFFFFUL) {
  uint32_t v;
  return tlJsonGetU32(body.c_str(), key, &v) ? v : def;
}
static std::string jS(const std::string& body, const char* key) {
  char b[64];
  return tlJsonGetStr(body.c_str(), key, b, sizeof(b)) ? std::string(b) : std::string("");
}

// 전원을 껐다 켠 것처럼 새 객체를 만든다(EEPROM 내용은 그대로).
static TravelLearn* boot() {
  TravelLearn* tl = new TravelLearn();
  tl->setUserKey(1, KEY_HEX);
  tl->begin(0);
  return tl;
}

// ---------------------------------------------------------------- 1. SHA-256 / HMAC
static void testCrypto() {
  section("SHA-256, HMAC");
  uint8_t d[32];
  TlSha256Ctx c;
  tlSha256Init(&c); tlSha256Update(&c, "abc", 3); tlSha256Final(&c, d);
  CHECK("sha256(abc)", hexOf(d, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  tlSha256Init(&c); tlSha256Final(&c, d);
  CHECK("sha256(empty)", hexOf(d, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const char* m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  tlSha256Init(&c); tlSha256Update(&c, m2, strlen(m2)); tlSha256Final(&c, d);
  CHECK("sha256(56바이트: 패딩이 두 블록)", hexOf(d, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  tlSha256Init(&c);
  char a1000[1000]; memset(a1000, 'a', sizeof(a1000));
  for (int i = 0; i < 1000; i++) tlSha256Update(&c, a1000, sizeof(a1000));
  tlSha256Final(&c, d);
  CHECK("sha256(a x 1,000,000)", hexOf(d, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // 조각을 다르게 나눠 넣어도 같은 결과
  tlSha256Init(&c);
  for (size_t i = 0; i < strlen(m2); i++) tlSha256Update(&c, m2 + i, 1);
  tlSha256Final(&c, d);
  CHECK("sha256 1바이트씩 넣기", hexOf(d, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  TlHmacCtx h;
  uint8_t k1[20]; memset(k1, 0x0b, sizeof(k1));
  tlHmacInit(&h, k1, 20); tlHmacUpdate(&h, "Hi There", 8); tlHmacFinal(&h, d);
  CHECK("HMAC RFC4231 #1", hexOf(d, 32) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  tlHmacInit(&h, (const uint8_t*)"Jefe", 4); tlHmacUpdate(&h, "what do ya want for nothing?", 28); tlHmacFinal(&h, d);
  CHECK("HMAC RFC4231 #2", hexOf(d, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  uint8_t k6[131]; memset(k6, 0xaa, sizeof(k6));
  const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  tlHmacInit(&h, k6, 131); tlHmacUpdate(&h, m6, strlen(m6)); tlHmacFinal(&h, d);
  CHECK("HMAC RFC4231 #6(블록보다 긴 키)", hexOf(d, 32) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

  // 통신규격 3장의 기준값
  char mac[TL_MAC_HEX + 1], rmac[TL_MAC_HEX + 1];
  const char* body = "{\"v\":1,\"user\":1,\"ip\":\"192.168.0.23\",\"port\":8765,\"epoch\":1763004600,\"tz\":540,\"pending\":0}";
  tlMacRequest(KEY, "POST", "/v1/hello", body, mac);
  CHECK("요청 MAC 기준값", strcmp(mac, "ae07c41f8c9413e3c0da3d1b4dc02d61") == 0);
  tlMacResponse(KEY, "/v1/hello", mac, "{\"ok\":1,\"boot\":127,\"reg\":1}", rmac);
  CHECK("응답 MAC 기준값", strcmp(rmac, "ad7a99b393afc68ae01228bb496c8237") == 0);

  uint8_t kb[16];
  CHECK("hex 해석", tlHexDecode(KEY_HEX, kb, 16) && memcmp(kb, KEY, 16) == 0);
  CHECK("hex 길이 오류", !tlHexDecode("0001", kb, 16));
  CHECK("hex 문자 오류", !tlHexDecode("zz0102030405060708090a0b0c0d0e0f", kb, 16));
  CHECK("고정 시간 비교", tlConstEq("abcd", "abcd", 4) && !tlConstEq("abcd", "abce", 4));
}

// ---------------------------------------------------------------- 2. JSON
static void testJson() {
  section("JSON");
  uint32_t u; int32_t i; char s[16];
  const char* j = " { \"v\":1, \"st\":\"trip\", \"trip\":4294967295 ,\"tz\":-540,\"name\":\"a\\\"b\" } ";
  CHECK("유효", tlJsonValid(j));
  CHECK("u32 최댓값", tlJsonGetU32(j, "trip", &u) && u == 4294967295UL);
  CHECK("값 안의 글자를 키로 착각하지 않음", tlJsonGetStr(j, "st", s, sizeof(s)) && strcmp(s, "trip") == 0);
  CHECK("음수 i32", tlJsonGetI32(j, "tz", &i) && i == -540);
  CHECK("음수는 u32가 아님", !tlJsonGetU32(j, "tz", &u));
  CHECK("이스케이프", tlJsonGetStr(j, "name", s, sizeof(s)) && strcmp(s, "a\"b") == 0);
  CHECK("없는 키", !tlJsonGetU32(j, "nope", &u));
  CHECK("문자열을 숫자로 읽지 않음", !tlJsonGetU32(j, "st", &u));
  CHECK("u32 넘침", !tlJsonGetU32("{\"a\":4294967296}", "a", &u));
  CHECK("실수 거부", !tlJsonGetU32("{\"a\":1.5}", "a", &u));
  CHECK("문자열 버퍼 넘침", !tlJsonGetStr("{\"a\":\"0123456789abcdefXYZ\"}", "a", s, sizeof(s)));
  CHECK("중첩 객체 무효", !tlJsonValid("{\"a\":{\"b\":1}}"));
  CHECK("배열 무효", !tlJsonValid("{\"a\":[1]}"));
  CHECK("잘린 본문 무효", !tlJsonValid("{\"a\":1"));
  CHECK("빈 본문 무효", !tlJsonValid(""));
  CHECK("객체 뒤 쓰레기 무효", !tlJsonValid("{\"a\":1}x"));
  CHECK("빈 객체 유효", tlJsonValid("{}"));
}

// ---------------------------------------------------------------- 3. 통계 계산
struct RefCell { int n = 0; int strikes = 0; int meanDs = 0; int sdDs = 0; };
static int refDs(double sec) { double v = sec * 10.0 + 0.5; if (v < 0) return 0; if (v > 65535) return 65535; return (int)v; }
// 설계 문서의 식을 double로 그대로 쓴 기준 구현
static bool refUpdate(RefCell& c, double x, const char** why) {
  *why = "";
  if (x < TL_MIN_DUR_S || x > TL_MAX_DUR_S) { *why = "range"; return false; }
  double mean = c.meanDs / 10.0, sd = c.sdDs / 10.0;
  if (c.n >= TL_OUTLIER_MIN_N) {
    double lim = TL_OUTLIER_SIGMA * sd;
    if (lim < TL_OUTLIER_FLOOR_S) lim = TL_OUTLIER_FLOOR_S;
    if (fabs(x - mean) > lim && c.strikes < TL_OUTLIER_STRIKES) { c.strikes++; *why = "outlier"; return false; }
  }
  if (c.n == 0) { mean = x; sd = 0; c.n = 1; }
  else {
    int n1 = c.n < 255 ? c.n + 1 : 255;
    double alpha = 1.0 / (n1 < TL_EW_CAP ? n1 : TL_EW_CAP);
    double d = x - mean, var = sd * sd;
    mean += alpha * d;
    var = (1 - alpha) * (var + alpha * d * d);
    sd = sqrt(var);
    c.n = n1;
  }
  c.strikes = 0; c.meanDs = refDs(mean); c.sdDs = refDs(sd);
  return true;
}

static void testStats() {
  section("통계 갱신");
  HostEeprom::inst().reset();
  TravelLearn* tl = boot();
  TlStats& st = tl->stats();
  TlWhy why; TlCell after;

  CHECK("칸 번호", TlStats::cellIndex(1, 1, 0, 0) == 0 && TlStats::cellIndex(1, 2, 1, 0) == 16
        && TlStats::cellIndex(TL_MAX_USERS, TL_MAX_STOPS, 2, 3) == TL_NUM_CELLS - 1);
  CHECK("칸 번호 범위 밖", TlStats::cellIndex(0, 1, 0, 0) < 0 && TlStats::cellIndex(1, TL_MAX_STOPS + 1, 0, 0) < 0
        && TlStats::cellIndex(1, 1, 3, 0) < 0 && TlStats::cellIndex(1, 1, 0, 4) < 0);

  // 임의 수열 400개를 기준 구현과 한 걸음씩 비교(이상값·범위 밖 포함)
  srand(12345);
  RefCell ref;
  bool same = true;
  int nOut = 0, nRange = 0;
  for (int k = 0; k < 400; k++) {
    double base = (k < 200) ? 400.0 : 520.0;                  // 중간에 생활 패턴이 바뀐다
    double x = base + (rand() % 600 - 300) / 10.0;
    if (k % 37 == 5) x = 1500.0;                              // 가끔 엉뚱한 값
    if (k % 53 == 7) x = 5.0;                                 // 범위 밖
    unsigned long ms = (unsigned long)(x * 1000.0 + 0.5);
    const char* rw;
    bool ra = refUpdate(ref, ms / 1000.0, &rw);
    bool a = st.update(1, 1, 0, 0, ms, &why, &after);
    if (ra != a || strcmp(rw, tlWhyStr(why)) != 0 || after.n != ref.n || after.strikes != ref.strikes
        || abs((int)after.meanDs - ref.meanDs) > 1 || abs((int)after.sdDs - ref.sdDs) > 1) {
      same = false;
      printf("  불일치 k=%d x=%.1f lib(n=%u m=%u sd=%u st=%u %s) ref(n=%d m=%d sd=%d st=%d %s)\n", k, x,
             after.n, after.meanDs, after.sdDs, after.strikes, tlWhyStr(why), ref.n, ref.meanDs, ref.sdDs, ref.strikes, rw);
      break;
    }
    // float와 double의 반올림 차이가 쌓이지 않도록 기준을 라이브러리 값에 맞춘다(차이는 위에서 1 이하로 확인).
    ref.meanDs = after.meanDs; ref.sdDs = after.sdDs;
    if (why == TL_WHY_OUTLIER) nOut++;
    if (why == TL_WHY_RANGE) nRange++;
  }
  CHECK("기준 구현과 400걸음 일치(0.1초 이내)", same);
  CHECK("이상값·범위 밖 경로가 실제로 실행됨", nOut >= 5 && nRange >= 5);
  CHECK("패턴이 바뀐 뒤 평균이 새 값(520초) 근처로 따라감", after.meanDs > 4900 && after.meanDs < 5500);

  // 손 계산: 400, 420, 380초
  st.update(1, 2, 1, 2, 400000, &why, &after);
  CHECK("첫 표본: 평균 = 값, 편차 0", after.n == 1 && after.meanDs == 4000 && after.sdDs == 0);
  st.update(1, 2, 1, 2, 420000, &why, &after);
  CHECK("둘째: 평균 410, 편차 10", after.n == 2 && after.meanDs == 4100 && after.sdDs == 100);
  st.update(1, 2, 1, 2, 380000, &why, &after);
  CHECK("셋째: 평균 400, 편차 16.3", after.n == 3 && after.meanDs == 4000 && after.sdDs == 163);

  // 이상값 3연속 규칙
  for (int k = 0; k < 3; k++) st.update(1, 2, 1, 2, 400000, &why, &after);      // n = 6
  TlCell before = after;
  bool a1 = st.update(1, 2, 1, 2, 900000, &why, &after);
  CHECK("이상값 1회: 미반영, strikes 1", !a1 && why == TL_WHY_OUTLIER && after.strikes == 1 && after.n == before.n && after.meanDs == before.meanDs);
  bool a2 = st.update(1, 2, 1, 2, 900000, &why, &after);
  CHECK("이상값 2회: 미반영, strikes 2", !a2 && after.strikes == 2);
  bool a3 = st.update(1, 2, 1, 2, 900000, &why, &after);
  CHECK("이상값 3회 연속: 반영, strikes 0", a3 && after.strikes == 0 && after.n == before.n + 1 && after.meanDs > before.meanDs);
  st.update(1, 2, 2, 0, 400000, &why, &after);
  for (int k = 0; k < 5; k++) st.update(1, 2, 2, 0, 400000, &why, &after);
  st.update(1, 2, 2, 0, 900000, &why, &after);
  st.update(1, 2, 2, 0, 405000, &why, &after);
  CHECK("정상값이 들어오면 strikes 초기화", after.strikes == 0);

  bool r1 = st.update(1, 3, 0, 0, 19999, &why, &after);
  CHECK("19.999초 → range", !r1 && why == TL_WHY_RANGE && after.n == 0);
  bool r2 = st.update(1, 3, 0, 0, 3600001, &why, &after);
  CHECK("3600.001초 → range", !r2 && why == TL_WHY_RANGE);
  bool r3 = st.update(1, 3, 0, 0, 20000, &why, &after);
  bool r4 = st.update(1, 3, 0, 1, 3600000, &why, &after);
  CHECK("경계값 20초·3600초는 반영", r3 && r4);
  bool r5 = st.update(1, 0, 0, 0, 300000, &why, &after);
  CHECK("정류장 0 → nostop", !r5 && why == TL_WHY_NOSTOP);

  // 표본 수는 255에서 멈춘다
  for (int k = 0; k < 300; k++) st.update(2, 1, 0, 0, 300000 + (k % 7) * 1000, &why, &after);
  CHECK("표본 수 상한 255", after.n == 255);
  delete tl;
}

static void testSlack() {
  section("표본 부족 시 대체, 여유 시간");
  HostEeprom::inst().reset();
  TravelLearn* tl = boot();
  TlWhy why; TlCell after;
  TlSlack s = tl->slack(1, 2, 0, 0, 600);
  CHECK("수준 3: 기본값 300초, 편차 45, 여유 225", s.level == 3 && s.n == 0 && s.walkS == 300 && s.safetyS == 75 && s.slackS == 225);
  tl->setDefaultWalk(1, 2, 500);
  s = tl->slack(1, 2, 0, 0, 600);
  CHECK("수준 3: 사용자·정류장별 초기 설정값 500초", s.level == 3 && s.walkS == 500 && s.safetyS == 105 && s.slackS == -5);
  s = tl->slack(1, 1, 0, 0, 600);
  CHECK("다른 정류장은 여전히 기본값", s.walkS == 300);

  tl->stats().update(1, 2, 2, 3, 400000, &why, &after);          // 다른 칸(눈·밤)에 표본 1개
  s = tl->slack(1, 2, 0, 0, 600);
  CHECK("수준 2: 같은 정류장 전체 묶음(표본 1 → 편차는 15%)", s.level == 2 && s.n == 1 && s.walkS == 400 && s.safetyS == 90 && s.slackS == 110);

  tl->stats().update(1, 2, 1, 0, 400000, &why, &after);          // 비·아침 2개 → 아침 묶음은 아직 2개
  tl->stats().update(1, 2, 1, 0, 420000, &why, &after);
  s = tl->slack(1, 2, 0, 0, 600);
  CHECK("표본 2개로는 수준 1이 안 됨(수준 2 유지)", s.level == 2 && s.n == 3);
  tl->stats().update(1, 2, 2, 0, 380000, &why, &after);          // 눈·아침 1개 → 아침 묶음 3개
  s = tl->slack(1, 2, 0, 0, 600);
  // 아침 묶음: (비: n2 m410 sd10), (눈: n1 m380 sd0) → 평균 400, 분산 = (2*(100+100) + 1*(0+400))/3 = 266.7 → 편차 16.3
  CHECK("수준 1: 같은 시간대 묶음(평균 400, 편차 16.3 → 안전 46)", s.level == 1 && s.n == 3 && s.walkS == 400 && s.safetyS == 46 && s.slackS == 154);

  tl->stats().update(1, 2, 0, 0, 300000, &why, &after);
  tl->stats().update(1, 2, 0, 0, 300000, &why, &after);
  tl->stats().update(1, 2, 0, 0, 330000, &why, &after);
  s = tl->slack(1, 2, 0, 0, 600);
  // 해당 칸: 300,300,330 → 평균 310, 분산 (2/3)*(0 + (1/3)*900) = 200 → 편차 14.1
  CHECK("수준 0: 해당 칸(평균 310, 편차 14.1 → 안전 44)", s.level == 0 && s.n == 3 && s.walkS == 310 && s.safetyS == 44 && s.slackS == 246);
  s = tl->slack(1, 2, 0, 1, 600);
  CHECK("낮에는 기록이 없어 같은 날씨 묶음으로(수준 1)", s.level == 1 && s.walkS == 310);
  s = tl->slack(1, 2, 0, TL_TB_UNKNOWN, 600);
  CHECK("시간대를 모르면 같은 날씨 묶음", s.level == 1 && s.walkS == 310);
  s = tl->slack(1, 2, 9, 0, 600);
  CHECK("날씨 값이 범위를 벗어나면 강수 없음으로 취급", s.level == 0 && s.walkS == 310);
  s = tl->slack(1, 2, 0, 0, -1);
  CHECK("ETA를 모르면(-1) 여유는 음수", s.slackS < 0);
  s = tl->slack(0, 2, 0, 0, 600);
  CHECK("사용자 0: 죽지 않고 기본값", s.level == 3 && s.walkS == 300);
  s = tl->slack(1, 0, 0, 0, 600);
  CHECK("정류장 0: 죽지 않고 기본값", s.level == 3 && s.walkS == 300);
  s = tl->slack(9, 9, 9, 9, 600);
  CHECK("모든 인자 범위 밖: 죽지 않고 기본값", s.level == 3);
  CHECK("PTY 변환", TravelLearn::wxFromPty(0) == 0 && TravelLearn::wxFromPty(1) == 1 && TravelLearn::wxFromPty(2) == 1
        && TravelLearn::wxFromPty(5) == 1 && TravelLearn::wxFromPty(6) == 1 && TravelLearn::wxFromPty(3) == 2
        && TravelLearn::wxFromPty(7) == 2 && TravelLearn::wxFromPty(-1) == 0 && TravelLearn::wxFromPty(99) == 0);
  CHECK("시간대 변환", TravelLearn::tbFromHour(5) == 3 && TravelLearn::tbFromHour(6) == 0 && TravelLearn::tbFromHour(9) == 0
        && TravelLearn::tbFromHour(10) == 1 && TravelLearn::tbFromHour(15) == 1 && TravelLearn::tbFromHour(16) == 2
        && TravelLearn::tbFromHour(20) == 2 && TravelLearn::tbFromHour(21) == 3 && TravelLearn::tbFromHour(0) == 3);
  delete tl;
}

// ---------------------------------------------------------------- 4. 요청 처리
static void testHandlers() {
  section("요청 처리(인증·형식·순서)");
  HostEeprom::inst().reset();
  TravelLearn* tl = boot();
  uint8_t bad[16]; memset(bad, 0xFF, sizeof(bad));

  Resp r = call(*tl, mkReq("GET", "/v1/ping", "", 0, NULL));
  CHECK("ping: 인증 없이 200, MAC 없음", r.status == 200 && r.mac.empty() && jS(r.body, "dev") == "board" && jU(r.body, "boot") == 1);

  const char* hello = "{\"v\":1,\"user\":1,\"ip\":\"192.168.0.23\",\"port\":8765,\"epoch\":1763004600,\"tz\":540,\"pending\":0}";
  TlHttpReq hq = mkReq("POST", "/v1/hello", hello, 1, KEY);
  r = call(*tl, hq);
  char want[TL_MAC_HEX + 1];
  tlMacResponse(KEY, "/v1/hello", hq.mac, r.body.c_str(), want);
  CHECK("hello 등록", r.status == 200 && jU(r.body, "reg") == 1 && r.body == "{\"ok\":1,\"boot\":1,\"reg\":1}");
  CHECK("hello 응답 MAC", r.mac == want);
  uint8_t ip[4]; uint16_t port;
  CHECK("폰 주소 저장", tl->store().getPhone(1, ip, &port) && ip[0] == 192 && ip[3] == 23 && port == 8765);
  CHECK("hello 후 phoneOnline", tl->phoneOnline(1) && !tl->phoneOnline(2));
  uint32_t ep; int16_t tz;
  CHECK("폰 시각 보관", tl->phoneTime(&ep, &tz) && ep >= 1763004600UL && ep < 1763004700UL && tz == 540);
  uint32_t commitsBefore = tl->store().commitCount();
  call(*tl, hq);
  CHECK("같은 주소의 hello는 EEPROM에 다시 쓰지 않음", tl->store().commitCount() == commitsBefore);

  // 대문자 MAC도 받아들이고, 응답 MAC은 소문자 요청 MAC으로 묶는다
  TlHttpReq up = hq;
  for (int i = 0; i < TL_MAC_HEX; i++) if (up.mac[i] >= 'a' && up.mac[i] <= 'f') up.mac[i] = (char)(up.mac[i] - 32);
  r = call(*tl, up);
  CHECK("대문자 MAC 허용, 응답 MAC은 소문자 기준", r.status == 200 && r.mac == want);

  r = call(*tl, mkReq("POST", "/v1/hello", hello, 1, bad));
  CHECK("키 불일치 → 401, MAC 없음", r.status == 401 && r.mac.empty() && jS(r.body, "err") == "auth");
  TlHttpReq tam = mkReq("POST", "/v1/hello", hello, 1, KEY);
  tam.body[strlen(tam.body) - 2] = '1';
  CHECK("본문 변조 → 401", call(*tl, tam).status == 401);
  CHECK("헤더 사용자 ≠ 본문 사용자 → 401", call(*tl, mkReq("POST", "/v1/hello", hello, 2, KEY)).status == 401);
  CHECK("키가 없는 사용자 → 401", call(*tl, mkReq("POST", "/v1/hello", hello, 3, KEY)).status == 401);
  TlHttpReq nomac = mkReq("POST", "/v1/hello", hello, 1, NULL);
  CHECK("MAC 헤더 없음 → 401", call(*tl, nomac).status == 401);
  CHECK("없는 경로 → 인증보다 먼저 404", call(*tl, mkReq("POST", "/v1/nope", "{}", 1, NULL)).status == 404);
  CHECK("메서드가 다르면 404", call(*tl, mkReq("GET", "/v1/hello", "", 1, KEY)).status == 404);
  r = call(*tl, mkReq("POST", "/v1/hello", "{oops", 1, KEY));
  CHECK("형식 오류 → 400 json", r.status == 400 && jS(r.body, "err") == "json");
  r = call(*tl, mkReq("POST", "/v1/hello", "", 1, KEY));
  CHECK("빈 본문(Content-Length 없음) → 400 json", r.status == 400 && jS(r.body, "err") == "json");
  r = call(*tl, mkReq("POST", "/v1/hello", "{\"v\":1,\"user\":1}", 1, KEY));
  CHECK("필수 키 없음 → 400 field", r.status == 400 && jS(r.body, "err") == "field");
  r = call(*tl, mkReq("POST", "/v1/hello", "{\"v\":2,\"user\":1,\"ip\":\"1.2.3.4\",\"port\":1,\"epoch\":1,\"tz\":0}", 1, KEY));
  CHECK("버전 불일치 → 400 field", r.status == 400);
  r = call(*tl, mkReq("POST", "/v1/hello", "{\"v\":1,\"user\":1,\"ip\":\"1.2.3.999\",\"port\":1,\"epoch\":1,\"tz\":0}", 1, KEY));
  CHECK("잘못된 IP → 400 field", r.status == 400);

  // trips: 판정 순서 dup → status → nostop → range → outlier
  r = postTrip(*tl, 1001, 2, 1, 0, "arrived", 400000);
  CHECK("도착 기록 반영", r.status == 200 && jU(r.body, "stored") == 1 && jU(r.body, "applied") == 1 && jU(r.body, "n") == 1 && jU(r.body, "mean_ds") == 4000);
  CHECK("trips 응답 MAC 있음", r.mac.size() == TL_MAC_HEX);
  r = postTrip(*tl, 1001, 2, 1, 0, "arrived", 400000);
  CHECK("중복 → stored 1, applied 0, dup", r.status == 200 && jU(r.body, "stored") == 1 && jU(r.body, "applied") == 0 && jS(r.body, "why") == "dup");
  r = postTrip(*tl, 1001, 0, 1, 0, "canceled", 0);
  CHECK("중복 판정이 상태 판정보다 먼저", jS(r.body, "why") == "dup");
  r = postTrip(*tl, 1002, 0, 1, 0, "canceled", 0);
  CHECK("취소 → status", jU(r.body, "stored") == 1 && jU(r.body, "applied") == 0 && jS(r.body, "why") == "status");
  r = postTrip(*tl, 1003, 0, 1, 0, "timeout", 0);
  CHECK("시간 초과 → status", jS(r.body, "why") == "status");
  r = postTrip(*tl, 1004, 0, 1, 0, "lost", 0);
  CHECK("유실 → status", jS(r.body, "why") == "status");
  r = postTrip(*tl, 1005, 0, 1, 0, "arrived", 5000);
  CHECK("정류장 0 + 5초: nostop이 range보다 먼저", jS(r.body, "why") == "nostop");
  r = postTrip(*tl, 1006, 9, 1, 0, "arrived", 300000);
  CHECK("정류장 9 → nostop", jS(r.body, "why") == "nostop");
  r = postTrip(*tl, 1007, 2, 1, 0, "arrived", 5000);
  CHECK("5초 → range", jS(r.body, "why") == "range" && jU(r.body, "n") == 1);
  r = postTrip(*tl, 1002, 0, 1, 0, "canceled", 0);
  CHECK("미반영 기록도 중복 목록에 들어감", jS(r.body, "why") == "dup");
  r = postTrip(*tl, 1007, 2, 1, 0, "arrived", 400000);
  CHECK("range로 거절된 ID를 다시 보내도 dup", jS(r.body, "why") == "dup" && jU(r.body, "applied") == 0);
  std::string b = tripBody(1008, 1, 2, 1, 0, "walking", 300000);
  CHECK("모르는 상태 → 400 field", call(*tl, mkReq("POST", "/v1/trips", b.c_str(), 1, KEY)).status == 400);
  b = tripBody(0, 1, 2, 1, 0, "arrived", 300000);
  CHECK("이동 ID 0 → 400 field", call(*tl, mkReq("POST", "/v1/trips", b.c_str(), 1, KEY)).status == 400);
  b = tripBody(1009, 2, 2, 1, 0, "arrived", 300000);
  CHECK("trips 사용자 불일치 → 401", call(*tl, mkReq("POST", "/v1/trips", b.c_str(), 1, KEY)).status == 401);
  r = postTrip(*tl, 0x80000001UL, 2, 1, 0, "arrived", 420000);
  CHECK("폰이 만든 ID(최상위 비트 1)도 처리", jU(r.body, "applied") == 1 && jU(r.body, "trip") == 0x80000001UL && jU(r.body, "n") == 2);

  // stats
  TlHttpReq sq = mkReq("GET", "/v1/stats?user=1&stop=2", "", 1, KEY);
  r = call(*tl, sq);
  tlMacResponse(KEY, "/v1/stats?user=1&stop=2", sq.mac, r.body.c_str(), want);
  CHECK("stats 응답 MAC은 쿼리 포함 경로로", r.status == 200 && r.mac == want);
  std::string cells = jS(r.body, "c");
  CHECK("stats 본문이 버퍼에 들어감", cells.empty() && r.body.find("\"c\":\"0,0,0;0,0,0;0,0,0;0,0,0;2,4100,100;") != std::string::npos
        && r.body.size() < TL_BODY_MAX && jU(r.body, "def_ds") == 3000);
  CHECK("stats 사용자 불일치 → 401", call(*tl, mkReq("GET", "/v1/stats?user=2&stop=2", "", 1, KEY)).status == 401);
  CHECK("stats 정류장 범위 밖 → 400", call(*tl, mkReq("GET", "/v1/stats?user=1&stop=9", "", 1, KEY)).status == 400);
  CHECK("stats 쿼리 없음 → 400", call(*tl, mkReq("GET", "/v1/stats", "", 1, KEY)).status == 400);

  // 최악의 경우에도 stats 응답이 버퍼를 넘지 않는지: 12칸 모두 최대 자릿수
  TlCell big; big.n = 255; big.strikes = 2; big.meanDs = 65535; big.sdDs = 65535;
  for (int i = 0; i < 12; i++) tl->store().setCell((uint16_t)(TlStats::cellIndex(1, 2, 0, 0) + i), &big);
  r = call(*tl, sq);
  CHECK("stats 최대 길이 응답도 온전함", r.status == 200 && r.body.size() < TL_BODY_MAX && r.body.back() == '}');
  delete tl;

  // B15: 최근 목록(8개)에서 밀려난 오래된 서명 기록을 다시 보내도 통계에 또 반영되지 않는다
  HostEeprom::inst().reset();
  tl = boot();
  std::string old1001 = tripBody(1001, 1, 2, 1, 0, "arrived", 300000);
  r = call(*tl, mkReq("POST", "/v1/trips", old1001.c_str(), 1, KEY));
  CHECK("B15 1001 정상 도착 반영", r.status == 200 && jU(r.body, "applied") == 1);
  for (uint32_t t = 1002; t <= 1009; t++) postTrip(*tl, t, 2, 1, 0, "arrived", 300000);   // 8건 → 1001이 최근 목록에서 밀려남
  TlCell cb; tl->store().getCell(TlStats::cellIndex(1, 2, 1, 0), &cb);
  r = call(*tl, mkReq("POST", "/v1/trips", old1001.c_str(), 1, KEY));                     // 같은 본문을 다시 보냄
  TlCell ca; tl->store().getCell(TlStats::cellIndex(1, 2, 1, 0), &ca);
  CHECK("B15 밀려난 옛 기록 재전송 → dup, 반영 0, 표본 수 그대로", r.status == 200 && jS(r.body, "why") == "dup"
        && jU(r.body, "applied") == 0 && cb.n == 9 && ca.n == cb.n);
  delete tl;
}

// ---------------------------------------------------------------- 5. 저장소
static void testStore() {
  section("EEPROM 저장소");
  HostEeprom::inst().reset();
  TravelLearn* tl = boot();
  CHECK("처음 켜면 초기화하고 부팅 횟수 1", tl->store().wasFormatted() && tl->store().boot() == 1 && tl->store().storageOk());
  uint32_t t1 = tl->notifyDeparture(1, 0, 0, 0);
  uint32_t t2 = tl->notifyDeparture(1, 0, 0, 0);
  CHECK("이동 ID = 부팅횟수<<16 | 순번", t1 == ((1UL << 16) | 1) && t2 == ((1UL << 16) | 2));
  CHECK("사용자 범위 밖 출발은 거절", tl->notifyDeparture(0, 0, 0, 0) == 0 && tl->notifyDeparture(TL_MAX_USERS + 1, 0, 0, 0) == 0);
  postTrip(*tl, 500, 1, 0, 0, "arrived", 300000);
  uint8_t ip[4] = { 10, 0, 0, 7 };
  tl->store().setPhone(2, ip, 9000);
  tl->store().commit();
  delete tl;

  std::vector<uint32_t> ids;
  ids.push_back(t1); ids.push_back(t2);
  bool unique = true;
  for (int k = 0; k < 5; k++) {
    tl = boot();
    uint32_t t = tl->notifyDeparture(1, 0, 0, 0);
    for (size_t i = 0; i < ids.size(); i++) if (ids[i] == t) unique = false;
    ids.push_back(t);
    CHECK("부팅 횟수 증가", tl->store().boot() == (uint16_t)(k + 2) && !tl->store().wasFormatted());
    delete tl;
  }
  CHECK("재부팅을 거쳐도 이동 ID가 겹치지 않음", unique);

  tl = boot();
  uint8_t ip2[4]; uint16_t port;
  CHECK("폰 주소 유지", tl->store().getPhone(2, ip2, &port) && memcmp(ip, ip2, 4) == 0 && port == 9000 && !tl->store().getPhone(1, ip2, &port));
  TlCell c; tl->store().getCell(0, &c);
  CHECK("통계 유지", c.n == 1 && c.meanDs == 3000);
  CHECK("중복 목록 유지", tl->store().seenTrip(1, 500) && !tl->store().seenTrip(2, 500));

  // 중복 목록은 최근 8개: 9번째를 넣으면 가장 오래된 것이 빠진다
  for (uint32_t k = 0; k < TL_DEDUP_RING; k++) tl->store().addTrip(3, 7000 + k);
  CHECK("목록이 꽉 찬 상태", tl->store().seenTrip(3, 7000) && tl->store().seenTrip(3, 7000 + TL_DEDUP_RING - 1));
  tl->store().addTrip(3, 7999);
  CHECK("9번째가 들어가면 목록에선 빠져도 최고값 이하라 여전히 중복 (B15)", tl->store().seenTrip(3, 7000) && tl->store().seenTrip(3, 7001) && tl->store().seenTrip(3, 7999));
  CHECK("최고값보다 큰 새 ID는 중복 아님 (B15)", !tl->store().seenTrip(3, 8000));
  CHECK("폰 ID(최상위 비트 1)는 최고값을 따로 셈 (B15)", !tl->store().seenTrip(3, 0x80000001UL));
  CHECK("이동 ID 0은 기록·조회하지 않음", !tl->store().seenTrip(1, 0));

  // 한 벌이 깨져도 다른 벌로 살아난다
  tl->store().commit();
  delete tl;
  HostEeprom& e = HostEeprom::inst();
  uint8_t saved[HOST_EE_SIZE]; memcpy(saved, e.mem, sizeof(saved));
  tl = boot(); uint16_t bootA = tl->store().boot(); delete tl;                 // 한 번 더 저장해 두 벌 다 유효하게
  memcpy(saved, e.mem, sizeof(saved));
  for (int slot = 0; slot < 2; slot++) {
    memcpy(e.mem, saved, sizeof(saved));
    e.mem[TL_EE_BASE + slot * TL_COPY_STRIDE + TL_OFF_CELLS + 3] ^= 0x55;     // 한 벌의 통계 영역을 훼손
    tl = boot();
    TlCell cc; tl->store().getCell(0, &cc);
    CHECK("한 벌 훼손: 남은 벌로 복구, 통계 유지", !tl->store().wasFormatted() && cc.n == 1 && cc.meanDs == 3000
          && (tl->store().boot() == bootA + 1 || tl->store().boot() == bootA));
    delete tl;
  }
  memcpy(e.mem, saved, sizeof(saved));
  e.mem[TL_EE_BASE + TL_OFF_CELLS + 3] ^= 0x55;
  e.mem[TL_EE_BASE + TL_COPY_STRIDE + TL_OFF_CELLS + 3] ^= 0x55;
  tl = boot();
  CHECK("두 벌 모두 훼손: 초기화하고 계속 동작", tl->store().wasFormatted() && tl->store().storageOk() && tl->store().boot() == 1);
  CHECK("통계 영역만 쓰고 그 밖은 건드리지 않음", [&]() {
    for (int i = TL_EE_BASE + TL_EE_SIZE; i < HOST_EE_SIZE; i++) if (e.mem[i] != 0xFF) return false;
    return true;
  }());
  tl->resetStats();
  TlCell z; tl->store().getCell(0, &z);
  CHECK("resetStats 뒤 통계 비움", z.n == 0);
  delete tl;
}

// ---------------------------------------------------------------- 6. 전원 차단
// 기록 1건을 반영하는 동안의 모든 쓰기 단계에서 전원을 끊어 본다.
// 재부팅 뒤에는 "전혀 반영 안 됨" 또는 "완전히 반영됨" 둘 중 하나여야 하고, 폰이 다시 보내면 정확히 한 번만 반영돼야 한다.
static void testPowerCut() {
  section("전원 차단(기록 반영 중 모든 쓰기 단계)");
  HostEeprom& e = HostEeprom::inst();
  e.reset();
  TravelLearn* tl = boot();
  postTrip(*tl, 100, 1, 0, 0, "arrived", 400000);
  postTrip(*tl, 101, 1, 0, 0, "arrived", 420000);
  uint8_t ipx[4] = { 192, 168, 0, 23 };
  tl->store().setPhone(1, ipx, 8765);
  tl->store().commit();
  delete tl;
  uint8_t base[HOST_EE_SIZE];
  memcpy(base, e.mem, sizeof(base));

  // 끊지 않았을 때의 단계 수와 기대 결과
  tl = boot();
  unsigned long s0 = e.steps;
  Resp ok = postTrip(*tl, 102, 1, 0, 0, "arrived", 380000);
  unsigned long total = e.steps - s0;
  TlCell expect; tl->store().getCell(0, &expect);
  delete tl;
  CHECK("기준 실행: n 3, 평균 4000, 편차 163", jU(ok.body, "stored") == 1 && expect.n == 3 && expect.meanDs == 4000 && expect.sdDs == 163);
  CHECK("기록 1건 = 블록 2개 지우기+쓰기", total == 2UL * (1 + HOST_EE_BLOCK));

  int nOld = 0, nNew = 0, nBad = 0, nResendBad = 0, nAckWithoutStore = 0;
  for (unsigned long cut = 0; cut <= total; cut++) {
    memcpy(e.mem, base, sizeof(base));
    tl = boot();
    e.cutAfterSteps = (long)cut;
    bool acked = false;
    try {
      Resp r = postTrip(*tl, 102, 1, 0, 0, "arrived", 380000);
      acked = (r.status == 200 && jU(r.body, "stored") == 1);
    } catch (HostPowerCut&) {
    }
    e.cutAfterSteps = -1;
    // 전원이 끊긴 객체는 버린다(소멸자만 실행).
    delete tl;

    tl = boot();
    TlCell c; tl->store().getCell(0, &c);
    bool seen = tl->store().seenTrip(1, 102);
    bool isOld = (!seen && c.n == 2 && c.meanDs == 4100 && c.sdDs == 100);
    bool isNew = (seen && c.n == expect.n && c.meanDs == expect.meanDs && c.sdDs == expect.sdDs);
    uint8_t ipr[4]; uint16_t pr;
    bool regOk = tl->store().getPhone(1, ipr, &pr) && memcmp(ipr, ipx, 4) == 0 && pr == 8765;
    bool otherOk = tl->store().seenTrip(1, 100) && tl->store().seenTrip(1, 101) && !tl->store().wasFormatted();
    if (isOld && regOk && otherOk) nOld++;
    else if (isNew && regOk && otherOk) nNew++;
    else nBad++;
    if (acked && !isNew) nAckWithoutStore++;      // 저장 완료라고 답했는데 저장이 안 된 경우(있으면 안 됨)

    // 폰의 재전송
    Resp r2 = postTrip(*tl, 102, 1, 0, 0, "arrived", 380000);
    tl->store().getCell(0, &c);
    if (!(jU(r2.body, "stored") == 1 && c.n == expect.n && c.meanDs == expect.meanDs && c.sdDs == expect.sdDs)) nResendBad++;
    Resp r3 = postTrip(*tl, 102, 1, 0, 0, "arrived", 380000);
    tl->store().getCell(0, &c);
    if (!(jS(r3.body, "why") == "dup" && c.n == expect.n)) nResendBad++;
    delete tl;
  }
  printf("   차단 지점 %lu곳: 이전 상태 유지 %d, 새 상태 완성 %d, 깨짐 %d\n", total + 1, nOld, nNew, nBad);
  CHECK("어느 단계에서 끊겨도 깨진 상태 없음", nBad == 0 && nOld > 0 && nNew > 0);
  CHECK("재전송하면 정확히 한 번만 반영", nResendBad == 0);
  CHECK("저장 완료 응답은 실제 저장 뒤에만", nAckWithoutStore == 0);

  // 부팅 중(부팅 횟수 저장 중) 전원 차단
  int bootBad = 0;
  for (unsigned long cut = 0; cut <= total; cut += 7) {
    memcpy(e.mem, base, sizeof(base));
    e.cutAfterSteps = (long)cut;
    TravelLearn* t = new TravelLearn();
    t->setUserKey(1, KEY_HEX);
    try { t->begin(0); } catch (HostPowerCut&) { }
    e.cutAfterSteps = -1;
    delete t;
    t = boot();
    TlCell c; t->store().getCell(0, &c);
    if (t->store().wasFormatted() || c.n != 2 || c.meanDs != 4100 || !t->store().seenTrip(1, 101)) bootBad++;
    delete t;
  }
  CHECK("부팅 도중 끊겨도 통계 유지", bootBad == 0);

  // 마모 추정용 수치: 기록 100건의 블록 지우기 횟수
  e.reset();
  tl = boot();
  for (uint32_t k = 0; k < 100; k++) postTrip(*tl, 2000 + k, 1, 0, 0, "arrived", 400000 + (k % 5) * 1000);
  unsigned long maxErase = 0;
  for (int b = 0; b < HOST_EE_SIZE / HOST_EE_BLOCK; b++) if (e.blockErases[b] > maxErase) maxErase = e.blockErases[b];
  printf("   기록 100건: 블록당 지우기 최대 %lu회 (블록 0~3: %lu %lu %lu %lu)\n", maxErase,
         e.blockErases[0], e.blockErases[1], e.blockErases[2], e.blockErases[3]);
  CHECK("기록 1건당 블록 지우기는 0.5회 수준(두 벌 번갈아)", maxErase <= 52);
  delete tl;
}

// ---------------------------------------------------------------- 7. 소켓 수준(루프백)
static int rawConnect(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a; memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0) { close(fd); return -1; }
  struct timeval tv; tv.tv_sec = 4; tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  return fd;
}
static std::string rawExchange(int port, const std::string& data, bool halfClose = false) {
  int fd = rawConnect(port);
  if (fd < 0) return "CONNECT_FAIL";
  if (!data.empty()) (void)!write(fd, data.data(), data.size());
  if (halfClose) shutdown(fd, SHUT_WR);
  std::string out; char buf[512]; ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) out.append(buf, (size_t)n);
  close(fd);
  return out;
}
static std::string httpReq(const char* method, const char* path, const std::string& body, const uint8_t* key, bool withLen = true) {
  char mac[TL_MAC_HEX + 1] = "";
  if (key) tlMacRequest(key, method, path, body.c_str(), mac);
  std::string s = std::string(method) + " " + path + " HTTP/1.1\r\nHost: x\r\n";
  if (withLen) s += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  if (key) s += std::string("X-TL-User: 1\r\nx-tl-mac: ") + mac + "\r\n";
  s += "\r\n" + body;
  return s;
}

// 가짜 폰: 요청 MAC을 확인하고 응답한다. mode 0 정상, 1 응답 MAC 틀림, 2 다른 trip으로 응답, 3 응답 없이 끊음
struct FakePhone {
  int lfd = -1, port = 0;
  std::atomic<int> mode{0};
  std::atomic<int> hits{0};
  std::atomic<int> cancelHits{0};   // /v1/cancel 요청을 받은 횟수 (B03)
  std::atomic<bool> run{true};
  std::string lastBody, lastPath;
  std::thread th;
  void start() {
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = 0; inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    bind(lfd, (struct sockaddr*)&a, sizeof(a)); listen(lfd, 8);
    socklen_t l = sizeof(a); getsockname(lfd, (struct sockaddr*)&a, &l); port = ntohs(a.sin_port);
    th = std::thread([this]() { loop(); });
  }
  void stop() { run = false; shutdown(lfd, SHUT_RDWR); close(lfd); if (th.joinable()) th.join(); }
  void loop() {
    while (run) {
      int fd = accept(lfd, NULL, NULL);
      if (fd < 0) return;
      struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      std::string rq; char buf[512]; ssize_t n; size_t hdrEnd = std::string::npos; size_t need = 0;
      while ((n = read(fd, buf, sizeof(buf))) > 0) {
        rq.append(buf, (size_t)n);
        if (hdrEnd == std::string::npos) {
          hdrEnd = rq.find("\r\n\r\n");
          if (hdrEnd != std::string::npos) {
            size_t p = rq.find("Content-Length: ");
            need = (p != std::string::npos) ? (size_t)atoi(rq.c_str() + p + 16) : 0;
          }
        }
        if (hdrEnd != std::string::npos && rq.size() >= hdrEnd + 4 + need) break;
      }
      hits++;
      if (hdrEnd != std::string::npos && mode != 3) {
        std::string body = rq.substr(hdrEnd + 4);
        size_t sp1 = rq.find(' '), sp2 = rq.find(' ', sp1 + 1);
        std::string path = rq.substr(sp1 + 1, sp2 - sp1 - 1);
        size_t mp = rq.find("X-TL-Mac: ");
        std::string reqMac = (mp != std::string::npos) ? rq.substr(mp + 10, 32) : "";
        char want[TL_MAC_HEX + 1];
        tlMacRequest(KEY, "POST", path.c_str(), body.c_str(), want);
        lastBody = body; lastPath = path;
        if (path == "/v1/cancel") cancelHits++;   // B03
        std::string resp;
        if (reqMac != want) {
          resp = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 20\r\nConnection: close\r\n\r\n{\"ok\":0,\"err\":\"auth\"}";
        } else {
          uint32_t trip = 0; tlJsonGetU32(body.c_str(), "trip", &trip);
          if (mode == 2) trip += 1;
          char rb[96]; snprintf(rb, sizeof(rb), "{\"ok\":1,\"trip\":%lu,\"st\":\"tracking\"}", (unsigned long)trip);
          char rmac[TL_MAC_HEX + 1];
          tlMacResponse(KEY, path.c_str(), reqMac.c_str(), rb, rmac);
          if (mode == 1) rmac[0] = (rmac[0] == '0') ? '1' : '0';
          resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(strlen(rb))
               + "\r\nConnection: close\r\nX-TL-Mac: " + rmac + "\r\n\r\n" + rb;
        }
        (void)!write(fd, resp.data(), resp.size());
      }
      close(fd);
    }
  }
};

static int gDepOk = 0, gDepFail = 0;
static uint32_t gDepTrip = 0;
static void onDep(uint8_t, uint32_t trip, bool ok) { if (ok) gDepOk++; else gDepFail++; gDepTrip = trip; }

static void pump(TravelLearn& tl, unsigned long ms) {
  unsigned long start = millis();
  while ((unsigned long)(millis() - start) < ms) { tl.loop(); delay(2); }
}

static void testSockets() {
  section("소켓 수준: 서버 견고성, 출발 신호 전송");
  HostEeprom::inst().reset();
  TravelLearn tl;
  tl.setUserKey(1, KEY_HEX);
  int port = 18000 + (int)(getpid() % 2000);
  tl.begin((uint16_t)port);
  tl.onDepartResult(onDep);
  std::atomic<bool> run{true};
  std::thread loopTh([&]() { while (run) { tl.loop(); delay(2); } });

  std::string r = rawExchange(port, "GET /v1/ping HTTP/1.1\r\n\r\n");
  CHECK("ping 응답", r.find("HTTP/1.1 200") == 0 && r.find("\"dev\":\"board\"") != std::string::npos && r.find("Connection: close") != std::string::npos);
  std::string big(TL_BODY_MAX + 1, 'x');
  r = rawExchange(port, httpReq("POST", "/v1/trips", big, KEY));
  CHECK("본문 초과 → 413", r.find("HTTP/1.1 413") == 0 && r.find("\"err\":\"size\"") != std::string::npos);
  r = rawExchange(port, "garbage\r\n\r\n");
  CHECK("요청 줄 오류 → 400", r.find("HTTP/1.1 400") == 0);
  r = rawExchange(port, httpReq("POST", "/v1/hello", "", KEY, false));
  CHECK("Content-Length 없는 POST → 400 json", r.find("HTTP/1.1 400") == 0 && r.find("json") != std::string::npos);
  unsigned long t0 = millis();
  r = rawExchange(port, "POST /v1/hello HTTP/1.1\r\nContent-Length: 50\r\n\r\n{\"v\":1");   // 본문을 다 보내지 않음
  unsigned long waited = millis() - t0;
  CHECK("본문이 오다 말면 응답 없이 끊음(약 1.5초 뒤)", r.empty() && waited >= 1200 && waited < 3500);
  r = rawExchange(port, "GET /v1/ping HTTP/1.1\r\n\r\n");
  CHECK("그 뒤에도 서버는 정상", r.find("HTTP/1.1 200") == 0);
  std::string longLine = "GET /" + std::string(600, 'a') + " HTTP/1.1\r\n\r\n";
  r = rawExchange(port, longLine);
  CHECK("너무 긴 요청 줄: 죽지 않고 오류 응답", r.find("HTTP/1.1 4") == 0);
  std::string hello = "{\"v\":1,\"user\":1,\"ip\":\"127.0.0.1\",\"port\":1,\"epoch\":1763004600,\"tz\":540,\"pending\":0}";
  r = rawExchange(port, httpReq("POST", "/v1/hello", hello, KEY));
  CHECK("소문자 헤더 이름으로도 인증", r.find("HTTP/1.1 200") == 0 && r.find("X-TL-Mac: ") != std::string::npos);
  run = false; loopTh.join();

  // --- 출발 신호: 정상 전달
  FakePhone ph; ph.start();
  uint8_t ip[4] = { 127, 0, 0, 1 };
  tl.store().setPhone(1, ip, (uint16_t)ph.port);
  gDepOk = gDepFail = 0;
  uint32_t trip = tl.notifyDeparture(1, 2, 1, 0, 420, -3, 7000);
  pump(tl, 300);
  CHECK("출발 신호 전달·응답 MAC 확인", gDepOk == 1 && gDepFail == 0 && gDepTrip == trip && tl.phoneOnline(1) && ph.hits == 1);
  uint32_t v;
  CHECK("조회 중 문 열림: 최초 전송에도 기존 경과 7초 반영",
        tlJsonGetU32(ph.lastBody.c_str(), "age_ms", &v) && v >= 7000 && v < 8500);
  CHECK("출발 신호 본문(선택 키 포함)", ph.lastPath == "/v1/depart" && tlJsonGetU32(ph.lastBody.c_str(), "trip", &v) && v == trip
        && tlJsonGetU32(ph.lastBody.c_str(), "stop", &v) && v == 2 && tlJsonGetU32(ph.lastBody.c_str(), "wx", &v) && v == 1
        && tlJsonGetU32(ph.lastBody.c_str(), "try", &v) && v == 1 && tlJsonGetU32(ph.lastBody.c_str(), "eta_s", &v) && v == 420
        && ph.lastBody.find("\"tc\":-3") != std::string::npos && tlJsonValid(ph.lastBody.c_str()));
  pump(tl, 1500);
  CHECK("전달된 뒤에는 다시 보내지 않음", ph.hits == 1);

  // --- 취소
  tl.cancelDeparture(1);
  pump(tl, 300);
  CHECK("취소 신호 전달", ph.hits == 2 && ph.lastPath == "/v1/cancel" && tlJsonGetU32(ph.lastBody.c_str(), "trip", &v) && v == trip);
  tl.cancelDeparture(1);
  pump(tl, 300);
  CHECK("이미 끝난 이동은 다시 취소하지 않음", ph.hits == 2);

  // --- 응답 MAC이 틀리면 전달로 인정하지 않고 재전송
  ph.mode = 1; ph.hits = 0; gDepOk = gDepFail = 0;
  trip = tl.notifyDeparture(1, 0, 0, 0);
  pump(tl, 2600);
  CHECK("응답 MAC 불일치 → 미전달, 약 1초마다 재전송", gDepOk == 0 && ph.hits >= 2 && ph.hits <= 4);
  ph.mode = 0;
  pump(tl, 1300);
  CHECK("이후 정상 응답이 오면 전달 완료", gDepOk == 1 && gDepTrip == trip);
  CHECK("재전송 본문의 try가 증가", tlJsonGetU32(ph.lastBody.c_str(), "try", &v) && v >= 3);
  uint32_t age;
  CHECK("재전송 본문의 age_ms가 경과 시간만큼 증가", tlJsonGetU32(ph.lastBody.c_str(), "age_ms", &age) && age >= 2000 && age < 5000);

  ph.mode = 2; ph.hits = 0; gDepOk = gDepFail = 0;
  tl.notifyDeparture(1, 0, 0, 0);
  pump(tl, 1300);
  CHECK("다른 trip에 대한 응답은 인정하지 않음", gDepOk == 0 && ph.hits >= 1);
  ph.mode = 3;
  uint32_t t3 = tl.notifyDeparture(1, 0, 0, 0);       // 같은 사용자의 새 출발이 이전 것을 대체
  pump(tl, 1500);
  CHECK("새 출발 신호가 이전 대기 건을 대체", gDepOk == 0 && t3 != trip);
  ph.mode = 0;
  pump(tl, 1300);
  CHECK("대체된 뒤 최신 trip만 전달", gDepOk == 1 && gDepTrip == t3);

  // --- B03: 응답이 유실돼 전달을 포기(45초)한 뒤에도 취소 신호를 보낸다. 가짜 시계로 빠르게 돌린다.
  ph.mode = 1; ph.hits = 0; ph.cancelHits = 0; gDepOk = gDepFail = 0;   // 폰은 요청을 받지만 응답 MAC이 틀려 미전달로 남는다
  hostFakeTime(true, 1000000UL);
  tl.notifyDeparture(1, 0, 0, 0);
  for (int i = 0; gDepFail == 0 && i < 600; i++) { tl.loop(); hostAdvance(100); }   // 46초쯤 진행해 포기 상태로
  CHECK("B03 응답 유실 시험 준비: 폰은 받았지만 전달 포기", gDepOk == 0 && gDepFail == 1 && ph.hits >= 1 && ph.cancelHits == 0);
  tl.cancelDeparture(1);
  for (int i = 0; i < 20; i++) { tl.loop(); hostAdvance(100); }
  hostFakeTime(false);
  CHECK("B03 전달 포기 뒤에도 취소 신호를 보냄", ph.cancelHits >= 1);
  ph.mode = 0;
  ph.stop();

  // --- 폰이 없음(연결 거부): 45초 동안 재시도 후 포기. 가짜 시계로 빠르게 돌린다.
  gDepOk = gDepFail = 0;
  hostFakeTime(true, 4294960000UL);                   // millis()가 32비트 한계를 넘어가는 구간에서 시험
  trip = tl.notifyDeparture(1, 0, 0, 0);
  int loops = 0;
  unsigned long fakeStart = millis();
  while (gDepFail == 0 && loops < 2000) { tl.loop(); hostAdvance(100); loops++; }
  unsigned long gaveUpAfter = (unsigned long)(uint32_t)(millis() - fakeStart);
  hostFakeTime(false);
  CHECK("폰 무응답: 45초 뒤 포기(millis 넘침 구간 포함)", gDepFail == 1 && gDepOk == 0 && gaveUpAfter >= TL_DEPART_WINDOW_MS
        && gaveUpAfter <= TL_DEPART_WINDOW_MS + 3000 && !tl.phoneOnline(1));

  // --- 등록된 폰이 없는 사용자 / 키 없는 사용자: 즉시 포기
  gDepOk = gDepFail = 0;
  tl.setUserKey(2, "ffffffffffffffffffffffffffffffff");
  tl.notifyDeparture(2, 0, 0, 0);
  pump(tl, 50);
  CHECK("폰 주소가 없으면 즉시 포기", gDepFail == 1);
  gDepFail = 0;
  tl.notifyDeparture(3, 0, 0, 0);
  pump(tl, 50);
  CHECK("키가 없으면 즉시 포기", gDepFail == 1);
}

int main() {
  tlHexDecode(KEY_HEX, KEY, 16);
  testCrypto();
  testJson();
  testStats();
  testSlack();
  testHandlers();
  testStore();
  testPowerCut();
  testSockets();
  printf("----------------------------------------\n");
  printf("통과 %d / 실패 %d\n", gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
