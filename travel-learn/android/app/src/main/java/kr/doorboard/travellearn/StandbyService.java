// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.provider.Settings;

import java.io.IOException;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;
import java.util.TimeZone;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import kr.doorboard.travellearn.core.BoardClient;
import kr.doorboard.travellearn.core.Geo;
import kr.doorboard.travellearn.core.HttpMini;
import kr.doorboard.travellearn.core.PhoneServerLogic;
import kr.doorboard.travellearn.core.Proto;
import kr.doorboard.travellearn.core.TripRecord;
import kr.doorboard.travellearn.core.TripTracker;
import kr.doorboard.travellearn.core.SyncRetry;

/**
 * 상시 대기 서비스.
 *
 * 대기 중: connectedDevice 유형의 포그라운드 서비스로 서버 소켓만 열어 둔다. 위치는 요청하지 않는다.
 * 출발 신호 수신: 같은 서비스에 location 유형을 더해(startForeground 재호출) 위치 측정을 시작한다.
 *   - 앱이 백그라운드일 때 location 유형을 더하려면 위치 "항상 허용"이 필요하다(공식 문서의 while-in-use 제한).
 *   - 그래서 통신 서비스가 돌고 있다는 이유만으로 위치를 켤 수 있다고 가정하지 않고, 권한을 확인한 뒤
 *     안 되면 "noperm"으로 응답하고 사용자에게 알림으로 확인을 요청한다.
 * 도착·취소·시간 초과: 위치 요청을 해제하고 connectedDevice 유형으로 되돌린다.
 */
