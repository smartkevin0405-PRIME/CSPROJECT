// travel_link.cpp
// travel_iface.h의 약속을 실제 이동 시간 모듈(TravelLearn 라이브러리)로 연결한다.
// config.h의 USE_TRAVEL_STUB가 0일 때 쓰인다. 1이면 travel_stub.cpp(240초 고정)가 대신 쓰인다.
// 라이브러리 위치: ../../travel-learn/arduino/TravelLearn (Arduino 라이브러리 폴더에 복사해 둔다)
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "config.h"
#if !USE_TRAVEL_STUB
#include "travel_iface.h"
#include "secrets.h"
#include <TravelLearn.h>

static_assert(USER_COUNT <= TL_MAX_USERS, "TravelLearn은 사용자 4명까지");
static_assert(STOP_COUNT <= TL_MAX_STOPS, "TravelLearn은 정류장 4곳까지");

static TravelLearn tl;
static const char* const USER_KEYS[TL_MAX_USERS] = { TL_KEY_USER1, TL_KEY_USER2, TL_KEY_USER3, TL_KEY_USER4 };

// 메인의 정류장 키 → TravelLearn 정류장 번호(1~4). 없으면 0
static uint8_t stopSlot(const char* stopId) {
  for (uint8_t i = 0; i < STOP_COUNT; i++) {
    if (strcmp(STOPS[i].key, stopId) == 0) return i + 1;
  }
  return 0;
}

// 시각(UTC 초) → 시간대. 보드 시각이 없으면 폰이 hello로 알려 준 시각을 쓰고, 그것도 없으면 255
static uint8_t timeBand(uint32_t epoch) {
  int16_t tz = 540;
  if (epoch == 0 && !tl.phoneTime(&epoch, &tz)) return TL_TB_UNKNOWN;
  int32_t local = (int32_t)(epoch % 86400UL) + (int32_t)tz * 60;
  local %= 86400L;
  if (local < 0) local += 86400L;
  return TravelLearn::tbFromHour((int)(local / 3600L));
}

static void onDepartResult(uint8_t user, uint32_t trip, bool delivered) {
  Serial.print(F("[travel] depart user="));
  Serial.print(user);
  Serial.print(F(" trip="));
  Serial.print(trip);
  Serial.println(delivered ? F(" delivered") : F(" FAILED (phone not reached)"));
}

static void onTripStored(uint8_t user, uint8_t stop, uint32_t trip, bool applied) {
  Serial.print(F("[travel] stored user="));
  Serial.print(user);
  Serial.print(F(" stop="));
  Serial.print(stop);
  Serial.print(F(" trip="));
  Serial.print(trip);
  Serial.println(applied ? F(" applied") : F(" not applied"));
}

void travelBegin() {
  for (uint8_t u = 0; u < USER_COUNT; u++) {
    tl.setUserKey(u + 1, USER_KEYS[u]);
    for (uint8_t i = 0; i < USERS[u].stopCount; i++) {
      uint8_t s = USERS[u].stopIdx[i];
      tl.setDefaultWalk(u + 1, s + 1, STOPS[s].walkSec);
    }
  }
  tl.onDepartResult(onDepartResult);
  tl.onTripStored(onTripStored);
  if (!tl.begin()) Serial.println(F("[travel] EEPROM store reset or unavailable"));
}

// 출발 신호를 다시 보내는 동안에는 한 번에 최대 약 2.7초 걸릴 수 있다(통신규격 5장)
void travelLoop() { tl.loop(); }

void travelOnDeparture(const DepartureEvent& ev) {
  int32_t eta = -1;
  if (ev.arrivalCount > 0 && ev.arrivals[0].etaSec[0] != TRAVEL_UNKNOWN_SEC) eta = ev.arrivals[0].etaSec[0];
  uint8_t wx = TravelLearn::wxFromPty(ev.pty);
  uint8_t tb = timeBand(ev.epochSec);
  // 정류장은 아직 모르므로 0 (폰이 그 사용자의 모든 정류장을 감시한다)
  uint32_t trip = tl.notifyDeparture(ev.userId + 1, 0, wx, tb, eta, ev.outdoorTempC);
  Serial.print(F("[travel] departure user="));
  Serial.print(ev.userId + 1);
  Serial.print(F(" wx="));
  Serial.print(wx);
  Serial.print(F(" tb="));
  Serial.print(tb);
  Serial.print(F(" trip="));
  Serial.println(trip);
}

void travelOnCancel(uint8_t userId) { tl.cancelDeparture(userId + 1); }

uint16_t travelGetWalkSec(uint8_t userId, const char* stopId) {
  uint8_t s = stopSlot(stopId);
  if (s == 0) return 0;
  TlSlack r = tl.slack(userId + 1, s, TravelLearn::wxFromPty(hostWeatherPty()), timeBand(hostEpochNow()), 0);
  return r.walkS;
}

int16_t travelGetSlackSec(uint8_t userId, const char* stopId, uint16_t etaSec) {
  if (etaSec == TRAVEL_UNKNOWN_SEC) return TRAVEL_NO_SLACK;
  uint8_t s = stopSlot(stopId);
  if (s == 0) return TRAVEL_NO_SLACK;
  TlSlack r = tl.slack(userId + 1, s, TravelLearn::wxFromPty(hostWeatherPty()), timeBand(hostEpochNow()), etaSec);
  if (r.slackS < -32767) return -32767;
  if (r.slackS > 32767) return 32767;
  return (int16_t)r.slackS;
}

bool travelPhoneOnline(uint8_t userId) { return tl.phoneOnline(userId + 1); }
#endif
