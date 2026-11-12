// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.location.Location;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.Executor;
import java.util.function.Consumer;

import kr.doorboard.travellearn.core.BoardClient;
import kr.doorboard.travellearn.core.Geo;
import kr.doorboard.travellearn.core.Proto;
import kr.doorboard.travellearn.core.TripRecord;

/** 초기 설정(권한·연결·정류장)과 상태 확인 화면. 평소에는 열 필요가 없다. */
public class MainActivity extends Activity {

    public static final String EXTRA_START_TRIP = "start_trip";
    /** 화면에 보이는 동안 true. 이때는 "앱 사용 중" 위치 권한만으로도 측정을 시작할 수 있다. */
    public static volatile boolean visible;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private LinearLayout root;
    private TextView statusView;
    private TextView setupView;
    private TextView tripsView;
    private TextView logView;
    private TextView statsView;
    private EditText userEdit;
    private EditText hostEdit;
    private EditText portEdit;
    private EditText keyEdit;
    private EditText maxEdit;
    private final EditText[] stopName = new EditText[Proto.MAX_STOPS];
    private final EditText[] stopPos = new EditText[Proto.MAX_STOPS];
    private final EditText[] stopRad = new EditText[Proto.MAX_STOPS];
    private boolean pendingStartTrip;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        ScrollView sv = new ScrollView(this);
        sv.setFitsSystemWindows(true);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);
        sv.addView(root);
        setContentView(sv);
        build();
        pendingStartTrip = getIntent().getIntExtra(EXTRA_START_TRIP, 0) == 1;
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (intent.getIntExtra(EXTRA_START_TRIP, 0) == 1) pendingStartTrip = true;
    }

    @Override
    protected void onResume() {
        super.onResume();
        visible = true;
        if (pendingStartTrip) {
            // 출발 알림을 눌러 들어온 경우: 화면이 보이는 지금 측정을 시작한다.
            pendingStartTrip = false;
            sendToService(StandbyService.ACTION_USER_START);
        }
        ui.post(refresher);
    }

    @Override
    protected void onPause() {
        visible = false;
        ui.removeCallbacks(refresher);
        super.onPause();
    }

    private final Runnable refresher = new Runnable() {
        @Override
        public void run() {
            refresh();
            ui.postDelayed(this, 2000L);
        }
    };

    // ------------------------------------------------------------------ 화면 구성

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private TextView header(String t) {
        TextView v = new TextView(this);
        v.setText(t);
        v.setTextSize(17f);
        v.setTypeface(Typeface.DEFAULT_BOLD);
        v.setPadding(0, dp(20), 0, dp(6));
        root.addView(v);
        return v;
    }

    private TextView text(String t) {
        TextView v = new TextView(this);
        v.setText(t);
        v.setTextSize(14f);
        v.setTextIsSelectable(true);
        root.addView(v);
        return v;
    }

    private Button button(ViewGroup parent, String t, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(t);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        parent.addView(b);
        return b;
    }

    private LinearLayout row() {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(r);
        return r;
    }

    private EditText edit(ViewGroup parent, String hint, int inputType, float weight) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setInputType(inputType);
        e.setTextSize(14f);
        e.setSingleLine(true);
        if (parent instanceof LinearLayout && weight > 0) {
            parent.addView(e, new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, weight));
        } else {
            parent.addView(e, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        return e;
    }

    private void build() {
        TextView title = new TextView(this);
        title.setText("현관판 이동 시간 학습");
        title.setTextSize(22f);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);

        header("상태");
        statusView = text("");
        LinearLayout r0 = row();
        button(r0, "대기 시작", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                saveAll();
                Prefs.setEnabled(MainActivity.this, true);
                sendToService(StandbyService.ACTION_START);
            }
        });
        button(r0, "대기 중지", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Prefs.setEnabled(MainActivity.this, false);
                if (StandbyService.get() != null) {
                    startService(new Intent(MainActivity.this, StandbyService.class)
                            .setAction(StandbyService.ACTION_STOP));
                }
            }
        });
        button(r0, "지금 동기화", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (StandbyService.get() != null) sendToService(StandbyService.ACTION_SYNC);
                else toast("먼저 대기를 시작하세요");
            }
        });

        header("1. 초기 설정 (한 번만)");
        setupView = text("");
        LinearLayout r1 = row();
        button(r1, "알림", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (Build.VERSION.SDK_INT >= 33) {
                    requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
                }
            }
        });
        button(r1, "위치", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                requestPermissions(new String[] {Manifest.permission.ACCESS_FINE_LOCATION,
                        Manifest.permission.ACCESS_COARSE_LOCATION}, 2);
            }
        });
        button(r1, "위치 항상 허용", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (!Perms.fineLocation(MainActivity.this)) {
                    toast("먼저 '위치'를 허용하세요");
                    return;
                }
                // Android 11 이상에서는 설정 화면으로 이동한다. 거기서 "항상 허용"을 고른다.
                requestPermissions(
                        new String[] {Manifest.permission.ACCESS_BACKGROUND_LOCATION}, 3);
            }
        });
        LinearLayout r2 = row();
        button(r2, "로컬 네트워크", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (Build.VERSION.SDK_INT >= Perms.API_37) {
                    requestPermissions(new String[] {Perms.ACCESS_LOCAL_NETWORK}, 4);
                } else {
                    toast("이 Android 버전에서는 필요 없습니다");
                }
            }
        });
        button(r2, "배터리 제한 없음", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                try {
                    startActivity(new Intent(
                            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                            Uri.parse("package:" + getPackageName())));
                } catch (RuntimeException e) {
                    openAppSettings();
                }
            }
        });
        button(r2, "앱 설정 열기", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                openAppSettings();
            }
        });

        header("2. 현관판 연결");
        LinearLayout c1 = row();
        userEdit = edit(c1, "사용자 번호(1~4)", InputType.TYPE_CLASS_NUMBER, 1f);
        maxEdit = edit(c1, "최대 측정 시간(분)", InputType.TYPE_CLASS_NUMBER, 1f);
        LinearLayout c2 = row();
        hostEdit = edit(c2, "현관판 IP (예: 192.168.0.50)", InputType.TYPE_CLASS_TEXT, 2f);
        portEdit = edit(c2, "포트", InputType.TYPE_CLASS_NUMBER, 1f);
        keyEdit = edit(root, "공유 키(16진수 32자리)", InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD, 0f);
        LinearLayout c3 = row();
        button(c3, "저장", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (saveAll()) toast("저장했습니다");
            }
        });
        button(c3, "연결 시험", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (saveAll()) testConnection();
            }
        });
        button(c3, "학습 결과", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (saveAll()) fetchStats();
            }
        });
        statsView = text("");

        header("3. 정류장 (위도,경도 / 반경 m)");
        text("좌표를 비워 두면 그 정류장은 자동 판정 없이 알림의 '도착' 버튼으로만 기록합니다. "
                + "정류장에 서서 '현재 위치'를 누르면 좌표가 채워집니다.");
        for (int i = 0; i < Proto.MAX_STOPS; i++) {
            final int idx = i;
            LinearLayout sr = row();
            stopName[i] = edit(sr, "정류장 " + (i + 1) + " 이름", InputType.TYPE_CLASS_TEXT, 2f);
            stopRad[i] = edit(sr, "반경", InputType.TYPE_CLASS_NUMBER, 1f);
            LinearLayout sr2 = row();
            stopPos[i] = edit(sr2, "위도,경도", InputType.TYPE_CLASS_TEXT, 3f);
            button(sr2, "현재 위치", new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    fillCurrentLocation(idx);
                }
            });
        }

        header("4. 시험");
        text("현관판 없이 측정 흐름만 확인할 때 씁니다. 가상 출발은 '폰에서 수동 출발'로 기록됩니다.");
        LinearLayout t1 = row();
        button(t1, "가상 출발", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (StandbyService.get() == null) {
                    toast("먼저 대기를 시작하세요");
                    return;
                }
                sendToService(StandbyService.ACTION_FAKE_DEPART);
            }
        });
        button(t1, "도착", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (StandbyService.get() != null) sendToService(StandbyService.ACTION_ARRIVE);
            }
        });
        button(t1, "취소", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (StandbyService.get() != null) sendToService(StandbyService.ACTION_CANCEL);
            }
        });

        header("최근 기록");
        tripsView = text("");

        header("이벤트 기록");
        LinearLayout l1 = row();
        button(l1, "공유", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent i = new Intent(Intent.ACTION_SEND).setType("text/plain")
                        .putExtra(Intent.EXTRA_TEXT, EventLog.all(MainActivity.this));
                startActivity(Intent.createChooser(i, "이벤트 기록 공유"));
            }
        });
        button(l1, "지우기", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                EventLog.clear(MainActivity.this);
                refresh();
            }
        });
        logView = text("");
        logView.setTextSize(12f);
        logView.setTypeface(Typeface.MONOSPACE);

        loadFields();
    }

    // ------------------------------------------------------------------ 설정 읽기·쓰기

    private void loadFields() {
        userEdit.setText(String.valueOf(Prefs.user(this)));
        maxEdit.setText(String.valueOf(Prefs.maxTripMin(this)));
        hostEdit.setText(Prefs.boardHost(this));
        portEdit.setText(String.valueOf(Prefs.boardPort(this)));
        keyEdit.setText(Prefs.keyHex(this));
        List<Geo.Stop> stops = Prefs.stops(this);
        for (int i = 0; i < Proto.MAX_STOPS; i++) {
            Geo.Stop s = stops.get(i);
            stopName[i].setText(s.name);
            stopRad[i].setText(String.valueOf((int) s.radiusM));
            stopPos[i].setText(s.has
                    ? String.format(Locale.US, "%.6f,%.6f", s.lat, s.lon) : "");
        }
    }

    private static int parseInt(String s, int def) {
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return def;
        }
    }

    /** 입력값을 검사해 저장한다. 잘못된 값이 있으면 알려 주고 false. */
    private boolean saveAll() {
        int user = parseInt(userEdit.getText().toString(), 0);
        if (user < 1 || user > Proto.MAX_USERS) {
            toast("사용자 번호는 1~" + Proto.MAX_USERS);
            return false;
        }
        int port = parseInt(portEdit.getText().toString(), Proto.BOARD_PORT);
        if (port < 1 || port > 65535) {
            toast("포트 범위를 확인하세요");
            return false;
        }
        String key = keyEdit.getText().toString().trim();
        if (!key.isEmpty() && Proto.parseKey(key) == null) {
            toast("공유 키는 16진수 32자리입니다");
            return false;
        }
        int max = parseInt(maxEdit.getText().toString(), 30);
        if (max < 5 || max > 120) {
            toast("최대 측정 시간은 5~120분");
            return false;
        }
        Prefs.saveConn(this, user, hostEdit.getText().toString(), port, key, max);
        for (int i = 0; i < Proto.MAX_STOPS; i++) {
            Geo.Stop s = new Geo.Stop();
            s.id = i + 1;
            s.name = stopName[i].getText().toString().trim();
            int r = parseInt(stopRad[i].getText().toString(), 30);
            s.radiusM = Math.max(10, Math.min(150, r));
            String pos = stopPos[i].getText().toString().trim();
            if (!pos.isEmpty()) {
                String[] p = pos.split(",");
                try {
                    s.lat = Double.parseDouble(p[0].trim());
                    s.lon = Double.parseDouble(p[1].trim());
                    s.has = Math.abs(s.lat) <= 90 && Math.abs(s.lon) <= 180;
                } catch (RuntimeException e) {
                    s.has = false;
                }
                if (!s.has) {
                    toast("정류장 " + (i + 1) + " 좌표 형식: 37.123456,127.123456");
                    return false;
                }
            }
            Prefs.saveStop(this, s);
        }
        return true;
    }

    private void fillCurrentLocation(final int idx) {
        if (!Perms.fineLocation(this)) {
            toast("먼저 '위치'를 허용하세요");
            return;
        }
        LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        if (Build.VERSION.SDK_INT < 30 || !lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            toast("위치(GPS)를 켜 주세요");
            return;
        }
        toast("현재 위치 확인 중… 하늘이 보이는 곳에서 잠시 기다리세요");
        try {
            lm.getCurrentLocation(LocationManager.GPS_PROVIDER, null, new Executor() {
                @Override
                public void execute(Runnable r) {
                    ui.post(r);
                }
            }, new Consumer<Location>() {
                @Override
                public void accept(Location loc) {
                    if (loc == null) {
                        toast("위치를 얻지 못했습니다");
                        return;
                    }
                    stopPos[idx].setText(String.format(Locale.US, "%.6f,%.6f",
                            loc.getLatitude(), loc.getLongitude()));
                    toast("오차 약 " + (int) loc.getAccuracy() + "m · '저장'을 눌러야 반영됩니다");
                }
            });
        } catch (SecurityException e) {
            toast("위치 권한이 없습니다");
        }
    }

    // ------------------------------------------------------------------ 동작

    private void sendToService(String action) {
        try {
            startForegroundService(new Intent(this, StandbyService.class).setAction(action));
        } catch (RuntimeException e) {
            toast("서비스 시작 실패: " + e.getClass().getSimpleName());
        }
    }

    private void openAppSettings() {
        startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.parse("package:" + getPackageName())));
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }

    private BoardClient client() {
        // 화면에서 누르는 시험은 기본 망을 쓴다(집 Wi-Fi에 연결된 상태에서 누른다고 가정).
        return new BoardClient(null, Prefs.boardHost(this), Prefs.boardPort(this),
                Prefs.user(this), Prefs.key(this));
    }

    private void testConnection() {
        statsView.setText("연결 시험 중…");
        final BoardClient c = client();
        new Thread(new Runnable() {
            @Override
            public void run() {
                final BoardClient.Result p = c.ping();
                final BoardClient.Result s = p.ok ? c.stats(Prefs.user(MainActivity.this), 1) : null;
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        String msg;
                        if (!p.ok) {
                            msg = "✗ 현관판 응답 없음: " + p.error
                                    + "\n  같은 Wi-Fi인지, IP·포트, 로컬 네트워크 권한을 확인하세요";
                        } else if (!s.ok) {
                            msg = "✓ 현관판 응답 (부팅 " + p.getLong("boot", 0) + "회)\n✗ 인증 실패: "
                                    + s.error + "\n  사용자 번호와 공유 키를 확인하세요";
                        } else {
                            msg = "✓ 현관판 응답 (부팅 " + p.getLong("boot", 0) + "회)\n✓ 인증 성공";
                            if (StandbyService.get() != null) {
                                StandbyService.get().requestHelloSync("연결 시험");
                            }
                        }
                        statsView.setText(msg);
                    }
                });
            }
        }).start();
    }

    private static final String[] WX = {"맑음", "비", "눈"};
    private static final String[] TB = {"아침", "낮", "저녁", "밤"};

    private void fetchStats() {
        statsView.setText("조회 중…");
        final BoardClient c = client();
        final int user = Prefs.user(this);
        new Thread(new Runnable() {
            @Override
            public void run() {
                final StringBuilder sb = new StringBuilder();
                for (int stop = 1; stop <= Proto.MAX_STOPS; stop++) {
                    BoardClient.Result r = c.stats(user, stop);
                    if (!r.ok) {
                        sb.append("조회 실패: ").append(r.error);
                        break;
                    }
                    String[] cells = r.getString("c", "").split(";");
                    StringBuilder line = new StringBuilder();
                    for (int i = 0; i < cells.length && i < 12; i++) {
                        String[] f = cells[i].split(",");
                        if (f.length < 3 || parseInt(f[0], 0) == 0) continue;
                        line.append("  ").append(WX[i / 4]).append('·').append(TB[i % 4])
                                .append(": ").append(f[0]).append("회, 평균 ")
                                .append(parseInt(f[1], 0) / 10).append("초, 편차 ")
                                .append(parseInt(f[2], 0) / 10).append("초\n");
                    }
                    sb.append("정류장 ").append(stop).append(" (초기값 ")
                            .append(r.getLong("def_ds", 0) / 10).append("초)\n")
                            .append(line.length() == 0 ? "  아직 기록 없음\n" : line.toString());
                }
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        statsView.setText(sb.toString());
                    }
                });
            }
        }).start();
    }

    // ------------------------------------------------------------------ 표시 갱신

    private static String mark(boolean ok) {
        return ok ? "✓ " : "✗ ";
    }

    private void refresh() {
        StandbyService s = StandbyService.get();
        statusView.setText(s != null ? s.statusText()
                : "대기 서비스: 꺼짐" + (Prefs.enabled(this) ? " (켜 두었지만 실행 중이 아님)" : ""));

        StringBuilder sb = new StringBuilder();
        sb.append(mark(Perms.notifications(this))).append("알림 허용\n");
        sb.append(mark(Perms.fineLocation(this))).append("위치 허용(정확한 위치)\n");
        sb.append(mark(Perms.backgroundLocation(this)))
                .append("위치 '항상 허용' — 없으면 화면이 꺼진 채로는 측정을 스스로 시작하지 못함\n");
        sb.append(mark(Perms.localNetwork(this))).append("로컬 네트워크(근처 기기) 허용")
                .append(Build.VERSION.SDK_INT >= Perms.API_37 ? "" : " — 이 버전에서는 불필요").append('\n');
        sb.append(mark(Perms.batteryUnrestricted(this))).append("배터리 '제한 없음'\n");
        sb.append(mark(Perms.locationEnabled(this))).append("기기 위치 켜짐\n");
        sb.append("  삼성: 설정 > 배터리 > 백그라운드 사용 제한에서 이 앱을 '자동 절전 예외 앱'에 추가");
        setupView.setText(sb.toString());

        StringBuilder tb = new StringBuilder();
        List<TripRecord> list = TripDb.get(this).recent(12);
        SimpleDateFormat f = new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA);
        for (int i = 0; i < list.size(); i++) {
            TripRecord r = list.get(i);
            tb.append(f.format(new Date(r.epoch * 1000L))).append("  ")
                    .append(StandbyService.statusKo(r.status));
            if (Proto.ST_ARRIVED.equals(r.status)) {
                tb.append(" 정류장 ").append(r.stop).append(" · ")
                        .append(StandbyService.fmtDur(r.durMs));
                if (r.q == 2) tb.append(" (수동)");
                else if (r.q == 1) tb.append(" (오차 큼)");
            } else if (r.note != null && !r.note.isEmpty()) {
                tb.append(" · ").append(r.note);
            }
            if (r.isFinished()) tb.append(r.synced ? "  [저장됨]" : "  [미전송]");
            tb.append('\n');
        }
        tripsView.setText(tb.length() == 0 ? "아직 없음" : tb.toString());
        logView.setText(EventLog.tail(this, 40));
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        refresh();
    }
}
