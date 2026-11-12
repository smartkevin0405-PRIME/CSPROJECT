<!-- 이 문서는 생성형 AI(Claude)의 도움을 받아 작성했습니다. -->
# tools/ 시뮬레이터

통신규격(`docs/통신규격.md`) 기준으로 현관판과 폰을 서로 없이 개발하기 위한 도구다.
Python 3.9 이상, 표준 라이브러리만 쓴다 (Windows / macOS / Linux 공통).

| 파일 | 용도 |
|---|---|
| `virtual_board.py` | 가상 현관판. 실제 폰 앱을 PC에서 시험할 때 쓴다 |
| `virtual_phone.py` | 가상 폰. 실제 현관판(아두이노)을 앱 없이 시험할 때 쓴다 |
| `selftest.py` | 두 시뮬레이터를 서로 연결해 자동 시험 (네트워크는 localhost만 사용) |
| `tlcommon.py` | 공용 코드 (MAC, JSON, 통계, HTTP) |

## 상황 1: 폰 앱을 PC로 시험하기

PC와 폰을 같은 Wi-Fi에 연결하고 PC에서 실행한다.

```
python virtual_board.py --port 8080 --key 1=000102030405060708090a0b0c0d0e0f
```

시작할 때 나오는 `192.168.x.x:8080` 을 폰 앱의 현관판 주소에 입력한다.
앱이 hello를 보내면 `폰 등록` 줄이 뜬다. 이후 콘솔에서:

```
depart 1                 출발 신호 (정류장 미정, 시간대는 폰이 채움)
depart 1 0 1 255 420     STOP WX TB ETA 지정
cancel 1                 외출 취소
slack 1 2 0 1 600        여유 시간 계산
stats 1 2                통계 칸 보기
phones                   등록된 폰
```

- 키는 `--key 사용자=32자리16진수`를 여러 번 쓸 수 있다. 생략하면 사용자 1에 `000102...0e0f`.
- 상태(부팅 횟수, 폰 주소, 중복 목록, 통계)는 `--state`(기본 `virtual_board_state.json`)에 저장된다. 처음부터 다시 하려면 이 파일을 지운다.
- 표본이 없을 때의 이동 시간은 `--default-walk`(기본 300초).

결함 주입 (폰 앱의 재전송 동작 시험용):

| 옵션 | 효과 |
|---|---|
| `--drop-trips N` | 처음 N개 `/v1/trips` 요청에 응답 없이 연결을 닫음 |
| `--busy-trips N` | 그다음 N개에 503 busy |
| `--bad-mac-trips N` | 그다음 N개에 틀린 응답 MAC (기록은 저장됨 -> 다음 전송은 dup) |
| `--delay-ms M` | 모든 응답을 M 밀리초 늦춤 |

## 상황 2: 실제 현관판을 가상 폰으로 시험하기

```
python virtual_phone.py --board 192.168.0.50:8080 --user 1 --key 000102030405060708090a0b0c0d0e0f
```

- `--board`는 현관판의 고정 IP와 포트. 시작하면 바로 hello를 보내 주소를 등록한다 (주소는 `--ip`로 직접 지정 가능).
- 현관판이 출발 신호를 보내면 `--walk-s`(기본 400)±`--jitter-s`(기본 30)초를 `--speedup`(기본 60)배속으로 걷고, 결과를 현관판에 동기화한다. `dur_ms`에는 모의 시간 전체가 들어간다.
- `--stop N`(도착 정류장), `--outcome arrived|canceled|timeout|lost`, `--q 0|1|2` 로 보고 내용을 바꾼다.
- 콘솔 명령: `hello`, `sync`, `manual [STOP]`, `pending`, `resend TRIP`, `quit`.
- 결함 주입: `--ignore-departs N`(처음 N개 출발 신호에 응답 안 함 -> 현관판의 1초 재전송 확인), `--dup-send`(결과를 항상 두 번 전송).

## 방화벽

Windows에서는 처음 실행할 때 뜨는 창에서 Python의 **사설망 인바운드 허용**을 눌러야 한다.
(또는 Windows Defender 방화벽에서 선택한 포트 8080 / 8765 의 인바운드 TCP를 허용한다.)
허용하지 않으면 폰/현관판의 요청이 PC에 닿지 않는다.

## 자체 시험

```
python selftest.py
```

각 항목마다 `PASS`/`FAIL`을 출력하고, 하나라도 실패하면 종료 코드 1이다 (약 5초).
마지막에 다른 구현과 대조할 MAC 알려진 답이 나온다.

```
KAT_REQ_MAC=...
KAT_RESP_MAC=...
```
