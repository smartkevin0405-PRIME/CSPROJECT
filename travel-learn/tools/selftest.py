# 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
"""가상 현관판 + 가상 폰 자체 시험. 실패가 하나라도 있으면 종료 코드 1."""
import hashlib
import hmac
import os
import shutil
import socket
import sys
import tempfile
import time

import tlcommon as tl
from tlcommon import jdump
from virtual_board import VirtualBoard
from virtual_phone import VirtualPhone

K1 = tl.parse_key("000102030405060708090a0b0c0d0e0f")
K2 = tl.parse_key("101112131415161718191a1b1c1d1e1f")
K3 = tl.parse_key("202122232425262728292a2b2c2d2e2f")
KEYS = {1: K1, 2: K2, 3: K3}
LO = "127.0.0.1"

failures = []
tmp = tempfile.mkdtemp(prefix="tl_selftest_")
_trip_counter = [0x80100000]


def check(name, cond, detail=""):
    print("%s  %s%s" % ("PASS" if cond else "FAIL", name, ("  -- " + detail) if (detail and not cond) else ""), flush=True)
    if not cond:
        failures.append(name)
    return cond


def wait_until(fn, timeout=10.0, step=0.02):
    end = time.time() + timeout
    while time.time() < end:
        if fn():
            return True
        time.sleep(step)
    return False


def mk_board(name, **kw):
    port = tl.free_port()
    b = VirtualBoard(port, kw.pop("keys", KEYS), os.path.join(tmp, name + ".json"), quiet=True,
                     bind=LO, retry_s=0.3, **kw)
    b.start()
    return b


def mk_phone(name, board, **kw):
    p = VirtualPhone(LO, board.port, kw.pop("user", 1), kw.pop("key", K1), tl.free_port(), LO,
                     walk_s=kw.pop("walk_s", 200), jitter_s=kw.pop("jitter_s", 0),
                     speedup=kw.pop("speedup", 100), state_path=os.path.join(tmp, name + ".json"),
                     quiet=True, bind=LO, **kw)
    p.start()
    return p


def post_trip(board, user, key, st="arrived", stop=1, wx=0, tb=0, dur_ms=300000, trip=None, q=0, src=1):
    if trip is None:
        _trip_counter[0] += 1
        trip = _trip_counter[0]
    body = jdump({"v": 1, "trip": trip, "user": user, "stop": stop, "wx": wx, "tb": tb, "st": st,
                  "dur_ms": dur_ms, "q": q, "src": src, "epoch": 1763004600})
    r = tl.http_call(LO, board.port, "POST", "/v1/trips", key, user, body)
    return trip, r


def cell(board, u, s, w, t):
    return board.stats.get(u, s, w, t)


def approx(a, b, tol=1e-6):
    return abs(a - b) <= tol


