// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// TravelLearn 구현: 요청 처리(ping/hello/trips/stats), 출발 신호 전송·재전송, 취소 신호.
#include "TravelLearn.h"
#include "TlJson.h"
#include "TlSha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------- 설정

bool TravelLearn::begin(uint16_t port) {
  bool ok = _store.begin();
  _stats.attach(&_store);
  _server.begin(port);
  _begun = true;
  return ok;
}

void TravelLearn::setUserKey(uint8_t user, const char* hex32) {
  if (user < 1 || user > TL_MAX_USERS) return;
  _hasKey[user - 1] = tlHexDecode(hex32, _keys[user - 1], 16);
}

void TravelLearn::setDefaultWalk(uint8_t user, uint8_t stop, uint16_t seconds) {
  _stats.setDefault(user, stop, seconds);
}

void TravelLearn::onTripStored(void (*cb)(uint8_t, uint8_t, uint32_t, bool)) { _cbStored = cb; }
void TravelLearn::onDepartResult(void (*cb)(uint8_t, uint32_t, bool)) { _cbDepart = cb; }

uint8_t TravelLearn::wxFromPty(int pty) {
  switch (pty) {
    case 1: case 2: case 5: case 6: return 1;   // 비, 비/눈, 빗방울, 빗방울눈날림
    case 3: case 7: return 2;                   // 눈, 눈날림
    default: return 0;                          // 없음 또는 모름
  }
}

uint8_t TravelLearn::tbFromHour(int hour) {
  if (hour >= 6 && hour <= 9) return 0;         // 아침
  if (hour >= 10 && hour <= 15) return 1;       // 낮
  if (hour >= 16 && hour <= 20) return 2;       // 저녁
  return 3;                                     // 밤
}

uint8_t TravelLearn::tbFromEpoch(uint32_t epoch) const {
  int32_t local = (int32_t)(epoch % 86400UL) + (int32_t)_timeTz * 60;
  local %= 86400L;
  if (local < 0) local += 86400L;
  return tbFromHour((int)(local / 3600L));
}

bool TravelLearn::phoneTime(uint32_t* epoch, int16_t* tzMin) {
  if (!_timeKnown) return false;
  if (epoch) *epoch = _timeEpoch + (uint32_t)(millis() - _timeAtMs) / 1000UL;
  if (tzMin) *tzMin = _timeTz;
  return true;
}

bool TravelLearn::phoneOnline(uint8_t user) {
  if (user < 1 || user > TL_MAX_USERS) return false;
  return _online[user - 1];
}

TlSlack TravelLearn::slack(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, int32_t etaS) {
  return _stats.slack(user, stop, wx, tb, etaS);
}

void TravelLearn::resetStats() {
  _store.clearStats();
}

void TravelLearn::dumpStats(Print& out) {
  char line[96];
  snprintf(line, sizeof(line), "TL boot=%u commits=%lu storage=%s",
           (unsigned)_store.boot(), (unsigned long)_store.commitCount(), _store.storageOk() ? "ok" : "FAIL");
  out.println(line);
  for (uint8_t u = 1; u <= TL_MAX_USERS; u++) {
    for (uint8_t s = 1; s <= TL_MAX_STOPS; s++) {
      for (uint8_t w = 0; w < TL_NUM_WX; w++) {
        for (uint8_t t = 0; t < TL_NUM_TB; t++) {
          TlCell c;
          _store.getCell((uint16_t)TlStats::cellIndex(u, s, w, t), &c);
          if (c.n == 0) continue;
          snprintf(line, sizeof(line), "u%u s%u wx%u tb%u n=%u mean_ds=%u sd_ds=%u strikes=%u",
                   (unsigned)u, (unsigned)s, (unsigned)w, (unsigned)t,
                   (unsigned)c.n, (unsigned)c.meanDs, (unsigned)c.sdDs, (unsigned)c.strikes);
          out.println(line);
        }
      }
    }
  }
}

// ---------------------------------------------------------------- 출발·취소

