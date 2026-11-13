// 생성형 AI의 도움을 받아 작성함.
// JSON 문자열의 이스케이프(\uXXXX 등)를 풀어 UTF-8로 쌓는 작은 도구. Arduino에 의존하지 않아 PC에서 시험한다. (C06)
#pragma once
#include <stdint.h>

// JSON 문자열 한 개를 out 버퍼(크기 len, 끝의 0 포함)에 UTF-8로 쌓는다.
// 한 번이라도 못 넣은 글자가 있으면 full이 되고, 그 뒤로는 아무것도 넣지 않는다(중간이 빠진 문자열을 만들지 않음).
struct JsonStrOut {
  char* out; uint8_t len; uint8_t n; bool full; uint16_t high;   // high: 짝을 기다리는 앞쪽 서로게이트(없으면 0)

  void begin(char* o, uint8_t l) { out = o; len = l; n = 0; full = false; high = 0; }

  // 이스케이프가 풀린 일반 바이트 1개
  void putChar(uint8_t c) {
    flushHigh();
    putBytes(&c, 1);
  }

  // \uXXXX 하나
  void putCode(uint16_t cp) {
    if (cp >= 0xD800 && cp <= 0xDBFF) {                  // 앞쪽 서로게이트
      if (high) putQuestion();                           // 앞쪽이 연달아 오면 앞의 것은 짝이 없다
      high = cp;
      return;
    }
    if (cp >= 0xDC00 && cp <= 0xDFFF) {                  // 뒤쪽 서로게이트
      if (high) {
        uint32_t full32 = 0x10000UL + ((uint32_t)(high - 0xD800) << 10) + (uint32_t)(cp - 0xDC00);
        high = 0;
        uint8_t u8[4];
        u8[0] = (uint8_t)(0xF0 | (full32 >> 18));
        u8[1] = (uint8_t)(0x80 | ((full32 >> 12) & 0x3F));
        u8[2] = (uint8_t)(0x80 | ((full32 >> 6) & 0x3F));
        u8[3] = (uint8_t)(0x80 | (full32 & 0x3F));
        putBytes(u8, 4);
      } else {
        putQuestion();                                   // 짝 없는 뒤쪽
      }
      return;
    }
    flushHigh();
    uint8_t u8[3];
    uint8_t k;
    if (cp < 0x80) { u8[0] = (uint8_t)cp; k = 1; }
    else if (cp < 0x800) { u8[0] = (uint8_t)(0xC0 | (cp >> 6)); u8[1] = (uint8_t)(0x80 | (cp & 0x3F)); k = 2; }
    else { u8[0] = (uint8_t)(0xE0 | (cp >> 12)); u8[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
           u8[2] = (uint8_t)(0x80 | (cp & 0x3F)); k = 3; }
    putBytes(u8, k);
  }

  // 문자열이 끝났다: 짝을 못 찾은 앞쪽 서로게이트를 정리하고 끝에 0을 붙인다
  void finish() {
    flushHigh();
    if (len == 0) return;
    out[n] = 0;
  }

 private:
  // 바이트 k개를 통째로 넣는다. 못 넣으면 full이 되고 그 뒤로는 아무것도 넣지 않는다.
  void putBytes(const uint8_t* b, uint8_t k) {
    if (full || n + k > len - 1) { full = true; return; }
    for (uint8_t i = 0; i < k; i++) out[n++] = (char)b[i];
  }
  void putQuestion() { uint8_t q = '?'; putBytes(&q, 1); }
  void flushHigh() {
    if (high) { high = 0; putQuestion(); }
  }
};
