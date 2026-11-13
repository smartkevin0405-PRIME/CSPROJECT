// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;

import kr.doorboard.travellearn.core.Geo;
import kr.doorboard.travellearn.core.Proto;

/** 앱 설정과 진행 중인 이동의 임시 저장. */
public final class Prefs {
    private Prefs() {}

    private static SharedPreferences sp(Context c) {
        return c.getApplicationContext().getSharedPreferences("tl", Context.MODE_PRIVATE);
    }

    public static int user(Context c) { return sp(c).getInt("user", 1); }
    public static String boardHost(Context c) { return sp(c).getString("board_host", ""); }
    public static int boardPort(Context c) { return sp(c).getInt("board_port", Proto.BOARD_PORT); }
    public static int phonePort(Context c) { return sp(c).getInt("phone_port", Proto.PHONE_PORT); }
    public static String keyHex(Context c) { return sp(c).getString("key_hex", ""); }
    public static byte[] key(Context c) { return Proto.parseKey(keyHex(c)); }
    /** 사용자가 "대기 시작"을 눌러 둔 상태인지(재부팅 후 자동 시작 여부). */
    public static boolean enabled(Context c) { return sp(c).getBoolean("enabled", false); }
    public static int maxTripMin(Context c) {
        return Math.max(5, Math.min(60, sp(c).getInt("max_trip_min", 30)));   // B19
    }

    public static void setEnabled(Context c, boolean v) {
        sp(c).edit().putBoolean("enabled", v).apply();
    }

    public static void saveConn(Context c, int user, String host, int boardPort, String keyHex,
            int maxTripMin) {
        sp(c).edit().putInt("user", user).putString("board_host", host.trim())
                .putInt("board_port", boardPort).putString("key_hex", keyHex.trim().toLowerCase())
                .putInt("max_trip_min", maxTripMin).apply();
    }

    public static List<Geo.Stop> stops(Context c) {
        SharedPreferences p = sp(c);
        List<Geo.Stop> out = new ArrayList<Geo.Stop>();
        for (int i = 1; i <= Proto.MAX_STOPS; i++) {
            Geo.Stop s = new Geo.Stop();
            s.id = i;
            s.name = p.getString("stop" + i + "_name", "");
            s.has = p.getBoolean("stop" + i + "_has", false);
            s.lat = Double.longBitsToDouble(p.getLong("stop" + i + "_lat", 0));
            s.lon = Double.longBitsToDouble(p.getLong("stop" + i + "_lon", 0));
            s.radiusM = p.getFloat("stop" + i + "_r", 30f);
            out.add(s);
        }
        return out;
    }

    public static void saveStop(Context c, Geo.Stop s) {
        sp(c).edit().putString("stop" + s.id + "_name", s.name)
                .putBoolean("stop" + s.id + "_has", s.has)
                .putLong("stop" + s.id + "_lat", Double.doubleToLongBits(s.lat))
                .putLong("stop" + s.id + "_lon", Double.doubleToLongBits(s.lon))
                .putFloat("stop" + s.id + "_r", s.radiusM).apply();
    }

    // ---- 진행 중인 이동(프로세스가 죽었다 살아날 때 이어서 재기 위함) ----

    public static void saveActive(Context c, long trip, long t0Elapsed, int bootCount,
            boolean leftHome, boolean wifiLost) {
        sp(c).edit().putLong("active_trip", trip).putLong("active_t0", t0Elapsed)
                .putInt("active_boot", bootCount).putBoolean("active_left", leftHome)
                .putBoolean("active_wifi_lost", wifiLost).apply();   // 두 상태를 따로 저장 (B06)
    }

    public static void clearActive(Context c) {
        sp(c).edit().remove("active_trip").remove("active_t0").remove("active_boot")
                .remove("active_left").remove("active_wifi_lost").apply();
    }

    public static long activeTrip(Context c) { return sp(c).getLong("active_trip", 0); }
    public static long activeT0(Context c) { return sp(c).getLong("active_t0", 0); }
    public static int activeBoot(Context c) { return sp(c).getInt("active_boot", -1); }
    public static boolean activeLeft(Context c) { return sp(c).getBoolean("active_left", false); }
    public static boolean activeWifiLost(Context c) { return sp(c).getBoolean("active_wifi_lost", false); }
}
