// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 평면 JSON 읽기 구현. 객체 전체를 훑으며 형식을 검사하고 원하는 키의 값을 찾는다.
#include "TlJson.h"
#include <string.h>

static const char* skipWs(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// 여는 따옴표 다음 위치 p에서 닫는 따옴표를 찾는다. 없으면 NULL.
static const char* strEnd(const char* p) {
  while (*p != '\0') {
    if (*p == '\\') {
      if (p[1] == '\0') return NULL;
      p += 2;
      continue;
    }
    if (*p == '"') return p;
    if ((unsigned char)*p < 0x20) return NULL;   // 제어 문자는 허용하지 않는다
    p++;
  }
  return NULL;
}

// 객체 전체를 검사한다. key가 있으면 그 값의 범위(vs~ve)와 종류(type)를 돌려준다.
// type: 's' 문자열(따옴표 안쪽), 'n' 숫자, 'l' true/false/null
static bool scan(const char* json, const char* key, const char** vs, const char** ve, char* type) {
  if (json == NULL) return false;
  size_t keyLen = (key != NULL) ? strlen(key) : 0;
  bool found = false;
  const char* p = skipWs(json);
  if (*p != '{') return false;
  p = skipWs(p + 1);
  if (*p == '}') {
    p = skipWs(p + 1);
    return (*p == '\0') && (key == NULL);
  }
  for (;;) {
    p = skipWs(p);
    if (*p != '"') return false;
    const char* ks = p + 1;
    const char* ke = strEnd(ks);
    if (ke == NULL) return false;
    p = skipWs(ke + 1);
    if (*p != ':') return false;
    p = skipWs(p + 1);
    const char* s; const char* e; char t;
    if (*p == '"') {
      s = p + 1;
      e = strEnd(s);
      if (e == NULL) return false;
      t = 's';
      p = e + 1;
    } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
      s = p;
      if (*p == '-') p++;
      if (!(*p >= '0' && *p <= '9')) return false;
      while ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-') p++;
      e = p;
      t = 'n';
    } else if (strncmp(p, "true", 4) == 0) {
      s = p; p += 4; e = p; t = 'l';
    } else if (strncmp(p, "false", 5) == 0) {
      s = p; p += 5; e = p; t = 'l';
    } else if (strncmp(p, "null", 4) == 0) {
      s = p; p += 4; e = p; t = 'l';
    } else {
      return false;            // 중첩 객체·배열 등은 지원하지 않는다
    }
    if (!found && key != NULL && (size_t)(ke - ks) == keyLen && memcmp(ks, key, keyLen) == 0) {
      found = true;
      if (vs) *vs = s;
      if (ve) *ve = e;
      if (type) *type = t;
    }
    p = skipWs(p);
    if (*p == ',') { p++; continue; }
    if (*p == '}') { p++; break; }
    return false;
  }
  p = skipWs(p);
  if (*p != '\0') return false;
  return (key == NULL) ? true : found;
}

bool tlJsonValid(const char* json) {
  return scan(json, NULL, NULL, NULL, NULL);
}

bool tlJsonGetU32(const char* json, const char* key, uint32_t* out) {
  const char* s; const char* e; char t;
  if (!scan(json, key, &s, &e, &t) || t != 'n') return false;
  if (e - s > 10) return false;
  uint32_t v = 0;
  for (const char* p = s; p < e; p++) {
    if (*p < '0' || *p > '9') return false;       // 음수·실수 거절
    uint8_t d = (uint8_t)(*p - '0');
    if (v > 429496729UL || (v == 429496729UL && d > 5)) return false;   // 범위 초과
    v = v * 10 + d;
  }
  *out = v;
  return true;
}

bool tlJsonGetI32(const char* json, const char* key, int32_t* out) {
  const char* s; const char* e; char t;
  if (!scan(json, key, &s, &e, &t) || t != 'n') return false;
  bool neg = false;
  if (*s == '-') { neg = true; s++; }
  if (e - s > 10 || e == s) return false;
  uint32_t v = 0;
  for (const char* p = s; p < e; p++) {
    if (*p < '0' || *p > '9') return false;
    uint8_t d = (uint8_t)(*p - '0');
    if (v > 214748364UL || (v == 214748364UL && d > (neg ? 8 : 7))) return false;
    v = v * 10 + d;
  }
  *out = neg ? (int32_t)((uint32_t)0 - v) : (int32_t)v;
  return true;
}

bool tlJsonGetStr(const char* json, const char* key, char* out, size_t cap) {
  const char* s; const char* e; char t;
  if (cap == 0) return false;
  if (!scan(json, key, &s, &e, &t) || t != 's') return false;
  size_t n = 0;
  for (const char* p = s; p < e; p++) {
    char ch = *p;
    if (ch == '\\') {
      p++;
      switch (*p) {
        case 'n': ch = '\n'; break;
        case 't': ch = '\t'; break;
        case 'r': ch = '\r'; break;
        case 'b': ch = '\b'; break;
        case 'f': ch = '\f'; break;
        case 'u':               // \uXXXX는 '?'로 바꾼다(규격상 ASCII만 쓴다)
          for (uint8_t i = 0; i < 4 && p + 1 < e; i++) p++;
          ch = '?';
          break;
        default: ch = *p; break;  // \" \\ \/
      }
    }
    if (n + 1 >= cap) return false;
    out[n++] = ch;
  }
  out[n] = '\0';
  return true;
}
