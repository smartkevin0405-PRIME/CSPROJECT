// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * 재부팅·앱 업데이트 뒤 대기 서비스를 다시 띄운다.
 * BOOT_COMPLETED / MY_PACKAGE_REPLACED 수신은 "백그라운드에서 포그라운드 서비스 시작 금지"의 공식 예외다.
 * 이때 시작하는 유형은 connectedDevice뿐이다(위치 유형은 출발 신호를 받은 뒤에만 더한다).
 * 주의: 사용자가 설정에서 앱을 "강제 종료"하면 앱을 직접 다시 열기 전까지 이 수신기도 호출되지 않는다.
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String a = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(a)
                && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(a)) {
            return;
        }
        if (!Prefs.enabled(context)) return;
        EventLog.add(context, "자동 시작 요청: "
                + (Intent.ACTION_BOOT_COMPLETED.equals(a) ? "재부팅" : "앱 업데이트"));
        try {
            context.startForegroundService(new Intent(context, StandbyService.class)
                    .setAction(StandbyService.ACTION_START));
        } catch (RuntimeException e) {
            EventLog.add(context, "자동 시작 실패: " + e.getClass().getSimpleName());
        }
    }
}
