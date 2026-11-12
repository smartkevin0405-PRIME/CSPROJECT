// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.os.Build;
import android.os.PowerManager;

/** 자동 동작에 필요한 권한·설정 상태 확인. */
public final class Perms {
    private Perms() {}

    /** Android 17(API 37)에서 새로 생긴 로컬 네트워크 권한. 컴파일 SDK와 무관하게 문자열로 쓴다. */
    public static final String ACCESS_LOCAL_NETWORK = "android.permission.ACCESS_LOCAL_NETWORK";
    public static final int API_37 = 37;

    private static boolean granted(Context c, String p) {
        return c.checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean notifications(Context c) {
        return Build.VERSION.SDK_INT < 33 || granted(c, Manifest.permission.POST_NOTIFICATIONS);
    }

    public static boolean fineLocation(Context c) {
        return granted(c, Manifest.permission.ACCESS_FINE_LOCATION);
    }

    /** 위치 "항상 허용". 이것이 없으면 화면이 꺼진 상태에서 위치 측정을 스스로 시작할 수 없다. */
    public static boolean backgroundLocation(Context c) {
        return granted(c, Manifest.permission.ACCESS_BACKGROUND_LOCATION);
    }

    /** Android 17 미만에서는 권한 자체가 없으므로 true. */
    public static boolean localNetwork(Context c) {
        return Build.VERSION.SDK_INT < API_37 || granted(c, ACCESS_LOCAL_NETWORK);
    }

    public static boolean batteryUnrestricted(Context c) {
        PowerManager pm = (PowerManager) c.getSystemService(Context.POWER_SERVICE);
        return pm != null && pm.isIgnoringBatteryOptimizations(c.getPackageName());
    }

    public static boolean locationEnabled(Context c) {
        LocationManager lm = (LocationManager) c.getSystemService(Context.LOCATION_SERVICE);
        return lm != null && lm.isLocationEnabled();
    }

    /** 사용자 조작 없이(화면 꺼짐 상태에서) 위치 측정을 시작할 수 있는 조건. */
    public static boolean canAutoLocateInBackground(Context c) {
        return fineLocation(c) && backgroundLocation(c) && locationEnabled(c);
    }
}
