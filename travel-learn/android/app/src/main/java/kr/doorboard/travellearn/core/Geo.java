// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

/** 위도·경도 거리 계산과 정류장 설정. */
public final class Geo {
    private Geo() {}

    /** 정류장 한 곳. 좌표가 없으면(has == false) 자동 도착 판정에서 빠지고 수동 확인만 가능하다. */
    public static final class Stop {
        public int id;
        public String name = "";
        public boolean has;
        public double lat;
        public double lon;
        public float radiusM = 30f;
    }

    /** 두 좌표 사이 거리(m). 하버사인 공식. */
    public static double distanceM(double lat1, double lon1, double lat2, double lon2) {
        double r = 6371000.0;
        double p1 = Math.toRadians(lat1);
        double p2 = Math.toRadians(lat2);
        double dp = p2 - p1;
        double dl = Math.toRadians(lon2 - lon1);
        double a = Math.sin(dp / 2) * Math.sin(dp / 2)
                + Math.cos(p1) * Math.cos(p2) * Math.sin(dl / 2) * Math.sin(dl / 2);
        return 2 * r * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    }
}