def main():
    tl.setup_console()
    boards, phones = [], []
    try:
        A = mk_board("A")
        boards.append(A)
        P1 = mk_phone("p1", A, stop=2)
        phones.append(P1)

        # (a) hello 등록
        ok = wait_until(lambda: "1" in A.state["phones"])
        ph = A.state["phones"].get("1", {})
        check("(a) hello가 폰을 등록함", ok and ph.get("ip") == LO and ph.get("port") == P1.port, str(ph))
        body = jdump({"v": 1, "user": 1, "ip": LO, "port": P1.port, "epoch": 1763004600, "tz": 540, "pending": 0})
        r = tl.http_call(LO, A.port, "POST", "/v1/hello", K1, 1, body)
        check("(a) hello 응답 {ok:1,reg:1}이고 응답 MAC이 맞음",
              r.valid and r.data and r.data.get("reg") == 1 and r.data.get("boot") == A.boot, r.short())

        # (b) 출발 -> 추적 -> 결과 동기화 -> 통계
        res, trip1, tries = A.send_depart(1, stop=0, wx=0, tb=1, eta=420)
        check("(b) 출발 신호에 tracking 응답", res == "tracking" and tries == 1, "%s %s" % (res, tries))
        check("(b) 현관판 이동 ID 규칙(최상위 비트 0, 부팅횟수<<16 | 순번 1)",
              trip1 >> 31 == 0 and (trip1 >> 16) == (A.boot & 0x7FFF) and (trip1 & 0xFFFF) == 1, hex(trip1))
        ok = wait_until(lambda: cell(A, 1, 2, 0, 1)["n"] == 1, 10)
        c = cell(A, 1, 2, 0, 1)
        check("(b) 결과 동기화 후 통계 칸 n==1, 평균이 모의 200초와 일치(±4초)",
              ok and abs(c["mean_ds"] / 10.0 - 200.0) <= 4.0, str(c))
        check("(b) 폰의 기록이 완료 처리됨", wait_until(lambda: not P1.pending(), 3))
        rs = tl.http_call(LO, A.port, "GET", "/v1/stats?user=1&stop=2", K1, 1)
        cs = (rs.data or {}).get("c", "").split(";")
        check("(b) GET /v1/stats 형식과 응답 MAC",
              rs.valid and len(cs) == 12 and cs[1].startswith("1,") and rs.data.get("def_ds") == 3000, rs.short())

        # (c) 중복 출발 신호
        dbody = jdump({"v": 1, "trip": trip1, "user": 1, "stop": 0, "wx": 0, "tb": 1, "age_ms": 100, "try": 2})
        r = tl.http_call(LO, P1.port, "POST", "/v1/depart", K1, 1, dbody)
        check("(c) 같은 trip의 두 번째 depart는 st==dup", r.valid and r.data.get("st") == "dup", r.short())

        # (d) 중복 결과 전송
        n_before = cell(A, 1, 2, 0, 1)["n"]
        r = P1.resend(trip1)
        d = r.data if r is not None and r.valid else {}
        check("(d) 중복 결과는 stored 1, applied 0, why dup으로 확인됨",
              d.get("stored") == 1 and d.get("applied") == 0 and d.get("why") == "dup", str(d))
        check("(d) 중복 결과로 n이 바뀌지 않음", cell(A, 1, 2, 0, 1)["n"] == n_before == 1)

        # (e) 잘못된 키
        snap = jdump(A.state["cells"])
        bad = tl.parse_key("ffffffffffffffffffffffffffffffff")
        _t, r = post_trip(A, 1, bad, stop=2, tb=1, dur_ms=250000)
        check("(e) 잘못된 키 -> 401, MAC 없음, 상태 불변",
              r.status == 401 and "x-tl-mac" not in r.headers and jdump(A.state["cells"]) == snap, r.short())
        r = tl.http_call(LO, P1.port, "POST", "/v1/depart", bad, 1, dbody)
        check("(e) 폰도 잘못된 키 -> 401", r.status == 401 and "x-tl-mac" not in r.headers, r.short())
        t2 = jdump({"v": 1, "trip": 99, "user": 2, "stop": 1, "wx": 0, "tb": 0, "st": "arrived",
                    "dur_ms": 300000, "q": 0, "src": 1, "epoch": 1})
        r = tl.http_call(LO, A.port, "POST", "/v1/trips", K1, 1, t2)  # 헤더 user 1, 본문 user 2
        check("(e) 헤더 user와 본문 user 불일치 -> 401", r.status == 401, r.short())

        # (f) depart 무시 2회 -> 재전송으로 전달, age_ms 보정
        P1.stop()
        phones.remove(P1)
        P2 = mk_phone("p2", A, stop=3, ignore_departs=2)
        phones.append(P2)
        wait_until(lambda: A.state["phones"].get("1", {}).get("port") == P2.port)
        res, trip2, tries = A.send_depart(1, stop=0, wx=0, tb=2)
        check("(f) --ignore-departs 2: 3번째 시도에 전달됨", res == "tracking" and tries == 3, "%s %s" % (res, tries))
        check("(f) 전달된 age_ms에 재전송 지연이 실림(>=500ms)", (P2.last_depart_age_ms or 0) >= 500,
              str(P2.last_depart_age_ms))
        ok = wait_until(lambda: cell(A, 1, 3, 0, 2)["n"] == 1, 10)
        dur = cell(A, 1, 3, 0, 2)["mean_ds"] / 10.0
        check("(f) 측정 시간이 재전송 지연으로 부풀지 않음(200초 +-4초)", ok and abs(dur - 200.0) <= 4.0, "dur=%s" % dur)

        # (g) --drop-trips 1
        G = mk_board("G", drop_trips=1)
        boards.append(G)
        PG = mk_phone("pg", G, stop=1)
        phones.append(PG)
        PG.manual()
        ok = wait_until(lambda: not PG.pending(), 10)
        check("(g) --drop-trips 1: 재시도로 전달되고 정확히 한 번 반영됨",
              ok and cell(G, 1, 1, 0, tl.tb_from_epoch(time.time()))["n"] == 1 and G.trips_seen == 2 and PG.send_count == 2,
              "seen=%d sent=%d" % (G.trips_seen, PG.send_count))

        # (h) 틀린 응답 MAC
        H = mk_board("H", bad_mac_trips=1)
        boards.append(H)
        PH = mk_phone("ph", H, stop=1)
        phones.append(PH)
        PH.manual()
        ok = wait_until(lambda: not PH.pending(), 10)
        tbh = tl.tb_from_epoch(time.time())
        check("(h) 틀린 응답 MAC은 저장 완료로 보지 않음(재전송 후에만 완료)",
              ok and PH.badmac_count == 1 and PH.send_count == 2, "bad=%d sent=%d" % (PH.badmac_count, PH.send_count))
        check("(h) 현관판은 한 번만 반영(두 번째는 dup)", cell(H, 1, 1, 0, tbh)["n"] == 1)
        _t, r = post_trip(H, 1, K1, trip=0x80000042, stop=1)  # 이미 소진된 결함주입 -> 정상 응답
        check("(h) 정상 응답은 MAC 유효", r.valid, r.short())

        # (i) 범위 밖
        _t, r = post_trip(A, 1, K1, stop=1, dur_ms=5000)
        d = r.data or {}
        check("(i) dur 5초 -> applied 0, why range, stored 1",
              r.valid and d.get("applied") == 0 and d.get("why") == "range" and d.get("stored") == 1, r.short())
        _t, r = post_trip(A, 1, K1, stop=1, dur_ms=3600001)
        check("(i) dur 3600.001초 -> range", (r.data or {}).get("why") == "range", r.short())
        _t, r = post_trip(A, 1, K1, stop=0, dur_ms=300000)
        check("(i) arrived인데 stop 0 -> nostop", (r.data or {}).get("why") == "nostop", r.short())

        # (j) 이상값 / strike (사용자 2, 정류장 1, wx0 tb0)
        res = []
        for x in (100, 100, 100, 100, 100):
            res.append(post_trip(A, 2, K2, stop=1, dur_ms=x * 1000)[1].data)
        check("(j) 처음 5개 정상 반영, n==5 평균 100초",
              all(d["applied"] == 1 for d in res) and res[-1]["n"] == 5 and res[-1]["mean_ds"] == 1000, str(res[-1]))
        d = post_trip(A, 2, K2, stop=1, dur_ms=500000)[1].data
        check("(j) 500초는 이상값(applied 0, why outlier)", d["applied"] == 0 and d["why"] == "outlier" and d["n"] == 5, str(d))
        d = post_trip(A, 2, K2, stop=1, dur_ms=100000)[1].data
        check("(j) 정상값이 오면 strikes 초기화(n==6)", d["applied"] == 1 and d["n"] == 6, str(d))
        d1 = post_trip(A, 2, K2, stop=1, dur_ms=500000)[1].data
        d2 = post_trip(A, 2, K2, stop=1, dur_ms=500000)[1].data
        check("(j) 연속 이상값 2회는 반영 안 됨", d1["why"] == "outlier" and d2["why"] == "outlier" and d2["n"] == 6, "%s %s" % (d1, d2))
        d3 = post_trip(A, 2, K2, stop=1, dur_ms=500000)[1].data
        # 손계산: n'=7, a=1/7, 평균=100+400/7=157.14 -> 1571, 분산=(6/7)*(1/7)*160000=19591.8 -> sd 139.97 -> 1400
        check("(j) 세 번째 연속 이상값은 반영(n 7, 평균 1571, sd 1400)",
              d3["applied"] == 1 and d3["n"] == 7 and d3["mean_ds"] == 1571 and d3["sd_ds"] == 1400, str(d3))
        c = cell(A, 2, 1, 0, 0)
        check("(j) 반영 뒤 strikes==0", c["strikes"] == 0, str(c))
        # n<5에서는 이상값 규칙 없음
        ds = [post_trip(A, 2, K2, stop=2, dur_ms=100000)[1].data for _ in range(4)]
        d = post_trip(A, 2, K2, stop=2, dur_ms=1000000)[1].data
        # 손계산: n'=5, a=1/5, d=900, 평균=280.0 -> 2800, 분산=0.8*0.2*810000=129600 -> sd 360 -> 3600
        check("(j) 표본 4개일 때는 큰 값도 반영(평균 2800, sd 3600)",
              d["applied"] == 1 and d["n"] == 5 and d["mean_ds"] == 2800 and d["sd_ds"] == 3600, str(d))

        # (k) 여유 시간 근거 수준 3 -> 2 -> 0 (사용자 3, 정류장 4)
        s = A.compute_slack(3, 4, 0, 0, 600)
        check("(k) 표본 없음: 수준 3, 여유 = 600-300-(30+45) = 225",
              s["level"] == 3 and s["walk"] == 300 and approx(s["slack"], 225.0), str(s))
        post_trip(A, 3, K3, stop=4, dur_ms=400000)
        s = A.compute_slack(3, 4, 0, 0, 600)
        check("(k) 표본 1개: 수준 2, 여유 = 600-400-(30+60) = 110",
              s["level"] == 2 and s["walk"] == 400 and approx(s["slack"], 110.0), str(s))
        post_trip(A, 3, K3, stop=4, dur_ms=420000)
        post_trip(A, 3, K3, stop=4, dur_ms=380000)
        # 손계산: 400, 420, 380초 -> 평균 400.0(4000), 분산 266.67 -> sd 16.33 -> 저장 163 -> 16.3초
        c = cell(A, 3, 4, 0, 0)
        check("(k) 표본 3개 후 칸 값: n 3, 평균 4000, sd 163", c["n"] == 3 and c["mean_ds"] == 4000 and c["sd_ds"] == 163, str(c))
        s = A.compute_slack(3, 4, 0, 0, 600)
        check("(k) 수준 0: 여유 = 600-400-(30+16.3) = 153.7",
              s["level"] == 0 and s["walk"] == 400 and approx(s["slack"], 153.7), str(s))
        s = A.compute_slack(3, 4, 1, 0, 600)
        check("(k) 같은 시간대 합산(다른 날씨): 수준 1, 여유 153.7", s["level"] == 1 and approx(s["slack"], 153.7), str(s))
        s = A.compute_slack(3, 4, 0, 1, 600)
        check("(k) 같은 날씨 합산(다른 시간대): 수준 1, 여유 153.7", s["level"] == 1 and approx(s["slack"], 153.7), str(s))
        s = A.compute_slack(3, 4, 2, 3, 600)
        check("(k) 겹치는 칸 없음 -> 수준 2", s["level"] == 2 and approx(s["slack"], 153.7), str(s))
        post_trip(A, 3, K3, stop=4, wx=0, tb=1, dur_ms=500000)
        s = A.compute_slack(3, 4, 1, 2, 600)
        # 손계산: 칸A(n3, 평균400, v=16.3^2=265.69), 칸B(n1, 500, 0) -> 평균 425, 분산 (3*(265.69+625)+5625)/4 = 2074.2675
        sd = 2074.2675 ** 0.5
        check("(k) 전체 합산 평균/분산: 수준 2, 평균 425",
              s["level"] == 2 and s["walk"] == 425 and approx(s["slack"], 600 - 425 - (30 + sd), 1e-6), str(s))
        s = A.compute_slack(3, 4, 0, 2, 600)
        check("(k) 같은 날씨(wx=0) 합산: 수준 1, 평균 425, sd 45.54",
              s["level"] == 1 and s["walk"] == 425 and approx(s["slack"], 600 - 425 - (30 + sd), 1e-6), str(s))

        # (l) 도착이 아닌 상태
        tid, r = post_trip(A, 1, K1, st="timeout", stop=0, dur_ms=0)
        d = r.data or {}
        check("(l) timeout -> stored 1, applied 0, why status",
              d.get("stored") == 1 and d.get("applied") == 0 and d.get("why") == "status", r.short())
        r = post_trip(A, 1, K1, st="timeout", stop=0, dur_ms=0, trip=tid)[1]
        check("(l) 같은 기록 재전송은 dup(비도착도 중복 목록에 들어감)", (r.data or {}).get("why") == "dup", r.short())
        # 중복 목록은 8개까지만
        first = None
        for i in range(9):
            t, _r = post_trip(A, 2, K2, st="lost", stop=0, dur_ms=0)
            first = first or t
        r = post_trip(A, 2, K2, st="lost", stop=0, dur_ms=0, trip=first)[1]
        check("(l) 중복 목록은 최근 8개만 보관(9번째 전 기록은 dup 아님)", (r.data or {}).get("why") == "status", r.short())

        # 기타: 404 / 413 / 400
        r = tl.http_call(LO, A.port, "GET", "/v1/nothing", K1, 1)
        check("(n) 없는 경로 404, MAC 없음", r.status == 404 and "x-tl-mac" not in r.headers, r.short())
        s = socket.create_connection((LO, A.port), timeout=3)
        s.sendall(b"POST /v1/trips HTTP/1.1\r\nHost: x\r\nContent-Length: 600\r\nX-TL-User: 1\r\nX-TL-Mac: 00\r\n\r\n" + b"x" * 600)
        buf = s.recv(4096)
        s.close()
        check("(n) 본문 480바이트 초과 413", buf.startswith(b"HTTP/1.1 413"), repr(buf[:40]))
        r = tl.http_call(LO, A.port, "POST", "/v1/trips", K1, 1, "{not json")
        check("(n) 깨진 JSON(MAC은 유효) 400 json", r.status == 400 and (r.data or {}).get("err") == "json", r.short())
        r = tl.http_call(LO, A.port, "POST", "/v1/trips", K1, 1, jdump({"v": 1, "user": 1}))
        check("(n) 필수 키 없음 400 field", r.status == 400 and (r.data or {}).get("err") == "field", r.short())
        r = tl.http_call(LO, A.port, "GET", "/v1/ping")
        check("(n) 인증 없는 ping", r.status == 200 and (r.data or {}).get("dev") == "board", r.short())
        r = tl.http_call(LO, P2.port, "POST", "/v1/depart", K1, 1,
                         jdump({"v": 1, "trip": 5, "user": 1, "stop": 0, "wx": 0, "tb": 0, "age_ms": 120001, "try": 1}))
        check("(n) 폰: age_ms 120001 -> 400 stale", r.status == 400 and (r.data or {}).get("err") == "stale", r.short())

        # (m) 알려진 답 시험
        key = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
        hb = '{"v":1,"user":1,"ip":"192.168.0.23","port":8765,"epoch":1763004600,"tz":540,"pending":0}'
        req_hex = tl.req_mac(key, "POST", "/v1/hello", hb)
        rb = '{"ok":1,"boot":127,"reg":1}'
        resp_hex = tl.resp_mac(key, "/v1/hello", req_hex, rb)
        ind1 = hmac.new(key, ("POST /v1/hello\n" + hb).encode(), hashlib.sha256).hexdigest()[:32]
        ind2 = hmac.new(key, ("R /v1/hello\n" + req_hex + "\n" + rb).encode(), hashlib.sha256).hexdigest()[:32]
        check("(m) 알려진 답: 별도 계산과 일치, 32자리 소문자 16진수",
              req_hex == ind1 and resp_hex == ind2 and len(req_hex) == 32 and req_hex == req_hex.lower())
        print("KAT_REQ_MAC=" + req_hex)
        print("KAT_RESP_MAC=" + resp_hex)
    except Exception as e:
        import traceback
        traceback.print_exc()
        check("시험 중 예외 없음", False, repr(e))
    finally:
        for p in phones:
            p.stop()
        for b in boards:
            b.stop()
        shutil.rmtree(tmp, ignore_errors=True)
    print("-" * 40)
    if failures:
        print("실패 %d건: %s" % (len(failures), "; ".join(failures)))
        return 1
    print("모든 시험 통과")
    return 0


if __name__ == "__main__":
    sys.exit(main())