uint32_t TravelLearn::notifyDeparture(uint8_t user, uint8_t stopHint, uint8_t wx, uint8_t tb,
                                      int32_t etaS, int16_t tempC, uint32_t ageMs) {
  if (!_begun || user < 1 || user > TL_MAX_USERS) return 0;
  if (stopHint > TL_MAX_STOPS) stopHint = 0;
  if (wx >= TL_NUM_WX) wx = 0;
  if (tb >= TL_NUM_TB) {
    // 메인 프로그램이 시각을 모르면 폰에서 받은 시각으로 시간대를 정한다.
    uint32_t ep;
    tb = phoneTime(&ep, NULL) ? tbFromEpoch(ep) : (uint8_t)TL_TB_UNKNOWN;
  }
  _seq++;
  if (_seq == 0) _seq = 1;
  Pending& p = _pend[user - 1];
  p.trip = ((uint32_t)(_store.boot() & 0x7FFF) << 16) | _seq;
  p.nextTryMs = millis();
  p.t0Ms = p.nextTryMs - ageMs;              // API 조회 중 감지한 문 열림 시각까지 보정
  p.etaS = etaS;
  p.tempC = tempC;
  p.stop = stopHint; p.wx = wx; p.tb = tb;
  p.tries = 0;
  p.cancelTries = 0;
  p.state = ST_SENDING;
  return p.trip;
}

void TravelLearn::cancelDeparture(uint8_t user) {
  if (user < 1 || user > TL_MAX_USERS) return;
  Pending& p = _pend[user - 1];
  if (p.state == ST_SENDING) {
    // 아직 전달 확인을 못 받았어도 폰이 받았을 수 있으므로 취소 신호는 보낸다.
    if (_cbDepart) _cbDepart(user, p.trip, false);
  } else if (p.state != ST_DELIVERED) {
    return;
  }
  p.state = ST_CANCELING;
  p.cancelTries = 0;
  p.nextTryMs = millis();
}

// 폰에 연결해 POST 한 건을 보내고 응답을 검증한다.
bool TravelLearn::postToPhone(uint8_t user, const char* path, uint32_t trip) {
  uint8_t ip[4];
  uint16_t port;
  if (!_hasKey[user - 1] || !_store.getPhone(user, ip, &port)) return false;
  Pending& p = _pend[user - 1];
  bool ok = false;
  WiFiClient c;
  c.setConnectionTimeout(TL_CONNECT_TIMEOUT_MS);
  if (c.connect(IPAddress(ip[0], ip[1], ip[2], ip[3]), port)) {
    // 연결된 뒤에 본문을 만든다(age_ms에 연결 시간이 반영되도록).
    char* body = _out;
    int n;
    if (strcmp(path, "/v1/depart") == 0) {
      n = snprintf(body, sizeof(_out),
                   "{\"v\":%d,\"trip\":%lu,\"user\":%u,\"stop\":%u,\"wx\":%u,\"tb\":%u,\"age_ms\":%lu,\"try\":%u",
                   TL_PROTO_V, (unsigned long)trip, (unsigned)user, (unsigned)p.stop, (unsigned)p.wx,
                   (unsigned)p.tb, (unsigned long)(uint32_t)(millis() - p.t0Ms), (unsigned)p.tries);
      if (p.etaS >= 0 && n > 0 && (size_t)n < sizeof(_out)) {
        n += snprintf(body + n, sizeof(_out) - (size_t)n, ",\"eta_s\":%ld", (long)p.etaS);
      }
      if (p.tempC != -128 && n > 0 && (size_t)n < sizeof(_out)) {
        n += snprintf(body + n, sizeof(_out) - (size_t)n, ",\"tc\":%d", (int)p.tempC);
      }
      if (n > 0 && (size_t)n < sizeof(_out)) n += snprintf(body + n, sizeof(_out) - (size_t)n, "}");
    } else {
      n = snprintf(body, sizeof(_out), "{\"v\":%d,\"trip\":%lu,\"user\":%u}",
                   TL_PROTO_V, (unsigned long)trip, (unsigned)user);
    }
    if (n > 0 && (size_t)n < sizeof(_out)) {
      char reqMac[TL_MAC_HEX + 1];
      char respMac[TL_MAC_HEX + 1];
      char want[TL_MAC_HEX + 1];
      tlMacRequest(_keys[user - 1], "POST", path, body, reqMac);
      // 응답 본문은 _req.body를 빌려 쓴다(요청 처리와 동시에 쓰이지 않는다).
      int status = tlHttpPost(c, path, body, user, reqMac, _req.body, sizeof(_req.body), respMac,
                              _scratch, sizeof(_scratch), TL_IO_TIMEOUT_MS);
      if (status == 200 && strlen(respMac) == TL_MAC_HEX) {
        tlMacResponse(_keys[user - 1], path, reqMac, _req.body, want);
        uint32_t okv = 0, tripv = 0;
        if (tlConstEq(want, respMac, TL_MAC_HEX) &&
            tlJsonGetU32(_req.body, "ok", &okv) && okv == 1 &&
            tlJsonGetU32(_req.body, "trip", &tripv) && tripv == trip) {
          ok = true;
        }
      }
    }
  }
  c.stop();
  return ok;
}

