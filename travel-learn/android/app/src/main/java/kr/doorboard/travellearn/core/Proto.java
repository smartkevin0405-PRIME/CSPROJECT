// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

import java.nio.charset.StandardCharsets;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/** 통신규격 v1의 상수와 MAC 계산. docs/통신규격.md 2~3장과 1:1 대응. */
public final class Proto {
    private Proto() {}

    public static final int V = 1;
    public static final int BOARD_PORT = 8080;
    public static final int PHONE_PORT = 8765;
    public static final int MAX_BODY = 480;
    public static final int TB_UNKNOWN = 255;
    public static final long STALE_AGE_MS = 120000L;
    public static final int MAX_USERS = 4;
    public static final int MAX_STOPS = 4;

    public static final String ST_ARRIVED = "arrived";
    public static final String ST_CANCELED = "canceled";
    public static final String ST_TIMEOUT = "timeout";
    public static final String ST_LOST = "lost";
    /** 폰 내부 전용 상태(망으로 보내지 않음). */
    public static final String ST_ACTIVE = "active";
    public static final String ST_WAIT_USER = "waituser";

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    /** 32자리 16진수 → 16바이트. 형식이 틀리면 null. */
    public static byte[] parseKey(String hex) {
        if (hex == null) return null;
        String h = hex.trim();
        if (h.length() != 32) return null;
        byte[] out = new byte[16];
        for (int i = 0; i < 16; i++) {
            int hi = Character.digit(h.charAt(i * 2), 16);
            int lo = Character.digit(h.charAt(i * 2 + 1), 16);
            if (hi < 0 || lo < 0) return null;
            out[i] = (byte) ((hi << 4) | lo);
        }
        return out;
    }

    static String hmacHex16(byte[] key, String msg) {
        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(key, "HmacSHA256"));
            byte[] d = mac.doFinal(msg.getBytes(StandardCharsets.UTF_8));
            char[] out = new char[32];
            for (int i = 0; i < 16; i++) {
                out[i * 2] = HEX[(d[i] >> 4) & 0xF];
                out[i * 2 + 1] = HEX[d[i] & 0xF];
            }
            return new String(out);
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    /** 요청 MAC: HMAC(키, 메서드 + " " + 경로 + "\n" + 본문)의 앞 16바이트. */
    public static String mac(byte[] key, String method, String path, String body) {
        return hmacHex16(key, method + " " + path + "\n" + (body == null ? "" : body));
    }

    /** 응답 MAC: HMAC(키, "R " + 경로 + "\n" + 요청 MAC + "\n" + 응답 본문)의 앞 16바이트. */
    public static String respMac(byte[] key, String path, String reqMac, String body) {
        return hmacHex16(key, "R " + path + "\n" + reqMac + "\n" + (body == null ? "" : body));
    }

    /** 길이가 같을 때 걸리는 시간이 내용과 무관한 비교. */
    public static boolean macEquals(String a, String b) {
        if (a == null || b == null) return false;
        String x = a.trim().toLowerCase();
        String y = b.trim().toLowerCase();
        if (x.length() != y.length()) return false;
        int diff = 0;
        for (int i = 0; i < x.length(); i++) diff |= x.charAt(i) ^ y.charAt(i);
        return diff == 0;
    }

    /** 현지 시(0~23) → 시간대 구간. */
    public static int tbFromHour(int hour) {
        if (hour >= 6 && hour <= 9) return 0;
        if (hour >= 10 && hour <= 15) return 1;
        if (hour >= 16 && hour <= 20) return 2;
        return 3;
    }

    /** 폰이 만드는 이동 ID(수동·가상 출발). 최상위 비트 1. */
    public static long phoneTripId(long epochS) {
        return 0x80000000L | (epochS & 0x7FFFFFFFL);
    }

    public static boolean isPhoneTrip(long trip) {
        return (trip & 0x80000000L) != 0;
    }
}
