// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 호스트 시험용 Client 기본 클래스(Arduino API와 같은 모양).
#ifndef HOST_CLIENT_H
#define HOST_CLIENT_H
#include "Arduino.h"

class Client : public Stream {
 public:
  virtual int connect(IPAddress ip, uint16_t port) = 0;
  virtual int connect(const char* host, uint16_t port) = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buf, size_t size) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int read(uint8_t* buf, size_t size) = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  virtual operator bool() = 0;
};
#endif
