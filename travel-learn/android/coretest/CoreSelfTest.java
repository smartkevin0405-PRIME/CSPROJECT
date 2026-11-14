// 생성형 AI(Claude)의 도움을 받아 작성함.
// 앱의 core 패키지(Android API를 쓰지 않는 부분)를 PC의 JVM에서 시험한다.
// 실행: android/coretest/run.sh
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;

import kr.doorboard.travellearn.core.FlatJson;
import kr.doorboard.travellearn.core.Geo;
import kr.doorboard.travellearn.core.HttpMini;
import kr.doorboard.travellearn.core.PhoneServerLogic;
import kr.doorboard.travellearn.core.Proto;
import kr.doorboard.travellearn.core.TripRecord;
import kr.doorboard.travellearn.core.TripTracker;
import kr.doorboard.travellearn.core.SyncRetry;

public class CoreSelfTest {
    static int pass;
    static int fail;

    static void check(String name, boolean ok) {
        if (ok) {
            pass++;
            System.out.println("PASS  " + name);
        } else {
            fail++;
            System.out.println("FAIL  " + name);
        }
    }

    static final byte[] KEY = Proto.parseKey("000102030405060708090a0b0c0d0e0f");

    // ---------------------------------------------------------------- 이동 시뮬레이션 도구

    static final double HOME_LAT = 37.585000;
    static final double HOME_LON = 126.997000;

    /** 북쪽으로 m 미터 이동한 위도 */
    static double north(double lat, double m) {
        return lat + m / 111320.0;
    }

    static double east(double lat, double lon, double m) {
        return lon + m / (111320.0 * Math.cos(Math.toRadians(lat)));
    }

    static class Sink implements TripTracker.Listener {
        TripRecord done;
        int needManual;

        public void onFinished(TripRecord rec) {
            done = rec;
        }

        public void onNeedManual(TripRecord rec) {
            needManual++;
        }
    }

    static List<Geo.Stop> stops(double... latLonRad) {
        List<Geo.Stop> l = new ArrayList<Geo.Stop>();
        for (int i = 0; i + 2 < latLonRad.length; i += 3) {
            Geo.Stop s = new Geo.Stop();
            s.id = i / 3 + 1;
            s.has = !Double.isNaN(latLonRad[i]);
            s.lat = latLonRad[i];
            s.lon = latLonRad[i + 1];
            s.radiusM = (float) latLonRad[i + 2];
            l.add(s);
        }
        return l;
    }

    static TripRecord rec(long trip) {
        TripRecord r = new TripRecord();
        r.trip = trip;
        r.user = 1;
        r.wx = 1;
        r.tb = 0;
        return r;
    }

    /**
     * 집에서 정북으로 distM 떨어진 정류장까지 speed m/s로 걷는다. 2초마다 위치, 잡음 sigma m, 보고 오차 acc m.
     * 반환: 도착 판정 시각과 "반경에 실제로 들어선 시각"의 차(ms). 끝나지 않으면 Long.MIN_VALUE.
     */
    static long walk(TripTracker t, Sink sink, long t0, double distM, double speed, double sigma,
            float acc, double radius, long seed, long maxMs) {
        Random rnd = new Random(seed);
        long trueEnter = (long) ((distM - radius) / speed * 1000.0);
        for (long ms = 2000; ms <= maxMs && sink.done == null; ms += 2000) {
            double d = Math.min(distM, speed * ms / 1000.0);
            double lat = north(HOME_LAT, d + rnd.nextGaussian() * sigma);
            double lon = east(HOME_LAT, HOME_LON, rnd.nextGaussian() * sigma);
            if (ms == 30000) t.onWifiLost();
            t.onFix(lat, lon, acc, t0 + ms);
            if (ms % 6000 == 0) t.onTick(t0 + ms, false);
        }
        if (sink.done == null) return Long.MIN_VALUE;
        return sink.done.durMs - trueEnter;
    }

    // ---- 2차 수정 회귀 시험 도우미 ----
    static final class Cap implements TripTracker.Listener {
        TripRecord done;
        int manual;
        public void onFinished(TripRecord r) { done = r; }
        public void onNeedManual(TripRecord r) { manual++; }
    }

