# 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
"""가상 폰: 실제 현관판을 앱 없이 시험하기 위한 폰 흉내 (통신규격 v1)."""
import argparse
import random
import sys
import threading
import time

import tlcommon as tl
from tlcommon import Resp, err_resp, jdump, say
from virtual_board import console_loop

RETRY_WAITS = (5, 15, 60, 300)  # 초 (speedup으로 나눔)


class VirtualPhone(object):
    def __init__(self, board_host, board_port, user=1, key=None, port=tl.TL_PHONE_PORT, ip=None,
                 walk_s=400, jitter_s=30, speedup=60.0, stop=1, outcome="arrived", q=0,
                 state_path="virtual_phone_state.json", ignore_departs=0, dup_send=False,
                 quiet=False, bind="0.0.0.0", hello_interval_s=3600.0):
        self.board_host, self.board_port = board_host, board_port
        self.user = user
        self.key = key if key else tl.parse_key(tl.DEFAULT_KEY_HEX)
        self.port = port
        self.ip = ip or tl.outbound_ip(board_host)
        self.walk_s, self.jitter_s, self.speedup = walk_s, jitter_s, float(speedup)
        self.stop_slot, self.outcome, self.q = stop, outcome, q
        self.state_path = state_path
        self.ignore_departs = ignore_departs
        self.dup_send = dup_send
        self.quiet = quiet
        self.hello_interval_s = hello_interval_s
        self.lock = threading.RLock()
        self.departs_seen = 0
        self.last_depart_age_ms = None
        self.last_depart_try = None
        self.send_count = 0  # /v1/trips 전송 횟수
        self.badmac_count = 0  # 응답 MAC이 틀려 무시한 횟수
        self.active = {}  # trip -> Event(취소)
        self._stop = threading.Event()
        self._sync_ev = threading.Event()
        st = tl.load_json(state_path, {})
        self.seen = list(st.get("seen", []))
        self.records = list(st.get("records", []))
        # 이전 실행에서 진행 중이던 이동은 폰이 재부팅된 것과 같으므로 lost 처리
        for a in st.get("active", []):
            self.records.append({"trip": a["trip"], "user": user, "stop": 0, "wx": a["wx"], "tb": a["tb"],
                                 "st": "lost", "dur_ms": 0, "q": 0, "src": 0, "epoch": a["epoch"], "done": 0})
        self.active_info = []
        self._save()
        self.server = tl.TLServer((bind, port), self.dispatch, 0.0, quiet)

    def log(self, msg):
        say(msg, self.quiet)

    def _save(self):
        with self.lock:
            done = [r for r in self.records if r["done"]][-20:]
            pend = [r for r in self.records if not r["done"]]
            self.records = done + pend
            tl.save_json(self.state_path, {"seen": self.seen[-64:], "records": self.records,
                                           "active": self.active_info})

    def pending(self):
        with self.lock:
            return sorted([r for r in self.records if not r["done"]], key=lambda r: r["trip"])

    # ---- 시작 / 종료
    def start(self):
        self.server.start()
        threading.Thread(target=self._sync_loop, daemon=True).start()
        threading.Thread(target=self._hello_timer, daemon=True).start()
        self.request_sync()

    def stop(self):
        self._stop.set()
        self._sync_ev.set()
        for ev in list(self.active.values()):
            ev.set()
        self.server.stop()

    # ---- 서버 (ping, depart, cancel)
    def dispatch(self, req):
        resp = self._handle(req)
        if resp is None:
            self.log("← %s : 응답하지 않음 (시험용 --ignore-departs)" % req.path)
        else:
            self.log("← %s %s → %d" % (req.method, req.path, resp.status))
        return resp

    def _handle(self, req):
        path = req.path.partition("?")[0]
        m = req.method
        if m == "GET" and path == "/v1/ping":
            return Resp(200, jdump({"ok": 1, "dev": "phone", "v": 1, "user": self.user,
                                    "busy": 1 if self.active else 0}))
        if m != "POST" or path not in ("/v1/depart", "/v1/cancel"):
            return err_resp(404, "path")
        auth = tl.check_auth(req, {self.user: self.key})
        if isinstance(auth, Resp):
            self.log("  인증 실패(MAC 또는 사용자 불일치)")
            return auth
        user, key, _mac = auth
        obj = tl.parse_obj(req)
        if isinstance(obj, Resp):
            return obj
        if tl.is_int(obj.get("user")) and obj["user"] != user:
            return err_resp(401, "auth")
        if path == "/v1/cancel":
            return self._cancel(req, obj, key)
        return self._depart(req, obj, key)

    def _depart(self, req, obj, key):
        f = tl.need_ints(obj, [("trip", 0, 0xFFFFFFFF), ("user", 1, 4), ("stop", 0, 4), ("wx", 0, 2),
                               ("age_ms", 0, 0xFFFFFFFF), ("try", 1, 0xFFFFFFFF)])
        tb = obj.get("tb")
        if f is None or not tl.is_int(tb) or not (0 <= tb <= 3 or tb == 255):
            return err_resp(400, "field")
        if f["age_ms"] > 120000:
            return err_resp(400, "stale")
        t_recv = time.monotonic()
        with self.lock:
            self.departs_seen += 1
            if self.departs_seen <= self.ignore_departs:
                self.log("  [결함주입] %d번째 depart 요청을 무시함 (try=%d)" % (self.departs_seen, f["try"]))
                return None
            trip = f["trip"]

            def reply(st):
                return tl.signed(req, key, jdump({"ok": 1, "trip": trip, "st": st}))

            if trip in self.seen:
                self.log("  출발 신호 trip=%d 중복 → dup (try=%d)" % (trip, f["try"]))
                return reply("dup")
            self.seen.append(trip)
            self.last_depart_age_ms = f["age_ms"]
            self.last_depart_try = f["try"]
            door_mono = t_recv - f["age_ms"] / 1000.0  # 문 열림 순간 (내 단조 시계)
            epoch_door = int(time.time() - f["age_ms"] / 1000.0)
            if tb == 255:
                tb = tl.tb_from_epoch(epoch_door)
                self.log("  tb=255 → 폰 시계로 채움: %d" % tb)
            ev = threading.Event()
            self.active[trip] = ev
            self.active_info.append({"trip": trip, "wx": f["wx"], "tb": tb, "epoch": epoch_door})
            self._save()
        self.log("  출발 신호 수락 trip=%d age_ms=%d try=%d → 측정 시작" % (trip, f["age_ms"], f["try"]))
        threading.Thread(target=self._walk, args=(trip, door_mono, f["wx"], tb, epoch_door, ev), daemon=True).start()
        return reply("tracking")

    def _cancel(self, req, obj, key):
        f = tl.need_ints(obj, [("trip", 0, 0xFFFFFFFF), ("user", 1, 4)])
        if f is None:
            return err_resp(400, "field")
        trip = f["trip"]
        with self.lock:
            if trip in self.active:
                self.active[trip].set()
                st = "canceled"
            elif any(r["trip"] == trip for r in self.records):
                st = "done"
            else:
                st = "unknown"
        self.log("  취소 신호 trip=%d → %s" % (trip, st))
        return tl.signed(req, key, jdump({"ok": 1, "trip": trip, "st": st}))

    # ---- 걷기 흉내
    def _walk(self, trip, door_mono, wx, tb, epoch_door, ev):
        dur_sim = max(1.0, self.walk_s + random.uniform(-self.jitter_s, self.jitter_s))
        deadline = door_mono + dur_sim / self.speedup
        canceled = ev.wait(max(0.0, deadline - time.monotonic()))
        if self._stop.is_set():
            return
        now = time.monotonic()
        if canceled:
            st, stop, dur = "canceled", 0, 0
        elif self.outcome == "arrived":
            st, stop = "arrived", self.stop_slot
            dur = int(round((now - door_mono) * self.speedup * 1000))  # 실제 시간 x 배속 = 모의 시간
        else:
            st, stop, dur = self.outcome, 0, 0
        rec = {"trip": trip, "user": self.user, "stop": stop, "wx": wx, "tb": tb, "st": st,
               "dur_ms": dur, "q": self.q if st == "arrived" else 0, "src": 0, "epoch": epoch_door, "done": 0}
        with self.lock:
            self.active.pop(trip, None)
            self.active_info = [a for a in self.active_info if a["trip"] != trip]
            self.records.append(rec)
            self._save()
        self.log("측정 끝 trip=%d: %s, 정류장 %d, 모의 이동 시간 %.1f초 → 동기화 예정" % (trip, st, stop, dur / 1000.0))
        self.request_sync()

    # ---- 수동 출발
    def manual(self, stop=None):
        epoch = int(time.time())
        with self.lock:
            while any(r["trip"] == (0x80000000 | (epoch & 0x7FFFFFFF)) for r in self.records):
                epoch += 1
            trip = 0x80000000 | (epoch & 0x7FFFFFFF)
            dur = int(round(max(1.0, self.walk_s + random.uniform(-self.jitter_s, self.jitter_s)) * 1000))
            rec = {"trip": trip, "user": self.user, "stop": stop if stop else self.stop_slot, "wx": 0,
                   "tb": tl.tb_from_epoch(epoch), "st": "arrived", "dur_ms": dur, "q": self.q, "src": 1,
                   "epoch": epoch, "done": 0}
            self.records.append(rec)
            self._save()
        self.log("수동 기록 생성 trip=%d 정류장 %d %.1f초 (src=1)" % (trip, rec["stop"], dur / 1000.0))
        self.request_sync()
        return trip

    # ---- 동기화
    def request_sync(self):
        self._sync_ev.set()

    def send_hello(self):
        d = {"v": 1, "user": self.user, "ip": self.ip, "port": self.port, "epoch": int(time.time()),
             "tz": 540, "pending": len(self.pending())}
        r = tl.http_call(self.board_host, self.board_port, "POST", "/v1/hello", self.key, self.user, jdump(d), 3.0, 3.0)
        if r.valid and r.data and r.data.get("ok") == 1 and r.data.get("reg") == 1:
            self.log("→ hello 성공: 현관판 부팅 %s, 주소 %s:%d 등록됨" % (r.data.get("boot"), self.ip, self.port))
            return True
        self.log("→ hello 실패: %s" % r.short())
        return False

    def _send_record(self, rec, mark=True):
        d = {"v": 1, "trip": rec["trip"], "user": rec["user"], "stop": rec["stop"], "wx": rec["wx"],
             "tb": rec["tb"], "st": rec["st"], "dur_ms": rec["dur_ms"], "q": rec["q"], "src": rec["src"],
             "epoch": rec["epoch"]}
        with self.lock:
            self.send_count += 1
        r = tl.http_call(self.board_host, self.board_port, "POST", "/v1/trips", self.key, self.user, jdump(d), 3.0, 3.0)
        if r.status == 200 and not r.mac_ok and not r.err:
            with self.lock:
                self.badmac_count += 1
            self.log("→ trip=%d 응답 MAC이 맞지 않음 → 저장 확인으로 인정하지 않음" % rec["trip"])
            return False, r
        if r.valid and r.data and r.data.get("ok") == 1 and r.data.get("trip") == rec["trip"] \
                and r.data.get("stored") == 1:
            self.log("→ trip=%d 저장 확인: 반영 %s%s n=%s 평균 %.1f초 sd %.1f초" % (
                rec["trip"], r.data.get("applied"),
                "" if r.data.get("applied") else " (이유: %s)" % r.data.get("why"),
                r.data.get("n"), r.data.get("mean_ds", 0) / 10.0, r.data.get("sd_ds", 0) / 10.0))
            if mark:
                with self.lock:
                    rec["done"] = 1
                    self._save()
            return True, r
        self.log("→ trip=%d 전송 실패: %s" % (rec["trip"], r.short()))
        return False, r

    def _do_sync(self):
        if not self.send_hello():
            return False
        for rec in self.pending():  # trip 오름차순
            ok, _r = self._send_record(rec)
            if not ok:
                return False
            if self.dup_send:
                self.log("  [결함주입] 같은 기록을 한 번 더 보냄")
                self._send_record(rec, mark=False)
        return True

    def _sync_loop(self):
        while not self._stop.is_set():
            self._sync_ev.wait()
            if self._stop.is_set():
                return
            self._sync_ev.clear()
            attempt = 0
            while not self._stop.is_set():
                if self._do_sync():
                    break
                if attempt >= len(RETRY_WAITS):
                    self.log("재시도 횟수 소진 → 다음 hello 때 다시 시도")
                    break
                w = RETRY_WAITS[attempt] / self.speedup
                attempt += 1
                self.log("%.2f초 뒤 재시도 (%d/%d)" % (w, attempt, len(RETRY_WAITS)))
                if self._sync_ev.wait(w):  # 새 요청이 오면 바로 다시
                    self._sync_ev.clear()
                    attempt = 0

    def _hello_timer(self):
        while not self._stop.wait(self.hello_interval_s):
            if self.send_hello() and self.pending():
                self.request_sync()

    def resend(self, trip):
        """이미 동기화된 기록도 다시 보낸다. 반환: Reply 또는 None."""
        with self.lock:
            rec = next((r for r in self.records if r["trip"] == trip), None)
        if rec is None:
            self.log("trip=%d 기록이 없음" % trip)
            return None
        _ok, r = self._send_record(rec, mark=False)
        return r

    # ---- 콘솔
    HELP = (
        "명령어:\n"
        "  hello          지금 hello 보내기 (성공하면 밀린 기록도 동기화)\n"
        "  sync           hello 후 밀린 기록 모두 보내기\n"
        "  manual [STOP]  폰에서 수동 출발한 기록 만들고 동기화\n"
        "  pending        아직 동기화 안 된 기록 보기\n"
        "  resend TRIP    이미 보낸 기록을 일부러 다시 보내기 (현관판 중복 처리 확인)\n"
        "  help / quit")

    def command(self, line):
        p = line.strip().split()
        if not p:
            return True
        c, a = p[0].lower(), p[1:]
        try:
            if c in ("quit", "exit", "q"):
                return False
            elif c == "help":
                print(self.HELP, flush=True)
            elif c == "hello":
                def job():
                    if self.send_hello() and self.pending():
                        self.request_sync()
                threading.Thread(target=job, daemon=True).start()
            elif c == "sync":
                self.request_sync()
            elif c == "manual":
                self.manual(int(a[0]) if a else None)
            elif c == "pending":
                ps = self.pending()
                if not ps:
                    print("  미동기화 기록 없음", flush=True)
                for r in ps:
                    print("  trip=%d (0x%08X) %s 정류장 %d %.1f초 wx=%d tb=%d src=%d" % (
                        r["trip"], r["trip"], r["st"], r["stop"], r["dur_ms"] / 1000.0, r["wx"], r["tb"], r["src"]),
                        flush=True)
            elif c == "resend":
                t = int(a[0], 0)
                threading.Thread(target=self.resend, args=(t,), daemon=True).start()
            else:
                print("모르는 명령입니다. help 를 입력하세요.", flush=True)
        except (ValueError, IndexError):
            print("인자가 잘못되었습니다. help 를 입력하세요.", flush=True)
        return True


