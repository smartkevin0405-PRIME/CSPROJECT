// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// TravelLearn: 현관판(Arduino UNO R4 WiFi)의 폰 통신·이동 시간 학습 모듈.
//
//  - 출발 신호를 사용자 폰으로 보낸다(수신 확인, 재전송).
//  - 폰이 잰 "문 열림 ~ 정류장 도착" 시간을 받아 조건별 평균·편차를 갱신하고 EEPROM에 남긴다.
//  - 버스 도착까지 남은 시간에서 예상 이동 시간과 안전 여유를 뺀 여유 시간을 계산한다.
//
// 센서·화면·날씨/교통 API·Wi-Fi 연결은 메인 프로그램이 맡는다. 통신 형식은 docs/통신규격.md를 따른다.
#ifndef TRAVEL_LEARN_H
#define TRAVEL_LEARN_H

#include <Arduino.h>
#include <WiFiS3.h>
#include "TlConfig.h"
#include "TlStore.h"
#include "TlStats.h"
#include "TlHttp.h"

class TravelLearn {
 public:
  // EEPROM을 읽고 서버를 연다. Wi-Fi 연결(WiFi.begin)은 미리 메인 프로그램이 해 둔다.
  bool begin(uint16_t port = TL_BOARD_PORT);
  // loop()에서 매번 부른다. 들어온 요청 1건, 내보낼 신호 1건까지 처리한다.
  void loop();

  // 사용자별 공유 키(16진수 32글자). 형식이 틀리면 그 사용자의 인증은 모두 실패한다.
  void setUserKey(uint8_t user, const char* hex32);
  // 표본이 없을 때 쓸 초기 이동 시간(초). setup()에서 부른다(EEPROM에 저장하지 않음).
  void setDefaultWalk(uint8_t user, uint8_t stop, uint16_t seconds);

  // RFID 확인 뒤 문 열림을 감지했을 때 부른다. 반환: 이동 ID(0이면 거절)
  uint32_t notifyDeparture(uint8_t user, uint8_t stopHint, uint8_t wx, uint8_t tb,
                           int32_t etaS = -1, int16_t tempC = -128);
  // 외출 취소로 판단했을 때 부른다.
  void cancelDeparture(uint8_t user);

  // 여유 시간 = etaS - 예상 이동 시간 - 안전 여유
  TlSlack slack(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, int32_t etaS);

  // 폰과 연락이 되는 상태인지(마지막 출발 신호 전달 성공, 또는 그 뒤 hello 수신)
  bool phoneOnline(uint8_t user);

  void onTripStored(void (*cb)(uint8_t user, uint8_t stop, uint32_t trip, bool applied));
  void onDepartResult(void (*cb)(uint8_t user, uint32_t trip, bool delivered));

  void resetStats();
  void dumpStats(Print& out);

  // 마지막 hello에서 받은 폰 시각(그 뒤 흐른 시간만큼 더해서 돌려준다). 모르면 false.
  bool phoneTime(uint32_t* epoch, int16_t* tzMin);

  static uint8_t wxFromPty(int pty);      // 기상청 PTY 코드 → 날씨 구간
  static uint8_t tbFromHour(int hour);    // 현지 시(0~23) → 시간대

  // ---- 아래는 점검·시험용 ----
  TlStore& store() { return _store; }
  TlStats& stats() { return _stats; }
  // 요청 하나를 처리해 응답을 만든다(소켓 없이 시험할 수 있도록 분리).
  // 반환: HTTP 상태 코드. out에 본문, outMac에 응답 MAC(붙이지 않으면 빈 문자열).
  int handleRequest(const TlHttpReq* req, char* out, size_t outCap, char outMac[TL_MAC_HEX + 1]);

 private:
  friend struct TlSizeProbe;   // 메모리 사용량 보고용(extras/host/tests/size_report.cpp)
  enum : uint8_t { ST_IDLE = 0, ST_SENDING, ST_DELIVERED, ST_FAILED, ST_CANCELING };

  // 사용자별 출발 신호 상태
  struct Pending {
    uint32_t trip;
    uint32_t t0Ms;        // 문 열림 시각(millis)
    uint32_t nextTryMs;   // 다음 전송 시각
    int32_t etaS;
    int16_t tempC;
    uint8_t stop, wx, tb;
    uint8_t tries;
    uint8_t state;
    uint8_t cancelTries;
  };

  TlStore _store;
  TlStats _stats;
  WiFiServer _server;
  bool _begun = false;
  uint16_t _seq = 0;                       // 부팅 후 이동 순번
  uint8_t _rr = 0;                         // 사용자 순환 처리 위치
  uint8_t _keys[TL_MAX_USERS][16] = {};
  bool _hasKey[TL_MAX_USERS] = {};
  bool _online[TL_MAX_USERS] = {};
  Pending _pend[TL_MAX_USERS] = {};

  bool _timeKnown = false;                 // 폰 시각을 받은 적 있는지
  uint32_t _timeEpoch = 0;
  uint32_t _timeAtMs = 0;
  int16_t _timeTz = 540;

  void (*_cbStored)(uint8_t, uint8_t, uint32_t, bool) = NULL;
  void (*_cbDepart)(uint8_t, uint32_t, bool) = NULL;

  // 큰 버퍼는 스택 대신 여기 둔다.
  TlHttpReq _req;
  char _out[TL_BODY_MAX + 1];
  char _scratch[768];

  void serveOne();
  void sendOne();
  bool attemptDepart(uint8_t user);
  bool attemptCancel(uint8_t user);
  bool postToPhone(uint8_t user, const char* path, uint32_t trip);
  bool checkAuth(const TlHttpReq* req);
  int handleHello(const TlHttpReq* req, char* out, size_t cap);
  int handleTrips(const TlHttpReq* req, char* out, size_t cap);
  int handleStats(const TlHttpReq* req, char* out, size_t cap);
  uint8_t tbFromEpoch(uint32_t epoch) const;
};

#endif