// 출발 신호 한 번 시도. 반환: 이번 호출로 상태가 끝났으면(성공·포기) true
bool TravelLearn::attemptDepart(uint8_t user) {
  Pending& p = _pend[user - 1];
  uint8_t ip[4];
  uint16_t port;
  bool give = false;
  if ((uint32_t)(millis() - p.t0Ms) >= TL_DEPART_WINDOW_MS) give = true;            // 시간 초과
  if (!_hasKey[user - 1] || !_store.getPhone(user, ip, &port)) give = true;          // 보낼 곳이 없음
  if (!give) {
    if (p.tries < 255) p.tries++;
    if (postToPhone(user, "/v1/depart", p.trip)) {
      p.state = ST_DELIVERED;
      _online[user - 1] = true;
      if (_cbDepart) _cbDepart(user, p.trip, true);
      return true;
    }
    if ((uint32_t)(millis() - p.t0Ms) >= TL_DEPART_WINDOW_MS) {
      give = true;
    } else {
      p.nextTryMs = millis() + TL_DEPART_RETRY_MS;
    }
  }
  if (give) {
    p.state = ST_FAILED;
    _online[user - 1] = false;
    if (_cbDepart) _cbDepart(user, p.trip, false);
    return true;
  }
  return false;
}

bool TravelLearn::attemptCancel(uint8_t user) {
  Pending& p = _pend[user - 1];
  p.cancelTries++;
  if (postToPhone(user, "/v1/cancel", p.trip) || p.cancelTries >= TL_CANCEL_TRIES) {
    p.state = ST_IDLE;
    return true;
  }
  p.nextTryMs = millis() + TL_DEPART_RETRY_MS;
  return false;
}

void TravelLearn::sendOne() {
  uint32_t now = millis();
  for (uint8_t k = 0; k < TL_MAX_USERS; k++) {
    uint8_t i = (uint8_t)((_rr + k) % TL_MAX_USERS);
    Pending& p = _pend[i];
    if (p.state != ST_SENDING && p.state != ST_CANCELING) continue;
    if ((int32_t)(now - p.nextTryMs) < 0) continue;      // 아직 때가 아님
    _rr = (uint8_t)((i + 1) % TL_MAX_USERS);             // 다음에는 다른 사용자부터
    if (p.state == ST_SENDING) attemptDepart((uint8_t)(i + 1));
    else attemptCancel((uint8_t)(i + 1));
    return;                                              // 한 번에 한 건만
  }
}

// ---------------------------------------------------------------- 요청 처리

static int errBody(char* out, size_t cap, int status, const char* err) {
  snprintf(out, cap, "{\"ok\":0,\"err\":\"%s\"}", err);
  return status;
}

bool TravelLearn::checkAuth(const TlHttpReq* req) {
  if (req->user < 1 || req->user > TL_MAX_USERS) return false;
  if (!_hasKey[req->user - 1]) return false;
  if (strlen(req->mac) != TL_MAC_HEX) return false;
  char got[TL_MAC_HEX + 1];
  for (uint8_t i = 0; i < TL_MAC_HEX; i++) {
    char ch = req->mac[i];
    got[i] = (ch >= 'A' && ch <= 'F') ? (char)(ch + 32) : ch;
  }
  got[TL_MAC_HEX] = '\0';
  char want[TL_MAC_HEX + 1];
  tlMacRequest(_keys[req->user - 1], req->method, req->path, req->body, want);
  return tlConstEq(want, got, TL_MAC_HEX);
}

