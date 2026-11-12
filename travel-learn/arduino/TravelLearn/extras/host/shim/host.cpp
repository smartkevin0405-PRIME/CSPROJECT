// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 호스트 시험용 대역 구현: 시계, Serial, POSIX 소켓 기반 WiFiClient/WiFiServer.
#include "Arduino.h"
#include "WiFiS3.h"

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

HostSerial Serial;
HostWiFi WiFi;

// ---- 시계 ----
static bool gFake = false;
static unsigned long gFakeMs = 0;

static unsigned long long realMicros() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)(ts.tv_nsec / 1000);
}
static unsigned long long gStart = realMicros();

unsigned long millis() {
  if (gFake) return (unsigned long)(uint32_t)gFakeMs;
  return (unsigned long)(uint32_t)((realMicros() - gStart) / 1000ULL);   // 실제 보드처럼 32비트로 돈다
}
unsigned long micros() {
  if (gFake) return (unsigned long)(uint32_t)(gFakeMs * 1000UL);
  return (unsigned long)(uint32_t)(realMicros() - gStart);
}
void delay(unsigned long ms) {
  if (gFake) { gFakeMs += ms; return; }
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000);
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}
void hostFakeTime(bool enable, unsigned long startMs) { gFake = enable; gFakeMs = startMs; }
void hostAdvance(unsigned long ms) { gFakeMs += ms; }

// ---- 소켓 ----
HostSock::~HostSock() { close(); }
void HostSock::close() {
  if (fd >= 0) { ::close(fd); fd = -1; }
}

static void setNonBlock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

WiFiClient::WiFiClient() {}
WiFiClient::WiFiClient(int fd) : _s(std::make_shared<HostSock>()) { _s->fd = fd; }

int WiFiClient::connect(IPAddress ip, uint16_t port) {
  char host[20];
  snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return connect(host, port);
}

int WiFiClient::connect(const char* host, uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  setNonBlock(fd);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { ::close(fd); return 0; }
  int r = ::connect(fd, (struct sockaddr*)&a, sizeof(a));
  if (r != 0 && errno != EINPROGRESS) { ::close(fd); return 0; }
  if (r != 0) {
    struct pollfd p = { fd, POLLOUT, 0 };
    int tmo = (_timeoutMs > 0) ? _timeoutMs : 3000;
    if (poll(&p, 1, tmo) <= 0) { ::close(fd); return 0; }
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) { ::close(fd); return 0; }
  }
  _s = std::make_shared<HostSock>();
  _s->fd = fd;
  return 1;
}

size_t WiFiClient::write(uint8_t b) { return write(&b, 1); }

size_t WiFiClient::write(const uint8_t* buf, size_t size) {
  if (!_s || _s->fd < 0) return 0;
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = ::send(_s->fd, buf + sent, size - sent, MSG_NOSIGNAL);
    if (n > 0) { sent += (size_t)n; continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd p = { _s->fd, POLLOUT, 0 };
      if (poll(&p, 1, 1000) <= 0) break;
      continue;
    }
    break;
  }
  return sent;
}

int WiFiClient::available() {
  if (!_s || _s->fd < 0) return 0;
  int n = 0;
  if (ioctl(_s->fd, FIONREAD, &n) != 0) return 0;
  return n;
}

int WiFiClient::read() {
  uint8_t b;
  return (read(&b, 1) == 1) ? b : -1;
}

int WiFiClient::read(uint8_t* buf, size_t size) {
  if (!_s || _s->fd < 0) return -1;
  ssize_t n = ::recv(_s->fd, buf, size, 0);
  return (n > 0) ? (int)n : -1;
}

int WiFiClient::peek() {
  if (!_s || _s->fd < 0) return -1;
  uint8_t b;
  ssize_t n = ::recv(_s->fd, &b, 1, MSG_PEEK);
  return (n == 1) ? b : -1;
}

void WiFiClient::flush() {}

void WiFiClient::stop() {
  if (_s) _s->close();
}

uint8_t WiFiClient::connected() {
  if (!_s || _s->fd < 0) return 0;
  if (available() > 0) return 1;
  uint8_t b;
  ssize_t n = ::recv(_s->fd, &b, 1, MSG_PEEK);
  if (n == 0) return 0;                                   // 상대가 닫음
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return 0;
  return 1;
}

WiFiClient::operator bool() { return _s && _s->fd >= 0; }

IPAddress WiFiClient::remoteIP() {
  if (!_s || _s->fd < 0) return IPAddress(0, 0, 0, 0);
  struct sockaddr_in a;
  socklen_t len = sizeof(a);
  if (getpeername(_s->fd, (struct sockaddr*)&a, &len) != 0) return IPAddress(0, 0, 0, 0);
  uint32_t v = ntohl(a.sin_addr.s_addr);
  return IPAddress((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

WiFiServer::WiFiServer() {}
WiFiServer::WiFiServer(int port) : _port(port) {}
WiFiServer::~WiFiServer() { end(); }

void WiFiServer::begin() { begin(_port); }

void WiFiServer::begin(int port) {
  if (_fd >= 0) return;                 // 실제 라이브러리처럼 이미 열려 있으면 그대로 둔다
  _port = port;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(fd, 8) != 0) {
    perror("host WiFiServer bind/listen");
    ::close(fd);
    return;
  }
  setNonBlock(fd);
  _fd = fd;
}

void WiFiServer::end() {
  if (_fd >= 0) { ::close(_fd); _fd = -1; }
  _clients.clear();
}

// 새 연결을 받아 목록에 넣고, 닫힌 연결은 뺀다.
void WiFiServer::pump() {
  if (_fd < 0) return;
  for (;;) {
    int cfd = ::accept(_fd, NULL, NULL);
    if (cfd < 0) break;
    setNonBlock(cfd);
    int one = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    _clients.push_back(WiFiClient(cfd));
  }
  for (size_t i = 0; i < _clients.size();) {
    if (!_clients[i] || (_clients[i].available() == 0 && !_clients[i].connected())) {
      _clients[i].stop();
      _clients.erase(_clients.begin() + (long)i);
    } else {
      i++;
    }
  }
}

WiFiClient WiFiServer::available() {
  pump();
  for (size_t i = 0; i < _clients.size(); i++) {
    if (_clients[i].available() > 0) return _clients[i];
  }
  return WiFiClient();
}

WiFiClient WiFiServer::accept() {
  pump();
  if (_clients.empty()) return WiFiClient();
  WiFiClient c = _clients.front();
  _clients.erase(_clients.begin());
  return c;
}
