// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.Date;
import java.util.Locale;

/**
 * 현장 시험용 이벤트 기록. 밤샘 대기·재부팅 시험 뒤에 "언제 무엇이 일어났는지"를 폰에서 바로 확인하기 위한 것이다.
 * 파일(events.log)에 누적하고 최근 줄은 메모리에도 둔다.
 */
public final class EventLog {
    private EventLog() {}

    private static final String TAG = "TravelLearn";
    private static final int MAX_FILE = 200 * 1024;
    private static final ArrayDeque<String> recent = new ArrayDeque<String>();
    private static File file;

    private static synchronized void init(Context c) {
        if (file == null) file = new File(c.getApplicationContext().getFilesDir(), "events.log");
    }

    public static synchronized void add(Context c, String msg) {
        init(c);
        String line = new SimpleDateFormat("MM-dd HH:mm:ss", Locale.KOREA).format(new Date())
                + " " + msg;
        Log.i(TAG, msg);
        recent.addLast(line);
        while (recent.size() > 150) recent.removeFirst();
        try {
            if (file.length() > MAX_FILE) trim();
            FileOutputStream out = new FileOutputStream(file, true);
            try {
                out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
            } finally {
                out.close();
            }
        } catch (IOException ignored) { }
    }

    /** 파일이 커지면 뒤쪽 절반만 남긴다. */
    private static void trim() throws IOException {
        byte[] all = readAll();
        int from = all.length / 2;
        while (from < all.length && all[from] != '\n') from++;
        FileOutputStream out = new FileOutputStream(file, false);
        try {
            if (from + 1 < all.length) out.write(all, from + 1, all.length - from - 1);
        } finally {
            out.close();
        }
    }

    private static byte[] readAll() throws IOException {
        if (file == null || !file.exists()) return new byte[0];
        FileInputStream in = new FileInputStream(file);
        try {
            byte[] b = new byte[(int) file.length()];
            int off = 0;
            while (off < b.length) {
                int n = in.read(b, off, b.length - off);
                if (n < 0) break;
                off += n;
            }
            return b;
        } finally {
            in.close();
        }
    }

    /** 최근 n줄(최신이 아래). 메모리에 없으면 파일 끝부분을 읽는다. */
    public static synchronized String tail(Context c, int n) {
        init(c);
        if (recent.isEmpty()) {
            try {
                String all = new String(readAll(), StandardCharsets.UTF_8);
                String[] lines = all.split("\n");
                for (int i = Math.max(0, lines.length - 150); i < lines.length; i++) {
                    if (!lines[i].isEmpty()) recent.addLast(lines[i]);
                }
            } catch (IOException ignored) { }
        }
        StringBuilder sb = new StringBuilder();
        int skip = Math.max(0, recent.size() - n);
        int i = 0;
        for (String s : recent) {
            if (i++ >= skip) sb.append(s).append('\n');
        }
        return sb.toString();
    }

    public static synchronized String all(Context c) {
        init(c);
        try {
            return new String(readAll(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    public static synchronized void clear(Context c) {
        init(c);
        recent.clear();
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }
}
