// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 통계 갱신(지수 가중 평균·분산), 표본 부족 시 대체, 여유 시간 계산.
#include "TlStats.h"
#include <math.h>
#include <string.h>

#define TL_ANY 255

const char* tlWhyStr(TlWhy w) {
  switch (w) {
    case TL_WHY_DUP: return "dup";
    case TL_WHY_STATUS: return "status";
    case TL_WHY_RANGE: return "range";
    case TL_WHY_OUTLIER: return "outlier";
    case TL_WHY_NOSTOP: return "nostop";
    case TL_WHY_MANUAL: return "manual";
    default: return "";
  }
}

void TlStats::attach(TlStore* store) {
  _store = store;
}

int TlStats::cellIndex(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb) {
  if (user < 1 || user > TL_MAX_USERS) return -1;
  if (stop < 1 || stop > TL_MAX_STOPS) return -1;
  if (wx >= TL_NUM_WX || tb >= TL_NUM_TB) return -1;
  return ((int)(user - 1) * TL_MAX_STOPS + (int)(stop - 1)) * TL_CELLS_PER_PAIR + (int)wx * TL_NUM_TB + (int)tb;
}

void TlStats::setDefault(uint8_t user, uint8_t stop, uint16_t seconds) {
  if (user < 1 || user > TL_MAX_USERS || stop < 1 || stop > TL_MAX_STOPS) return;
  _defS[user - 1][stop - 1] = seconds;
}

uint16_t TlStats::getDefault(uint8_t user, uint8_t stop) const {
  if (user < 1 || user > TL_MAX_USERS || stop < 1 || stop > TL_MAX_STOPS) return TL_DEFAULT_WALK_S;
  uint16_t v = _defS[user - 1][stop - 1];
  return (v == 0) ? (uint16_t)TL_DEFAULT_WALK_S : v;
}

static uint16_t toDs(float seconds) {
  float ds = seconds * 10.0f + 0.5f;
  if (ds < 0.0f) return 0;
  if (ds > 65535.0f) return 65535;
  return (uint16_t)ds;
}

bool TlStats::update(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, uint32_t durMs,
                     TlWhy* why, TlCell* after) {
  TlCell c;
  c.n = 0; c.strikes = 0; c.meanDs = 0; c.sdDs = 0;
  if (after) *after = c;
  *why = TL_WHY_NONE;
  int idx = cellIndex(user, stop, wx, tb);
  if (idx < 0 || _store == NULL) { *why = TL_WHY_NOSTOP; return false; }
  _store->getCell((uint16_t)idx, &c);
  if (after) *after = c;
  if (durMs < (uint32_t)TL_MIN_DUR_S * 1000UL || durMs > (uint32_t)TL_MAX_DUR_S * 1000UL) {
    *why = TL_WHY_RANGE;
    return false;
  }
  float x = (float)durMs / 1000.0f;
  float mean = (float)c.meanDs / 10.0f;
  float sd = (float)c.sdDs / 10.0f;

  // 이상값: 평균에서 너무 멀면 버린다. 단, 연달아 계속 멀면(생활 패턴 변화) 받아들인다.
  if (c.n >= TL_OUTLIER_MIN_N) {
    float limit = (float)TL_OUTLIER_SIGMA * sd;
    if (limit < (float)TL_OUTLIER_FLOOR_S) limit = (float)TL_OUTLIER_FLOOR_S;
    if (fabsf(x - mean) > limit && c.strikes < TL_OUTLIER_STRIKES) {
      c.strikes++;
      _store->setCell((uint16_t)idx, &c);
      if (after) *after = c;
      *why = TL_WHY_OUTLIER;
      return false;
    }
  }

  if (c.n == 0) {
    mean = x;
    sd = 0.0f;
    c.n = 1;
  } else {
    uint8_t n1 = (c.n < 255) ? (uint8_t)(c.n + 1) : 255;
    uint8_t eff = (n1 < TL_EW_CAP) ? n1 : TL_EW_CAP;
    float alpha = 1.0f / (float)eff;
    float d = x - mean;
    float var = sd * sd;
    mean = mean + alpha * d;
    var = (1.0f - alpha) * (var + alpha * d * d);
    sd = sqrtf(var);
    c.n = n1;
  }
  c.strikes = 0;
  c.meanDs = toDs(mean);
  c.sdDs = toDs(sd);
  _store->setCell((uint16_t)idx, &c);
  if (after) *after = c;
  return true;
}

