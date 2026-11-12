// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// HTTP 요청·응답 처리 구현. 연결 하나에 요청 하나만 처리한다(Connection: close).
#include "TlHttp.h"
#include "TlSha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TL_LINE_MAX 128

// 한 바이트를 읽는다. 반환: 0~255, 제한 시간 초과 -1, 연결 끊김 -2
static int readByte(Client& c, uint32_t startMs, uint32_t timeoutMs) {
  for (;;) {
    if (c.available() > 0) return c.read();
    if ((uint32_t)(millis() - startMs) >= timeoutMs) return -1;
    if (!c.connected()) {
      if (c.available() > 0) return c.read();
      return -2;
    }
    delay(1);
  }
}

// 한 줄을 읽는다(CRLF 제거). 너무 긴 줄은 잘라서 담고 나머지는 버린다.
// 반환: 줄 길이, 제한 시간 초과 -1, 연결 끊김 -2
static int readLine(Client& c, char* line, size_t cap, uint32_t startMs, uint32_t timeoutMs) {
  size_t n = 0;
  for (;;) {
    int b = readByte(c, startMs, timeoutMs);
    if (b < 0) return b;
    if (b == '\n') break;
    if (b == '\r') continue;
    if (n + 1 < cap) line[n++] = (char)b;
  }
  line[n] = '\0';
  return (int)n;
}

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

// 헤더 줄이 name(소문자)으로 시작하면 값의 시작 위치를 돌려준다. 아니면 NULL.
static const char* headerValue(const char* line, const char* name) {
  size_t i = 0;
  for (; name[i] != '\0'; i++) {
    if (lower(line[i]) != name[i]) return NULL;
  }
  if (line[i] != ':') return NULL;
  const char* v = line + i + 1;
  while (*v == ' ' || *v == '\t') v++;
  return v;
}

