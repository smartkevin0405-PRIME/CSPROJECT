// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 최소한의 HTTP/1.1 요청·응답 처리와 메시지 인증(MAC) 계산.
#ifndef TL_HTTP_H
#define TL_HTTP_H

#include <Arduino.h>
#include <Client.h>
#include "TlConfig.h"

#define TL_PATH_MAX 64
#define TL_MAC_HEX 32

// 받은 요청
struct TlHttpReq {
  char method[8];
  char path[TL_PATH_MAX];          // 쿼리 포함
  char mac[TL_MAC_HEX + 1];        // X-TL-Mac(없으면 빈 문자열)
  uint8_t user;                    // X-TL-User(없으면 0)
  uint16_t bodyLen;
  char body[TL_BODY_MAX + 1];
};

enum TlReadResult {
  TL_READ_OK = 0,
  TL_READ_TIMEOUT,     // 제한 시간 안에 다 오지 않음
  TL_READ_BAD,         // 형식 오류
  TL_READ_TOOBIG       // 본문이 TL_BODY_MAX 초과
};

// 요청 한 건을 읽는다(제한 시간 timeoutMs).
TlReadResult tlHttpReadRequest(Client& c, TlHttpReq* req, uint32_t timeoutMs);

// 응답을 보낸다. macHex가 NULL이 아니면 X-TL-Mac 헤더를 붙인다.
// scratch는 머리말+본문을 한 번에 보내기 위한 작업 버퍼.
void tlHttpSendResponse(Client& c, int status, const char* body, const char* macHex,
                        char* scratch, size_t scratchCap);

// POST 요청을 보내고 응답을 읽는다. 반환: HTTP 상태 코드, 실패하면 -1.
// respBody(크기 respCap)와 respMac(33바이트)에 응답 본문과 X-TL-Mac을 담는다.
int tlHttpPost(Client& c, const char* path, const char* body, uint8_t user, const char* macHex,
               char* respBody, size_t respCap, char* respMac,
               char* scratch, size_t scratchCap, uint32_t timeoutMs);

// 요청 MAC = HMAC(키, 메서드 + " " + 경로 + "\n" + 본문)의 앞 16바이트(16진수 32글자)
void tlMacRequest(const uint8_t key[16], const char* method, const char* path, const char* body, char out[TL_MAC_HEX + 1]);
// 응답 MAC = HMAC(키, "R " + 경로 + "\n" + 요청 MAC + "\n" + 응답 본문)의 앞 16바이트
void tlMacResponse(const uint8_t key[16], const char* path, const char* reqMacHex, const char* body, char out[TL_MAC_HEX + 1]);

#endif
