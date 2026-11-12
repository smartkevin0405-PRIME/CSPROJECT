// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

import java.util.Map;

/** 폰 서버가 받는 요청(ping, depart, cancel)의 검증과 응답 생성. 소켓·Android와 분리되어 있다. */
public final class PhoneServerLogic {

    /** 출발 신호 내용(4.3). */
    public static final class Depart {
        public long trip;
        public int user;
        public int stop;
        public int wx;
        public int tb;
        public long ageMs;
        public int tryNo;
        public int etaS = -1;
        public int tempC = -128;
    }

    public interface Callbacks {
        /** 이 폰의 사용자 번호 */
        int user();

        /** 공유 키(없으면 null) */
        byte[] key();

        /** 측정 중이면 true (ping의 busy) */
        boolean busy();

        /** 출발 신호 처리. 반환값은 "tracking" | "dup" | "noperm" | "off". */
        String onDepart(Depart d, long receivedElapsedMs);

        /** 취소 처리. 반환값은 "canceled" | "done" | "unknown". */
        String onCancel(long trip);
    }

    private final Callbacks cb;

    public PhoneServerLogic(Callbacks cb) {
        this.cb = cb;
    }

    private static HttpMini.Response err(int status, String code) {
        return new HttpMini.Response(status, "{\"ok\":0,\"err\":\"" + code + "\"}");
    }

    /** 요청 한 건 처리. receivedElapsedMs는 연결을 받은 순간의 단조 시계 값. */
    public HttpMini.Response handle(HttpMini.Request req, long receivedElapsedMs) {
        String path = req.path == null ? "" : req.path;
        if ("GET".equals(req.method) && "/v1/ping".equals(path)) {
            return new HttpMini.Response(200, FlatJson.obj()
                    .put("ok", 1).put("dev", "phone").put("v", Proto.V)
                    .put("user", cb.user()).put("busy", cb.busy() ? 1 : 0).build());
        }
        boolean depart = "/v1/depart".equals(path);
        boolean cancel = "/v1/cancel".equals(path);
        if (!"POST".equals(req.method) || !(depart || cancel)) return err(404, "path");
        if (req.body.length() > Proto.MAX_BODY) return err(413, "size");

        // 인증: 헤더 사용자, MAC
        byte[] key = cb.key();
        String hUser = req.headers.get("x-tl-user");
        String hMac = req.headers.get("x-tl-mac");
        if (key == null || hUser == null || hMac == null) return err(401, "auth");
        if (!hUser.trim().equals(String.valueOf(cb.user()))) return err(401, "auth");
        if (!Proto.macEquals(hMac, Proto.mac(key, req.method, path, req.body))) return err(401, "auth");

        Map<String, Object> j;
        try {
            j = FlatJson.parse(req.body);
        } catch (FlatJson.ParseError e) {
            return err(400, "json");
        }
        if (!FlatJson.hasLong(j, "trip") || !FlatJson.hasLong(j, "user")) return err(400, "field");
        long trip = FlatJson.getLong(j, "trip", 0);
        if (trip <= 0 || trip > 0xFFFFFFFFL) return err(400, "field");
        if (FlatJson.getLong(j, "user", -1) != cb.user()) return err(401, "auth");

        String st;
        if (depart) {
            if (!FlatJson.hasLong(j, "stop") || !FlatJson.hasLong(j, "wx")
                    || !FlatJson.hasLong(j, "tb") || !FlatJson.hasLong(j, "age_ms")) {
                return err(400, "field");
            }
            Depart d = new Depart();
            d.trip = trip;
            d.user = cb.user();
            d.stop = (int) FlatJson.getLong(j, "stop", 0);
            d.wx = (int) FlatJson.getLong(j, "wx", 0);
            d.tb = (int) FlatJson.getLong(j, "tb", Proto.TB_UNKNOWN);
            d.ageMs = FlatJson.getLong(j, "age_ms", 0);
            d.tryNo = (int) FlatJson.getLong(j, "try", 1);
            d.etaS = (int) FlatJson.getLong(j, "eta_s", -1);
            d.tempC = (int) FlatJson.getLong(j, "tc", -128);
            if (d.stop < 0 || d.stop > Proto.MAX_STOPS || d.wx < 0 || d.wx > 2
                    || !((d.tb >= 0 && d.tb <= 3) || d.tb == Proto.TB_UNKNOWN) || d.ageMs < 0) {
                return err(400, "field");
            }
            if (d.ageMs > Proto.STALE_AGE_MS) return err(400, "stale");
            st = cb.onDepart(d, receivedElapsedMs);
        } else {
            st = cb.onCancel(trip);
        }

        HttpMini.Response resp = new HttpMini.Response(200,
                FlatJson.obj().put("ok", 1).put("trip", trip).put("st", st).build());
        resp.headers.put("X-TL-Mac", Proto.respMac(key, path, hMac.trim().toLowerCase(), resp.body));
        return resp;
    }
}
