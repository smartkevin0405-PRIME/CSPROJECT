// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

import java.util.List;
import java.util.ArrayList;

/**
 * 이동 한 건의 상태 기계. Android API에 의존하지 않으므로 PC의 JVM에서 그대로 시험할 수 있다.
 *
 * 시간은 모두 "부팅 후 경과 ms"(Android의 elapsedRealtime) 기준이다.
 * 호출하는 쪽(서비스)이 위치 수신·타이머·Wi-Fi 이벤트를 넘겨주면, 도착·취소·시간 초과를 판단해 Listener로 알린다.
 */
public final class TripTracker {

    public interface Listener {
        /** 이동이 끝남(도착·취소·시간 초과). 위치 측정을 멈추고 기록을 저장해야 한다. */
        void onFinished(TripRecord rec);

        /** 위치가 계속 부정확함. 사용자에게 수동 도착 확인을 요청해야 한다(한 번만 호출). */
        void onNeedManual(TripRecord rec);
    }

    public static final class Config {
        /** 이 시간 안에 도착하지 못하면 시간 초과. */
        public long maxTripMs = 30 * 60 * 1000L;
        /** 출발 신호 후 이 시간이 지나도 집 Wi-Fi에 그대로 있으면 외출하지 않은 것으로 본다. */
        public long notLeftMs = 4 * 60 * 1000L;
        /** 이 값 이하의 오차는 양호한 위치로 본다(m). */
        public float goodAccM = 50f;
        /** 이 값을 넘는 오차의 위치는 버린다(m). */
        public float maxAccM = 100f;
        /** 오차가 큰 위치로 도착을 인정하려면 정류장 근처에 이만큼 머물러야 한다. */
        public long lowAccDwellMs = 20 * 1000L;
        /** 쓸 만한 위치가 이만큼 안 들어오면 수동 확인을 요청한다. */
        public long needManualMs = 90 * 1000L;
        /** 이보다 짧은 이동은 도착으로 인정하지 않는다(통신규격의 최소 20초). */
        public long minTripMs = 20 * 1000L;
        /** 첫 위치에서 이만큼 멀어지면 집을 떠난 것으로 본다(m). */
        public float leftHomeM = 60f;
    }

    private final Config cfg;
    private final List<Geo.Stop> stops;
    private final Listener listener;

    private TripRecord rec;
    private long t0;
    private boolean leftHome;
    private boolean wifiLost;
    private boolean needManualSent;
    private long lastUsableFixMs;
    private long lastFixMs;
    private int fixCount;

    private boolean haveFirst;
    private double firstLat;
    private double firstLon;

    // 연속 판정 상태
    private int insideStop;
    private int insideCount;
    private long insideFirstMs;
    private int nearStop;
    private int nearCount;
    private long nearFirstMs;

    public TripTracker(Config cfg, List<Geo.Stop> stops, Listener listener) {
        this.cfg = cfg;
        this.stops = stops;
        this.listener = listener;
    }

    public boolean isActive() {
        return rec != null;
    }

    public TripRecord current() {
        return rec;
    }

    public long t0() {
        return t0;
    }

    public boolean hasLeftHome() {
        return leftHome;
    }

    public int fixCount() {
        return fixCount;
    }

    /** 측정 시작. t0ElapsedMs는 문 열림 시점(수신 시각 - age_ms). */
    public void start(TripRecord r, long t0ElapsedMs) {
        rec = r;
        rec.status = Proto.ST_ACTIVE;
        t0 = t0ElapsedMs;
        leftHome = false;
        wifiLost = false;
        needManualSent = false;
        lastUsableFixMs = t0ElapsedMs;
        lastFixMs = 0;
        fixCount = 0;
        haveFirst = false;
        resetRuns();
    }

    /** 재시작 후 이어서 측정할 때: 이미 집을 떠났는지 여부를 복원한다. */
    public void restoreLeftHome(boolean v) {
        leftHome = v;
        wifiLost = v;
    }

    private void resetRuns() {
        insideStop = 0;
        insideCount = 0;
        nearStop = 0;
        nearCount = 0;
    }

    /** 집 Wi-Fi가 끊어짐 = 집을 떠났다는 신호. */
    public void onWifiLost() {
        if (rec != null) {
            leftHome = true;
            wifiLost = true;
        }
    }

