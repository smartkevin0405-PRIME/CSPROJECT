// 생성형 AI의 도움을 받아 작성함. JsonStr.h(JSON 문자열 이스케이프 → UTF-8) 시험.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../SmartEntrance/JsonStr.h"

static bool is(const char* got, const uint8_t* want, size_t n) {
  return strlen(got) == n && memcmp(got, want, n) == 0;
}

int main() {
  char buf[16];
  JsonStrOut js;

  // abc
  js.begin(buf, sizeof(buf));
  js.putChar('a'); js.putChar('b'); js.putChar('c'); js.finish();
  assert(strcmp(buf, "abc") == 0 && !js.full);

  // 가 (U+AC00) → EA B0 80
  js.begin(buf, sizeof(buf));
  js.putCode(0xAC00); js.finish();
  const uint8_t ga[] = {0xEA, 0xB0, 0x80};
  assert(is(buf, ga, 3));

  // 버퍼 5(넣을 수 있는 바이트 4): ab + 가 + c → 가가 안 들어가면 full, 뒤의 c도 붙지 않는다
  char small[5];
  js.begin(small, sizeof(small));
  js.putChar('a'); js.putChar('b'); js.putCode(0xAC00); js.putChar('c'); js.finish();
  assert(strcmp(small, "ab") == 0 && js.full);

  // D83D + DE00 → F0 9F 98 80 (이모지)
  js.begin(buf, sizeof(buf));
  js.putCode(0xD83D); js.putCode(0xDE00); js.finish();
  const uint8_t smile[] = {0xF0, 0x9F, 0x98, 0x80};
  assert(is(buf, smile, 4));

  // D83D 다음에 일반 글자 → ?x
  js.begin(buf, sizeof(buf));
  js.putCode(0xD83D); js.putChar('x'); js.finish();
  assert(strcmp(buf, "?x") == 0);

  // DE00만 → ?
  js.begin(buf, sizeof(buf));
  js.putCode(0xDE00); js.finish();
  assert(strcmp(buf, "?") == 0);

  // D83D에서 끝 → ?
  js.begin(buf, sizeof(buf));
  js.putCode(0xD83D); js.finish();
  assert(strcmp(buf, "?") == 0);

  // 줄바꿈 → 0A
  js.begin(buf, sizeof(buf));
  js.putChar('\n'); js.finish();
  const uint8_t lf[] = {0x0A};
  assert(is(buf, lf, 1));

  puts("JSON 문자열 이스케이프 시험 통과");
}
