// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 조건별(사용자·정류장·날씨·시간대) 이동 시간 통계와 여유 시간 계산.
#ifndef TL_STATS_H
#define TL_STATS_H

#include <Arduino.h>
#include "TlConfig.h"
#include "TlStore.h"

// 여유 시간 계산 결과
struct TlSlack {
  int32_t slackS;     // 여유 시간(초) = 버스 도착까지 남은 시간 - 예상 이동 시간 - 안전 여유. 음수면 늦음
  uint16_t walkS;     // 예상 이동 시간(초)
  uint16_t safetyS;   // 안전 여유(초)
  uint8_t level;      // 근거: 0 해당 칸, 1 같은 시간대(또는 같은 날씨) 묶음, 2 정류장 전체 묶음, 3 초기 설정값
  uint8_t n;          // 근거로 쓴 표본 수
};

// 통계에 반영하지 않은 이유(통신규격 4.5의 why)
enum TlWhy {
  TL_WHY_NONE = 0,
  TL_WHY_DUP,
  TL_WHY_STATUS,
  TL_WHY_RANGE,
  TL_WHY_OUTLIER,
  TL_WHY_NOSTOP,
  TL_WHY_MANUAL
};
const char* tlWhyStr(TlWhy w);

class TlStats {
 public:
  void attach(TlStore* store);

  // 칸 번호. 범위를 벗어나면 -1
  static int cellIndex(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb);

  // 표본 하나를 반영한다(RAM 이미지만 바꾼다. EEPROM 기록은 호출한 쪽이 commit).
  // 반환: 통계에 반영했으면 true. 반영 안 했으면 why에 이유. after에는 처리 후의 칸 값.
  bool update(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, uint32_t durMs, TlWhy* why, TlCell* after);

  // 예상 이동 시간과 표준편차(초). 표본이 부족하면 넓은 묶음 → 초기 설정값 순으로 대체한다.
  void predict(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb,
               float* meanS, float* sdS, uint8_t* level, uint8_t* n) const;

  TlSlack slack(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, int32_t etaS) const;

  void setDefault(uint8_t user, uint8_t stop, uint16_t seconds);
  uint16_t getDefault(uint8_t user, uint8_t stop) const;

 private:
  TlStore* _store = NULL;
  uint16_t _defS[TL_MAX_USERS][TL_MAX_STOPS] = {};   // 초기 설정값(초). 0이면 TL_DEFAULT_WALK_S

  // 조건에 맞는 칸들을 묶어 평균·분산을 구한다. wx/tb에 255를 주면 그 축 전체.
  // 반환: 묶인 칸들의 표본 수 합계
  uint16_t pool(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, float* meanS, float* varS) const;
};

#endif
