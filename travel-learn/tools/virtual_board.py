# 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
"""가상 현관판: 실제 폰 앱을 PC에서 시험하기 위한 현관판 흉내 (통신규격 v1)."""
import argparse
import sys
import threading
import time

import tlcommon as tl
from tlcommon import Resp, err_resp, jdump, say


class VirtualBoard(object):
    def __init__(self, port=tl.TL_BOARD_PORT, keys=None, state_path="virtual_board_state.json",
                 default_walk=300, drop_trips=0, busy_trips=0, bad_mac_trips=0, delay_ms=0,
                 retry_s=1.0, window_s=45.0, quiet=False, bind="0.0.0.0"):
        self.port = port
        self.keys = keys if keys else {1: tl.parse_key(tl.DEFAULT_KEY_HEX)}
        self.state_path = state_path
        self.default_walk = default_walk
        self.drop_trips = drop_trips
        self.busy_trips = busy_trips
        self.bad_mac_trips = bad_mac_trips
        self.delay_ms = delay_ms
        self.retry_s = retry_s
        self.window_s = window_s
        self.quiet = quiet
        self.bind = bind
        self.lock = threading.RLock()
        self.t_start = time.monotonic()
        self.trips_seen = 0  # 인증을 통과한 /v1/trips 요청 수 (결함 주입용)
        self.seq = 0  # 부팅 후 순번
        self.departs = {}  # user -> {"trip", "cancel", "result"}
        st = tl.load_json(state_path, {})
        self.state = {"boot": int(st.get("boot", 0)) + 1,
                      "phones": st.get("phones", {}),
                      "dedup": st.get("dedup", {}),
                      "hw": st.get("hw", {}),
                      "cells": st.get("cells", {})}
        self.stats = tl.Stats(self.state["cells"])
        self._save()
        self.server = tl.TLServer((bind, port), self.dispatch, delay_ms / 1000.0, quiet)

    # ---- 기본
    def log(self, msg):
        say(msg, self.quiet)

    @property
    def boot(self):
        return self.state["boot"]

    def _save(self):
        tl.save_json(self.state_path, self.state)

    def start(self):
        self.server.start()

    def stop(self):
        for d in list(self.departs.values()):
            d["cancel"].set()
        self.server.stop()

    # ---- 요청 처리
    def dispatch(self, req):
        with self.lock:
            resp = self._handle(req)
        if resp is None:
            self.log("← %s %s : 응답하지 않고 연결을 닫음 (시험용)" % (req.method, req.path))
        else:
            self.log("← %s %s (사용자 %s) → %d" % (req.method, req.path, req.headers.get("x-tl-user", "-"), resp.status))
        return resp

    def _handle(self, req):
        path, _, _q = req.path.partition("?")
        m = req.method
        if m == "GET" and path == "/v1/ping":
            return Resp(200, jdump({"ok": 1, "dev": "board", "v": 1, "boot": self.boot,
                                    "up_ms": int((time.monotonic() - self.t_start) * 1000)}))
        if (m, path) not in (("POST", "/v1/hello"), ("POST", "/v1/trips"), ("GET", "/v1/stats")):
            return err_resp(404, "path")
        auth = tl.check_auth(req, self.keys)
        if isinstance(auth, Resp):
            self.log("  인증 실패(MAC 또는 사용자 불일치)")
            return auth
        user, key, _mac = auth
        if m == "GET":
            return self._stats(req, user, key)
        obj = tl.parse_obj(req)
        if isinstance(obj, Resp):
            return obj
        if tl.is_int(obj.get("user")) and obj["user"] != user:
            self.log("  헤더 사용자(%d)와 본문 사용자(%s) 불일치" % (user, obj["user"]))
            return err_resp(401, "auth")
        if path == "/v1/hello":
            return self._hello(req, obj, user, key)
        return self._trips(req, obj, user, key)

    def _hello(self, req, obj, user, key):
        f = tl.need_ints(obj, [("user", 1, 4), ("port", 1, 65535), ("epoch", 0, 2 ** 62), ("tz", -1440, 1440)])
        ip = obj.get("ip")
        if f is None or not isinstance(ip, str) or not (7 <= len(ip) <= 15):
            return err_resp(400, "field")
        pend = obj.get("pending", 0)
        self.state["phones"][str(user)] = {"ip": ip, "port": f["port"], "epoch": f["epoch"], "tz": f["tz"],
                                           "pending": pend if tl.is_int(pend) else 0,
                                           "last": time.strftime("%Y-%m-%d %H:%M:%S")}
        self._save()
        self.log("  폰 등록: 사용자 %d → %s:%d (미동기화 %s건)" % (user, ip, f["port"], pend))
        return tl.signed(req, key, jdump({"ok": 1, "boot": self.boot, "reg": 1}))

    def _cell_fields(self, user, stop, wx, tb):
        if 1 <= stop <= 4:
            c = self.stats.get(user, stop, wx, tb)
            return c["n"], c["mean_ds"], c["sd_ds"]
        return 0, 0, 0

    def _trips(self, req, obj, user, key):
        f = tl.need_ints(obj, [("trip", 0, 0xFFFFFFFF), ("user", 1, 4), ("stop", 0, 4), ("wx", 0, 2),
                               ("tb", 0, 3), ("dur_ms", 0, 0xFFFFFFFF), ("q", 0, 2), ("src", 0, 1),
                               ("epoch", 0, 2 ** 62)])
        st = obj.get("st")
        if f is None or st not in ("arrived", "canceled", "timeout", "lost"):
            return err_resp(400, "field")
        self.trips_seen += 1
        k = self.trips_seen
        if k <= self.drop_trips:
            self.log("  [결함주입] %d번째 trips 요청: 응답 없이 닫음" % k)
            return None
        if k <= self.drop_trips + self.busy_trips:
            self.log("  [결함주입] %d번째 trips 요청: 503 busy" % k)
            return err_resp(503, "busy")
        bad_mac = k <= self.drop_trips + self.busy_trips + self.bad_mac_trips

        trip = f["trip"]
        dd = self.state["dedup"].setdefault(str(user), [])
        stop, wx, tb = f["stop"], f["wx"], f["tb"]
        applied, why = 0, None
        hw = self.state["hw"].setdefault(str(user), {"board": 0, "phone": 0})
        phone_id = bool(trip & 0x80000000)
        ns = "phone" if phone_id else "board"
        limit = ((self.boot & 0x7FFF) << 16) | (self.seq & 0xFFFF)  # 이 현관판이 지금까지 발급한 가장 큰 ID (C01)
        foreign = (not phone_id) and trip > limit                  # 다른 계보의 현관판 ID: 최고값 비교·갱신에서 뺀다 (C01)
        stale_hw = (not phone_id) and hw[ns] > limit               # 최고값 자체가 다른 계보에서 왔으면 쓰지 않는다 (C01)
        if trip in dd or (not foreign and not stale_hw and hw[ns] and trip <= hw[ns]):  # 처리한 최고값 이하는 재전송 (B15)
            why = "dup"
        else:
            dd.append(trip)
            del dd[:-8]  # 최근 8개만
            if not foreign and (stale_hw or trip > hw[ns]):
                hw[ns] = trip  # 다른 계보에서 온 최고값은 덮어쓴다 (C01)
            if st != "arrived":
                why = "status"
            elif not (1 <= stop <= 4):
                why = "nostop"
            elif not (20000 <= f["dur_ms"] <= 3600000):
                why = "range"
            else:
                ok, why, _c = self.stats.update(user, stop, wx, tb, f["dur_ms"] / 1000.0)
                applied = 1 if ok else 0
        self._save()
        n, mean_ds, sd_ds = self._cell_fields(user, stop, wx, tb)
        d = {"ok": 1, "trip": trip, "stored": 1, "applied": applied}
        if not applied:
            d["why"] = why
        d.update({"n": n, "mean_ds": mean_ds, "sd_ds": sd_ds})
        body = jdump(d)
        self.log("  이동 기록 trip=%d 사용자 %d 정류장 %d wx=%d tb=%d %s %.1f초 → 반영 %d%s, 칸 n=%d 평균 %.1f초 sd %.1f초" % (
            trip, user, stop, wx, tb, st, f["dur_ms"] / 1000.0, applied,
            "" if applied else " (이유: %s)" % why, n, mean_ds / 10.0, sd_ds / 10.0))
        resp = tl.signed(req, key, body)
        if bad_mac:
            self.log("  [결함주입] 응답 MAC을 일부러 틀리게 함 (기록은 저장됨)")
            m = resp.mac
            resp.mac = ("0" if m[0] != "0" else "1") + m[1:]
        return resp

    def _stats(self, req, user, key):
        q = tl.query_params(req.path)
        try:
            u, s = int(q.get("user", "")), int(q.get("stop", ""))
        except ValueError:
            return err_resp(400, "field")
        if not (1 <= u <= 4) or not (1 <= s <= 4):
            return err_resp(400, "field")
        if u != user:
            return err_resp(401, "auth")
        cs = []
        for w in range(3):
            for t in range(4):
                c = self.stats.get(u, s, w, t)
                cs.append("%d,%d,%d" % (c["n"], c["mean_ds"], c["sd_ds"]))
        body = jdump({"ok": 1, "user": u, "stop": s, "def_ds": int(self.default_walk * 10), "c": ";".join(cs)})
        return tl.signed(req, key, body)

    # ---- 동작
    def compute_slack(self, user, stop, wx, tb, eta):
        with self.lock:
            return self.stats.slack(user, stop, wx, tb, eta, self.default_walk)

    def new_trip_id(self):
        self.seq += 1
        return ((self.boot & 0x7FFF) << 16) | (self.seq & 0xFFFF)

    def send_depart(self, user, stop=0, wx=0, tb=255, eta=-1, tc=None):
        """출발 신호 전송(재시도 포함, 블로킹). 반환 (결과 문자열, trip, 시도 횟수)."""
        ph = self.state["phones"].get(str(user))
        key = self.keys.get(user)
        if ph is None or key is None:
            self.log("사용자 %d: 등록된 폰(또는 키)이 없어 출발 신호를 보낼 수 없음" % user)
            return "nophone", 0, 0
        old = self.departs.get(user)
        if old:
            old["cancel"].set()
        trip = self.new_trip_id()
        info = {"trip": trip, "cancel": threading.Event(), "result": None}
        self.departs[user] = info
        t0 = time.monotonic()
        tries = 0
        result = "timeout"
        self.log("문 열림! 사용자 %d 출발 신호 시작 trip=%d → %s:%d" % (user, trip, ph["ip"], ph["port"]))
        while not info["cancel"].is_set() and (time.monotonic() - t0) < self.window_s:
            tries += 1
            d = {"v": 1, "trip": trip, "user": user, "stop": stop, "wx": wx, "tb": tb,
                 "age_ms": int((time.monotonic() - t0) * 1000), "try": tries}
            if eta >= 0:
                d["eta_s"] = eta
            if tc is not None:
                d["tc"] = tc
            r = tl.http_call(ph["ip"], ph["port"], "POST", "/v1/depart", key, user, jdump(d), 1.2, 3.0)
            if r.valid and r.data and r.data.get("ok") == 1 and r.data.get("trip") == trip \
                    and r.data.get("st") in ("tracking", "dup", "noperm", "off"):
                result = r.data["st"]
                self.log("→ 출발 신호 %d번째 시도 성공: 폰 응답 st=%s" % (tries, result))
                break
            self.log("→ 출발 신호 %d번째 시도 실패: %s" % (tries, r.short()))
            if r.status in (400, 401) and not r.err:
                result = "rejected"
                self.log("  폰이 거절(재시도 안 함)")
                break
            info["cancel"].wait(self.retry_s)
        else:
            if info["cancel"].is_set():
                result = "canceled"
            else:
                self.log("→ %d초가 지나 출발 신호를 포기함" % self.window_s)
        info["result"] = result
        return result, trip, tries

    def depart_async(self, *a, **kw):
        t = threading.Thread(target=self.send_depart, args=a, kwargs=kw, daemon=True)
        t.start()
        return t

    def send_cancel(self, user):
        info = self.departs.get(user)
        ph = self.state["phones"].get(str(user))
        if not info or ph is None:
            self.log("사용자 %d: 취소할 최근 출발이 없음" % user)
            return None
        info["cancel"].set()
        r = tl.http_call(ph["ip"], ph["port"], "POST", "/v1/cancel", self.keys[user], user,
                         jdump({"v": 1, "trip": info["trip"], "user": user}), 1.2, 3.0)
        if r.valid and r.data:
            self.log("→ 취소 신호 trip=%d: 폰 응답 st=%s" % (info["trip"], r.data.get("st")))
            return r.data.get("st")
        self.log("→ 취소 신호 실패: %s" % r.short())
        return None

    # ---- 콘솔
    HELP = (
        "명령어:\n"
        "  depart USER [STOP WX TB ETA]  출발 신호 보내기 (기본 STOP 0, WX 0, TB 255=모름, ETA 생략)\n"
        "  cancel USER                   외출 취소 신호\n"
        "  slack USER STOP WX TB ETA     여유 시간 계산\n"
        "  stats USER STOP               통계 칸 보기\n"
        "  phones                        등록된 폰 목록\n"
        "  help / quit")

    def command(self, line):
        """콘솔 명령 1줄 처리. 종료해야 하면 False."""
        p = line.strip().split()
        if not p:
            return True
        c, a = p[0].lower(), p[1:]
        try:
            if c in ("quit", "exit", "q"):
                return False
            elif c == "help":
                print(self.HELP, flush=True)
            elif c == "depart":
                nums = [int(x) for x in a]
                if not nums:
                    raise ValueError
                self.depart_async(*nums[:5])
            elif c == "cancel":
                threading.Thread(target=self.send_cancel, args=(int(a[0]),), daemon=True).start()
            elif c == "slack":
                u, s, w, t, e = [int(x) for x in a[:5]]
                r = self.compute_slack(u, s, w, t, e)
                names = ["해당 칸", "같은 정류장 합산", "정류장 전체 합산", "초기값"]
                self.log("여유 %.1f초 = ETA %d - 예상 이동 %d - 안전 여유 %.1f  (근거: 수준 %d %s, 표본 %d, 평균 %.1f초, sd %.1f초)" % (
                    r["slack"], e, r["walk"], r["safety"], r["level"], names[r["level"]], r["n"], r["mean"], r["sd"]))
            elif c == "stats":
                u, s = int(a[0]), int(a[1])
                shown = 0
                for w in range(3):
                    for t in range(4):
                        x = self.stats.get(u, s, w, t)
                        if x["n"]:
                            shown += 1
                            print("  wx=%d tb=%d : n=%d 평균 %.1f초 sd %.1f초 (이상값 연속 %d회)" % (
                                w, t, x["n"], x["mean_ds"] / 10.0, x["sd_ds"] / 10.0, x["strikes"]), flush=True)
                if not shown:
                    print("  사용자 %d 정류장 %d: 표본 없음 (초기값 %d초)" % (u, s, self.default_walk), flush=True)
            elif c == "phones":
                if not self.state["phones"]:
                    print("  등록된 폰 없음", flush=True)
                for u, ph in sorted(self.state["phones"].items()):
                    print("  사용자 %s: %s:%d (마지막 hello %s, 미동기화 %s건)" % (
                        u, ph["ip"], ph["port"], ph.get("last", "?"), ph.get("pending", "?")), flush=True)
            else:
                print("모르는 명령입니다. help 를 입력하세요.", flush=True)
        except (ValueError, IndexError, KeyError):
            print("인자가 잘못되었습니다. help 를 입력하세요.", flush=True)
        return True


