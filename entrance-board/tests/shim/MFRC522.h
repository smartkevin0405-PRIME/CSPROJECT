// 생성형 AI의 도움을 받아 작성함. PC 시험용 API 대역.
#pragma once
#include <stdint.h>
struct MFRC522 {
 struct { uint8_t size; uint8_t uidByte[10]; } uid{};
 MFRC522(int,int) {}
 bool PICC_IsNewCardPresent() {return false;}
 bool PICC_ReadCardSerial() {return false;}
 void PICC_HaltA() {}
 void PCD_Init() {}
};
