// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// EEPROM 저장소. 전체 상태(부팅 횟수, 폰 주소, 중복 방지 목록, 통계 칸)를
// 이미지 하나로 묶어 RAM에 두고, 저장할 때 A/B 두 벌 중 안 쓰는 쪽에 통째로 쓴다.
//
// 이렇게 하는 이유: UNO R4의 EEPROM 라이브러리는 1바이트만 써도 그 바이트가 속한
// 1024바이트 블록 전체를 지우고 다시 쓴다. 쓰는 도중 전원이 끊기면 블록 전체가
// 깨질 수 있으므로, 항상 "지금 안 쓰는 벌"에만 쓰고 CRC가 맞는 최신 벌을 읽는다.
#ifndef TL_STORE_H
#define TL_STORE_H

#include <Arduino.h>
#include "TlConfig.h"

// ---- 이미지 배치(바이트 오프셋) ----
#define TL_IMG_HDR 16                                   // 머리말
#define TL_REG_REC 8                                    // 폰 주소 1건
#define TL_DEDUP_HW_OFF (TL_DEDUP_RING * 4 + 1)         // 레코드 안에서 최고값 2개(현관판 ID, 폰 ID)가 시작하는 위치 (B15)
#define TL_DEDUP_REC (TL_DEDUP_HW_OFF + 8)              // 사용자 1명: 최근 이동 ID 목록 + 다음 위치 + 최고값 2개
#define TL_CELL_REC 6                                   // 통계 1칸
#define TL_OFF_REG TL_IMG_HDR
#define TL_OFF_DEDUP (TL_OFF_REG + TL_MAX_USERS * TL_REG_REC)
#define TL_OFF_CELLS (TL_OFF_DEDUP + TL_MAX_USERS * TL_DEDUP_REC)
#define TL_OFF_CRC (TL_OFF_CELLS + TL_NUM_CELLS * TL_CELL_REC)
#define TL_IMG_SIZE (TL_OFF_CRC + 4)
// 한 벌이 차지하는 EEPROM 크기(블록 단위로 올림)
#define TL_COPY_STRIDE (((TL_IMG_SIZE + TL_EE_BLOCK - 1) / TL_EE_BLOCK) * TL_EE_BLOCK)
#define TL_LAYOUT_VER 2

static_assert(TL_EE_BASE % TL_EE_BLOCK == 0, "TL_EE_BASE must be a multiple of 1024");
static_assert(2 * TL_COPY_STRIDE <= TL_EE_SIZE, "TL_EE_SIZE too small for two image copies");
static_assert(TL_MAX_USERS >= 1 && TL_MAX_USERS <= 8, "TL_MAX_USERS out of range");
static_assert(TL_MAX_STOPS >= 1 && TL_MAX_STOPS <= 8, "TL_MAX_STOPS out of range");

// 통계 1칸(EEPROM에는 6바이트로 저장)
struct TlCell {
  uint8_t n;         // 표본 수(최대 255)
  uint8_t strikes;   // 연속 이상값 횟수
  uint16_t meanDs;   // 평균(0.1초)
  uint16_t sdDs;     // 표준편차(0.1초)
};

// EEPROM.put/get에 통째로 넘기기 위한 이미지 형식
struct TlImage {
  uint8_t b[TL_IMG_SIZE];
};

class TlStore {
 public:
  // EEPROM에서 읽어 온다. 유효한 벌이 없으면 새로 만든다. 부팅 횟수를 1 올려 저장한다.
  // 반환: 저장까지 성공하면 true(실패해도 RAM에서는 동작한다)
  bool begin();
  bool storageOk() const { return _ok; }
  bool wasFormatted() const { return _formatted; }
  uint16_t boot() const;

  // 통계 칸(RAM 이미지 기준). 바꾼 뒤 commit()을 불러야 EEPROM에 남는다.
  void getCell(uint16_t idx, TlCell* c) const;
  void setCell(uint16_t idx, const TlCell* c);

  // 폰 주소(user: 1부터)
  bool getPhone(uint8_t user, uint8_t ip[4], uint16_t* port) const;
  bool setPhone(uint8_t user, const uint8_t ip[4], uint16_t port);   // 값이 바뀌었으면 true

  // 중복 방지 목록
  bool seenTrip(uint8_t user, uint32_t trip) const;
  void addTrip(uint8_t user, uint32_t trip);

  // RAM 이미지를 EEPROM의 안 쓰는 벌에 쓰고 되읽어 확인한다.
  bool commit();
  // 마지막으로 저장된 상태로 RAM 이미지를 되돌린다(commit 실패 시).
  void rollback();
  // 통계 칸과 중복 방지 목록을 지운다(부팅 횟수와 폰 주소는 유지). commit 포함.
  bool clearStats();

  uint32_t commitCount() const { return _commits; }
  const uint8_t* raw() const { return _img.b; }

 private:
  TlImage _img;
  int8_t _active = -1;       // 현재 유효한 벌(0/1), 없으면 -1
  bool _ok = false;
  bool _formatted = false;
  uint32_t _commits = 0;

  void fresh(uint16_t boot);
  bool slotValid(uint8_t slot, uint32_t* seq) const;
  uint32_t slotCrc(uint8_t slot) const;
  static int slotAddr(uint8_t slot) { return TL_EE_BASE + (int)slot * TL_COPY_STRIDE; }
};

uint32_t tlCrc32(uint32_t crc, const uint8_t* data, size_t len);

#endif