    static TripTracker trackerAt(Cap cap, double stopLat, double stopLon) {
        Geo.Stop s = new Geo.Stop();
        s.id = 1; s.name = "S1"; s.has = true; s.lat = stopLat; s.lon = stopLon; s.radiusM = 30f;
        List<Geo.Stop> stops = new ArrayList<Geo.Stop>();
        stops.add(s);
        TripTracker t = new TripTracker(new TripTracker.Config(), stops, cap);
        TripRecord r = new TripRecord();
        r.trip = 1; r.user = 1;
        t.start(r, 0L);
        return t;
    }

    static HttpMini.Response postSigned(PhoneServerLogic lg, byte[] k, String path, String body) {
        HttpMini.Request q = new HttpMini.Request();
        q.method = "POST"; q.path = path; q.body = body;
        q.headers.put("x-tl-user", "1");
        q.headers.put("x-tl-mac", Proto.mac(k, "POST", path, body));
        return lg.handle(q, 0L);
    }

    public static void main(String[] args) throws Exception {
        // ------------------------------------------------------------ 1. MAC 기준값(통신규격 3장)
        String body = "{\"v\":1,\"user\":1,\"ip\":\"192.168.0.23\",\"port\":8765,\"epoch\":1763004600,"
                + "\"tz\":540,\"pending\":0}";
        String reqMac = Proto.mac(KEY, "POST", "/v1/hello", body);
        check("요청 MAC 기준값", "ae07c41f8c9413e3c0da3d1b4dc02d61".equals(reqMac));
        check("응답 MAC 기준값", "ad7a99b393afc68ae01228bb496c8237".equals(
                Proto.respMac(KEY, "/v1/hello", reqMac, "{\"ok\":1,\"boot\":127,\"reg\":1}")));
        check("MAC 비교: 대소문자 무시, 길이 다르면 거짓",
                Proto.macEquals("AB12", "ab12") && !Proto.macEquals("ab12", "ab1") && !Proto.macEquals(null, "a"));
        check("키 형식 검사", Proto.parseKey("zz0102030405060708090a0b0c0d0e0f") == null
                && Proto.parseKey("0001") == null && KEY != null && KEY.length == 16);
        check("시간대 구간", Proto.tbFromHour(6) == 0 && Proto.tbFromHour(9) == 0 && Proto.tbFromHour(10) == 1
                && Proto.tbFromHour(15) == 1 && Proto.tbFromHour(16) == 2 && Proto.tbFromHour(20) == 2
                && Proto.tbFromHour(21) == 3 && Proto.tbFromHour(0) == 3 && Proto.tbFromHour(5) == 3);
        check("폰 이동 ID는 최상위 비트 1", Proto.isPhoneTrip(Proto.phoneTripId(1763004600L))
                && Proto.phoneTripId(1763004600L) == (0x80000000L | 1763004600L) && !Proto.isPhoneTrip(8323073L));

        // ------------------------------------------------------------ 2. JSON
        Map<String, Object> j = FlatJson.parse(" {\"a\":1, \"b\":\"x\\\"y\" ,\"c\":-5,\"trip\":4294967295} ");
        check("JSON 읽기", FlatJson.getLong(j, "a", 0) == 1 && "x\"y".equals(FlatJson.getString(j, "b", ""))
                && FlatJson.getLong(j, "c", 0) == -5 && FlatJson.getLong(j, "trip", 0) == 4294967295L);
        boolean threw = false;
        try { FlatJson.parse("{\"a\":1.5}"); } catch (FlatJson.ParseError e) { threw = true; }
        check("JSON: 실수 거부", threw);
        threw = false;
        try { FlatJson.parse("{\"a\":{\"b\":1}}"); } catch (FlatJson.ParseError e) { threw = true; }
        check("JSON: 중첩 거부", threw);
        threw = false;
        try { FlatJson.parse("{\"a\":1"); } catch (FlatJson.ParseError e) { threw = true; }
        check("JSON: 잘린 본문 거부", threw);
        check("JSON 쓰기(공백 없음, 순서 유지)",
                "{\"v\":1,\"st\":\"arrived\"}".equals(FlatJson.obj().put("v", 1).put("st", "arrived").build()));
        TripRecord tr = rec(8323073L);
        tr.stop = 2;
        tr.status = Proto.ST_ARRIVED;
        tr.durMs = 412300;
        tr.epoch = 1763004600L;
        check("결과 본문이 규격 예시와 같음", ("{\"v\":1,\"trip\":8323073,\"user\":1,\"stop\":2,\"wx\":1,\"tb\":0,"
                + "\"st\":\"arrived\",\"dur_ms\":412300,\"q\":0,\"src\":0,\"epoch\":1763004600}").equals(tr.toTripJson()));
        tr.status = Proto.ST_CANCELED;
        check("도착이 아니면 dur_ms는 0", tr.toTripJson().contains("\"dur_ms\":0"));

        // ------------------------------------------------------------ 3. 도착 판정
        check("거리 계산(위도 0.001도 ≈ 111m)", Math.abs(Geo.distanceM(37.0, 127.0, 37.001, 127.0) - 111.2) < 0.5);

        double stopLat = north(HOME_LAT, 500);
        long t0 = 1000000L;

        // (a) 정상 보행: 500m, 1.4m/s, GPS 잡음 4m, 오차 8m, 반경 30m
        long worst = 0;
        boolean allArrived = true;
        for (int seed = 1; seed <= 200; seed++) {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(stopLat, HOME_LON, 30), s);
            t.start(rec(seed), t0);
            long err = walk(t, s, t0, 500, 1.4, 4, 8f, 30, seed, 20 * 60000L);
            if (err == Long.MIN_VALUE || !Proto.ST_ARRIVED.equals(s.done.status) || s.done.stop != 1
                    || s.done.q != 0) {
                allArrived = false;
            } else {
                worst = Math.max(worst, Math.abs(err));
            }
        }
        check("(a) 정상 보행 200회 모두 자동 도착(q=0)", allArrived);
        check("(a) 도착 시각 오차 최대 " + worst + "ms ≤ 12초", worst <= 12000);

