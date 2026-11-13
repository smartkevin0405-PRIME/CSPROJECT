// 생성형 AI의 도움을 받아 작성함. PC 시험용 보드 API 대역.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#define F(x) x
#define A0 14
#define HIGH 1
#define LOW 0
#define HEX 16
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1
extern uint32_t clockMs;
extern int doorLevel;
inline uint32_t millis() { return clockMs; }
inline uint32_t micros() { return clockMs * 1000; }
inline void delay(uint32_t ms) { clockMs += ms; }
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
inline int digitalRead(int pin) { return pin == 2 ? doorLevel : HIGH; }
inline int analogRead(int) { return 0; }
struct SerialType {
 void begin(int) {}
 template<class T> void print(T) {}
 template<class T> void print(T, int) {}
 void println() {}
 template<class T> void println(T) {}
};
inline SerialType Serial;
