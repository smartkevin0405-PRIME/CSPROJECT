// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// 메모리 사용량 보고: RAM 구조체 크기와 EEPROM 배치를 출력한다.
// 주의: 호스트(64비트)에서 잰 값이다. 포인터와 WiFiServer 대역의 크기는 실제 보드와 다르다.
#include <TravelLearn.h>
#include "TlSha256.h"

struct TlSizeProbe {
  static void print() {
    printf("== RAM (host sizeof, bytes) ==\n");
    printf("sizeof(TravelLearn)            %zu\n", sizeof(TravelLearn));
    printf("  TlStore                      %zu\n", sizeof(TlStore));
    printf("    TlImage (RAM mirror)       %zu\n", sizeof(TlImage));
    printf("  TlStats                      %zu\n", sizeof(TlStats));
    printf("  WiFiServer (host shim)       %zu\n", sizeof(WiFiServer));
    printf("  keys                         %zu\n", sizeof(((TravelLearn*)0)->_keys));
    printf("  hasKey+online                %zu\n", sizeof(((TravelLearn*)0)->_hasKey) + sizeof(((TravelLearn*)0)->_online));
    printf("  Pending (one)                %zu\n", sizeof(TravelLearn::Pending));
    printf("  Pending table                %zu\n", sizeof(((TravelLearn*)0)->_pend));
    printf("  TlHttpReq (request buffer)   %zu\n", sizeof(TlHttpReq));
    printf("  _out (response body)         %zu\n", sizeof(((TravelLearn*)0)->_out));
    printf("  _scratch (tx buffer)         %zu\n", sizeof(((TravelLearn*)0)->_scratch));
    printf("stack-side structures:\n");
    printf("  TlSha256Ctx                  %zu\n", sizeof(TlSha256Ctx));
    printf("  TlHmacCtx                    %zu\n", sizeof(TlHmacCtx));
    printf("  TlCell                       %zu\n", sizeof(TlCell));
    printf("  TlSlack                      %zu\n", sizeof(TlSlack));
  }
};

int main() {
  printf("config: TL_MAX_USERS=%d TL_MAX_STOPS=%d TL_NUM_WX=%d TL_NUM_TB=%d TL_DEDUP_RING=%d\n",
         TL_MAX_USERS, TL_MAX_STOPS, TL_NUM_WX, TL_NUM_TB, TL_DEDUP_RING);
  TlSizeProbe::print();
  printf("== EEPROM image layout (bytes) ==\n");
  printf("header        off=%4d size=%4d\n", 0, TL_IMG_HDR);
  printf("phone registry off=%4d size=%4d (%d x %d)\n", TL_OFF_REG, TL_MAX_USERS * TL_REG_REC, TL_MAX_USERS, TL_REG_REC);
  printf("dedup rings   off=%4d size=%4d (%d x %d)\n", TL_OFF_DEDUP, TL_MAX_USERS * TL_DEDUP_REC, TL_MAX_USERS, TL_DEDUP_REC);
  printf("stat cells    off=%4d size=%4d (%d x %d)\n", TL_OFF_CELLS, TL_NUM_CELLS * TL_CELL_REC, TL_NUM_CELLS, TL_CELL_REC);
  printf("crc32         off=%4d size=%4d\n", TL_OFF_CRC, 4);
  printf("image size    %d\n", TL_IMG_SIZE);
  printf("copy stride   %d (%d block(s) of %d)\n", TL_COPY_STRIDE, TL_COPY_STRIDE / TL_EE_BLOCK, TL_EE_BLOCK);
  printf("eeprom used   base=%d, %d bytes (2 copies), TL_EE_SIZE=%d\n", TL_EE_BASE, 2 * TL_COPY_STRIDE, TL_EE_SIZE);
  return 0;
}