// "a.b.c.d" 형식의 IPv4 주소를 읽는다.
static bool parseIp(const char* s, uint8_t ip[4]) {
  for (uint8_t i = 0; i < 4; i++) {
    if (*s < '0' || *s > '9') return false;
    uint16_t v = 0;
    uint8_t digits = 0;
    while (*s >= '0' && *s <= '9') {
      v = (uint16_t)(v * 10 + (*s - '0'));
      if (++digits > 3 || v > 255) return false;
      s++;
    }
    ip[i] = (uint8_t)v;
    if (i < 3) {
      if (*s != '.') return false;
      s++;
    }
  }
  return *s == '\0';
}

int TravelLearn::handleHello(const TlHttpReq* req, char* out, size_t cap) {
  const char* b = req->body;
  if (!tlJsonValid(b)) return errBody(out, cap, 400, "json");
  uint32_t v, user, port, epoch;
  int32_t tz;
  char ipStr[20];
  uint8_t ip[4];
  if (!tlJsonGetU32(b, "v", &v) || v != TL_PROTO_V ||
      !tlJsonGetU32(b, "user", &user) ||
      !tlJsonGetStr(b, "ip", ipStr, sizeof(ipStr)) || !parseIp(ipStr, ip) ||
      !tlJsonGetU32(b, "port", &port) || port < 1 || port > 65535UL ||
      !tlJsonGetU32(b, "epoch", &epoch) ||
      !tlJsonGetI32(b, "tz", &tz) || tz < -1440 || tz > 1440) {
    return errBody(out, cap, 400, "field");
  }
  if (user != req->user) return errBody(out, cap, 401, "auth");
  if (_store.setPhone((uint8_t)user, ip, (uint16_t)port)) {
    if (!_store.commit()) {
      // EEPROM 기록 실패: 이번 전원 동안은 RAM 값으로 동작한다.
      _store.rollback();
      _store.setPhone((uint8_t)user, ip, (uint16_t)port);
    }
  }
  _online[user - 1] = true;
  _timeKnown = true;
  _timeEpoch = epoch;
  _timeAtMs = millis();
  _timeTz = (int16_t)tz;
  snprintf(out, cap, "{\"ok\":1,\"boot\":%u,\"reg\":1}", (unsigned)_store.boot());
  return 200;
}

