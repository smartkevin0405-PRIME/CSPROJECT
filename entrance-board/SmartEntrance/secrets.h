// secrets.h  WiFi와 API 키. 남에게 공유할 때는 값을 지운다.
#pragma once
#define WIFI_SSID        "YOUR_WIFI"
#define WIFI_PASS        "YOUR_PASSWORD"
// 공공데이터포털 일반 인증키(Encoding). 버스와 기상청에 같이 쓴다.
#define KEY_DATA_GO_KR   "YOUR_ENCODED_SERVICE_KEY"
// 서울 열린데이터광장 지하철 실시간 인증키
#define KEY_SEOUL_SUBWAY "YOUR_SUBWAY_KEY"
// 이동 시간 모듈: 사용자별 폰 공유 키(16진수 32글자). 폰 앱의 "현관판 연결"에 같은 값을 넣는다.
// 만들기: python -c "import secrets; print(secrets.token_hex(16))"
#define TL_KEY_USER1     "00000000000000000000000000000000"
#define TL_KEY_USER2     "00000000000000000000000000000000"
#define TL_KEY_USER3     ""
#define TL_KEY_USER4     ""
