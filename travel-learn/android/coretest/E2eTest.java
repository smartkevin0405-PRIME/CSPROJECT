// 생성형 AI(Claude)의 도움을 받아 작성함.
// 종단 시험: 앱의 실제 통신 코드(core 패키지) ↔ 가상 현관판 프로세스.
//   - 아두이노 라이브러리를 PC에서 그대로 컴파일한 virtual_board (실제 모듈 코드)
//   - 또는 tools/virtual_board.py (파이썬 시뮬레이터)
// 사용: run.sh E2eTest <이름> <현관판 실행 명령... ({port}, {state} 자리표시자 포함)>
import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import kr.doorboard.travellearn.core.BoardClient;
import kr.doorboard.travellearn.core.HttpMini;
import kr.doorboard.travellearn.core.PhoneServerLogic;
import kr.doorboard.travellearn.core.Proto;
import kr.doorboard.travellearn.core.TripRecord;

public class E2eTest {
    static int pass;
    static int fail;

    static void check(String name, boolean ok) {
        if (ok) pass++; else fail++;
        System.out.println((ok ? "PASS  " : "FAIL  ") + name);
    }

    static final String KEY_HEX = "000102030405060708090a0b0c0d0e0f";
    static final byte[] KEY = Proto.parseKey(KEY_HEX);

    static long now() {
        return System.nanoTime() / 1000000L;
    }

    // ---------------------------------------------------------------- 가상 폰(앱의 서버 코드 그대로)
    static class Phone implements PhoneServerLogic.Callbacks, Runnable {
        final ServerSocket ss;
        final PhoneServerLogic logic = new PhoneServerLogic(this);
        final List<PhoneServerLogic.Depart> departs = Collections.synchronizedList(new ArrayList<PhoneServerLogic.Depart>());
        final List<Long> recvMs = Collections.synchronizedList(new ArrayList<Long>());
        final List<Long> cancels = Collections.synchronizedList(new ArrayList<Long>());
        final List<Long> known = new ArrayList<Long>();
        volatile int dropBeforeRead;   // 읽지 않고 끊을 연결 수(폰이 꺼진 것처럼)
        volatile int dropAfterHandle;  // 처리하고도 응답하지 않을 횟수(응답 유실)
        volatile int dupReplies;
        volatile int connections;
        volatile boolean running = true;

        Phone() throws IOException {
            ss = new ServerSocket();
            ss.setReuseAddress(true);
            ss.bind(new InetSocketAddress("127.0.0.1", 0));
        }

        int port() { return ss.getLocalPort(); }

        public int user() { return 1; }
        public byte[] key() { return KEY; }
        public boolean busy() { return false; }

        public String onDepart(PhoneServerLogic.Depart d, long receivedElapsedMs) {
            synchronized (known) {
                if (known.contains(d.trip)) {
                    dupReplies++;
                    return "dup";
                }
                known.add(d.trip);
            }
            departs.add(d);
            recvMs.add(receivedElapsedMs);
            return "tracking";
        }

        public String onCancel(long trip) {
            cancels.add(trip);
            return "canceled";
        }

        public void run() {
            while (running) {
                try {
                    Socket s = ss.accept();
                    long received = now();
                    connections++;
                    try {
                        if (dropBeforeRead > 0) {
                            dropBeforeRead--;
                            continue;
                        }
                        s.setSoTimeout(3000);
                        HttpMini.Response resp;
                        try {
                            resp = logic.handle(HttpMini.readRequest(s.getInputStream(), Proto.MAX_BODY), received);
                        } catch (HttpMini.HttpError e) {
                            resp = new HttpMini.Response(e.status, "{\"ok\":0,\"err\":\"" + e.err + "\"}");
                        }
                        if (dropAfterHandle > 0) {
                            dropAfterHandle--;
                            continue;
                        }
                        HttpMini.writeResponse(s.getOutputStream(), resp);
                    } finally {
                        s.close();
                    }
                } catch (IOException e) {
                    if (!running) return;
                }
            }
        }

