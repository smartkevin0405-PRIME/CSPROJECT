// 생성형 AI의 도움을 받아 작성함. PC 시험용 API 대역.
#pragma once
#include <stdint.h>
#include <string.h>
// 시험에서 카드를 대는 흉내: shimCardLen > 0이면 다음 확인 때 그 카드를 한 번 돌려준다
inline uint8_t shimCardUid[10];
inline uint8_t shimCardLen = 0;
struct MFRC522 {
 struct { uint8_t size; uint8_t uidByte[10]; } uid{};
 MFRC522(int,int) {}
 bool PICC_IsNewCardPresent() {return shimCardLen > 0;}
 bool PICC_ReadCardSerial() {
  if (shimCardLen == 0) return false;
  uid.size = shimCardLen; memcpy(uid.uidByte, shimCardUid, shimCardLen); shimCardLen = 0; return true;
 }
 void PICC_HaltA() {}
 void PCD_Init() {}
};
