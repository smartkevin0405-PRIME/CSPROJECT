// travel_stub.cpp
// 이동 시간 모듈이 완성되기 전까지 쓰는 가짜 구현. 이동 시간을 240초로 고정한다.
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "config.h"
#if USE_TRAVEL_STUB
#include "travel_iface.h"

static const uint16_t STUB_WALK_SEC = 240;

void travelBegin() {}
void travelLoop() {}

void travelOnDeparture(const DepartureEvent& ev) {
  Serial.print(F("[travel stub] departure seq="));
  Serial.print(ev.seq);
  Serial.print(F(" user="));
  Serial.print(ev.userId);
  Serial.print(F(" stop="));
  Serial.print(ev.stopId);
  Serial.print(F(" temp="));
  Serial.print(ev.outdoorTempC);
  Serial.print(F(" sky="));
  Serial.print(ev.sky);
  Serial.print(F(" pty="));
  Serial.print(ev.pty);
  Serial.print(F(" arrivals="));
  Serial.println(ev.arrivalCount);
}

uint16_t travelGetWalkSec(uint8_t, const char*) { return STUB_WALK_SEC; }

int16_t travelGetSlackSec(uint8_t, const char*, uint16_t etaSec) {
  if (etaSec == TRAVEL_UNKNOWN_SEC) return TRAVEL_NO_SLACK;
  return (int16_t)((int32_t)etaSec - STUB_WALK_SEC);
}

void travelOnCancel(uint8_t userId) {
  Serial.print(F("[travel stub] cancel user="));
  Serial.println(userId);
}

bool travelPhoneOnline(uint8_t) { return false; }
#endif
