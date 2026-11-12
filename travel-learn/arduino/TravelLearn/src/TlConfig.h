// 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
// TravelLearn 설정 상수. 값을 바꾸려면 이 파일을 고치거나 컴파일 옵션(-D)으로 덮어쓴다.
#ifndef TL_CONFIG_H
#define TL_CONFIG_H

#define TL_PROTO_V 1            // 통신규격 버전

// ---- 통계 표 크기 ----
#ifndef TL_MAX_USERS
#define TL_MAX_USERS 4          // 사용자 수(1~4번)
#endif
#ifndef TL_MAX_STOPS
#define TL_MAX_STOPS 4          // 정류장 슬롯 수(1~4번)
#endif
#define TL_NUM_WX 3             // 날씨 구간: 0 강수 없음, 1 비, 2 눈
#define TL_NUM_TB 4             // 시간대: 0 아침, 1 낮, 2 저녁, 3 밤
#define TL_TB_UNKNOWN 255       // 시간대 모름
#define TL_CELLS_PER_PAIR (TL_NUM_WX * TL_NUM_TB)
#define TL_NUM_CELLS (TL_MAX_USERS * TL_MAX_STOPS * TL_CELLS_PER_PAIR)

// ---- 통신 ----
#ifndef TL_BOARD_PORT
#define TL_BOARD_PORT 8080      // 현관판 서버 포트
#endif
#ifndef TL_PHONE_PORT
#define TL_PHONE_PORT 8765      // 폰 서버 기본 포트
#endif
#define TL_BODY_MAX 480         // 본문 최대 길이(바이트)
#ifndef TL_DEPART_WINDOW_MS
#define TL_DEPART_WINDOW_MS 45000UL   // 문 열림 후 출발 신호 재전송을 포기하는 시간
#endif
#ifndef TL_DEPART_RETRY_MS
#define TL_DEPART_RETRY_MS 1000UL     // 출발 신호 재전송 간격
#endif
#ifndef TL_CONNECT_TIMEOUT_MS
#define TL_CONNECT_TIMEOUT_MS 1200    // 폰 연결 제한 시간
#endif
#ifndef TL_IO_TIMEOUT_MS
#define TL_IO_TIMEOUT_MS 1500UL       // 요청/응답 읽기 제한 시간
#endif
#define TL_CANCEL_TRIES 2             // 취소 신호 시도 횟수

// ---- EEPROM ----
// UNO R4의 EEPROM은 1024바이트 블록 단위로 지우고 쓴다. 그래서 전체 상태를
// 한 덩어리(이미지)로 만들어 A/B 두 벌로 번갈아 쓴다(전원 차단에도 한 벌은 온전).
#define TL_EE_BLOCK 1024
#ifndef TL_EE_BASE
#define TL_EE_BASE 0            // 시작 주소(1024의 배수)
#endif
#ifndef TL_EE_SIZE
#define TL_EE_SIZE 4096         // 사용할 크기(이미지 두 벌)
#endif
#define TL_DEDUP_RING 8         // 사용자별로 기억하는 최근 이동 ID 개수

// ---- 통계 ----
#define TL_MIN_SAMPLES 3        // 이 표본 수 이상이어야 그 칸을 믿는다
#define TL_EW_CAP 20            // 지수 가중 평균의 최대 유효 표본 수
#define TL_SAFETY_BASE_S 30     // 안전 여유 기본값(초)
#define TL_SAFETY_K_X10 10      // 안전 여유 = 기본값 + (K/10) * 표준편차
#define TL_DEFAULT_WALK_S 300   // 초기 설정이 없을 때의 예상 이동 시간(초)
#define TL_DEFAULT_SD_PCT 15    // 표본 부족 시 표준편차 = 평균의 15%
#define TL_DEFAULT_SD_MIN_S 20  // 표본 부족 시 표준편차 최솟값(초)
#define TL_MIN_DUR_S 20         // 이보다 짧은 기록은 버린다
#define TL_MAX_DUR_S 3600       // 이보다 긴 기록은 버린다
#define TL_OUTLIER_MIN_N 5      // 이상값 판정을 시작하는 표본 수
#define TL_OUTLIER_SIGMA 3      // 이상값 기준: 평균에서 3 표준편차
#define TL_OUTLIER_FLOOR_S 120  // 이상값 기준 최솟값(초)
#define TL_OUTLIER_STRIKES 2    // 연속 이상값이 이만큼 쌓인 뒤의 값은 받아들인다
#ifndef TL_ACCEPT_MANUAL
#define TL_ACCEPT_MANUAL 1      // 수동 기록(q=2 또는 src=1)도 통계에 반영
#endif

#endif
