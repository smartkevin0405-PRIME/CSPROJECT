// 생성형 AI의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

/** 주소 등록은 미전송 기록이 없어도 재시도한다. 마지막 간격에서 계속 재시도한다. */
public final class SyncRetry {
    private static final long[] DELAYS_MS = {5000L, 15000L, 60000L, 300000L};
    private int index;

    public synchronized void reset() { index = 0; }

    public synchronized long nextDelayMs() {
        long delay = DELAYS_MS[index];
        if (index < DELAYS_MS.length - 1) index++;
        return delay;
    }
}
