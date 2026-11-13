// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// EEPROM 저장소 구현(A/B 두 벌, CRC32, 순번).
#include "TlStore.h"
#include <EEPROM.h>
#include <string.h>

// 머리말 배치: 0 "TLS1"(4) | 4 배치 버전 | 5 사용자 수 | 6 정류장 수 | 7 날씨 수
//             | 8 시간대 수 | 9 목록 길이 | 10 부팅 횟수(2) | 12 순번(4)
static const uint8_t MAGIC[4] = { 'T', 'L', 'S', '1' };

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// CRC-32(IEEE 802.3), 표 없이 비트 단위로 계산. 처음에는 crc = 0으로 부른다.
uint32_t tlCrc32(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t k = 0; k < 8; k++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
  }
  return ~crc;
}

static bool headerMatches(const uint8_t* h) {
  return memcmp(h, MAGIC, 4) == 0 && h[4] == TL_LAYOUT_VER && h[5] == TL_MAX_USERS &&
         h[6] == TL_MAX_STOPS && h[7] == TL_NUM_WX && h[8] == TL_NUM_TB && h[9] == TL_DEDUP_RING;
}

// 새 이미지를 만든다(통계·주소·목록 모두 비움).
void TlStore::fresh(uint16_t boot) {
  memset(_img.b, 0, sizeof(_img.b));
  memcpy(_img.b, MAGIC, 4);
  _img.b[4] = TL_LAYOUT_VER;
  _img.b[5] = TL_MAX_USERS;
  _img.b[6] = TL_MAX_STOPS;
  _img.b[7] = TL_NUM_WX;
  _img.b[8] = TL_NUM_TB;
  _img.b[9] = TL_DEDUP_RING;
  wr16(_img.b + 10, boot);
  wr32(_img.b + 12, 0);
}

// EEPROM의 한 벌을 읽으며 CRC를 계산한다(RAM을 더 쓰지 않으려고 조금씩 읽는다).
uint32_t TlStore::slotCrc(uint8_t slot) const {
  uint8_t chunk[32];
  uint32_t crc = 0;
  int addr = slotAddr(slot);
  size_t left = TL_OFF_CRC;
  while (left > 0) {
    size_t n = (left < sizeof(chunk)) ? left : sizeof(chunk);
    for (size_t i = 0; i < n; i++) chunk[i] = EEPROM.read(addr++);
    crc = tlCrc32(crc, chunk, n);
    left -= n;
  }
  return crc;
}

bool TlStore::slotValid(uint8_t slot, uint32_t* seq) const {
  uint8_t h[TL_IMG_HDR];
  int addr = slotAddr(slot);
  for (uint8_t i = 0; i < TL_IMG_HDR; i++) h[i] = EEPROM.read(addr + i);
  if (!headerMatches(h)) return false;
  uint8_t c[4];
  for (uint8_t i = 0; i < 4; i++) c[i] = EEPROM.read(addr + TL_OFF_CRC + i);
  if (slotCrc(slot) != rd32(c)) return false;
  *seq = rd32(h + 12);
  return true;
}

bool TlStore::begin() {
  uint32_t seq0 = 0, seq1 = 0;
  bool v0 = slotValid(0, &seq0);
  bool v1 = slotValid(1, &seq1);
  _active = -1;
  if (v0 && v1) {
    _active = ((int32_t)(seq1 - seq0) > 0) ? 1 : 0;   // 순번이 더 큰 쪽이 최신
  } else if (v0) {
    _active = 0;
  } else if (v1) {
    _active = 1;
  }
  if (_active >= 0) {
    EEPROM.get(slotAddr((uint8_t)_active), _img);
    _formatted = false;
  } else {
    fresh(0);
    _formatted = true;
  }
  wr16(_img.b + 10, (uint16_t)(rd16(_img.b + 10) + 1));   // 부팅 횟수 +1
  _ok = commit();
  return _ok;
}

uint16_t TlStore::boot() const { return rd16(_img.b + 10); }

void TlStore::getCell(uint16_t idx, TlCell* c) const {
  if (idx >= TL_NUM_CELLS) { c->n = 0; c->strikes = 0; c->meanDs = 0; c->sdDs = 0; return; }
  const uint8_t* p = _img.b + TL_OFF_CELLS + (size_t)idx * TL_CELL_REC;
  c->n = p[0]; c->strikes = p[1]; c->meanDs = rd16(p + 2); c->sdDs = rd16(p + 4);
}

