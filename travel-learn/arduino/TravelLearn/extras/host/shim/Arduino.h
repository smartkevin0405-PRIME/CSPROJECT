// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 호스트(PC) 시험용 Arduino.h 대역. 실제 보드에서는 쓰이지 않는다.
#ifndef HOST_ARDUINO_H
#define HOST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);

// 시험용 가짜 시계: enable하면 millis()가 직접 정한 값을 돌려주고 delay()가 그 값을 올린다.
void hostFakeTime(bool enable, unsigned long startMs = 0);
void hostAdvance(unsigned long ms);

#define F(x) (x)

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buf, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; i++) n += write(buf[i]);
    return n;
  }
  size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const char* s) { return write(s); }
  size_t print(char ch) { return write((uint8_t)ch); }
  size_t print(int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); return write(b); }
  size_t print(unsigned v) { char b[16]; snprintf(b, sizeof(b), "%u", v); return write(b); }
  size_t print(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); return write(b); }
  size_t print(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); return write(b); }
  size_t println() { return write("\r\n"); }
  size_t println(const char* s) { size_t n = write(s); return n + println(); }
  size_t println(int v) { size_t n = print(v); return n + println(); }
  size_t println(unsigned v) { size_t n = print(v); return n + println(); }
  size_t println(long v) { size_t n = print(v); return n + println(); }
  size_t println(unsigned long v) { size_t n = print(v); return n + println(); }
  virtual void flush() {}
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
};

class IPAddress {
 public:
  IPAddress() { memset(_a, 0, 4); }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _a[0] = a; _a[1] = b; _a[2] = c; _a[3] = d; }
  uint8_t operator[](int i) const { return _a[i]; }
  uint8_t& operator[](int i) { return _a[i]; }
 private:
  uint8_t _a[4];
};

class HostSerial : public Stream {
 public:
  void begin(unsigned long) {}
  size_t write(uint8_t b) override { fputc(b, stdout); return 1; }
  size_t write(const uint8_t* buf, size_t size) override { return fwrite(buf, 1, size, stdout); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override { fflush(stdout); }
  operator bool() { return true; }
};
extern HostSerial Serial;

#include "Client.h"
#include "Server.h"

#endif