def console_loop(handler, stop_event):
    try:
        while not stop_event.is_set():
            line = sys.stdin.readline()
            if not line:
                return  # 입력 끝: 서버는 계속 동작
            if handler(line) is False:
                stop_event.set()
                return
    except (OSError, ValueError):
        return


def main(argv=None):
    tl.setup_console()
    ap = argparse.ArgumentParser(description="가상 현관판 (통신규격 v1)")
    ap.add_argument("--port", type=int, default=tl.TL_BOARD_PORT)
    ap.add_argument("--key", action="append", default=[], metavar="USER=HEX32", help="여러 번 지정 가능")
    ap.add_argument("--state", default="virtual_board_state.json")
    ap.add_argument("--default-walk", type=int, default=300, help="표본이 없을 때 쓰는 이동 시간(초)")
    ap.add_argument("--drop-trips", type=int, default=0, help="처음 N개 /v1/trips 요청에 응답 없이 연결을 닫음")
    ap.add_argument("--busy-trips", type=int, default=0, help="그 다음 N개에 503 busy")
    ap.add_argument("--bad-mac-trips", type=int, default=0, help="그 다음 N개에 틀린 응답 MAC (기록은 저장됨)")
    ap.add_argument("--delay-ms", type=int, default=0, help="모든 응답을 이만큼 늦춤")
    ap.add_argument("--resp-mac-no-query", action="store_true", help="응답 MAC 경로에서 쿼리를 뺀다(규격과 다름, 호환 시험용)")
    args = ap.parse_args(argv)
    keys = {}
    for k in args.key:
        try:
            u, _, h = k.partition("=")
            keys[int(u)] = tl.parse_key(h)
        except ValueError as e:
            ap.error("--key 형식 오류 (%s): %s" % (k, e))
    if args.resp_mac_no_query:
        tl.RESP_MAC_WITH_QUERY = False
    try:
        b = VirtualBoard(args.port, keys or None, args.state, args.default_walk, args.drop_trips,
                         args.busy_trips, args.bad_mac_trips, args.delay_ms)
    except OSError as e:
        print("포트 %d 를 열 수 없습니다: %s" % (args.port, e))
        return 1
    b.start()
    say("가상 현관판 시작: 포트 %d, 부팅 횟수 %d, 사용자 %s" % (args.port, b.boot, sorted(b.keys)))
    ips = tl.lan_ips()
    say("폰 앱의 '현관판 주소'에 입력할 값: %s" % (", ".join("%s:%d" % (ip, args.port) for ip in ips) or "(LAN 주소를 찾지 못함)"))
    say("상태 파일: %s   (help 입력으로 명령어 보기, 종료: quit 또는 Ctrl+C)" % args.state)
    stop = threading.Event()
    threading.Thread(target=console_loop, args=(b.command, stop), daemon=True).start()
    try:
        while not stop.is_set():
            stop.wait(0.5)
    except KeyboardInterrupt:
        pass
    say("종료합니다.")
    b.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