    /** 위치 한 점 입력. fixElapsedMs는 위치가 측정된 시각(전달 시각이 아님). */
    public void onFix(double lat, double lon, float accM, long fixElapsedMs) {
        if (rec == null) return;
        if (fixElapsedMs < t0) return;            // 출발 전에 받아 둔 옛 위치
        if (fixElapsedMs <= lastFixMs) return;    // 순서가 뒤바뀐 위치
        lastFixMs = fixElapsedMs;
        fixCount++;
        if (accM <= 0 || accM > cfg.maxAccM) {
            resetRuns();
            return;
        }
        lastUsableFixMs = fixElapsedMs;

        if (!haveFirst) {
            haveFirst = true;
            firstLat = lat;
            firstLon = lon;
        } else if (!leftHome && accM <= cfg.goodAccM
                && Geo.distanceM(firstLat, firstLon, lat, lon) > cfg.leftHomeM) {
            leftHome = true;
        }

        // 가장 가까운 정류장 찾기
        Geo.Stop best = null;
        double bestD = Double.MAX_VALUE;
        for (int i = 0; i < stops.size(); i++) {
            Geo.Stop s = stops.get(i);
            if (!s.has) continue;
            double d = Geo.distanceM(s.lat, s.lon, lat, lon);
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        if (best == null) return;

        boolean inside = accM <= cfg.goodAccM && bestD <= best.radiusM;
        boolean near = bestD <= best.radiusM + accM;

        if (inside) {
            if (insideStop == best.id) {
                insideCount++;
            } else {
                insideStop = best.id;
                insideCount = 1;
                insideFirstMs = fixElapsedMs;
            }
        } else {
            insideStop = 0;
            insideCount = 0;
        }
        if (near) {
            if (nearStop == best.id) {
                nearCount++;
            } else {
                nearStop = best.id;
                nearCount = 1;
                nearFirstMs = fixElapsedMs;
            }
        } else {
            nearStop = 0;
            nearCount = 0;
        }

        // 양호한 위치 2회 연속 → 첫 번째 시각을 도착 시각으로 한다.
        if (insideCount >= 2 && insideFirstMs - t0 >= cfg.minTripMs) {
            finishArrived(insideStop, insideFirstMs, 0);
            return;
        }
        // 오차가 큰 위치라도 정류장 근처에 일정 시간 머물면 도착으로 본다.
        if (nearCount >= 3 && fixElapsedMs - nearFirstMs >= cfg.lowAccDwellMs
                && nearFirstMs - t0 >= cfg.minTripMs) {
            finishArrived(nearStop, nearFirstMs, 1);
        }
    }

    /** 주기 호출(5초 정도). 시간 초과·미출발·수동 확인 요청을 판단한다. */
    public void onTick(long nowElapsedMs, boolean onHomeWifi) {
        if (rec == null) return;
        long el = nowElapsedMs - t0;
        if (el >= cfg.maxTripMs) {
            finish(Proto.ST_TIMEOUT, 0, 0, 0, "시간 초과");
            return;
        }
        if (!leftHome && onHomeWifi && el >= cfg.notLeftMs) {
            finish(Proto.ST_CANCELED, 0, 0, 0, "집을 떠나지 않음");
            return;
        }
        if (!needManualSent && el >= 30000L && nowElapsedMs - lastUsableFixMs >= cfg.needManualMs) {
            needManualSent = true;
            listener.onNeedManual(rec);
        }
    }

    /** 집 Wi-Fi가 끊겼다가 도착 전에 현관판과 다시 통신됨 = 되돌아옴. */
    public void onHomeReturned() {
        if (rec != null && wifiLost) finish(Proto.ST_CANCELED, 0, 0, 0, "도착 전 귀가");
    }

    /** 수동 확인에 쓸 수 있는 설정된 정류장. 빈 슬롯은 제외한다. */
    public List<Geo.Stop> manualStops() {
        List<Geo.Stop> out = new ArrayList<Geo.Stop>();
        for (Geo.Stop s : stops) {
            if (s.has || !s.name.trim().isEmpty()) out.add(s);
        }
        return out;
    }

    private boolean knownStop(int id) {
        for (Geo.Stop s : manualStops()) if (s.id == id) return true;
        return false;
    }

    /** 목적지가 불명확하면 false를 반환해 선택을 요청한다. 임의로 1번에 저장하지 않는다. */
    public boolean manualArrive(long nowElapsedMs, int stopId) {
        if (rec == null) return false;
        if (nowElapsedMs - t0 < cfg.minTripMs) {
            finish(Proto.ST_CANCELED, 0, 0, 0, "너무 짧음");
            return true;
        }
        int sid = stopId;
        if (sid > 0 && !knownStop(sid)) return false;
        if (sid <= 0 && knownStop(rec.stopHint)) sid = rec.stopHint;
        if (sid <= 0 && knownStop(nearStop)) sid = nearStop;
        if (sid <= 0) {
            List<Geo.Stop> choices = manualStops();
            if (choices.size() != 1) return false;
            sid = choices.get(0).id;
        }
        finishArrived(sid, nowElapsedMs, 2);
        return true;
    }

    public void cancel(String why) {
        if (rec != null) finish(Proto.ST_CANCELED, 0, 0, 0, why);
    }

    /** 재부팅·강제 종료 등으로 이어서 잴 수 없음. */
    public void lost(String why) {
        if (rec != null) finish(Proto.ST_LOST, 0, 0, 0, why);
    }

    private void finishArrived(int stopId, long arriveMs, int q) {
        finish(Proto.ST_ARRIVED, stopId, arriveMs - t0, q, "");
    }

    private void finish(String status, int stopId, long durMs, int q, String note) {
        TripRecord r = rec;
        rec = null;
        r.status = status;
        r.stop = stopId;
        r.durMs = durMs;
        r.q = q;
        r.note = note;
        listener.onFinished(r);
    }
}