uint16_t TlStats::pool(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, float* meanS, float* varS) const {
  float sumN = 0.0f, sumM = 0.0f;
  uint16_t total = 0;
  // 1차: 가중 평균
  for (uint8_t w = 0; w < TL_NUM_WX; w++) {
    if (wx != TL_ANY && w != wx) continue;
    for (uint8_t t = 0; t < TL_NUM_TB; t++) {
      if (tb != TL_ANY && t != tb) continue;
      int idx = cellIndex(user, stop, w, t);
      if (idx < 0) continue;
      TlCell c;
      _store->getCell((uint16_t)idx, &c);
      if (c.n == 0) continue;
      uint8_t ni = (c.n < TL_EW_CAP) ? c.n : TL_EW_CAP;
      sumN += (float)ni;
      sumM += (float)ni * ((float)c.meanDs / 10.0f);
      total = (uint16_t)(total + c.n);          // 표본 수 합계는 상한 없이 센다(상한은 가중치에만 적용)
    }
  }
  if (total == 0) { *meanS = 0.0f; *varS = 0.0f; return 0; }
  float m = sumM / sumN;
  // 2차: 칸 내부 분산 + 칸 사이 분산
  float sumV = 0.0f;
  for (uint8_t w = 0; w < TL_NUM_WX; w++) {
    if (wx != TL_ANY && w != wx) continue;
    for (uint8_t t = 0; t < TL_NUM_TB; t++) {
      if (tb != TL_ANY && t != tb) continue;
      int idx = cellIndex(user, stop, w, t);
      if (idx < 0) continue;
      TlCell c;
      _store->getCell((uint16_t)idx, &c);
      if (c.n == 0) continue;
      uint8_t ni = (c.n < TL_EW_CAP) ? c.n : TL_EW_CAP;
      float mi = (float)c.meanDs / 10.0f;
      float si = (float)c.sdDs / 10.0f;
      sumV += (float)ni * (si * si + (mi - m) * (mi - m));
    }
  }
  *meanS = m;
  *varS = sumV / sumN;
  return total;
}

void TlStats::predict(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb,
                      float* meanS, float* sdS, uint8_t* level, uint8_t* n) const {
  float m = 0.0f, v = 0.0f;
  uint16_t cnt = 0;
  uint8_t lv = 3;
  bool pairOk = (_store != NULL && user >= 1 && user <= TL_MAX_USERS && stop >= 1 && stop <= TL_MAX_STOPS);
  if (wx >= TL_NUM_WX) wx = 0;                    // 날씨를 모르면 '강수 없음'으로 본다
  bool tbOk = (tb < TL_NUM_TB);
  if (pairOk) {
    // 수준 0: 조건이 정확히 같은 칸
    if (tbOk) {
      cnt = pool(user, stop, wx, tb, &m, &v);
      if (cnt >= TL_MIN_SAMPLES) lv = 0;
    }
    // 수준 1: 같은 시간대(날씨 무관) → 같은 날씨(시간대 무관)
    if (lv == 3 && tbOk) {
      cnt = pool(user, stop, TL_ANY, tb, &m, &v);
      if (cnt >= TL_MIN_SAMPLES) lv = 1;
    }
    if (lv == 3) {
      cnt = pool(user, stop, wx, TL_ANY, &m, &v);
      if (cnt >= TL_MIN_SAMPLES) lv = 1;
    }
    // 수준 2: 그 사용자·정류장의 모든 칸
    if (lv == 3) {
      cnt = pool(user, stop, TL_ANY, TL_ANY, &m, &v);
      if (cnt >= 1) lv = 2;
    }
  }
  float sd;
  if (lv == 3) {
    // 수준 3: 초기 설정값
    m = (float)getDefault(user, stop);
    cnt = 0;
    sd = 0.0f;
  } else {
    sd = sqrtf(v);
  }
  if (cnt < 2 || sd <= 0.0f) {
    // 편차를 알 수 없으면 평균의 일정 비율로 잡는다
    sd = m * (float)TL_DEFAULT_SD_PCT / 100.0f;
    if (sd < (float)TL_DEFAULT_SD_MIN_S) sd = (float)TL_DEFAULT_SD_MIN_S;
  }
  *meanS = m;
  *sdS = sd;
  *level = lv;
  *n = (cnt > 255) ? 255 : (uint8_t)cnt;
}

TlSlack TlStats::slack(uint8_t user, uint8_t stop, uint8_t wx, uint8_t tb, int32_t etaS) const {
  float m, sd;
  uint8_t lv, n;
  predict(user, stop, wx, tb, &m, &sd, &lv, &n);
  float safety = (float)TL_SAFETY_BASE_S + ((float)TL_SAFETY_K_X10 / 10.0f) * sd;
  TlSlack r;
  float walk = ceilf(m);
  if (walk > 65535.0f) walk = 65535.0f;
  if (safety > 65535.0f) safety = 65535.0f;
  r.walkS = (uint16_t)walk;
  r.safetyS = (uint16_t)(safety + 0.5f);
  r.slackS = etaS - (int32_t)r.walkS - (int32_t)r.safetyS;
  r.level = lv;
  r.n = n;
  return r;
}