        // (b) 오차 큰 위치만 들어옴(60m): 정류장 근처에 머물러야 q=1로 도착
        {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(stopLat, HOME_LON, 30), s);
            t.start(rec(1), t0);
            long err = walk(t, s, t0, 500, 1.4, 15, 60f, 30, 7, 20 * 60000L);
            check("(b) 오차 큰 위치 → q=1 도착", err != Long.MIN_VALUE && s.done.q == 1
                    && Proto.ST_ARRIVED.equals(s.done.status));
            check("(b) 도착 시각이 실제보다 3분 이상 벗어나지 않음", Math.abs(err) < 180000);
        }

        // (c) 쓸 만한 위치가 전혀 없음 → 수동 확인 요청 → 수동 도착(q=2)
        {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(stopLat, HOME_LON, 30), s);
            TripRecord r = rec(1);
            r.stopHint = 1;
            t.start(r, t0);
            for (long ms = 5000; ms <= 200000; ms += 5000) {
                t.onFix(HOME_LAT, HOME_LON, 500f, t0 + ms); // 오차 500m: 버려짐
                t.onTick(t0 + ms, false);
            }
            check("(c) 수동 확인 요청은 한 번만", s.needManual == 1 && s.done == null);
            t.manualArrive(t0 + 400000, 0);
            check("(c) 수동 도착 q=2, 길이 400초", s.done != null && s.done.q == 2 && s.done.durMs == 400000
                    && s.done.stop == 1 && Proto.ST_ARRIVED.equals(s.done.status));
        }

        // (d) 집을 떠나지 않음 → 4분 뒤 취소
        {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(stopLat, HOME_LON, 30), s);
            t.start(rec(1), t0);
            for (long ms = 5000; ms <= 300000 && s.done == null; ms += 5000) {
                t.onFix(HOME_LAT, HOME_LON, 20f, t0 + ms);
                t.onTick(t0 + ms, true);
            }
            check("(d) 미출발 취소", s.done != null && Proto.ST_CANCELED.equals(s.done.status)
                    && s.done.durMs == 0 && s.done.stop == 0);
        }

        // (e) 시간 초과
        {
            Sink s = new Sink();
            TripTracker.Config c = new TripTracker.Config();
            c.maxTripMs = 10 * 60000L;
            TripTracker t = new TripTracker(c, stops(stopLat, HOME_LON, 30), s);
            t.start(rec(1), t0);
            t.onWifiLost();
            for (long ms = 5000; ms <= 11 * 60000L && s.done == null; ms += 5000) {
                t.onFix(north(HOME_LAT, 200), HOME_LON, 10f, t0 + ms); // 중간에서 멈춤
                t.onTick(t0 + ms, false);
            }
            check("(e) 시간 초과", s.done != null && Proto.ST_TIMEOUT.equals(s.done.status));
        }

        // (f) 나갔다가 도착 전에 되돌아옴 → 취소. Wi-Fi가 끊긴 적 없으면 hello 성공만으로는 취소하지 않음
        {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(stopLat, HOME_LON, 30), s);
            t.start(rec(1), t0);
            t.onHomeReturned();
            check("(f) Wi-Fi가 끊긴 적 없으면 유지", s.done == null && t.isActive());
            t.onWifiLost();
            t.onHomeReturned();
            check("(f) 도착 전 귀가 → 취소", s.done != null && Proto.ST_CANCELED.equals(s.done.status));
        }

        // (g) 출발 전 위치·순서 뒤바뀐 위치는 무시, 너무 이른 도착은 인정 안 함
        {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(stopLat, HOME_LON, 30), s);
            t.start(rec(1), t0);
            t.onFix(stopLat, HOME_LON, 5f, t0 - 5000);
            t.onFix(stopLat, HOME_LON, 5f, t0 - 3000);
            check("(g) 출발 전 위치 무시", s.done == null && t.fixCount() == 0);
            t.onFix(stopLat, HOME_LON, 5f, t0 + 4000);
            t.onFix(stopLat, HOME_LON, 5f, t0 + 6000);
            check("(g) 20초 안의 도착은 인정 안 함", s.done == null);
            t.onFix(stopLat, HOME_LON, 5f, t0 + 5000);
            check("(g) 순서가 뒤바뀐 위치 무시", t.fixCount() == 2);
        }

        // (h) 정류장 2곳: 실제로 간 곳(동쪽 400m)을 고르고, 다른 정류장 옆 40m를 지나가도 오판하지 않음
        {
            Sink s = new Sink();
            double s1Lat = north(HOME_LAT, 200);
            double s1Lon = east(HOME_LAT, HOME_LON, 70);   // 경로(정동)에서 북쪽 200m: 닿지 않음
            double s2Lon = east(HOME_LAT, HOME_LON, 400);
            double nearLat = north(HOME_LAT, 40);          // 경로 중간 150m 지점에서 북쪽 40m
            double nearLon = east(HOME_LAT, HOME_LON, 150);
            List<Geo.Stop> l = stops(s1Lat, s1Lon, 30, HOME_LAT, s2Lon, 30, nearLat, nearLon, 30);
            TripTracker t = new TripTracker(new TripTracker.Config(), l, s);
            t.start(rec(1), t0);
            Random rnd = new Random(3);
            for (long ms = 2000; ms <= 600000 && s.done == null; ms += 2000) {
                double d = Math.min(400, 1.4 * ms / 1000.0);
                t.onFix(north(HOME_LAT, rnd.nextGaussian() * 3), east(HOME_LAT, HOME_LON, d + rnd.nextGaussian() * 3),
                        8f, t0 + ms);
            }
            check("(h) 정류장 2 도착, 옆 정류장은 오판 없음", s.done != null && s.done.stop == 2 && s.done.q == 0);
        }

        // (i) 좌표 없는 정류장만 있음 → 자동 도착 없음, 수동만
        {
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), stops(Double.NaN, Double.NaN, 30), s);
            t.start(rec(1), t0);
            for (long ms = 2000; ms <= 120000; ms += 2000) t.onFix(HOME_LAT, HOME_LON, 5f, t0 + ms);
            check("(i) 좌표 없는 정류장은 자동 판정 안 함", s.done == null);
            t.manualArrive(t0 + 10000, 0);
            check("(i) 20초 미만 수동 도착은 취소 처리", s.done != null && Proto.ST_CANCELED.equals(s.done.status));
        }

        // 수동 도착: 빈 슬롯을 건너뛰고, 목적지가 여러 곳이면 선택 전에는 저장하지 않는다.
        {
            List<Geo.Stop> l = stops(Double.NaN, Double.NaN, 30, Double.NaN, Double.NaN, 30);
            l.get(1).name = "실제 지하철역";
            Sink s = new Sink();
            TripTracker t = new TripTracker(new TripTracker.Config(), l, s);
            t.start(rec(1), t0);
            check("수동 도착: 2번만 설정되어 있으면 2번에 저장", t.manualArrive(t0 + 300000, 0)
                    && s.done != null && s.done.stop == 2);
            l.get(0).name = "버스 정류장";
            s = new Sink(); t = new TripTracker(new TripTracker.Config(), l, s); t.start(rec(2), t0);
            check("수동 도착: 여러 정류장 중 임의 선택 금지", !t.manualArrive(t0 + 300000, 0)
                    && t.isActive() && s.done == null);
            check("수동 도착: 없는 정류장 거부", !t.manualArrive(t0 + 300000, 4) && t.isActive());
            check("수동 도착: 명시적으로 고른 2번에 저장", t.manualArrive(t0 + 300000, 2)
                    && s.done.stop == 2 && s.done.q == 2);
            l.get(0).name = ""; l.get(1).name = "";
            s = new Sink(); t = new TripTracker(new TripTracker.Config(), l, s); t.start(rec(3), t0);
            check("수동 도착: 설정된 목적지가 없으면 저장 안 함", !t.manualArrive(t0 + 300000, 0)
                    && t.isActive());
        }
        {
            SyncRetry retry = new SyncRetry();
            check("등록 재시도: 기록 유무와 무관하게 5·15·60초", retry.nextDelayMs() == 5000
                    && retry.nextDelayMs() == 15000 && retry.nextDelayMs() == 60000);
            boolean capped = true;
            for (int i = 0; i < 20; i++) capped &= retry.nextDelayMs() == 300000;
            check("등록 재시도: 5분 간격으로 계속 유지", capped);
            retry.reset();
            check("연결 성공·새 Wi-Fi 연결 후 처음 간격으로 복귀", retry.nextDelayMs() == 5000);
        }

        // ------------------------------------------------------------ 4. 폰 서버 요청 검증
        final List<PhoneServerLogic.Depart> got = new ArrayList<PhoneServerLogic.Depart>();
        final long[] canceled = new long[1];
        PhoneServerLogic logic = new PhoneServerLogic(new PhoneServerLogic.Callbacks() {
            public int user() { return 1; }
            public byte[] key() { return KEY; }
            public boolean busy() { return false; }
            public String onDepart(PhoneServerLogic.Depart d, long ms) {
                for (int i = 0; i < got.size(); i++) if (got.get(i).trip == d.trip) return "dup";
                got.add(d);
                return "tracking";
            }
            public String onCancel(long trip) {
                canceled[0] = trip;
                return "canceled";
            }
        });
        String dep = "{\"v\":1,\"trip\":8323073,\"user\":1,\"stop\":0,\"wx\":1,\"tb\":0,\"age_ms\":350,\"try\":1}";
        HttpMini.Response r1 = logic.handle(req("POST", "/v1/depart", dep, 1, KEY), 5000);
        String m1 = Proto.mac(KEY, "POST", "/v1/depart", dep);
        check("출발 신호 수락", r1.status == 200 && r1.body.equals("{\"ok\":1,\"trip\":8323073,\"st\":\"tracking\"}")
                && got.size() == 1 && got.get(0).ageMs == 350 && got.get(0).tb == 0);
        check("응답 MAC이 규격대로", Proto.respMac(KEY, "/v1/depart", m1, r1.body).equals(r1.headers.get("X-TL-Mac")));
        check("같은 이동 ID는 dup", logic.handle(req("POST", "/v1/depart", dep, 1, KEY), 6000).body.contains("\"st\":\"dup\""));
        byte[] bad = Proto.parseKey("ffffffffffffffffffffffffffffffff");
        HttpMini.Response r2 = logic.handle(req("POST", "/v1/depart", dep.replace("8323073", "8323074"), 1, bad), 5000);
        check("키가 다르면 401, 응답 MAC 없음, 처리 안 함", r2.status == 401 && r2.headers.isEmpty() && got.size() == 1);
        HttpMini.Request tamper = req("POST", "/v1/depart", dep, 1, KEY);
        tamper.body = dep.replace("\"age_ms\":350", "\"age_ms\":351");
        check("본문 변조 → 401", logic.handle(tamper, 5000).status == 401);
        check("헤더 사용자 불일치 → 401", logic.handle(req("POST", "/v1/depart", dep, 2, KEY), 5000).status == 401);
        String dep2 = dep.replace("\"user\":1", "\"user\":2").replace("8323073", "8323075");
        check("본문 사용자 불일치 → 401", logic.handle(req("POST", "/v1/depart", dep2, 1, KEY), 5000).status == 401);
        String stale = dep.replace("\"age_ms\":350", "\"age_ms\":120001").replace("8323073", "8323076");
        HttpMini.Response r3 = logic.handle(req("POST", "/v1/depart", stale, 1, KEY), 5000);
        check("오래된 출발 신호 → 400 stale", r3.status == 400 && r3.body.contains("stale"));
        String nofield = "{\"v\":1,\"trip\":8323077,\"user\":1}";
        check("필수 키 없음 → 400 field", logic.handle(req("POST", "/v1/depart", nofield, 1, KEY), 5000).body.contains("field"));
        check("형식 오류 → 400 json", logic.handle(req("POST", "/v1/depart", "{oops", 1, KEY), 5000).body.contains("json"));
        check("없는 경로 → 404", logic.handle(req("POST", "/v1/nope", "{}", 1, KEY), 5000).status == 404);
        String unk = dep.replace("\"tb\":0", "\"tb\":255").replace("8323073", "8323078");
        logic.handle(req("POST", "/v1/depart", unk, 1, KEY), 5000);
        check("tb 255(모름) 허용", got.size() == 2 && got.get(1).tb == 255);
        String opt = "{\"v\":1,\"trip\":8323079,\"user\":1,\"stop\":2,\"wx\":2,\"tb\":3,\"age_ms\":0,\"try\":4,\"eta_s\":420,\"tc\":-3,\"zz\":9}";
        logic.handle(req("POST", "/v1/depart", opt, 1, KEY), 5000);
        check("선택 키·모르는 키 처리", got.size() == 3 && got.get(2).etaS == 420 && got.get(2).tempC == -3
                && got.get(2).tryNo == 4 && got.get(2).stop == 2);
        String can = "{\"v\":1,\"trip\":8323073,\"user\":1}";
        HttpMini.Response r4 = logic.handle(req("POST", "/v1/cancel", can, 1, KEY), 5000);
        check("취소 처리", r4.status == 200 && canceled[0] == 8323073L && r4.body.contains("\"st\":\"canceled\""));
        HttpMini.Request ping = new HttpMini.Request();
        ping.method = "GET";
        ping.path = "/v1/ping";
        check("ping은 인증 없이 응답", logic.handle(ping, 0).body.equals("{\"ok\":1,\"dev\":\"phone\",\"v\":1,\"user\":1,\"busy\":0}"));

        // ---- 2차 수정: B04, B08, B09, B06 ----
        double H = 37.5, L = 127.0;
        double S = H + 500 / 111320.0;            // 집에서 북쪽 500m
        double S2 = H + 20 / 111320.0;            // 집에서 북쪽 20m

        Cap c1 = new Cap();
        TripTracker t1 = trackerAt(c1, S, L);
        t1.onFix(H, L, 10f, 1000);
        t1.onWifiLost();
        t1.onFix(S, L, 10f, 30000);
        t1.onFix(S, L, 10f, 600000);              // 570초 공백 뒤
        check("B04 긴 공백 뒤 표본은 연속으로 보지 않음", c1.done == null && t1.isActive());
        t1.onFix(S, L, 10f, 602000);
        check("B04 공백 뒤 새로 2회 연속이면 도착, 시간은 600초", c1.done != null && c1.done.durMs == 600000L);

        Cap c2 = new Cap();
        TripTracker t2 = trackerAt(c2, S2, L);
        t2.onFix(H, L, 10f, 25000);
        t2.onFix(H, L, 10f, 27000);
        check("B08 집을 떠난 근거 없이 제자리면 도착 아님", c2.done == null && t2.isActive());
        t2.onWifiLost();
        t2.onFix(H, L, 10f, 41000);
        t2.onFix(H, L, 10f, 43000);
        check("B08 Wi-Fi가 끊긴 뒤에는 도착 판정", c2.done != null && c2.done.durMs == 41000L);

        Cap c3 = new Cap();
        TripTracker t3 = trackerAt(c3, S, L);
        t3.onWifiLost();
        for (long ms = 10000; ms <= 30000; ms += 2000) t3.onFix(S, L, 10f, ms);
        check("B09 20초 전 진입은 버리고 20초부터 다시 세어 도착", c3.done != null && c3.done.durMs == 20000L);

        Cap c4 = new Cap();
        TripTracker t4 = trackerAt(c4, S, L);
        t4.restoreState(true, false);
        t4.onHomeReturned();
        check("B06 Wi-Fi가 끊긴 적 없으면 귀가 취소 안 함", c4.done == null && t4.isActive());
        t4.restoreState(true, true);
        t4.onHomeReturned();
        check("B06 Wi-Fi가 끊겼다가 돌아오면 취소", c4.done != null && Proto.ST_CANCELED.equals(c4.done.status));

        // ---- 2차 수정: B18 ----
        final byte[] k18 = Proto.parseKey("000102030405060708090a0b0c0d0e0f");
        final int[] departs18 = {0};
        PhoneServerLogic lg18 = new PhoneServerLogic(new PhoneServerLogic.Callbacks() {
            public int user() { return 1; }
            public byte[] key() { return k18; }
            public boolean busy() { return false; }
            public String onDepart(PhoneServerLogic.Depart d, long rx) { departs18[0]++; return "tracking"; }
            public String onCancel(long trip) { return "unknown"; }
        });
        check("B18 v=99 거절", postSigned(lg18, k18, "/v1/depart",
                "{\"v\":99,\"trip\":5,\"user\":1,\"stop\":0,\"wx\":0,\"tb\":0,\"age_ms\":0}").status == 400);
        check("B18 v 없음 거절", postSigned(lg18, k18, "/v1/depart",
                "{\"trip\":6,\"user\":1,\"stop\":0,\"wx\":0,\"tb\":0,\"age_ms\":0}").status == 400);
        check("B18 큰 정수 stop 거절", postSigned(lg18, k18, "/v1/depart",
                "{\"v\":1,\"trip\":7,\"user\":1,\"stop\":4294967298,\"wx\":0,\"tb\":0,\"age_ms\":0}").status == 400);
        check("B18 정상 요청은 통과", postSigned(lg18, k18, "/v1/depart",
                "{\"v\":1,\"trip\":8,\"user\":1,\"stop\":0,\"wx\":0,\"tb\":0,\"age_ms\":0}").status == 200
                && departs18[0] == 1);

        // ---- 2차 수정: B16 ----
        InputStream slowCr = new InputStream() {
            public int read() throws IOException {
                try { Thread.sleep(100); } catch (InterruptedException e) { throw new IOException("중단"); }
                return '\r';
            }
        };
        long t16 = System.nanoTime();
        boolean timedOut16 = false;
        try {
            HttpMini.readRequest(slowCr, Proto.MAX_BODY, System.nanoTime() + 500L * 1000000L);
        } catch (IOException e) {
            timedOut16 = true;
        }
        long took16 = (System.nanoTime() - t16) / 1000000L;
        check("B16 느린 CR 입력은 마감 시각에 끊김", timedOut16 && took16 < 1500);
        InputStream manyCr = new InputStream() {
            public int read() { return '\r'; }
        };
        boolean big16 = false;
        try {
            HttpMini.readRequest(manyCr, Proto.MAX_BODY, 0L);
        } catch (HttpMini.HttpError e) {
            big16 = true;
        } catch (IOException e) {
            big16 = false;
        }
        check("B16 CR만 끝없이 와도 헤더 바이트 상한에서 끊김", big16);

        System.out.println("----------------------------------------");
        System.out.println("통과 " + pass + " / 실패 " + fail);
        if (fail > 0) System.exit(1);
    }

    static HttpMini.Request req(String method, String path, String body, int user, byte[] key) {
        HttpMini.Request r = new HttpMini.Request();
        r.method = method;
        r.path = path;
        r.body = body;
        r.headers.put("x-tl-user", String.valueOf(user));
        r.headers.put("x-tl-mac", Proto.mac(key, method, path, body));
        return r;
    }
}