int TravelLearn::handleTrips(const TlHttpReq* req, char* out, size_t cap) {
  const char* b = req->body;
  if (!tlJsonValid(b)) return errBody(out, cap, 400, "json");
  uint32_t v, trip, user, stop, wx, tb, durMs, q, src, epoch;
  char st[12];
  if (!tlJsonGetU32(b, "v", &v) || v != TL_PROTO_V ||
      !tlJsonGetU32(b, "trip", &trip) || trip == 0 ||
      !tlJsonGetU32(b, "user", &user) ||
      !tlJsonGetU32(b, "stop", &stop) ||
      !tlJsonGetU32(b, "wx", &wx) ||
      !tlJsonGetU32(b, "tb", &tb) ||
      !tlJsonGetStr(b, "st", st, sizeof(st)) ||
      !tlJsonGetU32(b, "dur_ms", &durMs) ||
      !tlJsonGetU32(b, "q", &q) ||
      !tlJsonGetU32(b, "src", &src) ||
      !tlJsonGetU32(b, "epoch", &epoch)) {
    return errBody(out, cap, 400, "field");
  }
  bool arrived = (strcmp(st, "arrived") == 0);
  if (!arrived && strcmp(st, "canceled") != 0 && strcmp(st, "timeout") != 0 && strcmp(st, "lost") != 0) {
    return errBody(out, cap, 400, "field");
  }
  if (user != req->user) return errBody(out, cap, 401, "auth");
  uint8_t u = (uint8_t)user;

  // 1) 중복: 이미 처리한 이동 ID
  if (_store.seenTrip(u, trip)) {
    snprintf(out, cap, "{\"ok\":1,\"trip\":%lu,\"stored\":1,\"applied\":0,\"why\":\"dup\"}", (unsigned long)trip);
    return 200;
  }

  // 2) 판정 순서: 도착 기록 아님 → 정류장 없음 → (수동 기록 제외) → 범위 밖 → 이상값
  TlWhy why = TL_WHY_NONE;
  TlCell after;
  after.n = 0; after.strikes = 0; after.meanDs = 0; after.sdDs = 0;
  bool applied = false;
  bool haveCell = false;
  uint8_t s = (stop >= 1 && stop <= TL_MAX_STOPS) ? (uint8_t)stop : 0;
  if (!arrived) {
    why = TL_WHY_STATUS;
  } else if (s == 0) {
    why = TL_WHY_NOSTOP;
  } else if (!TL_ACCEPT_MANUAL && (q == 2 || src == 1)) {
    why = TL_WHY_MANUAL;
  } else {
    uint8_t w = (wx < TL_NUM_WX) ? (uint8_t)wx : 0;                      // 날씨를 모르면 0
    uint8_t t = (tb < TL_NUM_TB) ? (uint8_t)tb : tbFromEpoch(epoch);     // 시간대가 비었으면 출발 시각으로 정한다
    applied = _stats.update(u, s, w, t, durMs, &why, &after);
    haveCell = true;
  }

  // 3) 중복 방지 목록에 넣고 EEPROM에 기록한다. 기록이 끝나야 stored:1을 보낸다.
  _store.addTrip(u, trip);
  if (!_store.commit()) {
    _store.rollback();
    return errBody(out, cap, 503, "busy");
  }

  int n = snprintf(out, cap, "{\"ok\":1,\"trip\":%lu,\"stored\":1,\"applied\":%d", (unsigned long)trip, applied ? 1 : 0);
  if (!applied && n > 0 && (size_t)n < cap) {
    n += snprintf(out + n, cap - (size_t)n, ",\"why\":\"%s\"", tlWhyStr(why));
  }
  if (haveCell && n > 0 && (size_t)n < cap) {
    n += snprintf(out + n, cap - (size_t)n, ",\"n\":%u,\"mean_ds\":%u,\"sd_ds\":%u",
                  (unsigned)after.n, (unsigned)after.meanDs, (unsigned)after.sdDs);
  }
  if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "}");
  if (_cbStored) _cbStored(u, s, trip, applied);
  return 200;
}

// 쿼리 문자열에서 name=숫자 값을 찾는다.
static bool queryU32(const char* path, const char* name, uint32_t* out) {
  const char* q = strchr(path, '?');
  if (q == NULL) return false;
  size_t nlen = strlen(name);
  q++;
  while (*q != '\0') {
    if (strncmp(q, name, nlen) == 0 && q[nlen] == '=') {
      const char* p = q + nlen + 1;
      if (*p < '0' || *p > '9') return false;
      uint32_t v = 0;
      uint8_t digits = 0;
      while (*p >= '0' && *p <= '9') {
        if (++digits > 9) return false;
        v = v * 10 + (uint32_t)(*p - '0');
        p++;
      }
      if (*p != '\0' && *p != '&') return false;
      *out = v;
      return true;
    }
    const char* amp = strchr(q, '&');
    if (amp == NULL) break;
    q = amp + 1;
  }
  return false;
}

int TravelLearn::handleStats(const TlHttpReq* req, char* out, size_t cap) {
  uint32_t user, stop;
  if (!queryU32(req->path, "user", &user) || !queryU32(req->path, "stop", &stop)) {
    return errBody(out, cap, 400, "field");
  }
  if (user != req->user) return errBody(out, cap, 401, "auth");
  if (stop < 1 || stop > TL_MAX_STOPS) return errBody(out, cap, 400, "field");
  int n = snprintf(out, cap, "{\"ok\":1,\"user\":%u,\"stop\":%u,\"def_ds\":%lu,\"c\":\"",
                   (unsigned)user, (unsigned)stop,
                   (unsigned long)_stats.getDefault((uint8_t)user, (uint8_t)stop) * 10UL);
  for (uint8_t i = 0; i < TL_CELLS_PER_PAIR && n > 0 && (size_t)n < cap; i++) {
    TlCell c;
    _store.getCell((uint16_t)(TlStats::cellIndex((uint8_t)user, (uint8_t)stop, 0, 0) + i), &c);
    n += snprintf(out + n, cap - (size_t)n, "%s%u,%u,%u", (i == 0) ? "" : ";",
                  (unsigned)c.n, (unsigned)c.meanDs, (unsigned)c.sdDs);
  }
  if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "\"}");
  return 200;
}

