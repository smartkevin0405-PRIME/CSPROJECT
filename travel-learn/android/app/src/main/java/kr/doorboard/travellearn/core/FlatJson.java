// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * 중첩 없는 평면 JSON 객체 전용 읽기/쓰기.
 * 값은 정수(Long) 또는 문자열(String)만 다룬다. 통신규격이 이 범위만 쓰므로 외부 라이브러리 없이 처리한다.
 */
public final class FlatJson {
    private FlatJson() {}

    public static final class ParseError extends Exception {
        public ParseError(String m) { super(m); }
    }

    public static Map<String, Object> parse(String s) throws ParseError {
        if (s == null) throw new ParseError("null");
        Map<String, Object> out = new LinkedHashMap<String, Object>();
        int[] p = new int[] {0};
        skipWs(s, p);
        expect(s, p, '{');
        skipWs(s, p);
        if (peek(s, p) == '}') { p[0]++; return out; }
        while (true) {
            skipWs(s, p);
            String key = readString(s, p);
            skipWs(s, p);
            expect(s, p, ':');
            skipWs(s, p);
            char c = peek(s, p);
            if (c == '"') {
                out.put(key, readString(s, p));
            } else if (c == '-' || (c >= '0' && c <= '9')) {
                out.put(key, Long.valueOf(readInt(s, p)));
            } else {
                throw new ParseError("값 형식");
            }
            skipWs(s, p);
            c = peek(s, p);
            if (c == ',') { p[0]++; continue; }
            if (c == '}') { p[0]++; break; }
            throw new ParseError("구분자");
        }
        skipWs(s, p);
        if (p[0] != s.length()) throw new ParseError("뒤에 남은 문자");
        return out;
    }

    private static char peek(String s, int[] p) throws ParseError {
        if (p[0] >= s.length()) throw new ParseError("끝");
        return s.charAt(p[0]);
    }

    private static void expect(String s, int[] p, char c) throws ParseError {
        if (peek(s, p) != c) throw new ParseError("'" + c + "' 필요");
        p[0]++;
    }

    private static void skipWs(String s, int[] p) {
        while (p[0] < s.length()) {
            char c = s.charAt(p[0]);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') p[0]++; else break;
        }
    }

    private static String readString(String s, int[] p) throws ParseError {
        expect(s, p, '"');
        StringBuilder sb = new StringBuilder();
        while (true) {
            char c = peek(s, p);
            p[0]++;
            if (c == '"') break;
            if (c == '\\') {
                char e = peek(s, p);
                p[0]++;
                if (e == '"' || e == '\\' || e == '/') sb.append(e);
                else if (e == 'n') sb.append('\n');
                else if (e == 't') sb.append('\t');
                else if (e == 'r') sb.append('\r');
                else throw new ParseError("이스케이프");
            } else {
                sb.append(c);
            }
            if (sb.length() > 200) throw new ParseError("문자열 길이");
        }
        return sb.toString();
    }

    private static long readInt(String s, int[] p) throws ParseError {
        int start = p[0];
        if (peek(s, p) == '-') p[0]++;
        int digits = 0;
        while (p[0] < s.length()) {
            char c = s.charAt(p[0]);
            if (c >= '0' && c <= '9') { p[0]++; digits++; } else break;
        }
        if (digits == 0 || digits > 18) throw new ParseError("숫자");
        if (p[0] < s.length()) {
            char c = s.charAt(p[0]);
            if (c == '.' || c == 'e' || c == 'E') throw new ParseError("실수 금지");
        }
        return Long.parseLong(s.substring(start, p[0]));
    }

    public static boolean hasLong(Map<String, Object> m, String k) {
        return m.get(k) instanceof Long;
    }

    public static long getLong(Map<String, Object> m, String k, long def) {
        Object o = m.get(k);
        return (o instanceof Long) ? ((Long) o).longValue() : def;
    }

    public static String getString(Map<String, Object> m, String k, String def) {
        Object o = m.get(k);
        return (o instanceof String) ? (String) o : def;
    }

    /** 키 순서를 유지하는 빌더. 공백 없이 출력한다(MAC은 보낸 바이트 그대로 계산). */
    public static final class Builder {
        private final StringBuilder sb = new StringBuilder("{");
        private boolean first = true;

        private void key(String k) {
            if (!first) sb.append(',');
            first = false;
            sb.append('"').append(k).append("\":");
        }

        public Builder put(String k, long v) {
            key(k);
            sb.append(v);
            return this;
        }

        public Builder put(String k, String v) {
            key(k);
            sb.append('"');
            for (int i = 0; i < v.length(); i++) {
                char c = v.charAt(i);
                if (c == '"' || c == '\\') sb.append('\\').append(c);
                else if (c < 0x20) sb.append(' ');
                else sb.append(c);
            }
            sb.append('"');
            return this;
        }

        public String build() {
            return sb.toString() + "}";
        }
    }

    public static Builder obj() {
        return new Builder();
    }
}