void TlStore::setCell(uint16_t idx, const TlCell* c) {
  if (idx >= TL_NUM_CELLS) return;
  uint8_t* p = _img.b + TL_OFF_CELLS + (size_t)idx * TL_CELL_REC;
  p[0] = c->n; p[1] = c->strikes; wr16(p + 2, c->meanDs); wr16(p + 4, c->sdDs);
}

bool TlStore::getPhone(uint8_t user, uint8_t ip[4], uint16_t* port) const {
  if (user < 1 || user > TL_MAX_USERS) return false;
  const uint8_t* p = _img.b + TL_OFF_REG + (size_t)(user - 1) * TL_REG_REC;
  if ((p[6] & 1) == 0) return false;      // 등록 안 됨
  memcpy(ip, p, 4);
  *port = rd16(p + 4);
  return true;
}

bool TlStore::setPhone(uint8_t user, const uint8_t ip[4], uint16_t port) {
  if (user < 1 || user > TL_MAX_USERS) return false;
  uint8_t* p = _img.b + TL_OFF_REG + (size_t)(user - 1) * TL_REG_REC;
  if ((p[6] & 1) && memcmp(p, ip, 4) == 0 && rd16(p + 4) == port) return false;   // 그대로
  memcpy(p, ip, 4);
  wr16(p + 4, port);
  p[6] = 1;
  p[7] = 0;
  return true;
}

bool TlStore::seenTrip(uint8_t user, uint32_t trip) const {
  if (user < 1 || user > TL_MAX_USERS || trip == 0) return false;
  const uint8_t* p = _img.b + TL_OFF_DEDUP + (size_t)(user - 1) * TL_DEDUP_REC;
  for (uint8_t i = 0; i < TL_DEDUP_RING; i++) {
    if (rd32(p + 4 * i) == trip) return true;
  }
  // 폰은 기록을 이동 ID 오름차순으로 보낸다. 이미 처리한 가장 큰 ID 이하는 재전송으로 본다. (B15)
  uint32_t hw = rd32(p + TL_DEDUP_HW_OFF + ((trip & 0x80000000UL) ? 4 : 0));
  return hw != 0 && trip <= hw;
}

void TlStore::addTrip(uint8_t user, uint32_t trip) {
  if (user < 1 || user > TL_MAX_USERS || trip == 0) return;
  uint8_t* p = _img.b + TL_OFF_DEDUP + (size_t)(user - 1) * TL_DEDUP_REC;
  uint8_t pos = (uint8_t)(p[TL_DEDUP_RING * 4] % TL_DEDUP_RING);
  wr32(p + 4 * pos, trip);
  p[TL_DEDUP_RING * 4] = (uint8_t)((pos + 1) % TL_DEDUP_RING);   // 가장 오래된 것부터 덮어쓴다
  uint8_t* h = p + TL_DEDUP_HW_OFF + ((trip & 0x80000000UL) ? 4 : 0);
  if (trip > rd32(h)) wr32(h, trip);                              // 최고값 갱신 (B15)
}

bool TlStore::commit() {
  uint32_t seq = rd32(_img.b + 12) + 1;
  wr32(_img.b + 12, seq);
  uint32_t crc = tlCrc32(0, _img.b, TL_OFF_CRC);
  wr32(_img.b + TL_OFF_CRC, crc);
  uint8_t target = (_active == 0) ? 1 : 0;        // 지금 안 쓰는 벌
  EEPROM.put(slotAddr(target), _img);              // 블록마다 지우기 1회 + 쓰기 1회
  // 되읽어 확인한다.
  uint8_t c[4];
  for (uint8_t i = 0; i < 4; i++) c[i] = EEPROM.read(slotAddr(target) + TL_OFF_CRC + i);
  if (rd32(c) != crc || slotCrc(target) != crc) return false;
  _active = (int8_t)target;
  _commits++;
  return true;
}

void TlStore::rollback() {
  if (_active >= 0) EEPROM.get(slotAddr((uint8_t)_active), _img);
}

bool TlStore::clearStats() {
  memset(_img.b + TL_OFF_DEDUP, 0, (size_t)(TL_OFF_CRC - TL_OFF_DEDUP));
  return commit();
}
