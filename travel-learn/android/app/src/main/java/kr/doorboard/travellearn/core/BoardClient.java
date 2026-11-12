// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

import javax.net.SocketFactory;

/** 폰 → 현관판 요청(hello, trips, stats, ping). 응답 MAC이 맞을 때만 성공으로 본다. */
public final class BoardClient {

    public static final class Result {
        /** HTTP 200 + ok:1 + 응답 MAC 일치 */
        public boolean ok;
        public int status;
        /** 실패 이유(사람이 읽을 문구) */
        public String error = "";
        public Map<String, Object> json = new HashMap<String, Object>();

        public long getLong(String k, long def) {
            return FlatJson.getLong(json, k, def);
        }

        public String getString(String k, String def) {
            return FlatJson.getString(json, k, def);
        }
    }

    private final SocketFactory factory;
    private final String host;
    private final int port;
    private final int user;
    private final byte[] key;
    public int connectMs = 3000;
    /** 현관판은 응답 전에 EEPROM에 기록하므로(수백 ms) 넉넉히 기다린다. */
    public int readMs = 5000;

    public BoardClient(SocketFactory factory, String host, int port, int user, byte[] key) {
        this.factory = factory;
        this.host = host;
        this.port = port;
        this.user = user;
        this.key = key;
    }

    private Result signed(String method, String path, String body) {
        Result r = new Result();
        if (key == null) {
            r.error = "공유 키 미설정";
            return r;
        }
        String reqMac = Proto.mac(key, method, path, body);
        Map<String, String> h = new HashMap<String, String>();
        h.put("X-TL-User", String.valueOf(user));
        h.put("X-TL-Mac", reqMac);
        HttpMini.Response resp;
        try {
            resp = HttpMini.send(factory, host, port, method, path, h, body, connectMs, readMs);
        } catch (IOException e) {
            r.error = "연결 실패: " + e.getMessage();
            return r;
        }
        r.status = resp.status;
        try {
            r.json = FlatJson.parse(resp.body);
        } catch (FlatJson.ParseError e) {
            r.error = "응답 형식 오류";
            return r;
        }
        if (resp.status != 200) {
            r.error = "HTTP " + resp.status + " " + r.getString("err", "");
            return r;
        }
        String got = resp.headers.get("x-tl-mac");
        if (!Proto.macEquals(got, Proto.respMac(key, path, reqMac, resp.body))) {
            r.error = "응답 MAC 불일치(키가 다르거나 위조된 응답)";
            return r;
        }
        if (r.getLong("ok", 0) != 1) {
            r.error = "ok 아님";
            return r;
        }
        r.ok = true;
        return r;
    }

    /** 4.1 연결 점검(인증 없음). */
    public Result ping() {
        Result r = new Result();
        try {
            HttpMini.Response resp = HttpMini.send(factory, host, port, "GET", "/v1/ping", null,
                    null, connectMs, readMs);
            r.status = resp.status;
            r.json = FlatJson.parse(resp.body);
            r.ok = resp.status == 200 && r.getLong("ok", 0) == 1
                    && "board".equals(r.getString("dev", ""));
            if (!r.ok) r.error = "현관판 응답이 아님";
        } catch (IOException e) {
            r.error = "연결 실패: " + e.getMessage();
        } catch (FlatJson.ParseError e) {
            r.error = "응답 형식 오류";
        }
        return r;
    }

    /** 4.2 주소 등록. */
    public Result hello(String ip, int phonePort, long epochS, int tzMin, int pending) {
        String body = FlatJson.obj()
                .put("v", Proto.V)
                .put("user", user)
                .put("ip", ip)
                .put("port", phonePort)
                .put("epoch", epochS)
                .put("tz", tzMin)
                .put("pending", pending)
                .build();
        return signed("POST", "/v1/hello", body);
    }

    /** 4.5 기록 1건 동기화. 성공 조건: ok && stored == 1 && 같은 trip. */
    public Result postTrip(TripRecord rec) {
        Result r = signed("POST", "/v1/trips", rec.toTripJson());
        if (r.ok && (r.getLong("stored", 0) != 1 || r.getLong("trip", -1) != rec.trip)) {
            r.ok = false;
            r.error = "저장 완료 응답이 아님";
        }
        return r;
    }

    /** 4.6 학습 결과 조회. */
    public Result stats(int statUser, int stop) {
        return signed("GET", "/v1/stats?user=" + statUser + "&stop=" + stop, "");
    }
}