// 헤더들을 읽어 필요한 값만 뽑는다. 반환: 0 정상, 음수는 readLine 오류
// line은 호출한 쪽의 줄 버퍼를 빌려 쓴다(스택을 아끼기 위해).
static int readHeaders(Client& c, char* line, size_t lineCap, long* contentLen, uint8_t* user, char* mac,
                       uint32_t startMs, uint32_t timeoutMs) {
  *contentLen = -1;
  if (user) *user = 0;
  if (mac) mac[0] = '\0';
  for (;;) {
    int n = readLine(c, line, lineCap, startMs, timeoutMs);
    if (n < 0) return n;
    if (n == 0) return 0;      // 빈 줄 = 헤더 끝
    const char* v;
    if ((v = headerValue(line, "content-length")) != NULL) {
      *contentLen = atol(v);
    } else if (user != NULL && (v = headerValue(line, "x-tl-user")) != NULL) {
      long u = atol(v);
      *user = (u >= 1 && u <= 255) ? (uint8_t)u : 0;
    } else if (mac != NULL && (v = headerValue(line, "x-tl-mac")) != NULL) {
      size_t len = strlen(v);
      while (len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t')) len--;
      if (len == TL_MAC_HEX) {
        for (size_t i = 0; i < TL_MAC_HEX; i++) mac[i] = lower(v[i]);
        mac[TL_MAC_HEX] = '\0';
      }
    }
  }
}

TlReadResult tlHttpReadRequest(Client& c, TlHttpReq* req, uint32_t timeoutMs) {
  uint32_t start = millis();
  char line[TL_LINE_MAX];
  req->method[0] = '\0'; req->path[0] = '\0'; req->mac[0] = '\0';
  req->user = 0; req->bodyLen = 0; req->body[0] = '\0';

  int n = readLine(c, line, sizeof(line), start, timeoutMs);
  if (n < 0) return TL_READ_TIMEOUT;
  // 요청 줄: "METHOD 경로 HTTP/1.1"
  char* sp1 = strchr(line, ' ');
  if (sp1 == NULL) return TL_READ_BAD;
  char* sp2 = strchr(sp1 + 1, ' ');
  if (sp2 == NULL) return TL_READ_BAD;
  size_t mlen = (size_t)(sp1 - line);
  size_t plen = (size_t)(sp2 - sp1 - 1);
  if (mlen == 0 || mlen >= sizeof(req->method) || plen == 0 || plen >= sizeof(req->path)) return TL_READ_BAD;
  memcpy(req->method, line, mlen); req->method[mlen] = '\0';
  memcpy(req->path, sp1 + 1, plen); req->path[plen] = '\0';

  long contentLen;
  if (readHeaders(c, line, sizeof(line), &contentLen, &req->user, req->mac, start, timeoutMs) < 0) return TL_READ_TIMEOUT;
  if (contentLen < 0) contentLen = 0;
  if (contentLen > TL_BODY_MAX) return TL_READ_TOOBIG;
  for (long i = 0; i < contentLen; i++) {
    int b = readByte(c, start, timeoutMs);
    if (b < 0) return TL_READ_TIMEOUT;
    req->body[i] = (char)b;
  }
  req->body[contentLen] = '\0';
  req->bodyLen = (uint16_t)contentLen;
  return TL_READ_OK;
}

static const char* reason(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

void tlHttpSendResponse(Client& c, int status, const char* body, const char* macHex,
                        char* scratch, size_t scratchCap) {
  size_t blen = strlen(body);
  int n;
  if (macHex != NULL) {
    n = snprintf(scratch, scratchCap,
                 "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n"
                 "Connection: close\r\nX-TL-Mac: %s\r\n\r\n%s",
                 status, reason(status), (unsigned)blen, macHex, body);
  } else {
    n = snprintf(scratch, scratchCap,
                 "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n"
                 "Connection: close\r\n\r\n%s",
                 status, reason(status), (unsigned)blen, body);
  }
  if (n <= 0 || (size_t)n >= scratchCap) return;   // 버퍼 부족(설계상 일어나지 않는다)
  c.write((const uint8_t*)scratch, (size_t)n);
  c.flush();
}

int tlHttpPost(Client& c, const char* path, const char* body, uint8_t user, const char* macHex,
               char* respBody, size_t respCap, char* respMac,
               char* scratch, size_t scratchCap, uint32_t timeoutMs) {
  respBody[0] = '\0';
  respMac[0] = '\0';
  int n = snprintf(scratch, scratchCap,
                   "POST %s HTTP/1.1\r\nHost: phone\r\nContent-Type: application/json\r\n"
                   "Content-Length: %u\r\nConnection: close\r\nX-TL-User: %u\r\nX-TL-Mac: %s\r\n\r\n%s",
                   path, (unsigned)strlen(body), (unsigned)user, macHex, body);
  if (n <= 0 || (size_t)n >= scratchCap) return -1;
  if (c.write((const uint8_t*)scratch, (size_t)n) != (size_t)n) return -1;
  c.flush();

  uint32_t start = millis();
  char line[TL_LINE_MAX];
  int len = readLine(c, line, sizeof(line), start, timeoutMs);
  if (len < 0) return -1;
  // 상태 줄: "HTTP/1.1 200 OK"
  const char* sp = strchr(line, ' ');
  if (sp == NULL || strncmp(line, "HTTP/", 5) != 0) return -1;
  int status = atoi(sp + 1);
  if (status < 100 || status > 599) return -1;
  long contentLen;
  if (readHeaders(c, line, sizeof(line), &contentLen, NULL, respMac, start, timeoutMs) < 0) return -1;
  if (contentLen < 0 || (size_t)contentLen >= respCap) return -1;   // 길이 없는 응답은 받지 않는다
  for (long i = 0; i < contentLen; i++) {
    int b = readByte(c, start, timeoutMs);
    if (b < 0) return -1;
    respBody[i] = (char)b;
  }
  respBody[contentLen] = '\0';
  return status;
}

// HMAC 계산용 작업 공간. 스택(기본 1KB)을 아끼려고 전역에 하나만 둔다(loop()에서만 쓰므로 동시에 쓰이지 않는다).
static TlHmacCtx gHmac;

static void macFinish(char out[TL_MAC_HEX + 1]) {
  uint8_t full[32];
  tlHmacFinal(&gHmac, full);
  tlHexEncode(full, 16, out);      // 앞 16바이트만 쓴다
}

void tlMacRequest(const uint8_t key[16], const char* method, const char* path, const char* body, char out[TL_MAC_HEX + 1]) {
  tlHmacInit(&gHmac, key, 16);
  tlHmacUpdate(&gHmac, method, strlen(method));
  tlHmacUpdate(&gHmac, " ", 1);
  tlHmacUpdate(&gHmac, path, strlen(path));
  tlHmacUpdate(&gHmac, "\n", 1);
  tlHmacUpdate(&gHmac, body, strlen(body));
  macFinish(out);
}

void tlMacResponse(const uint8_t key[16], const char* path, const char* reqMacHex, const char* body, char out[TL_MAC_HEX + 1]) {
  tlHmacInit(&gHmac, key, 16);
  tlHmacUpdate(&gHmac, "R ", 2);
  tlHmacUpdate(&gHmac, path, strlen(path));
  tlHmacUpdate(&gHmac, "\n", 1);
  tlHmacUpdate(&gHmac, reqMacHex, strlen(reqMacHex));
  tlHmacUpdate(&gHmac, "\n", 1);
  tlHmacUpdate(&gHmac, body, strlen(body));
  macFinish(out);
}