def main(argv=None):
    tl.setup_console()
    ap = argparse.ArgumentParser(description="가상 폰 (통신규격 v1)")
    ap.add_argument("--board", required=True, metavar="HOST:PORT", help="현관판 주소 (예: 192.168.0.50:8080)")
    ap.add_argument("--user", type=int, default=1)
    ap.add_argument("--key", default=tl.DEFAULT_KEY_HEX, metavar="HEX32")
    ap.add_argument("--port", type=int, default=tl.TL_PHONE_PORT, help="폰 서버 포트")
    ap.add_argument("--ip", default=None, help="hello에 실을 내 주소 (기본: 현관판 쪽 인터페이스 주소)")
    ap.add_argument("--walk-s", type=float, default=400)
    ap.add_argument("--jitter-s", type=float, default=30)
    ap.add_argument("--speedup", type=float, default=60)
    ap.add_argument("--stop", type=int, default=1)
    ap.add_argument("--outcome", choices=["arrived", "canceled", "timeout", "lost"], default="arrived")
    ap.add_argument("--q", type=int, choices=[0, 1, 2], default=0)
    ap.add_argument("--state", default="virtual_phone_state.json")
    ap.add_argument("--ignore-departs", type=int, default=0, help="처음 N개 depart 요청에 응답하지 않음")
    ap.add_argument("--dup-send", action="store_true", help="모든 결과를 두 번 보냄")
    ap.add_argument("--resp-mac-no-query", action="store_true", help="응답 MAC 경로에서 쿼리를 뺀다(규격과 다름, 호환 시험용)")
    args = ap.parse_args(argv)
    host, _, ps = args.board.rpartition(":")
    if not host or not ps.isdigit():
        ap.error("--board 는 HOST:PORT 형식이어야 합니다")
    if not (1 <= args.user <= 4) or not (0 <= args.stop <= 4) or args.speedup <= 0:
        ap.error("--user 1..4, --stop 0..4, --speedup > 0 이어야 합니다")
    try:
        key = tl.parse_key(args.key)
    except ValueError as e:
        ap.error(str(e))
    if args.resp_mac_no_query:
        tl.RESP_MAC_WITH_QUERY = False
    try:
        ph = VirtualPhone(host, int(ps), args.user, key, args.port, args.ip, args.walk_s, args.jitter_s,
                          args.speedup, args.stop, args.outcome, args.q, args.state, args.ignore_departs,
                          args.dup_send)
    except OSError as e:
        print("포트 %d 를 열 수 없습니다: %s" % (args.port, e))
        return 1
    ph.start()
    say("가상 폰 시작: 사용자 %d, 포트 %d, 광고 주소 %s:%d, 현관판 %s" % (args.user, args.port, ph.ip, args.port, args.board))
    say("모의 걷기 %.0f±%.0f초 (배속 %.0f배 → 실제 약 %.1f초), 결과 %s, 정류장 %d" % (
        args.walk_s, args.jitter_s, args.speedup, args.walk_s / args.speedup, args.outcome, args.stop))
    say("help 입력으로 명령어 보기, 종료: quit 또는 Ctrl+C")
    stop = threading.Event()
    threading.Thread(target=console_loop, args=(ph.command, stop), daemon=True).start()
    try:
        while not stop.is_set():
            stop.wait(0.5)
    except KeyboardInterrupt:
        pass
    say("종료합니다.")
    ph.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
