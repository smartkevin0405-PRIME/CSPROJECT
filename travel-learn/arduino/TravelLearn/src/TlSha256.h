// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// SHA-256, HMAC-SHA256, 16진수 변환. 외부 라이브러리 없이 동작한다.
#ifndef TL_SHA256_H
#define TL_SHA256_H

#include <stdint.h>
#include <stddef.h>

struct TlSha256Ctx {
  uint32_t h[8];
  uint32_t lenLo;      // 지금까지 넣은 바이트 수(하위)
  uint32_t lenHi;      // (상위)
  uint8_t buf[64];
  uint8_t fill;        // buf에 쌓인 바이트 수
};

void tlSha256Init(TlSha256Ctx* c);
void tlSha256Update(TlSha256Ctx* c, const void* data, size_t len);
void tlSha256Final(TlSha256Ctx* c, uint8_t out[32]);

struct TlHmacCtx {
  TlSha256Ctx inner;
  uint8_t okey[64];    // 바깥 해시용 키 패드
};

void tlHmacInit(TlHmacCtx* c, const uint8_t* key, size_t keyLen);
void tlHmacUpdate(TlHmacCtx* c, const void* data, size_t len);
void tlHmacFinal(TlHmacCtx* c, uint8_t out[32]);

// 바이트 n개를 소문자 16진수 2n글자 + '\0'로 바꾼다.
void tlHexEncode(const uint8_t* in, size_t n, char* out);
// 16진수 2n글자를 바이트 n개로 바꾼다. 길이·문자가 틀리면 false.
bool tlHexDecode(const char* hex, uint8_t* out, size_t n);
// 길이 n의 두 문자열을 시간 차 없이 비교한다(MAC 비교용).
bool tlConstEq(const char* a, const char* b, size_t n);

#endif
