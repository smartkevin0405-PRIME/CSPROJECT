# 이 파일은 생성형 AI(Claude)의 도움을 받아 작성했습니다.
"""현관판/폰 시뮬레이터 공용 코드 (표준 라이브러리만 사용, 통신규격 v1)."""
import datetime
import hashlib
import hmac
import json
import math
import os
import socket
import socketserver
import sys
import threading
import time
from urllib.parse import parse_qs

TL_BOARD_PORT = 8080
TL_PHONE_PORT = 8765
MAX_BODY = 480
DEFAULT_KEY_HEX = "000102030405060708090a0b0c0d0e0f"

# 응답 MAC의 경로에 쿼리를 포함할지 (규격은 요청에만 "쿼리 포함"이라 적음 -> 기본 False)
RESP_MAC_WITH_QUERY = True  # 규격 3장: 요청 MAC과 같은 경로 문자열(쿼리 포함)


# ---------------------------------------------------------------- 출력

_print_lock = threading.Lock()


def setup_console():
    """윈도우 콘솔에서도 한글이 깨지지 않게 한다."""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(errors="replace")
        except Exception:
            pass


def say(msg, quiet=False):
    if quiet:
        return
    with _print_lock:
        print("[%s] %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


# ---------------------------------------------------------------- MAC / JSON

def parse_key(text):
    t = text.strip()
    if len(t) != 32 or any(c not in "0123456789abcdefABCDEF" for c in t):
        raise ValueError("키는 32자리 16진수여야 합니다: %r" % text)
    return bytes.fromhex(t)


def _to_bytes(x):
    return x if isinstance(x, (bytes, bytearray)) else x.encode("utf-8")


def _mac16(key, data):
    return hmac.new(key, data, hashlib.sha256).digest()[:16].hex()


def req_mac(key, method, path_with_query, body=b""):
    """X-TL-Mac(요청) = HMAC(키, 메서드 + " " + 경로(쿼리 포함) + "\\n" + 본문)의 앞 16바이트."""
    data = ("%s %s\n" % (method, path_with_query)).encode("utf-8") + _to_bytes(body)
    return _mac16(key, data)


def resp_mac(key, path, request_mac_hex, body):
    """X-TL-Mac(응답) = HMAC(키, "R " + 경로 + "\\n" + 요청MAC + "\\n" + 응답 본문)의 앞 16바이트."""
    if not RESP_MAC_WITH_QUERY:
        path = path.split("?", 1)[0]
    data = ("R %s\n%s\n" % (path, request_mac_hex)).encode("utf-8") + _to_bytes(body)
    return _mac16(key, data)


def jdump(d):
    """공백 없는 JSON. 키 순서는 dict 삽입 순서 그대로."""
    return json.dumps(d, separators=(",", ":"), ensure_ascii=True)


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


# ---------------------------------------------------------------- 시각 / 네트워크 주소

KST = datetime.timezone(datetime.timedelta(hours=9))


def tb_from_hour(h):
    if 6 <= h <= 9:
        return 0
    if 10 <= h <= 15:
        return 1
    if 16 <= h <= 20:
        return 2
    return 3


def tb_from_epoch(epoch):
    return tb_from_hour(datetime.datetime.fromtimestamp(epoch, KST).hour)


def lan_ips():
    ips = []
    try:
        for ai in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.append(ai[4][0])
    except OSError:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 9))  # 패킷은 실제로 나가지 않는다
        ips.append(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    out = []
    for ip in ips:
        if ip not in out and not ip.startswith("127.") and ip != "0.0.0.0":
            out.append(ip)
    return out


def outbound_ip(host):
    """host 쪽으로 나가는 인터페이스의 내 주소."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((host, 9))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        ips = lan_ips()
        return ips[0] if ips else "127.0.0.1"


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


# ---------------------------------------------------------------- 파일

def load_json(path, default):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False)
    os.replace(tmp, path)


# ---------------------------------------------------------------- HTTP 서버

REASONS = {200: "OK", 400: "Bad Request", 401: "Unauthorized", 404: "Not Found",
           413: "Payload Too Large", 503: "Service Unavailable"}


class Req(object):
    def __init__(self, method, path, headers, body, peer):
        self.method = method
        self.path = path  # 쿼리 포함
        self.headers = headers  # 키는 소문자
        self.body = body
        self.peer = peer


class Resp(object):
    def __init__(self, status, body, mac=None):
        self.status = status
        self.body = body
        self.mac = mac

    def to_bytes(self):
        b = self.body.encode("utf-8")
        h = "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n" % (
            self.status, REASONS.get(self.status, "Error"), len(b))
        if self.mac:
            h += "X-TL-Mac: %s\r\n" % self.mac
        return (h + "\r\n").encode("latin-1") + b


def err_resp(status, code):
    """오류 응답: MAC 없음."""
    return Resp(status, jdump({"ok": 0, "err": code}))


def signed(req, key, body, status=200):
    """요청 MAC에 묶은 응답 MAC을 붙인다. req.headers의 MAC은 이미 검증된 값이어야 한다."""
    path = req.path
    return Resp(status, body, resp_mac(key, path, req.headers.get("x-tl-mac", ""), body))


def _read_request(sock):
    """(Req, None) | (None, 오류Resp) | (None, None=연결 종료)."""
    buf = b""
    while b"\r\n\r\n" not in buf:
        if len(buf) > 8192:
            return None, err_resp(400, "json")
        chunk = sock.recv(2048)
        if not chunk:
            return None, None
        buf += chunk
    head, _, rest = buf.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    parts = lines[0].split(" ")
    if len(parts) < 2:
        return None, err_resp(400, "json")
    headers = {}
    for ln in lines[1:]:
        k, _, v = ln.partition(":")
        headers[k.strip().lower()] = v.strip()
    method, path = parts[0], parts[1]
    cl_raw = headers.get("content-length")
    if cl_raw is None:
        if method == "POST":  # 규격: Content-Length 필수
            return None, err_resp(400, "json")
        cl = 0
    else:
        if not cl_raw.isdigit():
            return None, err_resp(400, "json")
        cl = int(cl_raw)
    if cl > MAX_BODY:
        return None, err_resp(413, "size")
    body = rest
    while len(body) < cl:
        chunk = sock.recv(2048)
        if not chunk:
            return None, err_resp(400, "json")
        body += chunk
    body = body[:cl]
    try:
        peer = sock.getpeername()[0]
    except OSError:
        peer = "?"
    return Req(method, path, headers, body, peer), None


class _Handler(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        try:
            sock.settimeout(3.0)
            req, early = _read_request(sock)
            if req is None:
                resp = early
            else:
                resp = self.server.dispatch(req)
            if resp is None:  # 응답하지 않고 닫는다 (시험용 / 연결 종료)
                return
            if self.server.delay_s > 0:
                time.sleep(self.server.delay_s)
            sock.sendall(resp.to_bytes())
        except OSError:
            pass
        except Exception as e:  # 시뮬레이터 버그가 서버를 죽이지 않게
            if not self.server.quiet:
                say("내부 오류: %r" % (e,))


class TLServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True  # SO_REUSEADDR
    daemon_threads = True
    request_queue_size = 16

    def __init__(self, addr, dispatch, delay_s=0.0, quiet=False):
        socketserver.TCPServer.__init__(self, addr, _Handler)
        self.dispatch = dispatch
        self.delay_s = delay_s
        self.quiet = quiet
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self.serve_forever, kwargs={"poll_interval": 0.2}, daemon=True)
        self._thread.start()

    def stop(self):
        try:
            self.shutdown()
        finally:
            self.server_close()


# ---------------------------------------------------------------- 요청 검증 공통

def check_auth(req, keys):
    """성공: (user, key, 요청MAC). 실패: 401 Resp. 비교는 상수 시간."""
    bad = err_resp(401, "auth")
    u = req.headers.get("x-tl-user", "")
    mac = req.headers.get("x-tl-mac", "")
    if not u.isdigit():
        return bad
    user = int(u)
    key = keys.get(user)
    if key is None or not mac:
        return bad
    expected = req_mac(key, req.method, req.path, req.body)
    if not hmac.compare_digest(expected.encode("ascii"), mac.encode("latin-1", "replace")):
        return bad
    return user, key, mac


def parse_obj(req):
    """본문 JSON 평면 객체. 실패하면 400 json Resp."""
    try:
        obj = json.loads(req.body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return err_resp(400, "json")
    if not isinstance(obj, dict):
        return err_resp(400, "json")
    return obj


def need_ints(obj, specs):
    """specs = [(이름, 최소, 최대)]. 모두 정수이고 범위 안이면 dict, 아니면 None."""
    out = {}
    for name, lo, hi in specs:
        v = obj.get(name)
        if not is_int(v) or v < lo or v > hi:
            return None
        out[name] = v
    return out


def query_params(path):
    q = path.partition("?")[2]
    return {k: v[0] for k, v in parse_qs(q).items()}


# ---------------------------------------------------------------- HTTP 클라이언트

class Reply(object):
    def __init__(self):
        self.status = 0
        self.headers = {}
        self.body = ""
        self.mac_ok = False
        self.err = None
        self.data = None

    @property
    def valid(self):
        """200 이고 응답 MAC이 맞을 때만 성공으로 본다."""
        return self.status == 200 and self.mac_ok and self.err is None

    def short(self):
        if self.err:
            return "통신 실패(%s)" % self.err
        return "HTTP %d%s %s" % (self.status, "" if self.mac_ok or self.status != 200 else " (MAC 불일치!)", self.body[:120])


def http_call(host, port, method, path, key=None, user=None, body="", connect_timeout=3.0, read_timeout=3.0):
    """요청 1건 (연결을 새로 열고 닫는다). key=None이면 인증 헤더 없음."""
    r = Reply()
    bb = body.encode("utf-8")
    head = "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n" % (method, path, host, port)
    mac = None
    if key is not None:
        mac = req_mac(key, method, path, bb)
        head += "X-TL-User: %d\r\nX-TL-Mac: %s\r\n" % (user, mac)
    if method == "POST":
        head += "Content-Type: application/json\r\nContent-Length: %d\r\n" % len(bb)
    data = (head + "\r\n").encode("latin-1") + bb
    try:
        s = socket.create_connection((host, port), timeout=connect_timeout)
    except OSError as e:
        r.err = "연결 실패: %s" % (e,)
        return r
    try:
        s.settimeout(read_timeout)
        deadline = time.monotonic() + read_timeout
        s.sendall(data)
        buf = b""
        while True:
            if time.monotonic() > deadline:
                raise socket.timeout("응답 시간 초과")
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            if len(buf) > 65536:
                break
    except OSError as e:
        r.err = "응답 없음: %s" % (e,)
        return r
    finally:
        s.close()
    if b"\r\n\r\n" not in buf:
        r.err = "응답 없음(연결이 닫힘)"
        return r
    h, _, b = buf.partition(b"\r\n\r\n")
    lines = h.decode("latin-1").split("\r\n")
    try:
        r.status = int(lines[0].split(" ")[1])
    except (IndexError, ValueError):
        r.err = "응답 형식 오류"
        return r
    for ln in lines[1:]:
        k, _, v = ln.partition(":")
        r.headers[k.strip().lower()] = v.strip()
    cl = r.headers.get("content-length")
    if cl and cl.isdigit():
        b = b[:int(cl)]
    r.body = b.decode("utf-8", "replace")
    got = r.headers.get("x-tl-mac")
    if key is not None and got and r.status == 200:
        exp = resp_mac(key, path, mac, r.body)
        r.mac_ok = hmac.compare_digest(exp.encode("ascii"), got.encode("latin-1", "replace"))
    try:
        d = json.loads(r.body)
        r.data = d if isinstance(d, dict) else None
    except ValueError:
        r.data = None
    return r


# ---------------------------------------------------------------- 통계 (현관판 알고리즘 미러)

def _round_ds(x):
    return int(math.floor(x * 10.0 + 0.5))


class Stats(object):
    """칸 = (사용자, 정류장 1..4, wx 0..2, tb 0..3) -> {n, strikes, mean_ds, sd_ds}."""

    def __init__(self, cells=None):
        self.cells = cells if cells is not None else {}

    @staticmethod
    def key(u, s, w, t):
        return "%d,%d,%d,%d" % (u, s, w, t)

    def get(self, u, s, w, t):
        return self.cells.get(self.key(u, s, w, t), {"n": 0, "strikes": 0, "mean_ds": 0, "sd_ds": 0})

    def update(self, u, s, w, t, x):
        """x = 이동 시간(초). 반환 (applied, why, 칸)."""
        k = self.key(u, s, w, t)
        c = self.cells.get(k)
        if c is None:
            c = {"n": 0, "strikes": 0, "mean_ds": 0, "sd_ds": 0}
        n = c["n"]
        mean = c["mean_ds"] / 10.0
        sd = c["sd_ds"] / 10.0
        if n >= 5 and abs(x - mean) > max(3.0 * sd, 120.0):
            if c["strikes"] >= 2:
                pass  # 세 번째 연속 이상값은 반영하고 strikes 초기화
            else:
                c["strikes"] += 1
                self.cells[k] = c
                return False, "outlier", c
        n2 = min(n + 1, 255)
        if n == 0:
            mean2, var2 = x, 0.0
        else:
            a = 1.0 / min(n2, 20)
            d = x - mean
            mean2 = mean + a * d
            var2 = (1.0 - a) * (sd * sd + a * d * d)
        c["n"] = n2
        c["strikes"] = 0
        c["mean_ds"] = _round_ds(mean2)
        c["sd_ds"] = _round_ds(math.sqrt(var2))
        self.cells[k] = c
        return True, None, c

    @staticmethod
    def _pool(cells):
        """cells = [(n, 평균초, 분산)] -> (합계 n, 평균, 분산). 가중치 n은 20으로 제한."""
        wsum = sum(min(n, 20) for n, _, _ in cells)
        m = sum(min(n, 20) * mu for n, mu, _ in cells) / wsum
        var = sum(min(n, 20) * (v + (mu - m) ** 2) for n, mu, v in cells) / wsum
        return sum(n for n, _, _ in cells), m, var

    def _cell_tuple(self, c):
        return (c["n"], c["mean_ds"] / 10.0, (c["sd_ds"] / 10.0) ** 2)

    def slack(self, u, s, w, t, eta_s, default_walk=300):
        level = 3
        n_src, mean, var = 0, float(default_walk), 0.0
        exact = self.get(u, s, w, t)
        if exact["n"] >= 3:
            level = 0
            n_src, mean, var = self._cell_tuple(exact)
        else:
            same_tb = [self._cell_tuple(self.get(u, s, ww, t)) for ww in range(3)]
            same_tb = [c for c in same_tb if c[0] > 0]
            same_wx = [self._cell_tuple(self.get(u, s, w, tt)) for tt in range(4)]
            same_wx = [c for c in same_wx if c[0] > 0]
            allc = [self._cell_tuple(self.get(u, s, ww, tt)) for ww in range(3) for tt in range(4)]
            allc = [c for c in allc if c[0] > 0]
            if sum(c[0] for c in same_tb) >= 3:
                level = 1
                n_src, mean, var = self._pool(same_tb)
            elif sum(c[0] for c in same_wx) >= 3:
                level = 1
                n_src, mean, var = self._pool(same_wx)
            elif sum(c[0] for c in allc) >= 1:
                level = 2
                n_src, mean, var = self._pool(allc)
        sd = math.sqrt(var)
        if n_src < 2 or sd == 0:
            sd = max(20.0, 0.15 * mean)
        walk = int(math.ceil(round(mean, 6)))
        safety = 30.0 + 1.0 * sd
        return {"level": level, "n": n_src, "mean": mean, "sd": sd, "walk": walk,
                "safety": safety, "slack": eta_s - walk - safety}