public class StandbyService extends Service
        implements TripTracker.Listener, PhoneServerLogic.Callbacks {

    private static final String P = "kr.doorboard.travellearn.";
    public static final String ACTION_START = P + "START";
    public static final String ACTION_STOP = P + "STOP";
    public static final String ACTION_ARRIVE = P + "ARRIVE";
    public static final String ACTION_CANCEL = P + "CANCEL";
    public static final String ACTION_USER_START = P + "USER_START";
    public static final String ACTION_FAKE_DEPART = P + "FAKE_DEPART";
    public static final String ACTION_SYNC = P + "SYNC";
    public static final String ACTION_KEEPALIVE = P + "KEEPALIVE";
    public static final String EXTRA_TRIP = P + "TRIP";
    public static final String EXTRA_STOP = P + "STOP_ID";
    public static final String EXTRA_ARRIVED_AT = P + "ARRIVED_AT";

    private static final int NOTIF_FGS = 1;
    private static final int NOTIF_PROMPT = 2;
    private static final int NOTIF_RESULT = 3;
    private static final String CH_STANDBY = "standby";
    private static final String CH_TRIP = "trip";

    private static final long KEEPALIVE_MS = 60 * 60 * 1000L;

    private static volatile StandbyService instance;

    public static StandbyService get() {
        return instance;
    }

    private final Object lock = new Object();
    private Handler main;
    private HandlerThread workerThread;
    private Handler worker;
    private ExecutorService net;
    private ConnectivityManager cm;
    private LocationManager lm;
    private NotificationManager nm;
    private PowerManager.WakeLock rxLock;
    private PowerManager.WakeLock tripLock;
    private TripDb db;
    private PhoneServerLogic logic;

    private volatile boolean running;
    private volatile ServerSocket serverSocket;
    private volatile Network wifiNet;
    private volatile String wifiIp = "";
    private volatile boolean atHome;
    private volatile long lastContactWall;
    private volatile String lastError = "";
    private volatile boolean serverUp;

    private final TripTracker.Config cfg = new TripTracker.Config();
    private final List<Geo.Stop> stops = new ArrayList<Geo.Stop>();
    private TripTracker tracker;
    private TripRecord waitRec;
    private long waitT0;
    private boolean savedLeft;

    private boolean started;
    private boolean locating;
    private boolean fgsLocation;
    private final SyncRetry syncRetry = new SyncRetry();
    private double lastLat;
    private double lastLon;
    private float lastAcc = 999f;
    private long lastFixMs;

    // ------------------------------------------------------------------ 수명 주기

    @Override
    public void onCreate() {
        super.onCreate();
        main = new Handler(Looper.getMainLooper());
        workerThread = new HandlerThread("tl-worker");
        workerThread.start();
        worker = new Handler(workerThread.getLooper());
        net = Executors.newSingleThreadExecutor();
        cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        rxLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TravelLearn:rx");
        rxLock.setReferenceCounted(false);
        tripLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TravelLearn:trip");
        tripLock.setReferenceCounted(false);
        db = TripDb.get(this);
        tracker = new TripTracker(cfg, stops, this);
        logic = new PhoneServerLogic(this);
        createChannels();
        instance = this;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String a = intent != null ? intent.getAction() : null;
        if (ACTION_STOP.equals(a)) {
            stopStandby();
            return START_NOT_STICKY;
        }
        if (!started && !ensureForeground()) {
            // 백그라운드에서 포그라운드 서비스 시작이 거부된 경우(배터리 "제한 없음"이 아닐 때 생길 수 있음).
            // 이미 실행 중일 때는 다시 부르지 않는다: 측정 중에 들어온 알람·버튼 때문에 서비스가 멈추면 안 된다.
            stopSelf();
            return START_NOT_STICKY;
        }
        if (!started) {
            started = true;
            running = true;
            EventLog.add(this, "대기 서비스 시작 (" + (a == null ? "시스템 재시작" : shortAction(a))
                    + ", Android " + Build.VERSION.SDK_INT + ")");
            startServer();
            registerNetwork();
            resumeActive();
            scheduleKeepalive();
        }
        if (ACTION_ARRIVE.equals(a)) {
            synchronized (lock) {
                if (tracker.isActive() && tracker.current().trip == intent.getLongExtra(EXTRA_TRIP, 0)) {
                    long at = intent.getLongExtra(EXTRA_ARRIVED_AT, SystemClock.elapsedRealtime());
                    if (at >= tracker.t0() && at <= SystemClock.elapsedRealtime()) {
                        tracker.manualArrive(at, intent.getIntExtra(EXTRA_STOP, 0));
                    }
                }
            }
        } else if (ACTION_CANCEL.equals(a)) {
            synchronized (lock) {
                if (tracker.isActive()) tracker.cancel("사용자 취소");
                else if (waitRec != null) finishWait(Proto.ST_CANCELED, "사용자 취소");
            }
        } else if (ACTION_USER_START.equals(a)) {
            userStart();
        } else if (ACTION_FAKE_DEPART.equals(a)) {
            fakeDepart();
        } else if (ACTION_SYNC.equals(a)) {
            requestHelloSync("수동 동기화");
        } else if (ACTION_KEEPALIVE.equals(a)) {
            syncRetry.reset();
            scheduleKeepalive();
            requestHelloSync("주기 점검");
        }
        return START_STICKY;
    }

    private static String shortAction(String a) {
        return a.substring(a.lastIndexOf('.') + 1);
    }

    @Override
    public void onDestroy() {
        running = false;
        instance = null;
        closeServer();
        try {
            cm.unregisterNetworkCallback(netCallback);
        } catch (RuntimeException ignored) { }
        stopLocation();
        if (rxLock.isHeld()) rxLock.release();
        if (tripLock.isHeld()) tripLock.release();
        worker.removeCallbacksAndMessages(null);
        workerThread.quitSafely();
        net.shutdownNow();
        EventLog.add(this, "대기 서비스 종료");
        super.onDestroy();
    }

    private void stopStandby() {
        Prefs.setEnabled(this, false);
        synchronized (lock) {
            if (tracker.isActive()) tracker.cancel("대기 중지");
            if (waitRec != null) finishWait(Proto.ST_CANCELED, "대기 중지");
        }
        AlarmManager am = (AlarmManager) getSystemService(Context.ALARM_SERVICE);
        am.cancel(keepalivePi());
        stopForeground(true);
        stopSelf();
    }

    // ------------------------------------------------------------------ 포그라운드 서비스 유형

    /** 현재 상태에 맞는 유형으로 포그라운드 상태를 보장한다. 거부되면 false. */
    private boolean ensureForeground() {
        boolean tracking;
        synchronized (lock) {
            tracking = tracker.isActive();
        }
        int type = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE;
        if (tracking && fgsLocation) type |= ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION;
        Notification n = tracking ? trackingNotification(null) : standbyNotification();
        try {
            startForeground(NOTIF_FGS, n, type);
            return true;
        } catch (RuntimeException e) {
            EventLog.add(this, "포그라운드 서비스 시작 거부: " + e.getClass().getSimpleName()
                    + " " + e.getMessage());
            lastError = "포그라운드 서비스 시작 거부";
            return false;
        }
    }

    /** 위치 측정 시작(메인 스레드). location 유형 추가가 거부되어도 측정은 시도한다. */
    private final Runnable beginTracking = new Runnable() {
        @Override
        public void run() {
            boolean typeOk = false;
            try {
                startForeground(NOTIF_FGS, trackingNotification(null),
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
                                | ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION);
                typeOk = true;
            } catch (RuntimeException e) {
                EventLog.add(StandbyService.this, "location 유형 전환 거부: "
                        + e.getClass().getSimpleName() + " " + e.getMessage());
                try {
                    startForeground(NOTIF_FGS, trackingNotification(null),
                            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
                } catch (RuntimeException ignored) { }
            }
            fgsLocation = typeOk;
            EventLog.add(StandbyService.this, "location 유형 전환 " + (typeOk ? "성공" : "실패"));
            startLocation();
            long hold = cfg.maxTripMs + 60000L;
            tripLock.acquire(hold);
            worker.removeCallbacks(tick);
            worker.postDelayed(tick, 5000L);
        }
    };

    /** 측정 종료(메인 스레드). 위치 요청을 해제하고 대기 유형으로 되돌린다. */
    private final Runnable endTracking = new Runnable() {
        @Override
        public void run() {
            synchronized (lock) {
                if (tracker.isActive()) return; // 곧바로 새 이동이 시작된 경우
            }
            stopLocation();
            if (tripLock.isHeld()) tripLock.release();
            fgsLocation = false;
            worker.removeCallbacks(tick);
            try {
                startForeground(NOTIF_FGS, standbyNotification(),
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            } catch (RuntimeException e) {
                EventLog.add(StandbyService.this, "대기 유형 복귀 실패: " + e.getMessage());
            }
        }
    };

    // ------------------------------------------------------------------ 위치

    private final LocationListener locListener = new LocationListener() {
        @Override
        public void onLocationChanged(Location loc) {
            long t = loc.getElapsedRealtimeNanos() / 1000000L;
            float acc = loc.hasAccuracy() ? loc.getAccuracy() : 999f;
            synchronized (lock) {
                lastLat = loc.getLatitude();
                lastLon = loc.getLongitude();
                lastAcc = acc;
                lastFixMs = t;
                tracker.onFix(loc.getLatitude(), loc.getLongitude(), acc, t);
            }
        }

        @Override
        public void onStatusChanged(String provider, int status, Bundle extras) { }

        @Override
        public void onProviderEnabled(String provider) { }

        @Override
        public void onProviderDisabled(String provider) { }
    };

    private void startLocation() {
        if (locating) return;
        try {
            int n = 0;
            if (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 2000L, 0f, locListener,
                        workerThread.getLooper());
                n++;
            }
            if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 5000L, 0f, locListener,
                        workerThread.getLooper());
                n++;
            }
            locating = n > 0;
            EventLog.add(this, n > 0 ? "위치 측정 시작(제공자 " + n + "개)" : "위치 제공자가 꺼져 있음");
        } catch (SecurityException e) {
            EventLog.add(this, "위치 요청 거부(권한): " + e.getMessage());
        } catch (IllegalArgumentException e) {
            EventLog.add(this, "위치 요청 실패: " + e.getMessage());
        }
    }

    private void stopLocation() {
        if (!locating) return;
        try {
            lm.removeUpdates(locListener);
        } catch (RuntimeException ignored) { }
        locating = false;
        EventLog.add(this, "위치 측정 중단");
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            synchronized (lock) {
                if (!tracker.isActive()) return;
                tracker.onTick(SystemClock.elapsedRealtime(), wifiNet != null && atHome);
                if (!tracker.isActive()) return;
                if (tracker.hasLeftHome() != savedLeft) {
                    savedLeft = tracker.hasLeftHome();
                    Prefs.saveActive(StandbyService.this, tracker.current().trip, tracker.t0(),
                            bootCount(), savedLeft);
                }
            }
            worker.postDelayed(this, 5000L);
        }
    };

    // ------------------------------------------------------------------ 출발 신호·취소 (PhoneServerLogic.Callbacks)

    @Override
    public int user() {
        return Prefs.user(this);
    }

    @Override
    public byte[] key() {
        return Prefs.key(this);
    }

    @Override
    public boolean busy() {
        synchronized (lock) {
            return tracker.isActive();
        }
    }

    @Override
    public String onDepart(PhoneServerLogic.Depart d, long receivedElapsedMs) {
        return handleDepart(d, receivedElapsedMs, 0);
    }

    private String handleDepart(PhoneServerLogic.Depart d, long receivedMs, int src) {
        synchronized (lock) {
            if (db.exists(d.trip)) {
                EventLog.add(this, "출발 신호 중복 #" + d.trip + " (시도 " + d.tryNo + ")");
                return "dup";
            }
            TripRecord r = new TripRecord();
            r.trip = d.trip;
            r.user = d.user;
            r.stopHint = d.stop;
            r.wx = d.wx;
            r.tb = d.tb;
            r.src = src;
            r.etaS = d.etaS;
            r.tempC = d.tempC;
            long departWall = System.currentTimeMillis() - d.ageMs;
            r.epoch = departWall / 1000L;
            if (r.tb == Proto.TB_UNKNOWN) {
                Calendar cal = Calendar.getInstance();
                cal.setTimeInMillis(departWall);
                r.tb = Proto.tbFromHour(cal.get(Calendar.HOUR_OF_DAY));
            }
            if (tracker.isActive()) tracker.cancel("새 출발 신호로 대체");
            if (waitRec != null) finishWait(Proto.ST_CANCELED, "새 출발 신호로 대체");

            long t0 = receivedMs - d.ageMs;
            cfg.maxTripMs = Prefs.maxTripMin(this) * 60000L;
            stops.clear();
            stops.addAll(Prefs.stops(this));

            boolean auto = Perms.canAutoLocateInBackground(this)
                    || (MainActivity.visible && Perms.fineLocation(this)
                    && Perms.locationEnabled(this));
            if (auto) {
                r.status = Proto.ST_ACTIVE;
                db.insert(r);
                tracker.start(r, t0);
                savedLeft = false;
                Prefs.saveActive(this, r.trip, t0, bootCount(), false);
                main.post(beginTracking);
                EventLog.add(this, "출발 신호 수신 #" + r.trip + " 경과 " + d.ageMs + "ms, 시도 "
                        + d.tryNo + " → 측정 시작");
                return "tracking";
            }
            r.status = Proto.ST_WAIT_USER;
            db.insert(r);
            waitRec = r;
            waitT0 = t0;
            worker.removeCallbacks(waitExpire);
            worker.postDelayed(waitExpire, cfg.maxTripMs);
            main.post(new Runnable() {
                @Override
                public void run() {
                    nm.notify(NOTIF_PROMPT, promptNotification());
                }
            });
            EventLog.add(this, "출발 신호 수신 #" + r.trip + " → 위치 '항상 허용'이 아니라 자동 시작 불가, "
                    + "알림으로 확인 요청");
            return "noperm";
        }
    }

    @Override
    public String onCancel(long trip) {
        synchronized (lock) {
            if (tracker.isActive() && tracker.current().trip == trip) {
                tracker.cancel("현관판에서 취소");
                return "canceled";
            }
            if (waitRec != null && waitRec.trip == trip) {
                finishWait(Proto.ST_CANCELED, "현관판에서 취소");
                return "canceled";
            }
            return db.exists(trip) ? "done" : "unknown";
        }
    }

    private final Runnable waitExpire = new Runnable() {
        @Override
        public void run() {
            synchronized (lock) {
                if (waitRec != null) finishWait(Proto.ST_LOST, "사용자 확인 없음(권한 부족)");
            }
        }
    };

    /** lock을 잡은 상태에서 호출. 사용자 확인을 기다리던 이동을 끝낸다. */
    private void finishWait(String status, String note) {
        TripRecord r = waitRec;
        waitRec = null;
        worker.removeCallbacks(waitExpire);
        r.status = status;
        r.note = note;
        db.update(r);
        nm.cancel(NOTIF_PROMPT);
        EventLog.add(this, "이동 #" + r.trip + " 종료: " + status + " (" + note + ")");
        requestHelloSync("기록 종료");
    }

    /** 사용자가 알림을 눌러 앱이 화면에 보이는 상태에서 호출된다(이때는 "앱 사용 중" 권한으로도 위치를 쓸 수 있다). */
    private void userStart() {
        synchronized (lock) {
            if (waitRec == null) return;
            TripRecord r = waitRec;
            long t0 = waitT0;
            if (SystemClock.elapsedRealtime() - t0 >= cfg.maxTripMs) {
                finishWait(Proto.ST_LOST, "확인이 너무 늦음");
                return;
            }
            waitRec = null;
            worker.removeCallbacks(waitExpire);
            nm.cancel(NOTIF_PROMPT);
            r.status = Proto.ST_ACTIVE;
            db.update(r);
            tracker.start(r, t0);
            savedLeft = false;
            Prefs.saveActive(this, r.trip, t0, bootCount(), false);
            main.post(beginTracking);
            EventLog.add(this, "사용자 확인으로 측정 시작 #" + r.trip);
        }
    }

    /** 현관판 없이 시험하기 위한 가상 출발(앱 화면의 버튼). */
    private void fakeDepart() {
        PhoneServerLogic.Depart d = new PhoneServerLogic.Depart();
        d.trip = Proto.phoneTripId(System.currentTimeMillis() / 1000L);
        d.user = Prefs.user(this);
        d.stop = 0;
        d.wx = 0;
        d.tb = Proto.TB_UNKNOWN;
        d.ageMs = 0;
        d.tryNo = 1;
        handleDepart(d, SystemClock.elapsedRealtime(), 1);
    }

    // ------------------------------------------------------------------ TripTracker.Listener

    /** lock을 잡은 상태에서 호출된다. */
    @Override
    public void onFinished(TripRecord rec) {
        // 수동 도착 확인 + 좌표 없는 정류장 → 지금 위치를 그 정류장 좌표로 학습
        if (Proto.ST_ARRIVED.equals(rec.status) && rec.q == 2) {
            for (int i = 0; i < stops.size(); i++) {
                Geo.Stop s = stops.get(i);
                if (s.id == rec.stop && !s.has && lastAcc <= 30f
                        && SystemClock.elapsedRealtime() - lastFixMs < 15000L) {
                    s.has = true;
                    s.lat = lastLat;
                    s.lon = lastLon;
                    Prefs.saveStop(this, s);
                    EventLog.add(this, "정류장 " + s.id + " 좌표를 현재 위치로 저장");
                }
            }
        }
        db.update(rec);
        Prefs.clearActive(this);
        final String text;
        if (Proto.ST_ARRIVED.equals(rec.status)) {
            text = "정류장 " + rec.stop + " 도착 · " + fmtDur(rec.durMs)
                    + (rec.q == 2 ? " (수동 확인)" : rec.q == 1 ? " (위치 오차 큼)" : "");
        } else {
            text = "측정 종료: " + statusKo(rec.status) + (rec.note.isEmpty() ? "" : " · " + rec.note);
        }
        EventLog.add(this, "이동 #" + rec.trip + " " + text + ", 위치 수신 " + tracker.fixCount() + "회");
        main.post(endTracking);
        main.post(new Runnable() {
            @Override
            public void run() {
                nm.notify(NOTIF_RESULT, new Notification.Builder(StandbyService.this, CH_STANDBY)
                        .setSmallIcon(R.drawable.ic_stat)
                        .setContentTitle(text)
                        .setContentText("집에 돌아오면 현관판에 저장합니다")
                        .setContentIntent(activityPi(0))
                        .setAutoCancel(true)
                        .build());
            }
        });
        requestHelloSync("기록 종료");
    }

    /** lock을 잡은 상태에서 호출된다. */
    @Override
    public void onNeedManual(TripRecord rec) {
        EventLog.add(this, "위치가 계속 부정확함 → 수동 도착 확인 요청");
        main.post(new Runnable() {
            @Override
            public void run() {
                nm.notify(NOTIF_FGS, trackingNotification(
                        "위치가 부정확합니다. 정류장에 도착하면 '도착'을 눌러 주세요"));
            }
        });
    }

    /** 프로세스가 죽었다 살아난 경우: 같은 부팅이면 이어서 재고, 아니면 유실로 끝낸다. */
    private void resumeActive() {
        synchronized (lock) {
            long trip = Prefs.activeTrip(this);
            List<TripRecord> dangling = db.unfinished();
            for (int i = 0; i < dangling.size(); i++) {
                TripRecord r = dangling.get(i);
                long t0 = Prefs.activeT0(this);
                cfg.maxTripMs = Prefs.maxTripMin(this) * 60000L;
                boolean sameBoot = Prefs.activeBoot(this) == bootCount();
                long el = SystemClock.elapsedRealtime() - t0;
                if (r.trip == trip && Proto.ST_ACTIVE.equals(r.status) && sameBoot && el >= 0
                        && el < cfg.maxTripMs) {
                    stops.clear();
                    stops.addAll(Prefs.stops(this));
                    tracker.start(r, t0);
                    savedLeft = Prefs.activeLeft(this);
                    tracker.restoreLeftHome(savedLeft);
                    main.post(beginTracking);
                    EventLog.add(this, "진행 중이던 이동 #" + r.trip + " 이어서 측정");
                } else {
                    r.status = Proto.ST_LOST;
                    r.note = sameBoot ? "서비스 중단" : "재부팅";
                    db.update(r);
                    EventLog.add(this, "이동 #" + r.trip + " 유실 처리(" + r.note + ")");
                }
            }
            if (!tracker.isActive()) Prefs.clearActive(this);
        }
    }

    private int bootCount() {
        return Settings.Global.getInt(getContentResolver(), Settings.Global.BOOT_COUNT, 0);
    }

    // ------------------------------------------------------------------ 폰 서버(출발 신호 수신)

    private void startServer() {
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                serverLoop();
            }
        }, "tl-server");
        t.setDaemon(true);
        t.start();
    }

    private void closeServer() {
        ServerSocket s = serverSocket;
        if (s != null) {
            try {
                s.close();
            } catch (IOException ignored) { }
        }
    }

    private void serverLoop() {
        while (running) {
            ServerSocket ss = null;
            try {
                ss = new ServerSocket();
                ss.setReuseAddress(true);
                ss.bind(new InetSocketAddress(Prefs.phonePort(this)), 8);
                serverSocket = ss;
                serverUp = true;
                EventLog.add(this, "출발 신호 대기 포트 " + ss.getLocalPort() + " 열림");
                while (running) {
                    Socket s = ss.accept();
                    long received = SystemClock.elapsedRealtime();
                    rxLock.acquire(20000L); // 처리하는 동안 CPU가 다시 잠들지 않게
                    serve(s, received);
                }
            } catch (IOException e) {
                if (running) EventLog.add(this, "서버 소켓 오류: " + e.getMessage());
            } finally {
                serverUp = false;
                if (ss != null) {
                    try {
                        ss.close();
                    } catch (IOException ignored) { }
                }
            }
            if (running) SystemClock.sleep(10000L);
        }
    }

    private void serve(Socket s, long received) {
        try {
            s.setSoTimeout(3000);
            HttpMini.Response resp;
            try {
                HttpMini.Request req = HttpMini.readRequest(s.getInputStream(), Proto.MAX_BODY);
                resp = logic.handle(req, received);
            } catch (HttpMini.HttpError e) {
                resp = new HttpMini.Response(e.status, "{\"ok\":0,\"err\":\"" + e.err + "\"}");
            }
            HttpMini.writeResponse(s.getOutputStream(), resp);
        } catch (IOException e) {
            EventLog.add(this, "요청 처리 실패: " + e.getMessage());
        } finally {
            try {
                s.close();
            } catch (IOException ignored) { }
        }
    }

    // ------------------------------------------------------------------ 집 Wi-Fi 감시, hello, 동기화

    private void registerNetwork() {
        NetworkRequest req = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build();
        try {
            cm.registerNetworkCallback(req, netCallback, worker);
        } catch (RuntimeException e) {
            EventLog.add(this, "Wi-Fi 감시 등록 실패: " + e.getMessage());
        }
    }

    private final ConnectivityManager.NetworkCallback netCallback =
            new ConnectivityManager.NetworkCallback() {
        @Override
        public void onAvailable(Network network) {
            wifiNet = network;
            syncRetry.reset(); // 새로 연결됐으니 재시도 간격을 처음부터
            EventLog.add(StandbyService.this, "Wi-Fi 연결됨");
            worker.postDelayed(new Runnable() {
                @Override
                public void run() {
                    requestHelloSync("Wi-Fi 연결");
                }
            }, 1500L);
        }

        @Override
        public void onLinkPropertiesChanged(Network network, LinkProperties lp) {
            if (!network.equals(wifiNet)) return;
            String ip = ipv4Of(lp);
            if (!ip.isEmpty() && !ip.equals(wifiIp)) {
                boolean had = !wifiIp.isEmpty();
                wifiIp = ip;
                if (had) requestHelloSync("폰 주소 변경");
            }
        }

        @Override
        public void onLost(Network network) {
            if (!network.equals(wifiNet)) return;
            wifiNet = null;
            wifiIp = "";
            atHome = false;
            worker.removeCallbacks(retrySync);
            EventLog.add(StandbyService.this, "Wi-Fi 끊김");
            synchronized (lock) {
                tracker.onWifiLost();
            }
            updateStandbyNotification();
        }
    };

    private static String ipv4Of(LinkProperties lp) {
        if (lp == null) return "";
        List<LinkAddress> list = lp.getLinkAddresses();
        for (int i = 0; i < list.size(); i++) {
            InetAddress a = list.get(i).getAddress();
            if (a instanceof Inet4Address) return a.getHostAddress();
        }
        return "";
    }

    public void requestHelloSync(final String why) {
        if (net.isShutdown()) return;
        net.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    helloSync(why);
                } catch (RuntimeException e) {
                    EventLog.add(StandbyService.this, "동기화 오류: " + e);
                }
            }
        });
    }

    private final Runnable retrySync = new Runnable() {
        @Override
        public void run() {
            requestHelloSync("재시도");
        }
    };

    /** 네트워크 전용 스레드에서 실행. hello가 성공하면 "집에 있음"으로 보고 미전송 기록을 보낸다. */
    private void helloSync(String why) {
        Network n = wifiNet;
        String host = Prefs.boardHost(this);
        byte[] key = Prefs.key(this);
        if (n == null) {
            atHome = false;
            return;
        }
        if (host.isEmpty() || key == null) {
            lastError = "현관판 주소 또는 공유 키 미설정";
            return;
        }
        String ip = wifiIp;
        if (ip.isEmpty()) {
            ip = ipv4Of(cm.getLinkProperties(n));
            wifiIp = ip;
        }
        if (ip.isEmpty()) return;

        // Wi-Fi 망에 묶인 소켓을 쓴다(모바일 데이터가 기본 망이어도 집 안 주소로 나가게).
        BoardClient c = new BoardClient(n.getSocketFactory(), host, Prefs.boardPort(this),
                Prefs.user(this), key);
        long now = System.currentTimeMillis();
        int tz = TimeZone.getDefault().getOffset(now) / 60000;
        int pending = db.countUnsynced();
        BoardClient.Result r = c.hello(ip, Prefs.phonePort(this), now / 1000L, tz, pending);
        if (!r.ok) {
            boolean was = atHome;
            atHome = false;
            lastError = "hello 실패: " + r.error;
            if (was || pending > 0) EventLog.add(this, lastError + " (" + why + ")");
            scheduleRetry();
            updateStandbyNotification();
            return;
        }
        if (!atHome) EventLog.add(this, "현관판 연결 확인(" + why + "), 미전송 " + pending + "건");
        atHome = true;
        lastContactWall = now;
        lastError = "";
        synchronized (lock) {
            tracker.onHomeReturned();
        }

        List<TripRecord> list = db.unsynced();
        boolean allOk = true;
        for (int i = 0; i < list.size(); i++) {
            TripRecord rec = list.get(i);
            BoardClient.Result tr = c.postTrip(rec);
            if (!tr.ok) {
                lastError = "기록 #" + rec.trip + " 전송 실패: " + tr.error;
                EventLog.add(this, lastError);
                allOk = false;
                break;
            }
            db.markSynced(rec.trip);
            if (tr.getLong("applied", 0) == 1) {
                EventLog.add(this, "기록 #" + rec.trip + " 저장 완료 · 표본 " + tr.getLong("n", 0)
                        + "개, 평균 " + tr.getLong("mean_ds", 0) / 10 + "초, 편차 "
                        + tr.getLong("sd_ds", 0) / 10 + "초");
            } else {
                EventLog.add(this, "기록 #" + rec.trip + " 저장 완료 · 통계 미반영("
                        + tr.getString("why", "?") + ")");
            }
        }
        if (allOk) {
            syncRetry.reset();
            worker.removeCallbacks(retrySync);
        } else {
            scheduleRetry();
        }
        updateStandbyNotification();
    }

    private void scheduleRetry() {
        worker.removeCallbacks(retrySync);
        if (wifiNet == null) return;
        worker.postDelayed(retrySync, syncRetry.nextDelayMs());
    }

    private PendingIntent keepalivePi() {
        Intent i = new Intent(this, StandbyService.class).setAction(ACTION_KEEPALIVE);
        return PendingIntent.getForegroundService(this, 10, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    /** 60분마다 hello를 다시 보내 주소 등록을 유지한다. 서비스가 죽어 있었다면 이때 되살아난다. */
    private void scheduleKeepalive() {
        AlarmManager am = (AlarmManager) getSystemService(Context.ALARM_SERVICE);
        am.setAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP,
                SystemClock.elapsedRealtime() + KEEPALIVE_MS, keepalivePi());
    }

    // ------------------------------------------------------------------ 알림

    private void createChannels() {
        NotificationChannel a = new NotificationChannel(CH_STANDBY, "대기 상태",
                NotificationManager.IMPORTANCE_LOW);
        a.setShowBadge(false);
        NotificationChannel b = new NotificationChannel(CH_TRIP, "이동 시간 측정",
                NotificationManager.IMPORTANCE_HIGH);
        nm.createNotificationChannel(a);
        nm.createNotificationChannel(b);
    }

    private PendingIntent activityPi(int startTrip) {
        Intent i = new Intent(this, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP)
                .putExtra(MainActivity.EXTRA_START_TRIP, startTrip);
        return PendingIntent.getActivity(this, startTrip, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private PendingIntent servicePi(String action, int code) {
        Intent i = new Intent(this, StandbyService.class).setAction(action);
        return PendingIntent.getService(this, code, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private Notification standbyNotification() {
        String text;
        if (wifiNet != null && atHome) text = "현관판 연결됨 · 출발 신호 대기";
        else if (wifiNet != null) text = "Wi-Fi 연결됨 · 현관판 미확인";
        else text = "집 밖 · 귀가하면 동기화";
        int pending = db.countUnsynced();
        if (pending > 0) text += " · 미전송 " + pending + "건";
        Notification.Builder b = new Notification.Builder(this, CH_STANDBY)
                .setSmallIcon(R.drawable.ic_stat)
                .setContentTitle("현관판 이동 시간 대기 중")
                .setContentText(text)
                .setOngoing(true)
                .setContentIntent(activityPi(0));
        if (Build.VERSION.SDK_INT >= 31) {
            b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return b.build();
    }

    private void updateStandbyNotification() {
        main.post(new Runnable() {
            @Override
            public void run() {
                if (!running) return;
                synchronized (lock) {
                    if (tracker.isActive()) return;
                }
                nm.notify(NOTIF_FGS, standbyNotification());
            }
        });
    }

    private Notification trackingNotification(String alert) {
        long t0;
        synchronized (lock) {
            t0 = tracker.isActive() ? tracker.t0() : SystemClock.elapsedRealtime();
        }
        long startWall = System.currentTimeMillis() - (SystemClock.elapsedRealtime() - t0);
        Notification.Builder b = new Notification.Builder(this, CH_TRIP)
                .setSmallIcon(R.drawable.ic_stat)
                .setContentTitle("정류장까지 이동 시간 측정 중")
                .setContentText(alert != null ? alert : "정류장에 도착하면 자동으로 끝납니다")
                .setOngoing(true)
                .setOnlyAlertOnce(alert == null)
                .setUsesChronometer(true)
                .setWhen(startWall)
                .setContentIntent(activityPi(0))
                .addAction(new Notification.Action.Builder((Icon) null, "도착",
                        activityPi(2)).build())
                .addAction(new Notification.Action.Builder((Icon) null, "외출 취소",
                        servicePi(ACTION_CANCEL, 2)).build());
        if (Build.VERSION.SDK_INT >= 31) {
            b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return b.build();
    }

    private Notification promptNotification() {
        return new Notification.Builder(this, CH_TRIP)
                .setSmallIcon(R.drawable.ic_stat)
                .setContentTitle("출발 신호를 받았습니다")
                .setContentText("눌러서 이동 시간 측정을 시작하세요 (위치 '항상 허용'이 아니라 자동 시작 불가)")
                .setContentIntent(activityPi(1))
                .setAutoCancel(true)
                .addAction(new Notification.Action.Builder((Icon) null, "취소",
                        servicePi(ACTION_CANCEL, 3)).build())
                .build();
    }

    // ------------------------------------------------------------------ 화면 표시용

    public long activeTripId() {
        synchronized (lock) { return tracker.isActive() ? tracker.current().trip : 0; }
    }

    public List<Geo.Stop> manualArrivalStops() {
        synchronized (lock) { return tracker.manualStops(); }
    }

    static String statusKo(String st) {
        if (Proto.ST_ARRIVED.equals(st)) return "도착";
        if (Proto.ST_CANCELED.equals(st)) return "취소";
        if (Proto.ST_TIMEOUT.equals(st)) return "시간 초과";
        if (Proto.ST_LOST.equals(st)) return "유실";
        if (Proto.ST_ACTIVE.equals(st)) return "측정 중";
        if (Proto.ST_WAIT_USER.equals(st)) return "확인 대기";
        return st;
    }

    static String fmtDur(long ms) {
        long s = ms / 1000L;
        return (s / 60) + "분 " + (s % 60) + "초";
    }

    public String statusText() {
        StringBuilder sb = new StringBuilder();
        sb.append("대기 서비스: 실행 중").append(serverUp ? " (포트 열림)" : " (포트 닫힘)").append('\n');
        sb.append("Wi-Fi: ").append(wifiNet != null ? "연결 " + wifiIp : "없음").append('\n');
        sb.append("현관판: ").append(atHome ? "연결 확인" : "미확인");
        if (lastContactWall > 0) {
            sb.append(" · 마지막 통신 ").append((System.currentTimeMillis() - lastContactWall) / 60000L)
                    .append("분 전");
        }
        sb.append('\n');
        synchronized (lock) {
            if (tracker.isActive()) {
                sb.append("측정 중 #").append(tracker.current().trip).append(" · ")
                        .append(fmtDur(SystemClock.elapsedRealtime() - tracker.t0()))
                        .append(" · 위치 ").append(tracker.fixCount()).append("회")
                        .append(fgsLocation ? "" : " · location 유형 아님").append('\n');
            } else if (waitRec != null) {
                sb.append("출발 신호 확인 대기 #").append(waitRec.trip).append('\n');
            }
        }
        sb.append("미전송 기록: ").append(db.countUnsynced()).append("건");
        if (!lastError.isEmpty()) sb.append("\n최근 오류: ").append(lastError);
        return sb.toString();
    }
}