        PhoneServerLogic.Depart waitDepart(int index, long timeoutMs) throws InterruptedException {
            long end = now() + timeoutMs;
            while (now() < end) {
                if (departs.size() > index) return departs.get(index);
                Thread.sleep(20);
            }
            return null;
        }
    }

    // ---------------------------------------------------------------- 현관판 프로세스
    static class Board {
        final String[] template;
        final int port;
        final String state;
        Process proc;
        OutputStream stdin;
        final List<String> lines = Collections.synchronizedList(new ArrayList<String>());

        Board(String[] template, int port, String state) {
            this.template = template;
            this.port = port;
            this.state = state;
        }

        void start() throws IOException {
            List<String> cmd = new ArrayList<String>();
            for (String t : template) {
                cmd.add(t.replace("{port}", String.valueOf(port)).replace("{state}", state).replace("{key}", KEY_HEX));
            }
            ProcessBuilder pb = new ProcessBuilder(cmd);
            pb.redirectErrorStream(true);
            proc = pb.start();
            stdin = proc.getOutputStream();
            final BufferedReader r = new BufferedReader(new InputStreamReader(proc.getInputStream(), StandardCharsets.UTF_8));
            Thread t = new Thread(new Runnable() {
                public void run() {
                    try {
                        String line;
                        while ((line = r.readLine()) != null) lines.add(line);
                    } catch (IOException ignored) { }
                }
            });
            t.setDaemon(true);
            t.start();
        }

        void cmd(String c) throws IOException {
            stdin.write((c + "\n").getBytes(StandardCharsets.UTF_8));
            stdin.flush();
        }

        void stop() throws Exception {
            try {
                cmd("quit");
            } catch (IOException ignored) { }
            if (!proc.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) proc.destroyForcibly();
            proc.waitFor();
        }

        boolean waitUp(BoardClient c, long timeoutMs) throws InterruptedException {
            long end = now() + timeoutMs;
            while (now() < end) {
                if (c.ping().ok) return true;
                Thread.sleep(100);
            }
            return false;
        }
    }

    static TripRecord trip(long id, int stop, int wx, int tb, String st, long durMs) {
        TripRecord r = new TripRecord();
        r.trip = id;
        r.user = 1;
        r.stop = stop;
        r.wx = wx;
        r.tb = tb;
        r.status = st;
        r.durMs = durMs;
        r.epoch = 1763004600L;
        return r;
    }

    static String cell(BoardClient.Result stats, int idx) {
        String[] cells = stats.getString("c", "").split(";");
        return idx < cells.length ? cells[idx] : "";
    }

