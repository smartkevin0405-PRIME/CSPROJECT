// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 호스트 시험용 Server 기본 클래스.
#ifndef HOST_SERVER_H
#define HOST_SERVER_H
#include "Arduino.h"

class Server : public Print {
 public:
  virtual void begin() = 0;
};
#endif