int TravelLearn::handleRequest(const TlHttpReq* req, char* out, size_t outCap, char outMac[TL_MAC_HEX + 1]) {
  outMac[0] = '\0';
  bool isGet = (strcmp(req->method, "GET") == 0);
  bool isPost = (strcmp(req->method, "POST") == 0);
  // 1) 경로 확인(인증보다 먼저)
  enum { R_NONE, R_PING, R_HELLO, R_TRIPS, R_STATS } route = R_NONE;
  if (isGet && strcmp(req->path, "/v1/ping") == 0) route = R_PING;
  else if (isPost && strcmp(req->path, "/v1/hello") == 0) route = R_HELLO;
  else if (isPost && strcmp(req->path, "/v1/trips") == 0) route = R_TRIPS;
  else if (isGet && strncmp(req->path, "/v1/stats", 9) == 0 && (req->path[9] == '?' || req->path[9] == '\0')) route = R_STATS;
  if (route == R_NONE) return errBody(out, outCap, 404, "path");

  if (route == R_PING) {      // 인증 없음, MAC 없음
    snprintf(out, outCap, "{\"ok\":1,\"dev\":\"board\",\"v\":%d,\"boot\":%u,\"up_ms\":%lu}",
             TL_PROTO_V, (unsigned)_store.boot(), (unsigned long)millis());
    return 200;
  }
  // 2) 인증
  if (!checkAuth(req)) return errBody(out, outCap, 401, "auth");
  // 3) 내용 처리
  int status;
  if (route == R_HELLO) status = handleHello(req, out, outCap);
  else if (route == R_TRIPS) status = handleTrips(req, out, outCap);
  else status = handleStats(req, out, outCap);
  // 4) 성공 응답에만 MAC을 붙인다(요청 MAC은 소문자로 묶는다).
  if (status == 200) {
    char reqMac[TL_MAC_HEX + 1];
    for (uint8_t i = 0; i < TL_MAC_HEX; i++) {
      char ch = req->mac[i];
      reqMac[i] = (ch >= 'A' && ch <= 'F') ? (char)(ch + 32) : ch;
    }
    reqMac[TL_MAC_HEX] = '\0';
    tlMacResponse(_keys[req->user - 1], req->path, reqMac, out, outMac);
  }
  return status;
}

void TravelLearn::serveOne() {
  WiFiClient c = _server.available();
  if (!c) return;
  char mac[TL_MAC_HEX + 1];
  mac[0] = '\0';
  int status = 0;
  TlReadResult r = tlHttpReadRequest(c, &_req, TL_IO_TIMEOUT_MS);
  if (r == TL_READ_OK) {
    status = handleRequest(&_req, _out, sizeof(_out), mac);
  } else if (r == TL_READ_TOOBIG) {
    status = errBody(_out, sizeof(_out), 413, "size");
  } else if (r == TL_READ_BAD) {
    status = errBody(_out, sizeof(_out), 400, "json");
  }
  // 제한 시간 초과(r == TL_READ_TIMEOUT)면 응답 없이 끊는다.
  if (status != 0) {
    tlHttpSendResponse(c, status, _out, (mac[0] != '\0') ? mac : NULL, _scratch, sizeof(_scratch));
    delay(2);     // 보낸 내용이 나갈 시간을 조금 준다
  }
  c.stop();
}

void TravelLearn::loop() {
  if (!_begun) return;
  serveOne();   // 들어온 요청 1건
  sendOne();    // 내보낼 신호 1건
}