    public static void main(String[] args) throws Exception {
        String name = args[0];
        String[] template = new String[args.length - 1];
        System.arraycopy(args, 1, template, 0, template.length);
        System.out.println("=== 종단 시험: 앱 통신 코드 ↔ " + name + " ===");

        ServerSocket tmp = new ServerSocket(0);
        int boardPort = tmp.getLocalPort();
        tmp.close();
        File state = File.createTempFile("tl-board", ".state");
        state.delete();

        Phone phone = new Phone();
        Thread pt = new Thread(phone);
        pt.setDaemon(true);
        pt.start();

        Board board = new Board(template, boardPort, state.getAbsolutePath());
        board.start();
        BoardClient c = new BoardClient(null, "127.0.0.1", boardPort, 1, KEY);
        check("현관판 기동, ping 응답", board.waitUp(c, 8000));
        long boot1 = c.ping().getLong("boot", -1);

        // --- 등록·인증
        long epoch = System.currentTimeMillis() / 1000L;
        BoardClient.Result h = c.hello("127.0.0.1", phone.port(), epoch, 540, 0);
        check("hello 등록(응답 MAC 검증 포함)", h.ok && h.getLong("reg", 0) == 1 && h.getLong("boot", -1) == boot1);
        BoardClient badKey = new BoardClient(null, "127.0.0.1", boardPort, 1, Proto.parseKey("ffffffffffffffffffffffffffffffff"));
        BoardClient.Result hb = badKey.hello("127.0.0.1", 1, epoch, 540, 0);
        check("키가 다르면 401", !hb.ok && hb.status == 401);
        BoardClient wrongUser = new BoardClient(null, "127.0.0.1", boardPort, 2, KEY);
        check("키가 없는 사용자 번호는 401", wrongUser.hello("127.0.0.1", 1, epoch, 540, 0).status == 401);

        BoardClient.Result s0 = c.stats(1, 2);
        check("초기 통계는 비어 있음(쿼리 포함 경로의 MAC 검증)", s0.ok && "0,0,0".equals(cell(s0, 0))
                && s0.getString("c", "").split(";").length == 12 && s0.getLong("def_ds", 0) == 3000);

        // --- 출발 신호
        long t1 = now();
        board.cmd("depart 1");
        PhoneServerLogic.Depart d1 = phone.waitDepart(0, 4000);
        check("출발 신호 수신", d1 != null);
        if (d1 == null) {
            finish(board);
            return;
        }
        check("이동 ID 형식(부팅횟수<<16 | 순번, 최상위 비트 0)", !Proto.isPhoneTrip(d1.trip)
                && ((d1.trip >> 16) & 0x7FFF) == (boot1 & 0x7FFF) && (d1.trip & 0xFFFF) >= 1);
        check("경과 시간 age_ms가 작음(" + d1.ageMs + "ms), 시도 1", d1.ageMs < 1500 && d1.tryNo == 1);
        long skew = Math.abs((phone.recvMs.get(0) - d1.ageMs) - t1);
        check("문 열림 시점 환산 오차 " + skew + "ms < 400ms", skew < 400);
        int wx = d1.wx;
        int tb = d1.tb == Proto.TB_UNKNOWN ? 1 : d1.tb;
        check("시간대: 현관판이 hello 시각으로 채웠거나 255", (d1.tb >= 0 && d1.tb <= 3) || d1.tb == 255);

        // --- 결과 동기화, 저장 완료 응답, 중복 방지
        BoardClient.Result r1 = c.postTrip(trip(d1.trip, 2, wx, tb, Proto.ST_ARRIVED, 400000));
        check("결과 저장 완료(stored 1, applied 1, n 1, 평균 4000ds)", r1.ok && r1.getLong("applied", 0) == 1
                && r1.getLong("n", 0) == 1 && r1.getLong("mean_ds", 0) == 4000 && r1.getLong("sd_ds", -1) == 0);
        BoardClient.Result r1b = c.postTrip(trip(d1.trip, 2, wx, tb, Proto.ST_ARRIVED, 400000));
        check("같은 기록 재전송 → 저장됨·미반영(dup)", r1b.ok && r1b.getLong("applied", 1) == 0
                && "dup".equals(r1b.getString("why", "")));
        BoardClient.Result s1 = c.stats(1, 2);
        check("통계 칸 반영 1회뿐", s1.ok && "1,4000,0".equals(cell(s1, wx * 4 + tb)));

        long base = Proto.phoneTripId(epoch);
        BoardClient.Result rr = c.postTrip(trip(base + 1, 2, wx, tb, Proto.ST_ARRIVED, 5000));
        check("5초 기록 → range", rr.ok && rr.getLong("applied", 1) == 0 && "range".equals(rr.getString("why", "")));
        rr = c.postTrip(trip(base + 2, 0, wx, tb, Proto.ST_CANCELED, 0));
        check("취소 기록 → status", rr.ok && rr.getLong("applied", 1) == 0 && "status".equals(rr.getString("why", "")));
        rr = c.postTrip(trip(base + 3, 0, wx, tb, Proto.ST_ARRIVED, 300000));
        check("정류장 0 도착 → nostop", rr.ok && rr.getLong("applied", 1) == 0 && "nostop".equals(rr.getString("why", "")));

        c.postTrip(trip(base + 4, 2, wx, tb, Proto.ST_ARRIVED, 420000));
        BoardClient.Result r3 = c.postTrip(trip(base + 5, 2, wx, tb, Proto.ST_ARRIVED, 380000));
        check("표본 3개: n 3, 평균 4000ds, 편차 163ds (손 계산값)", r3.ok && r3.getLong("n", 0) == 3
                && r3.getLong("mean_ds", 0) == 4000 && r3.getLong("sd_ds", 0) == 163);

        // --- 재전송: 폰이 처음 두 번 응답하지 않음
        phone.dropBeforeRead = 2;
        long t2 = now();
        board.cmd("depart 1");
        PhoneServerLogic.Depart d2 = phone.waitDepart(1, 12000);
        check("무응답 2회 뒤 재전송으로 전달", d2 != null && d2.tryNo >= 3 && d2.trip != d1.trip);
        if (d2 != null) {
            long skew2 = Math.abs((phone.recvMs.get(1) - d2.ageMs) - t2);
            check("재전송이 늦어도 문 열림 시점 환산 오차 " + skew2 + "ms < 400ms (age_ms " + d2.ageMs + ")",
                    skew2 < 400 && d2.ageMs >= 1500);
        }

        // --- 응답 유실: 폰은 처리했지만 응답이 사라짐 → 현관판이 같은 ID로 다시 보냄 → dup으로 수신 확인
        phone.dropAfterHandle = 1;
        int before = phone.departs.size();
        board.cmd("depart 1");
        PhoneServerLogic.Depart d3 = phone.waitDepart(before, 5000);
        long end = now() + 6000;
        while (now() < end && phone.dupReplies == 0) Thread.sleep(50);
        check("응답 유실 → 같은 ID 재전송 → 폰은 한 번만 측정(dup 응답)", d3 != null && phone.dupReplies == 1
                && phone.departs.size() == before + 1);

        // --- 취소
        Thread.sleep(300);
        board.cmd("cancel 1");
        end = now() + 5000;
        while (now() < end && phone.cancels.isEmpty()) Thread.sleep(50);
        check("취소 신호 전달(마지막 이동 ID)", d3 != null && phone.cancels.size() == 1 && phone.cancels.get(0) == d3.trip);

        // --- 전원 껐다 켜기: 통계·중복 목록·폰 주소·부팅 횟수 유지
        board.stop();
        board.start();
        check("현관판 재기동", board.waitUp(c, 8000));
        long boot2 = c.ping().getLong("boot", -1);
        check("부팅 횟수 +1 (" + boot1 + " → " + boot2 + ")", boot2 == boot1 + 1);
        BoardClient.Result r1c = c.postTrip(trip(d1.trip, 2, wx, tb, Proto.ST_ARRIVED, 400000));
        check("재기동 뒤에도 중복 방지 유지", r1c.ok && "dup".equals(r1c.getString("why", "")));
        BoardClient.Result s2 = c.stats(1, 2);
        check("재기동 뒤에도 통계 유지", s2.ok && "3,4000,163".equals(cell(s2, wx * 4 + tb)));
        before = phone.departs.size();
        board.cmd("depart 1");
        PhoneServerLogic.Depart d4 = phone.waitDepart(before, 4000);
        check("재기동 뒤 hello 없이도 폰 주소를 기억해 출발 신호 전달", d4 != null);
        check("재기동 뒤 이동 ID가 이전과 겹치지 않음", d4 != null && ((d4.trip >> 16) & 0x7FFF) == (boot2 & 0x7FFF)
                && d4.trip != d1.trip && (d2 == null || d4.trip != d2.trip));

        finish(board);
        //noinspection ResultOfMethodCallIgnored
        state.delete();
    }

    static void finish(Board board) throws Exception {
        board.stop();
        System.out.println("----------------------------------------");
        System.out.println("통과 " + pass + " / 실패 " + fail);
        if (fail > 0) {
            System.out.println("--- 현관판 출력 ---");
            for (String l : board.lines) System.out.println(l);
            System.exit(1);
        }
        System.exit(0);
    }
}
