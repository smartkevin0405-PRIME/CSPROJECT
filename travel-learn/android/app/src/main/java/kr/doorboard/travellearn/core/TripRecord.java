// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

/** 이동 한 건의 기록. 통신규격 4.3(출발 신호)과 4.5(결과)의 필드를 그대로 담는다. */
public final class TripRecord {
    public long trip;
    public int user;
    /** 출발 신호에 실린 예상 정류장(0 = 미정). */
    public int stopHint;
    /** 실제 도착한 정류장 슬롯(도착 못 하면 0). */
    public int stop;
    public int wx;
    public int tb;
    /** Proto.ST_* */
    public String status = Proto.ST_ACTIVE;
    public long durMs;
    /** 0 자동(양호), 1 자동(오차 큼), 2 수동 확인 */
    public int q;
    /** 0 현관판 신호, 1 폰 수동 출발 */
    public int src;
    /** 출발 시각(유닉스 초) */
    public long epoch;
    public int etaS = -1;
    public int tempC = -128;
    /** 폰 내부 메모(취소 이유 등). 망으로 보내지 않는다. */
    public String note = "";
    public boolean synced;
    /** 이 기기에서 기록을 처음 만든 벽시계 시각(ms). DB에서 읽을 때만 채운다 (N4). */
    public long createdMs;

    public boolean isFinished() {
        return !Proto.ST_ACTIVE.equals(status) && !Proto.ST_WAIT_USER.equals(status);
    }

    /** 4.5 요청 본문. */
    public String toTripJson() {
        return FlatJson.obj()
                .put("v", Proto.V)
                .put("trip", trip)
                .put("user", user)
                .put("stop", stop)
                .put("wx", wx)
                .put("tb", tb)
                .put("st", status)
                .put("dur_ms", Proto.ST_ARRIVED.equals(status) ? durMs : 0)
                .put("q", q)
                .put("src", src)
                .put("epoch", epoch)
                .build();
    }
}
