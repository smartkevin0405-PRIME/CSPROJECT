// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 가상 현관판: 실제 TravelLearn 라이브러리를 PC에서 돌린다(센서·화면 없음).
// 표준 입력으로 가상 출발 이벤트와 가상 교통 정보(ETA)를 넣고, 표준 출력으로 결과를 본다.
//
// 사용법: virtual_board --port 8080 --eeprom board.bin --key 1=<32자리 16진수> [--default 1,2=300]
// 명령:   depart U [STOP WX TB ETA] | cancel U | slack U STOP WX TB ETA | dump | quit
#include <TravelLearn.h>
#include <EEPROM.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>

static TravelLearn tl;

static void onDepart(uint8_t user, uint32_t trip, bool delivered) {
  printf("EVT depart_result user=%u trip=%lu delivered=%d\n", (unsigned)user, (unsigned long)trip, delivered ? 1 : 0);
  fflush(stdout);
}
static void onStored(uint8_t user, uint8_t stop, uint32_t trip, bool applied) {
  printf("EVT trip_stored user=%u stop=%u trip=%lu applied=%d\n", (unsigned)user, (unsigned)stop, (unsigned long)trip, applied ? 1 : 0);
  fflush(stdout);
}

// dumpStats 출력을 줄마다 "DUMP "를 붙여 내보낸다.
class DumpOut : public Print {
 public:
  size_t write(uint8_t b) override {
    if (b == '\r') return 1;
    if (_bol) { fputs("DUMP ", stdout); _bol = false; }
    fputc(b, stdout);
    if (b == '\n') _bol = true;
    return 1;
  }
 private:
  bool _bol = true;
};

static void handleLine(char* line, bool* quit) {
  char cmd[16] = "";
  long a[5] = { 0, 0, 0, 255, -1 };
  int n = sscanf(line, "%15s %ld %ld %ld %ld %ld", cmd, &a[0], &a[1], &a[2], &a[3], &a[4]);
  if (n < 1) return;
  if (strcmp(cmd, "quit") == 0) {
    *quit = true;
  } else if (strcmp(cmd, "depart") == 0 && n >= 2) {
    // depart U [STOP WX TB ETA]: 생략하면 정류장 0, 날씨 0, 시간대 모름, ETA 없음
    uint32_t trip = tl.notifyDeparture((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3], (int32_t)a[4]);
    if (trip == 0) printf("ERR depart rejected\n");
    else printf("EVT depart_sent user=%ld trip=%lu\n", a[0], (unsigned long)trip);
  } else if (strcmp(cmd, "cancel") == 0 && n >= 2) {
    tl.cancelDeparture((uint8_t)a[0]);
    printf("EVT cancel_requested user=%ld\n", a[0]);
  } else if (strcmp(cmd, "slack") == 0 && n >= 6) {
    TlSlack s = tl.slack((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3], (int32_t)a[4]);
    printf("SLACK slack=%ld walk=%u safety=%u level=%u n=%u\n", (long)s.slackS, (unsigned)s.walkS,
           (unsigned)s.safetyS, (unsigned)s.level, (unsigned)s.n);
  } else if (strcmp(cmd, "dump") == 0) {
    DumpOut d;
    tl.dumpStats(d);
    printf("DUMP end\n");
  } else {
    printf("ERR unknown command\n");
  }
  fflush(stdout);
}

int main(int argc, char** argv) {
  int port = TL_BOARD_PORT;
  signal(SIGPIPE, SIG_IGN);
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--eeprom") == 0 && i + 1 < argc) {
      HostEeprom::inst().load(argv[++i]);          // 없으면 빈 EEPROM으로 시작하고 이 파일에 저장한다
    } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
      int u; char hex[64];
      if (sscanf(argv[++i], "%d=%63s", &u, hex) == 2) tl.setUserKey((uint8_t)u, hex);
    } else if (strcmp(argv[i], "--default") == 0 && i + 1 < argc) {
      int u, s, sec;
      if (sscanf(argv[++i], "%d,%d=%d", &u, &s, &sec) == 3) tl.setDefaultWalk((uint8_t)u, (uint8_t)s, (uint16_t)sec);
    } else {
      fprintf(stderr, "usage: %s --port N --eeprom FILE --key U=HEX32 [--default U,S=SECONDS]\n", argv[0]);
      return 2;
    }
  }
  tl.onDepartResult(onDepart);
  tl.onTripStored(onStored);
  bool ok = tl.begin((uint16_t)port);
  printf("READY port=%d boot=%u storage=%d\n", port, (unsigned)tl.store().boot(), ok ? 1 : 0);
  fflush(stdout);

  char line[256];
  size_t fill = 0;
  bool quit = false;
  while (!quit) {
    tl.loop();
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, 2) > 0) {                      // 2 ms 쉬면서 표준 입력을 본다
      char ch;
      ssize_t r = read(0, &ch, 1);
      if (r == 0) break;                           // 입력이 닫히면 끝낸다
      while (r == 1) {
        if (ch == '\n') {
          line[fill] = '\0';
          fill = 0;
          handleLine(line, &quit);
          break;
        }
        if (fill + 1 < sizeof(line)) line[fill++] = ch;
        struct pollfd q = { 0, POLLIN, 0 };
        if (poll(&q, 1, 0) <= 0) break;
        r = read(0, &ch, 1);
        if (r == 0) { quit = true; }
      }
    }
  }
  return 0;
}
