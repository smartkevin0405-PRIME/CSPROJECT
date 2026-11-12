// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 중첩 없는(평면) JSON 객체 읽기. 통신규격의 메시지는 모두 평면 객체다.
#ifndef TL_JSON_H
#define TL_JSON_H

#include <stdint.h>
#include <stddef.h>

// 평면 JSON 객체로 올바른지 검사한다(중첩 객체·배열은 오류).
bool tlJsonValid(const char* json);
// key의 값이 0 이상 정수이면 out에 넣고 true. 없거나 실수·음수·범위 초과면 false.
bool tlJsonGetU32(const char* json, const char* key, uint32_t* out);
// key의 값이 정수이면 out에 넣고 true.
bool tlJsonGetI32(const char* json, const char* key, int32_t* out);
// key의 값이 문자열이면 out(크기 cap)에 복사하고 true. 넘치면 false.
bool tlJsonGetStr(const char* json, const char* key, char* out, size_t cap);

#endif
