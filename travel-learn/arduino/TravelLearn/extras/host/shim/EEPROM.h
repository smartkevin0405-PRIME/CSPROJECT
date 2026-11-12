// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 호스트 시험용 EEPROM 대역. UNO R4 코어(virtualEEPROM)의 동작을 흉내 낸다:
//  - 크기 8192바이트, 1024바이트 블록
//  - 쓰기 1회(write 또는 put의 블록 1개) = 블록 전체 지우기(필요할 때) + 블록 전체 다시 쓰기
//  - 전원 차단 주입: 지우기 1단계 + 바이트 쓰기 1024단계 중 원하는 단계에서 끊을 수 있다
#ifndef HOST_EEPROM_H
#define HOST_EEPROM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define HOST_EE_SIZE 8192
#define HOST_EE_BLOCK 1024

// 전원 차단을 나타내는 예외(시험 코드가 잡아서 '재부팅'한다)
struct HostPowerCut {};

struct HostEeprom {
  uint8_t mem[HOST_EE_SIZE];
  long cutAfterSteps = -1;                 // 0 이상이면 그만큼의 단계 뒤에 전원 차단
  unsigned long steps = 0;                 // 지금까지의 쓰기 단계 수(지우기 1 + 바이트당 1)
  unsigned long blockErases[HOST_EE_SIZE / HOST_EE_BLOCK];
  unsigned long blockPrograms[HOST_EE_SIZE / HOST_EE_BLOCK];
  char file[256];

  HostEeprom() { reset(); file[0] = '\0'; }
  void reset() {
    memset(mem, 0xFF, sizeof(mem));
    memset(blockErases, 0, sizeof(blockErases));
    memset(blockPrograms, 0, sizeof(blockPrograms));
    steps = 0;
    cutAfterSteps = -1;
  }
  static HostEeprom& inst() { static HostEeprom e; return e; }

  void step() {
    if (cutAfterSteps == 0) { cutAfterSteps = -1; throw HostPowerCut(); }
    if (cutAfterSteps > 0) cutAfterSteps--;
    steps++;
  }

  // 블록 하나를 새 내용(page)으로 바꾼다. 실제 코어처럼 바뀐 것이 없으면 아무것도 하지 않는다.
  void writeBlock(int block, const uint8_t* page, bool needErase) {
    uint8_t* dst = mem + block * HOST_EE_BLOCK;
    if (memcmp(dst, page, HOST_EE_BLOCK) == 0) return;
    if (needErase) {
      step();
      memset(dst, 0xFF, HOST_EE_BLOCK);
      blockErases[block]++;
    }
    blockPrograms[block]++;
    for (int i = 0; i < HOST_EE_BLOCK; i++) {
      step();                               // 여기서 끊기면 뒷부분은 지워진 채(0xFF)로 남는다
      dst[i] = page[i];
    }
    save();
  }

  void writeRange(int idx, const uint8_t* data, size_t len) {
    while (len > 0) {
      if (idx < 0 || idx >= HOST_EE_SIZE) return;
      int block = idx / HOST_EE_BLOCK;
      int off = idx % HOST_EE_BLOCK;
      size_t n = (size_t)(HOST_EE_BLOCK - off);
      if (n > len) n = len;
      uint8_t page[HOST_EE_BLOCK];
      memcpy(page, mem + block * HOST_EE_BLOCK, HOST_EE_BLOCK);
      bool needErase = false;
      for (size_t i = 0; i < n; i++) {
        if (page[off + i] != data[i] && page[off + i] != 0xFF) needErase = true;
        page[off + i] = data[i];
      }
      writeBlock(block, page, needErase);
      idx += (int)n; data += n; len -= n;
    }
  }

  bool load(const char* path) {
    snprintf(file, sizeof(file), "%s", path);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(mem, 1, sizeof(mem), f);
    fclose(f);
    return n == sizeof(mem);
  }
  void save() {
    if (file[0] == '\0') return;
    FILE* f = fopen(file, "wb");
    if (!f) return;
    fwrite(mem, 1, sizeof(mem), f);
    fclose(f);
  }
};

class EEPROMClass {
 public:
  uint8_t read(int idx) { return (idx >= 0 && idx < HOST_EE_SIZE) ? HostEeprom::inst().mem[idx] : 0xFF; }
  void write(int idx, uint8_t val) { HostEeprom::inst().writeRange(idx, &val, 1); }
  void update(int idx, uint8_t val) { if (read(idx) != val) write(idx, val); }
  uint16_t length() { return HOST_EE_SIZE; }
  template <typename T> T& get(int idx, T& t) {
    uint8_t* p = (uint8_t*)&t;
    for (size_t i = 0; i < sizeof(T); i++) p[i] = read(idx + (int)i);
    return t;
  }
  template <typename T> const T& put(int idx, const T& t) {
    HostEeprom::inst().writeRange(idx, (const uint8_t*)&t, sizeof(T));
    return t;
  }
};

static EEPROMClass EEPROM __attribute__((unused));

#endif
