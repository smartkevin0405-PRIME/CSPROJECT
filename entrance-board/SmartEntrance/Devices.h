// Devices.h  센서(PIR, 문, DHT11)와 출력(LED 매트릭스, 서보 다이얼)
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#pragma once
#include <Arduino.h>
#include "config.h"

void devicesBegin();
void devicesLoop();                       // loop()마다 호출 (문 디바운스, 서보 신호 끊기)

bool pirActive();                         // 사람 움직임이 있으면 true
bool doorIsOpen();                        // 지금 문이 열려 있으면 true
bool doorJustOpened();                    // 닫힘→열림으로 바뀐 순간 한 번만 true
bool dhtRead(int8_t& tempC, uint8_t& humidity);   // 실내 온습도. 실패하면 false

void matrixShowTemp(int8_t tempC);        // 내장 LED 매트릭스에 기온 숫자
void matrixShowNone();                    // 값이 없을 때 "--"
void dialSet(WeatherKind kind);           // 서보 바늘을 날씨 칸으로
