// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 호스트 시험용 WiFiS3 대역. WiFiClient/WiFiServer를 POSIX 소켓으로 흉내 낸다.
#ifndef HOST_WIFIS3_H
#define HOST_WIFIS3_H

#include "Arduino.h"
#include <memory>
#include <vector>

#define WL_IDLE_STATUS 0
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

class HostWiFi {
 public:
  uint8_t hostStatus = WL_CONNECTED;
  uint8_t status() { return hostStatus; }
  IPAddress localIP() { return IPAddress(127, 0, 0, 1); }
  unsigned long getTime() { return 0; }
};
extern HostWiFi WiFi;

// 소켓 하나의 상태. WiFiClient 복사본들이 함께 가리킨다(실제 라이브러리도 소켓 번호를 공유).
struct HostSock {
  int fd = -1;
  ~HostSock();
  void close();
};

class WiFiClient : public Client {
 public:
  WiFiClient();
  explicit WiFiClient(int fd);
  int connect(IPAddress ip, uint16_t port) override;
  int connect(const char* host, uint16_t port) override;
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t* buf, size_t size) override;
  int peek() override;
  void flush() override;
  void stop() override;
  uint8_t connected() override;
  operator bool() override;
  void setConnectionTimeout(int ms) { _timeoutMs = ms; }
  IPAddress remoteIP();
  int hostFd() const { return _s ? _s->fd : -1; }

 private:
  std::shared_ptr<HostSock> _s;
  int _timeoutMs = 0;      // 0이면 기본 3000 ms(모뎀 기본값과 같게)
};

class WiFiServer : public Server {
 public:
  WiFiServer();
  explicit WiFiServer(int port);
  ~WiFiServer();
  void begin(int port);
  void begin() override;
  void end();
  WiFiClient available();          // 받을 데이터가 있는 클라이언트를 돌려준다
  WiFiClient accept();
  size_t write(uint8_t) override { return 0; }
  int hostPort() const { return _port; }

 private:
  int _fd = -1;
  int _port = 0;
  std::vector<WiFiClient> _clients;
  void pump();
};

#endif
